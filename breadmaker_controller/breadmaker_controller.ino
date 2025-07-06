/*
FEATURES:
 - Replaces breadmaker logic board: controls motor, heater, light, buzzer via PWM at ~1V (analogWrite 77)
 - Web UI: live status, SVG icons, program editor, stage progress
 - Mix and KnockDown: 1 min burst (200ms/1.2s), then 2min ON, 30s pause, repeated, for whole stage
 - Dual-stage bake with independent times/temps, PID control
 - Manual icon toggles (motor, heater, light, buzzer)
 - LittleFS for persistent storage (programs, UI)
 - Status shows stage, time left (d:h:m:s), state of outputs, temperature (RTD analog input on A0)
 - OTA update page at /update (ESPAsyncHTTPUpdateServer)
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncHTTPUpdateServer.h>
#include <ArduinoJson.h>
#include <PID_v1.h>
#include <map>
#include <vector>
#include <time.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include "calibration.h"
#include "programs_manager.h"
#include "wifi_manager.h"
#include "outputs_manager.h"
#include <WebSocketsServer.h>

// --- Firmware build date ---
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// --- Forward declarations ---
String getStatusJson();
void broadcastStatusWSIfChanged();
void wsStatusEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

// --- Web server and OTA ---
AsyncWebServer server(80); // Main web server
ESPAsyncHTTPUpdateServer httpUpdater; // OTA update server
WebSocketsServer wsStatusServer = WebSocketsServer(81); // WebSocket server for status changes

// --- Pin assignments ---
const int PIN_RTD = A0;     // RTD analog input
// (Output pins are defined in outputs_manager.cpp)

// --- PID parameters for temperature control ---
double Setpoint, Input, Output;
double Kp=2.0, Ki=5.0, Kd=1.0;
unsigned long pidSampleTime = 1000;  // PID sample time in milliseconds
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);

// --- Time-proportional control variables ---
unsigned long windowStartTime = 0;
unsigned long windowSize = 30000;        // 30 second window in milliseconds (relay-friendly, now configurable)
unsigned long onTime = 0;                // ON time in milliseconds within current window
double lastPIDOutput = 0.0;              // Previous PID output for dynamic window adjustment
const double PID_CHANGE_THRESHOLD = 0.3; // Threshold for significant PID output change (30%)
const unsigned long MIN_WINDOW_TIME = 5000; // Minimum time before allowing dynamic restart (5 seconds)

time_t scheduledStart = 0; // Scheduled start time (epoch)

// --- State variables ---
bool isRunning = false;           // Is the breadmaker running?
char activeProgramName[32] = "default"; // Name of the active program (char*)

// --- Custom stage state variables (global, for customStages logic) ---
size_t customStageIdx = 0;         // Index of current custom stage
size_t customMixIdx = 0;           // Index of current mix step within stage
unsigned long customStageStart = 0;    // Start time (ms) of current custom stage
unsigned long customMixStepStart = 0;  // Start time (ms) of current mix step
time_t programStartTime = 0;           // Absolute program start time (seconds since epoch)
time_t actualStageStartTimes[20];      // Array to store actual start times of each stage (max 20 stages)

// --- Light/Buzzer management ---
unsigned long lightOnTime = 0;    // When was the light turned on?
unsigned long buzzStart = 0;      // When was the buzzer turned on?
bool buzzActive = false;          // Is the buzzer currently active?

/// --- Captive portal DNS and AP ---
DNSServer dnsServer;
const char* apSSID = "BreadmakerSetup";

// --- Output mode setting ---
const char* SETTINGS_FILE = "/settings.json";

// --- Debug serial setting ---
bool debugSerial = true; // Debug serial output (default: true)

// --- Manual mode global variable ---
bool manualMode = false; // Manual mode: true = direct manual control, false = automatic

// --- Startup delay to let temperature sensor stabilize ---
unsigned long startupTime = 0;           // Time when system started (millis())
const unsigned long STARTUP_DELAY_MS = 15000; // 15 second delay after power-on

// --- Temperature averaging system ---
int tempSampleCount = 10;
int tempRejectCount = 2; // Reject top N and bottom N
const int MAX_TEMP_SAMPLES = 50; // Maximum allowed sample count
float tempSamples[MAX_TEMP_SAMPLES];
int tempSampleIndex = 0;
bool tempSamplesReady = false;
unsigned long lastTempSample = 0;
unsigned long tempSampleInterval = 500; // milliseconds
float averagedTemperature = 0.0;
float lastTemp = 0.0; // Last temperature for change detection

// --- Dynamic restart tracking variables ---
unsigned long lastDynamicRestart = 0;
String lastDynamicRestartReason = "";
unsigned int dynamicRestartCount = 0;

// --- Function prototypes ---
void loadSettings();
void saveSettings();
void updateTemperatureSampling();
float getAveragedTemperature();

// --- Forward declaration for stopBreadmaker (must be before any use) ---
void stopBreadmaker();

// --- Forward declaration for startup delay check ---
bool isStartupDelayComplete();

// --- Core state machine endpoints ---
void setup() {
  startupTime = millis(); // Record startup time for sensor stabilization delay
  Serial.begin(115200);
  delay(100);
  Serial.println("[setup] Booting...");
  if (!LittleFS.begin()) {
    Serial.println("[setup] LittleFS mount failed! Halting.");
    while (1) delay(1000);
  }
  Serial.println("[setup] LittleFS mounted.");
  Serial.println("[setup] Loading programs...");
  loadPrograms();
  Serial.print("[setup] Programs loaded. Count: ");
  Serial.println(programs.size());
  loadCalibration();
  Serial.println("[setup] Calibration loaded.");
  loadSettings();
  Serial.println("[setup] Settings loaded.");
  loadResumeState();
  Serial.println("[setup] Resume state loaded.");

  // --- WiFi connection debug output ---
  Serial.println("[wifi] Reading WiFi config...");
  File wf = LittleFS.open("/wifi.json", "r");
  if (!wf) {
    Serial.println("[wifi] wifi.json not found! Starting AP mode.");
    // ...existing AP/captive portal code if any...
  } else {
    DynamicJsonDocument wdoc(256);
    DeserializationError werr = deserializeJson(wdoc, wf);
    if (werr) {
      Serial.print("[wifi] Failed to parse wifi.json: ");
      Serial.println(werr.c_str());
      // ...existing AP/captive portal code if any...
    } else {
      const char* ssid = wdoc["ssid"] | "";
      const char* pass = wdoc["pass"] | "";
      Serial.print("[wifi] Connecting to SSID: ");
      Serial.println(ssid);
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, pass);
      int tries = 0;
      while (WiFi.status() != WL_CONNECTED && tries < 40) {
        delay(250);
        Serial.print(".");
        tries++;
      }
      Serial.println();
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[wifi] Connected! IP address: ");
        Serial.println(WiFi.localIP());
      } else {
        Serial.println("[wifi] Failed to connect. Starting AP mode.");
        // ...existing AP/captive portal code if any...
      }
    }
    wf.close();
  }

  // --- Ensure all outputs are OFF at boot ---
  setHeater(false);
  setMotor(false);
  setLight(false);
  setBuzzer(false);
  outputsManagerInit(); // Initialize output pins and set all outputs OFF

  // --- Core state machine endpoints ---
  server.on("/start", HTTP_GET, [](AsyncWebServerRequest*req){
    if (debugSerial) Serial.println("[ACTION] /start called");
    
    // Check if startup delay has completed
    if (!isStartupDelayComplete()) {
      unsigned long remaining = STARTUP_DELAY_MS - (millis() - startupTime);
      if (debugSerial) Serial.printf("[START] Startup delay: %lu ms remaining\n", remaining);
      DynamicJsonDocument errorDoc(256);
      errorDoc["error"] = "Startup delay active";
      errorDoc["message"] = "Please wait for temperature sensor to stabilize";
      errorDoc["remainingMs"] = remaining;
      String errorJson;
      serializeJson(errorDoc, errorJson);
      req->send(423, "application/json", errorJson); // 423 Locked
      return;
    }
    
    isRunning = true;
    customStageIdx = 0;
    customMixIdx = 0;
    customStageStart = millis();
    customMixStepStart = 0;
    programStartTime = time(nullptr);
    // Initialize actual stage start times array
    for (int i = 0; i < 20; i++) actualStageStartTimes[i] = 0;
    actualStageStartTimes[0] = programStartTime; // Record actual start of first stage
    saveResumeState();
    if (debugSerial) Serial.printf("[STAGE] Entering custom stage: %d\n", customStageIdx);
    req->send(200, "application/json", getStatusJson());
    broadcastStatusWSIfChanged();
  });
  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest*req){
    if (debugSerial) Serial.println("[ACTION] /stop called");
    stopBreadmaker();
    req->send(200, "application/json", getStatusJson());
    broadcastStatusWSIfChanged();
  });
  server.on("/pause", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println("[ACTION] /pause called");
    isRunning = false;
    setMotor(false);
    setHeater(false);
    setLight(false);
    saveResumeState();
    req->send(200, "application/json", getStatusJson());
    broadcastStatusWSIfChanged();
  });
  server.on("/resume", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println("[ACTION] /resume called");
    isRunning = true;
    saveResumeState();
    req->send(200, "application/json", getStatusJson());
    broadcastStatusWSIfChanged();
  });
  server.on("/advance", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println("[ACTION] /advance called");
    if (programs.count(activeProgramName) == 0) { stopBreadmaker(); req->send(200,"application/json",getStatusJson()); return; }
    Program &p = programs[activeProgramName];
    customStageIdx++;
    if (customStageIdx >= p.customStages.size()) {
      stopBreadmaker(); customStageIdx = 0; customStageStart = 0; req->send(200,"application/json",getStatusJson()); return;
    }
    customStageStart = millis();
    customMixIdx = 0;
    customMixStepStart = 0;
    isRunning = true;
    if (customStageIdx == 0) programStartTime = time(nullptr);
    // Record actual start time of this stage
    if (customStageIdx < 20) actualStageStartTimes[customStageIdx] = time(nullptr);
    saveResumeState();
    if (debugSerial) Serial.printf("[STAGE] Entering custom stage: %d\n", customStageIdx);
    req->send(200,"application/json",getStatusJson());
    broadcastStatusWSIfChanged();
  });
  server.on("/back", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println("[ACTION] /back called");
    if (programs.count(activeProgramName) == 0) { stopBreadmaker(); req->send(200,"application/json",getStatusJson()); return; }
    Program &p = programs[activeProgramName];
    if (customStageIdx > 0) customStageIdx--;
    customStageStart = millis();
    customMixIdx = 0;
    customMixStepStart = 0;
    isRunning = true;
    if (customStageIdx == 0) programStartTime = time(nullptr);
    // Record actual start time of this stage when going back
    if (customStageIdx < 20) actualStageStartTimes[customStageIdx] = time(nullptr);
    saveResumeState();
    if (debugSerial) Serial.printf("[STAGE] Entering custom stage: %d\n", customStageIdx);
    req->send(200,"application/json",getStatusJson());
    broadcastStatusWSIfChanged();
  });
  server.on("/select", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("idx")) {
      int idx = req->getParam("idx")->value().toInt();
      int i = 0;
      for (auto it = programs.begin(); it != programs.end(); ++it, ++i) {
        if (i == idx) {
          strncpy(activeProgramName, it->first.c_str(), sizeof(activeProgramName)-1);
          activeProgramName[sizeof(activeProgramName)-1] = '\0';
          isRunning = false;
          saveSettings();
          saveResumeState(); // <--- Save resume state on program select (by idx)
          req->send(200, "text/plain", "Selected");
          return;
        }
      }
      req->send(400, "text/plain", "Bad program index");
      return;
    }
    if (req->hasParam("name")) {
      const char* name = req->getParam("name")->value().c_str();
      if (programs.count(name)) {
        strncpy(activeProgramName, name, sizeof(activeProgramName)-1);
        activeProgramName[sizeof(activeProgramName)-1] = '\0';
        isRunning = false;
        saveSettings();
        saveResumeState(); // <--- Save resume state on program select (by name)
        req->send(200, "text/plain", "Selected");
        return;
      }
    }
    req->send(400, "text/plain", "Bad program name or index");
  });
  // --- Manual output control endpoints ---
  server.on("/api/heater", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) {
      int on = req->getParam("on")->value().toInt();
      setHeater(on != 0);
    }
    req->send(200, "application/json", getStatusJson());
    broadcastStatusWSIfChanged();
  });
  server.on("/api/motor", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) {
      int on = req->getParam("on")->value().toInt();
      setMotor(on != 0);
    }
    req->send(200, "application/json", getStatusJson());
    broadcastStatusWSIfChanged();
  });
  server.on("/api/light", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) {
      int on = req->getParam("on")->value().toInt();
      setLight(on != 0);
      if (on) lightOnTime = millis();
    }
    req->send(200, "application/json", getStatusJson());
    broadcastStatusWSIfChanged();
  });
  server.on("/api/buzzer", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) {
      int on = req->getParam("on")->value().toInt();
      setBuzzer(on != 0);
      if (on) { buzzActive = true; buzzStart = millis(); }
    }
    req->send(200, "application/json", getStatusJson());
    broadcastStatusWSIfChanged();
  });
  // --- Manual mode API endpoint ---
  server.on("/api/manual_mode", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) {
      int on = req->getParam("on")->value().toInt();
      manualMode = (on != 0);
    }
    req->send(200, "application/json", getStatusJson());
    broadcastStatusWSIfChanged();
  });
  // --- Manual temperature setpoint API endpoint ---
  server.on("/api/temperature", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("setpoint")) {
      float temp = req->getParam("setpoint")->value().toFloat();
      if (temp >= 0 && temp <= 250) {  // Reasonable temperature range
        Setpoint = temp;
      }
    }
    req->send(200, "application/json", getStatusJson());
    broadcastStatusWSIfChanged();
  });
  // --- PID debugging endpoint ---
  server.on("/api/pid_debug", HTTP_GET, [](AsyncWebServerRequest* req){
    DynamicJsonDocument doc(512);
    doc["setpoint"] = Setpoint;
    doc["input"] = Input;
    doc["output"] = Output;
    doc["current_temp"] = getAveragedTemperature();
    doc["raw_temp"] = readTemperature(); // Add raw temperature for chart comparison
    doc["heater_state"] = heaterState;
    doc["manual_mode"] = manualMode;
    doc["is_running"] = isRunning;
    doc["kp"] = Kp;
    doc["ki"] = Ki;
    doc["kd"] = Kd;
    doc["sample_time_ms"] = pidSampleTime;
    doc["temp_sample_count"] = tempSampleCount;
    doc["temp_reject_count"] = tempRejectCount;
    doc["temp_sample_interval_ms"] = tempSampleInterval;
    // Time-proportional control info
    unsigned long now = millis();
    unsigned long elapsed = (windowStartTime > 0) ? (now - windowStartTime) : 0;
    doc["window_size_ms"] = windowSize;
    doc["window_elapsed_ms"] = elapsed;
    doc["on_time_ms"] = onTime;
    doc["duty_cycle_percent"] = (Output * 100.0);
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });
  
  // --- PID parameter tuning endpoint ---
  server.on("/api/pid_params", HTTP_GET, [](AsyncWebServerRequest* req){
    bool updated = false;
    bool pidTuningsChanged = false;
    String errors = "";
    
    // Store original values to check if they actually changed
    float originalKp = Kp, originalKi = Ki, originalKd = Kd;
    unsigned long originalSampleTime = pidSampleTime;
    
    if (req->hasParam("kp")) {
      float kp = req->getParam("kp")->value().toFloat();
      if (kp >= 0 && kp <= 100) {
        if (abs(Kp - kp) > 0.000001) { // Only update if actually different
          Kp = kp;
          pidTuningsChanged = true;
          updated = true;
          if (debugSerial) Serial.printf("[PID] Kp updated %.6f→%.6f\n", originalKp, Kp);
        } else {
          if (debugSerial) Serial.printf("[PID] Kp unchanged (%.6f)\n", Kp);
        }
      } else {
        errors += "Kp out of range (0-100); ";
      }
    }
    if (req->hasParam("ki")) {
      float ki = req->getParam("ki")->value().toFloat();
      if (ki >= 0 && ki <= 50) {
        if (abs(Ki - ki) > 0.000001) { // Only update if actually different
          Ki = ki;
          pidTuningsChanged = true;
          updated = true;
          if (debugSerial) Serial.printf("[PID] Ki updated %.6f→%.6f\n", originalKi, Ki);
        } else {
          if (debugSerial) Serial.printf("[PID] Ki unchanged (%.6f)\n", Ki);
        }
      } else {
        errors += "Ki out of range (0-50); ";
      }
    }
    if (req->hasParam("kd")) {
      float kd = req->getParam("kd")->value().toFloat();
      if (kd >= 0 && kd <= 100) {
        if (abs(Kd - kd) > 0.000001) { // Only update if actually different
          Kd = kd;
          pidTuningsChanged = true;
          updated = true;
          if (debugSerial) Serial.printf("[PID] Kd updated %.6f→%.6f\n", originalKd, Kd);
        } else {
          if (debugSerial) Serial.printf("[PID] Kd unchanged (%.6f)\n", Kd);
        }
      } else {
        errors += "Kd out of range (0-100); ";
      }
    }
    
    // Only call SetTunings if PID parameters actually changed
    if (pidTuningsChanged) {
      myPID.SetTunings(Kp, Ki, Kd);
      if (debugSerial) Serial.printf("[PID] Tunings applied: Kp=%.6f, Ki=%.6f, Kd=%.6f\n", Kp, Ki, Kd);
    }
    
    if (req->hasParam("sample_time")) {
      int sampleTime = req->getParam("sample_time")->value().toInt();
      if (sampleTime >= 100 && sampleTime <= 120000) {  // Allow up to 2 minutes (120 seconds)
        if (pidSampleTime != (unsigned long)sampleTime) { // Only update if different
          pidSampleTime = (unsigned long)sampleTime;
          myPID.SetSampleTime(pidSampleTime);
          updated = true;
          if (debugSerial) Serial.printf("[PID] Sample time updated %lums→%lums\n", originalSampleTime, pidSampleTime);
        } else {
          if (debugSerial) Serial.printf("[PID] Sample time unchanged (%lums)\n", pidSampleTime);
        }
      } else {
        errors += "Sample time out of range (100-120000ms); ";
      }
    }
    if (req->hasParam("window_size")) {
      int newWindowSize = req->getParam("window_size")->value().toInt();
      if (newWindowSize >= 5000 && newWindowSize <= 120000) {
        unsigned long oldWindowSize = windowSize;
        if (windowSize != (unsigned long)newWindowSize) { // Only update if different
          windowSize = (unsigned long)newWindowSize;
          updated = true;
          if (debugSerial) Serial.printf("[PID] Window size updated %lums→%lums\n", oldWindowSize, windowSize);
        } else {
          if (debugSerial) Serial.printf("[PID] Window size unchanged (%lums)\n", windowSize);
        }
      } else {
        errors += "Window size out of range (5000-120000ms); ";
      }
    }
    if (req->hasParam("temp_samples")) {
      int newTempSamples = req->getParam("temp_samples")->value().toInt();
      if (newTempSamples >= 5 && newTempSamples <= MAX_TEMP_SAMPLES) {
        int oldTempSamples = tempSampleCount;
        tempSampleCount = newTempSamples;
        updated = true;
        
        // Intelligent reset logic - only reset if necessary
        if (newTempSamples != oldTempSamples) {
          if (newTempSamples < oldTempSamples) {
            // Reducing sample count - keep existing data if we have enough
            if (tempSamplesReady && tempSampleIndex >= newTempSamples) {
              // We have enough samples, just adjust the count
              if (debugSerial) Serial.printf("[TEMP] Sample count reduced %d→%d, keeping existing data\n", oldTempSamples, newTempSamples);
            } else {
              // Not enough samples yet, reset to rebuild with new count
              tempSamplesReady = false;
              tempSampleIndex = 0;
              if (debugSerial) Serial.printf("[TEMP] Sample count reduced %d→%d, reset required\n", oldTempSamples, newTempSamples);
            }
          } else {
            // Increasing sample count - keep existing data, just need more samples
            if (debugSerial) Serial.printf("[TEMP] Sample count increased %d→%d, keeping existing data\n", oldTempSamples, newTempSamples);
            tempSamplesReady = false; // Need to fill the larger array
          }
        } else {
          if (debugSerial) Serial.printf("[TEMP] Sample count unchanged (%d), preserving data\n", tempSampleCount);
        }
      } else {
        errors += String("Temp samples out of range (5-") + MAX_TEMP_SAMPLES + "); ";
      }
    }
    if (req->hasParam("temp_reject")) {
      int newTempReject = req->getParam("temp_reject")->value().toInt();
      if (newTempReject >= 0 && newTempReject <= 10) {
        // Ensure we have at least 3 samples after rejection
        int effectiveSamples = tempSampleCount - (2 * newTempReject);
        if (effectiveSamples >= 3) {
          int oldTempReject = tempRejectCount;
          tempRejectCount = newTempReject;
          updated = true;
          
          // Intelligent reset - only if reject count actually changed
          if (newTempReject != oldTempReject) {
            if (debugSerial) Serial.printf("[TEMP] Reject count changed %d→%d, data preserved\n", oldTempReject, newTempReject);
            // No reset needed - rejection is applied during calculation, not storage
          } else {
            if (debugSerial) Serial.printf("[TEMP] Reject count unchanged (%d), data preserved\n", tempRejectCount);
          }
        } else {
          errors += "Temp reject count too high (would leave <3 effective samples); ";
        }
      } else {
        errors += "Temp reject count out of range (0-10); ";
      }
    }
    if (req->hasParam("temp_interval")) {
      int newTempInterval = req->getParam("temp_interval")->value().toInt();
      if (newTempInterval >= 100 && newTempInterval <= 5000) {
        unsigned long oldTempInterval = tempSampleInterval;
        tempSampleInterval = (unsigned long)newTempInterval;
        updated = true;
        
        // Intelligent reset - timing change affects sample collection rhythm
        if (newTempInterval != oldTempInterval) {
          if (debugSerial) Serial.printf("[TEMP] Sample interval changed %lums→%lums, preserving existing data\n", oldTempInterval, tempSampleInterval);
          // Keep existing data - new interval will apply to future samples
          // No reset needed unless we want to maintain exact timing consistency
        } else {
          if (debugSerial) Serial.printf("[TEMP] Sample interval unchanged (%lums), data preserved\n", tempSampleInterval);
        }
      } else {
        errors += "Temp interval out of range (100-5000ms); ";
      }
    }
    
    // Save settings if any parameter was updated
    if (updated) {
      saveSettings();
    }
    
    DynamicJsonDocument doc(768); // Increased size for additional status info
    doc["kp"] = Kp;
    doc["ki"] = Ki;
    doc["kd"] = Kd;
    doc["sample_time_ms"] = pidSampleTime;
    doc["window_size_ms"] = windowSize;
    doc["temp_sample_count"] = tempSampleCount;
    doc["temp_reject_count"] = tempRejectCount;
    doc["temp_sample_interval_ms"] = tempSampleInterval;
    doc["updated"] = updated;
    
    // Add intelligent update status information
    if (updated) {
      String updateDetails = "";
      
      // Track what types of changes were made
      bool pidChanged = pidTuningsChanged;
      bool tempStructuralChange = false;
      bool tempParametersChanged = false;
      
      // Check if temperature sampling count actually changed
      if (req->hasParam("temp_samples")) {
        int requestedSamples = req->getParam("temp_samples")->value().toInt();
        // Compare with what we had before this update cycle, not current value
        tempStructuralChange = true; // Assume structural change if temp_samples was in request
      }
      
      if (req->hasParam("temp_reject") || req->hasParam("temp_interval")) {
        tempParametersChanged = true;
      }
      
      if (pidChanged && (tempStructuralChange || tempParametersChanged)) {
        updateDetails = "PID and temperature parameters updated";
      } else if (pidChanged) {
        updateDetails = "PID parameters updated - temperature data preserved";
      } else if (tempStructuralChange) {
        updateDetails = "Temperature sampling updated with intelligent data preservation";
      } else if (tempParametersChanged) {
        updateDetails = "Temperature averaging updated - existing data preserved";
      } else {
        updateDetails = "Settings verified";
      }
      
      doc["update_details"] = updateDetails;
      
      // Provide data preservation status
      if (tempSamplesReady && !tempStructuralChange) {
        doc["data_preservation"] = "Temperature history preserved";
      } else if (tempStructuralChange) {
        doc["data_preservation"] = "Intelligent data preservation applied";
      }
      
      if (debugSerial) {
        Serial.printf("[API] Update summary: %s (PID changed: %s, Temp structural: %s, Temp params: %s)\n", 
                     updateDetails.c_str(), 
                     pidChanged ? "yes" : "no",
                     tempStructuralChange ? "yes" : "no", 
                     tempParametersChanged ? "yes" : "no");
      }
    } else {
      doc["update_details"] = "No changes required - all parameters already at requested values";
      doc["data_preservation"] = "All existing data preserved";
    }
    
    if (errors.length() > 0) {
      doc["errors"] = errors;
      String json; serializeJson(doc, json);
      req->send(400, "application/json", json);  // HTTP 400 Bad Request for validation errors
    } else {
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    }
  });
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(LittleFS, "/index.html", "text/html");
  });
  // --- Status endpoint for UI polling ---
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest*req){
    if (debugSerial) Serial.println("[DEBUG] /status requested");
    String buf = getStatusJson();
    req->send(200, "application/json", buf);
  });
  // --- Firmware info endpoint ---
  server.on("/api/firmware_info", HTTP_GET, [](AsyncWebServerRequest* req){
    DynamicJsonDocument doc(128);
    doc["build"] = FIRMWARE_BUILD_DATE;
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  // --- Home Assistant-friendly endpoint ---
  server.on("/ha", HTTP_GET, [](AsyncWebServerRequest*req){
    DynamicJsonDocument d(512);
    d["state"] = isRunning ? "on" : "off";
    // Set stage to Idle if not running, else current custom stage label
    if (programs.count(activeProgramName)) {
      Program &p = programs[activeProgramName];
      if (!isRunning) {
        d["stage"] = "Idle";
      } else if (customStageIdx < p.customStages.size()) {
        d["stage"] = p.customStages[customStageIdx].label.c_str();
      } else {
        d["stage"] = "";
      }
    } else {
      d["stage"] = "Idle";
    }
    d["program"] = activeProgramName;
    d["temperature"] = getAveragedTemperature();
    d["motor"] = motorState;
    d["light"] = lightState;
    d["buzzer"] = buzzerState;
    d["stage_time_left"] = (int)(d["timeLeft"] | 0);
    d["stage_ready_at"] = (int)(d["stageReadyAt"] | 0);
    d["program_ready_at"] = (int)(d["programReadyAt"] | 0);
    char buf[512];
    serializeJson(d, buf, sizeof(buf));
    req->send(200, "application/json", buf);
  });

// --- Add single calibration point endpoint ---
  server.on("/api/calibration/add", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      if (debugSerial) {
        Serial.printf("[CALIBRATION] Add point POST received %u bytes: ", len);
        for (size_t i = 0; i < len; i++) {
          Serial.print((char)data[i]);
        }
        Serial.println();
      }
      
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, data, len);
      if (error) { 
        if (debugSerial) Serial.printf("[CALIBRATION] JSON parse error: %s\n", error.c_str());
        req->send(400,"text/plain","Bad JSON"); 
        return; 
      }
      
      if (!doc.containsKey("raw") || !doc.containsKey("temp")) {
        if (debugSerial) Serial.println("[CALIBRATION] Missing raw or temp field");
        req->send(400,"text/plain","Missing fields"); 
        return;
      }
      
      int raw = doc["raw"];
      float temp = doc["temp"];
      
      // Check for duplicates
      for (auto& pt : rtdCalibTable) {
        if (pt.raw == raw) {
          req->send(400,"text/plain","Duplicate raw value"); 
          return;
        }
        if (abs(pt.temp - temp) < 0.01) {
          req->send(400,"text/plain","Duplicate temp value"); 
          return;
        }
      }
      
      // Add the new point
      CalibPoint newPt = { raw, temp };
      rtdCalibTable.push_back(newPt);
      
      // Sort by raw value for proper interpolation
      std::sort(rtdCalibTable.begin(), rtdCalibTable.end(), [](const CalibPoint& a, const CalibPoint& b) {
        return a.raw < b.raw;
      });
      
      // Save to LittleFS
      saveCalibration();
      if (debugSerial) Serial.printf("[CALIBRATION] Added point (%d, %.2f), total points: %d\n", raw, temp, rtdCalibTable.size());
      req->send(200,"text/plain","OK");
    }
  );

  // --- Update calibration point endpoint ---
  server.on("/api/calibration/update", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      if (debugSerial) {
        Serial.printf("[CALIBRATION] Update point POST received %u bytes: ", len);
        for (size_t i = 0; i < len; i++) {
          Serial.print((char)data[i]);
        }
        Serial.println();
      }
      
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, data, len);
      if (error) { 
        if (debugSerial) Serial.printf("[CALIBRATION] JSON parse error: %s\n", error.c_str());
        req->send(400,"text/plain","Bad JSON"); 
        return; 
      }
      
      if (!doc.containsKey("index") || !doc.containsKey("raw") || !doc.containsKey("temp")) {
        if (debugSerial) Serial.println("[CALIBRATION] Missing required fields");
        req->send(400,"text/plain","Missing fields"); 
        return;
      }
      
      int index = doc["index"];
      int newRaw = doc["raw"];
      float newTemp = doc["temp"];
      
      if (index < 0 || index >= rtdCalibTable.size()) {
        req->send(400,"text/plain","Invalid index"); 
        return;
      }
      
      // Check for duplicates (excluding current point)
      for (size_t i = 0; i < rtdCalibTable.size(); i++) {
        if (i != index) {
          if (rtdCalibTable[i].raw == newRaw) {
            req->send(400,"text/plain","Duplicate raw value"); 
            return;
          }
          if (abs(rtdCalibTable[i].temp - newTemp) < 0.01) {
            req->send(400,"text/plain","Duplicate temp value"); 
            return;
          }
        }
      }
      
      // Update the point
      rtdCalibTable[index].raw = newRaw;
      rtdCalibTable[index].temp = newTemp;
      
      // Sort by raw value for proper interpolation
      std::sort(rtdCalibTable.begin(), rtdCalibTable.end(), [](const CalibPoint& a, const CalibPoint& b) {
        return a.raw < b.raw;
      });
      
      // Save to LittleFS
      saveCalibration();
      if (debugSerial) Serial.printf("[CALIBRATION] Updated point to (%d, %.2f), total points: %d\n", newRaw, newTemp, rtdCalibTable.size());
      req->send(200,"text/plain","OK");
    }
  );

  // --- Delete calibration point endpoint ---
  server.on("/api/calibration/delete", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      if (debugSerial) {
        Serial.printf("[CALIBRATION] Delete point POST received %u bytes: ", len);
        for (size_t i = 0; i < len; i++) {
          Serial.print((char)data[i]);
        }
        Serial.println();
      }
      
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, data, len);
      if (error) { 
        if (debugSerial) Serial.printf("[CALIBRATION] JSON parse error: %s\n", error.c_str());
        req->send(400,"text/plain","Bad JSON"); 
        return; 
      }
      
      if (!doc.containsKey("index")) {
        if (debugSerial) Serial.println("[CALIBRATION] Missing index field");
        req->send(400,"text/plain","Missing index"); 
        return;
      }
      
      int index = doc["index"];
      
      if (index < 0 || index >= rtdCalibTable.size()) {
        req->send(400,"text/plain","Invalid index"); 
        return;
      }
      
      // Remove the point
      rtdCalibTable.erase(rtdCalibTable.begin() + index);
      
      // Save to LittleFS
      saveCalibration();
      if (debugSerial) Serial.printf("[CALIBRATION] Deleted point at index %d, remaining points: %d\n", index, rtdCalibTable.size());
      req->send(200,"text/plain","OK");
    }
  );

server.on("/api/calibration", HTTP_GET, [](AsyncWebServerRequest* req){
  DynamicJsonDocument doc(4096);
  int raw = analogRead(PIN_RTD);
  doc["raw"] = raw;
  JsonArray arr = doc.createNestedArray("table");
  for(auto &pt : rtdCalibTable) {
    JsonObject o = arr.createNestedObject();
    o["raw"] = pt.raw; o["temp"] = pt.temp;
  }
  String json; serializeJson(doc,json);
  req->send(200,"application/json",json);
});
server.on("/api/calibration", HTTP_POST, [](AsyncWebServerRequest* req){},NULL,
  [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
    if (debugSerial) {
      Serial.printf("[CALIBRATION] POST received %u bytes: ", len);
      for (size_t i = 0; i < len; i++) {
        Serial.print((char)data[i]);
      }
      Serial.println();
    }
    
    DynamicJsonDocument doc(8192);  // Increased buffer size
    DeserializationError error = deserializeJson(doc, data, len);
    if (error) { 
      if (debugSerial) Serial.printf("[CALIBRATION] JSON parse error: %s\n", error.c_str());
      req->send(400,"text/plain","Bad JSON"); 
      return; 
    }
    
    if (!doc.containsKey("table") || !doc["table"].is<JsonArray>()) {
      if (debugSerial) Serial.println("[CALIBRATION] Missing or invalid 'table' field");
      req->send(400,"text/plain","Missing table field"); 
      return;
    }
    
    rtdCalibTable.clear();
    JsonArray table = doc["table"].as<JsonArray>();
    for(JsonObject o : table) {
      if (!o.containsKey("raw") || !o.containsKey("temp")) {
        if (debugSerial) Serial.println("[CALIBRATION] Invalid calibration point format");
        continue;
      }
      CalibPoint pt = { o["raw"], o["temp"] };
      rtdCalibTable.push_back(pt);
    }
    
    // Sort by raw value for proper interpolation
    std::sort(rtdCalibTable.begin(), rtdCalibTable.end(), [](const CalibPoint& a, const CalibPoint& b) {
      return a.raw < b.raw;
    });
    
    // Save to LittleFS
    saveCalibration();
    if (debugSerial) Serial.printf("[CALIBRATION] Saved %d calibration points\n", rtdCalibTable.size());
    req->send(200,"text/plain","OK");
  });
  // Programs/cals as before...
  server.on("/api/programs",HTTP_GET,[](AsyncWebServerRequest*req){
    File f = LittleFS.open("/programs.json", "r");
    if (!f || f.size() == 0) {
      // File missing or empty, return empty array
      req->send(200, "application/json", "[]");
      return;
    }
    // Do not close the file here; let beginResponse handle it
    auto r = req->beginResponse(LittleFS, "/programs.json", "application/json");
    r->addHeader("Cache-Control", "no-cache");
    req->send(r);
  });
  server.on("/api/programs", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      static std::vector<uint8_t> body;
      if (index == 0) body.clear();
      body.insert(body.end(), data, data + len);
      if (index + len == total) {
        DynamicJsonDocument doc(8192);
        Serial.print("[POST /api/programs] Received: ");
        for (auto c : body) Serial.print((char)c);
        Serial.println();
        DeserializationError err = deserializeJson(doc, body.data(), body.size());
        if (err) { req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
        JsonArray arr;
        if (doc.is<JsonArray>()) {
          arr = doc.as<JsonArray>();
        } else if (doc["programs"].is<JsonArray>()) {
          arr = doc["programs"].as<JsonArray>();
        } else {
          req->send(400, "application/json", "{\"error\":\"Expected array or {programs:[]}\"}");
          return;
        }
        File f = LittleFS.open("/programs.json", "w");
        if (!f) { req->send(500, "application/json", "{\"error\":\"FS fail\"}"); return; }
        serializeJson(arr, f); f.close(); loadPrograms();
        req->send(200, "application/json", "{\"status\":\"ok\"}");
      }
    }
  );

  server.on("/setStartAt", HTTP_GET, [](AsyncWebServerRequest* req){
  if (req->hasParam("time")) {
    String t = req->getParam("time")->value(); // format: "HH:MM"
    struct tm nowTm;
    time_t now = time(nullptr);
    localtime_r(&now, &nowTm);
    int hh, mm;
    sscanf(t.c_str(), "%d:%d", &hh, &mm);
    // Compute next time today or tomorrow if already past
    struct tm startTm = nowTm;
    startTm.tm_hour = hh;
    startTm.tm_min = mm;
    startTm.tm_sec = 0;
    time_t startEpoch = mktime(&startTm);
    if (startEpoch <= now) startEpoch += 24*60*60; // Next day
    scheduledStart = startEpoch;
    req->send(200,"text/plain","Scheduled");
  } else {
    req->send(400,"text/plain","No time");
  }
});

  // --- Output mode API endpoint ---
  server.on("/api/output_mode", HTTP_GET, [](AsyncWebServerRequest* req){
    DynamicJsonDocument doc(64);
    doc["mode"] = (outputMode == OUTPUT_DIGITAL) ? "digital" : "analog";
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });
  server.on("/api/output_mode", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      DynamicJsonDocument doc(128);
      if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }
      const char* mode = doc["mode"] | "analog";
      outputMode = (strcmp(mode, "digital") == 0) ? OUTPUT_DIGITAL : OUTPUT_ANALOG;
      saveSettings();
      DynamicJsonDocument resp(64);
      resp["mode"] = (outputMode == OUTPUT_DIGITAL) ? "digital" : "analog";
      String json; serializeJson(resp, json);
      req->send(200, "application/json", json);
    }
  );

  // --- Debug Serial API endpoint ---
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* req){
    DynamicJsonDocument doc(128);
    doc["debugSerial"] = debugSerial;
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      DynamicJsonDocument doc(128);
      if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }
      if (doc.containsKey("debugSerial")) debugSerial = doc["debugSerial"];
      saveSettings();
      DynamicJsonDocument resp(64);
      resp["debugSerial"] = debugSerial;
      String json; serializeJson(resp, json);
      req->send(200, "application/json", json);
    }
  );

  // --- File upload API endpoint ---
  server.on("/api/upload", HTTP_POST, [](AsyncWebServerRequest* req){
    req->send(200, "text/plain", "Upload complete");
  }, [](AsyncWebServerRequest* req, String filename, size_t index, uint8_t* data, size_t len, bool final){
    static File uploadFile;
    if (!index) {
      // First chunk - open file for writing
      if (debugSerial) Serial.printf("[UPLOAD] Starting upload of %s\n", filename.c_str());
      String filepath = "/" + filename;
      uploadFile = LittleFS.open(filepath, "w");
      if (!uploadFile) {
        if (debugSerial) Serial.printf("[UPLOAD] Failed to open %s for writing\n", filepath.c_str());
        req->send(500, "text/plain", "Failed to open file for writing");
        return;
      }
    }
    
    if (uploadFile) {
      // Write chunk to file
      uploadFile.write(data, len);
      if (debugSerial && index == 0) Serial.printf("[UPLOAD] Writing %u bytes to %s\n", len, filename.c_str());
    }
    
    if (final) {
      // Last chunk - close file
      if (uploadFile) {
        uploadFile.close();
        if (debugSerial) Serial.printf("[UPLOAD] Completed upload of %s\n", filename.c_str());
      }
    }
  });

  // --- File delete API endpoint ---
  server.on("/api/delete", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      DynamicJsonDocument doc(128);
      if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }
      const char* filename = doc["filename"] | "";
      if (strlen(filename) == 0) { req->send(400, "application/json", "{\"error\":\"No filename provided\"}"); return; }
      String filepath = "/" + String(filename);
      if (LittleFS.remove(filepath)) {
        if (debugSerial) Serial.printf("[DELETE] Deleted file: %s\n", filename);
        req->send(200, "application/json", "{\"status\":\"deleted\"}");
      } else {
        if (debugSerial) Serial.printf("[DELETE] Failed to delete file: %s\n", filename);
        req->send(500, "application/json", "{\"error\":\"Failed to delete file\"}");
      }
    }
  );

  // --- File list API endpoint ---
  server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest* req){
    DynamicJsonDocument doc(2048);
    JsonArray files = doc.createNestedArray("files");
    Dir dir = LittleFS.openDir("/");
    while (dir.next()) {
      JsonObject file = files.createNestedObject();
      file["name"] = dir.fileName();
      file["size"] = dir.fileSize();
    }
    String json; serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.on("/listfiles", HTTP_GET, [](AsyncWebServerRequest* req){
  String out = "Files:<br>";
  Dir dir = LittleFS.openDir("/");
  while (dir.next()) {
    out += dir.fileName() + "<br>";
  }
  req->send(200, "text/html", out);
});
server.serveStatic("/", LittleFS, "/");
  // Register OTA update handler for /update endpoint
  httpUpdater.setup(&server);
  server.begin();
  Serial.println("HTTP server started");
  wsStatusServer.begin();
  wsStatusServer.onEvent(wsStatusEvent);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  // --- Initialize PID controller ---
  myPID.SetMode(AUTOMATIC);
  myPID.SetOutputLimits(0, 1);  // Output range: 0 (0% duty) to 1 (100% duty)
  myPID.SetSampleTime(pidSampleTime);    // Use loaded sample time (relay-friendly)
  Setpoint = 0;  // Default to no heating
  windowStartTime = millis();   // Initialize time-proportional control
  lastPIDOutput = 0.0;          // Initialize last PID output for dynamic window adjustment
  
  if (debugSerial) {
    Serial.printf("[PID] Initialized with Kp=%.6f, Ki=%.6f, Kd=%.6f, SampleTime=%lums\n", 
                  Kp, Ki, Kd, pidSampleTime);
  }
  
  // --- Initialize temperature averaging system ---
  for (int i = 0; i < MAX_TEMP_SAMPLES; i++) {
    tempSamples[i] = 0.0;
  }
  tempSampleIndex = 0;
  tempSamplesReady = false;
  lastTempSample = 0;
  averagedTemperature = readTemperature(); // Initialize with first reading
  
  Serial.println("PID controller initialized with relay-friendly time-proportional control");
  Serial.println("Temperature averaging system initialized (10 samples, 0.5s interval, reject top/bottom 2)");
  
  // Wait for time sync
  while (time(nullptr) < 100000) delay(200);
}

// Remove duplicate loadPrograms() implementation from this file. Use the one in programs_manager.cpp.

// --- Resume state management ---
const char* RESUME_FILE = "/resume.json";
unsigned long lastResumeSave = 0;

void saveResumeState() {
  DynamicJsonDocument doc(1024); // Increased size for stage start times array
  doc["activeProgramName"] = activeProgramName;
  doc["customStageIdx"] = customStageIdx;
  doc["customMixIdx"] = customMixIdx;
  // Save elapsed time in current stage and mix step (in seconds)
  doc["elapsedStageSec"] = (customStageStart > 0) ? (millis() - customStageStart) / 1000UL : 0;
  doc["elapsedMixSec"] = (customMixStepStart > 0) ? (millis() - customMixStepStart) / 1000UL : 0;
  doc["programStartTime"] = programStartTime;
  doc["isRunning"] = isRunning;
  // Save actual stage start times
  JsonArray stageStartArray = doc.createNestedArray("actualStageStartTimes");
  for (int i = 0; i < 20; i++) {
    stageStartArray.add((uint32_t)actualStageStartTimes[i]);
  }
  File f = LittleFS.open(RESUME_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

void clearResumeState() {
  LittleFS.remove(RESUME_FILE);
}

void loadResumeState() {
  File f = LittleFS.open(RESUME_FILE, "r");
  if (!f) return;
  DynamicJsonDocument doc(1024); // Increased size for stage start times array
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  if (doc.containsKey("activeProgramName")) {
    strncpy(activeProgramName, doc["activeProgramName"], sizeof(activeProgramName)-1);
    activeProgramName[sizeof(activeProgramName)-1] = '\0';
  }
  customStageIdx = doc["customStageIdx"] | 0;
  customMixIdx = doc["customMixIdx"] | 0;
  unsigned long elapsedStageSec = doc["elapsedStageSec"] | 0;
  unsigned long elapsedMixSec = doc["elapsedMixSec"] | 0;
  programStartTime = doc["programStartTime"] | 0;
  bool wasRunning = doc["isRunning"] | false;
  
  // Check startup delay before resuming
  if (wasRunning && !isStartupDelayComplete()) {
    // Don't resume immediately - wait for startup delay
    isRunning = false;
    if (debugSerial) {
      unsigned long remaining = STARTUP_DELAY_MS - (millis() - startupTime);
      Serial.printf("[RESUME] Delaying resume for %lu ms (temperature sensor stabilization)\n", remaining);
    }
  } else {
    isRunning = wasRunning;
  }
  
  // Load actual stage start times
  for (int i = 0; i < 20; i++) actualStageStartTimes[i] = 0;
  if (doc.containsKey("actualStageStartTimes") && doc["actualStageStartTimes"].is<JsonArray>()) {
    JsonArray stageStartArray = doc["actualStageStartTimes"];
    for (int i = 0; i < min(20, (int)stageStartArray.size()); i++) {
      actualStageStartTimes[i] = stageStartArray[i];
    }
  }
  // Fast-forward logic: if elapsed time > stage/mix durations, advance indices
  if (isRunning && programs.count(activeProgramName)) {
    Program &p = programs[activeProgramName];
    // Fast-forward stages
    size_t stageIdx = customStageIdx;
    unsigned long remainStageSec = elapsedStageSec;
    while (stageIdx < p.customStages.size()) {
      unsigned long stageDurSec = p.customStages[stageIdx].min * 60UL;
      if (remainStageSec < stageDurSec) break;
      remainStageSec -= stageDurSec;
      stageIdx++;
    }
    if (stageIdx >= p.customStages.size()) {
      // Program finished
      customStageIdx = 0;
      customMixIdx = 0;
      customStageStart = 0;
      customMixStepStart = 0;
      isRunning = false;
      clearResumeState();
      return;
    }
    customStageIdx = stageIdx;
    customStageStart = millis() - remainStageSec * 1000UL;
    if (customStageStart == 0) customStageStart = millis(); // Ensure nonzero for UI
    // Fast-forward mix steps if mixPattern exists
    CustomStage &st = p.customStages[customStageIdx];
    if (!st.mixPattern.empty()) {
      size_t mixIdx = 0;
      unsigned long remainMixSec = elapsedMixSec;
      while (mixIdx < st.mixPattern.size()) {
        unsigned long mixDurSec = st.mixPattern[mixIdx].mixSec + st.mixPattern[mixIdx].waitSec;
        if (remainMixSec < mixDurSec) break;
        remainMixSec -= mixDurSec;
        mixIdx++;
      }
      if (mixIdx >= st.mixPattern.size()) mixIdx = 0;
      customMixIdx = mixIdx;
      customMixStepStart = millis() - remainMixSec * 1000UL;
      if (customMixStepStart == 0) customMixStepStart = millis(); // Ensure nonzero for UI
    } else {
      customMixIdx = 0;
      customMixStepStart = millis(); // Ensure nonzero for UI
    }
  } else {
    // Not running or invalid program
    customStageStart = millis() - elapsedStageSec * 1000UL;
    customMixStepStart = millis() - elapsedMixSec * 1000UL;
    // Ensure all outputs are OFF if not running
    setHeater(false);
    setMotor(false);
    setLight(false);
    setBuzzer(false);
  }
}

void loop() {
  // Update temperature sampling (non-blocking, every 0.5s)
  updateTemperatureSampling();
  
  wsStatusServer.loop();
  
  // Update temperature for broadcasts
  float temp = getAveragedTemperature();
  if (fabs(temp - lastTemp) >= 0.1) {
    lastTemp = temp;
    broadcastStatusWSIfChanged();
  }
  
  if (!isRunning) {
    // Check if we should resume after startup delay
    static bool delayedResumeChecked = false;
    if (!delayedResumeChecked && isStartupDelayComplete()) {
      delayedResumeChecked = true;
      // Try to resume if there was a saved state indicating we were running
      File f = LittleFS.open(RESUME_FILE, "r");
      if (f) {
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (!err && (doc["isRunning"] | false)) {
          if (debugSerial) Serial.println("[RESUME] Startup delay complete, resuming program");
          loadResumeState(); // Reload and apply the full resume state
        }
      }
    }
    
    // Ensure all outputs are OFF when idle and not scheduled to start
    setHeater(false);
    setMotor(false);
    setLight(false);
    setBuzzer(false);
    if (scheduledStart && time(nullptr) >= scheduledStart) {
      scheduledStart = 0;
      isRunning = true;
      customStageIdx = 0;
      customMixIdx = 0;
      customStageStart = millis();
      customMixStepStart = 0;
      programStartTime = time(nullptr);
      // Initialize actual stage start times array for scheduled start
      for (int i = 0; i < 20; i++) actualStageStartTimes[i] = 0;
      actualStageStartTimes[0] = programStartTime;
      saveResumeState(); // Save on scheduled start
    }
    yield(); delay(1); return;
  }
  if (programs.count(activeProgramName) == 0) {
    stopBreadmaker(); yield(); delay(1); return;
  }
  Program &p = programs[activeProgramName];
  // --- Custom Stages Logic ---
  if (!p.customStages.empty()) {
    if (customStageIdx >= p.customStages.size()) {
      stopBreadmaker(); customStageIdx = 0; customStageStart = 0; clearResumeState(); yield(); delay(1); return;
    }
    CustomStage &st = p.customStages[customStageIdx];
    Setpoint = st.temp;
    Input = getAveragedTemperature();
    
    // Only run automatic control if not in manual mode
    if (!manualMode) {
      // --- Heater logic: PID control based on stage temperature ---
      if (st.temp > 0) {
        myPID.Compute();
        updateTimeProportionalHeater();  // Use time-proportional control
      } else {
        setHeater(false);  // No heating if temperature not set
        windowStartTime = 0;  // Reset window timing
        lastPIDOutput = 0.0;  // Reset last PID output
      }
      setLight(false);
      setBuzzer(false);
    }
    // --- Updated mix logic for new format and noMix flag ---
    if (!manualMode) {
      bool hasMix = false;
      if (st.noMix) {
        setMotor(false);
        hasMix = false;
      } else if (!st.mixPattern.empty()) {
        hasMix = true;
        // Ensure index is within bounds (safety check)
        if (customMixIdx >= st.mixPattern.size()) {
          customMixIdx = 0;
          if (debugSerial) Serial.printf("[MIX] Index out of bounds, reset to 0 (total patterns: %d)\n", st.mixPattern.size());
        }
        MixStep &step = st.mixPattern[customMixIdx];
        unsigned long stepDuration = (step.durationSec > 0) ? (unsigned long)step.durationSec : (unsigned long)(step.mixSec + step.waitSec);
        if (customMixStepStart == 0) {
          customMixStepStart = millis();
          if (debugSerial) {
            if (stepDuration > (unsigned long)(step.mixSec + step.waitSec)) {
              unsigned long expectedCycles = stepDuration / (step.mixSec + step.waitSec);
              Serial.printf("[MIX] Starting pattern %d/%d: mix=%ds, wait=%ds, duration=%ds (≈%lu cycles)\n", 
                          customMixIdx + 1, st.mixPattern.size(), step.mixSec, step.waitSec, stepDuration, expectedCycles);
            } else {
              Serial.printf("[MIX] Starting pattern %d/%d: mix=%ds, wait=%ds, duration=%ds (single cycle)\n", 
                          customMixIdx + 1, st.mixPattern.size(), step.mixSec, step.waitSec, stepDuration);
            }
          }
        }
        unsigned long elapsed = (millis() - customMixStepStart) / 1000UL;
        
        if (stepDuration > (unsigned long)(step.mixSec + step.waitSec)) {
          // Pattern should repeat within the stepDuration
          unsigned long cycleTime = step.mixSec + step.waitSec;
          unsigned long cycleElapsed = elapsed % cycleTime;
          
          if (cycleElapsed < (unsigned long)step.mixSec) {
            setMotor(true);
            if (debugSerial && (elapsed / cycleTime) != ((elapsed - 1) / cycleTime)) {
              Serial.printf("[MIX] Pattern %d cycle %lu: mixing (%lus elapsed)\n", customMixIdx + 1, elapsed / cycleTime + 1, elapsed);
            }
          } else {
            setMotor(false);
          }
          
          // Check if stepDuration is complete
          if (elapsed >= stepDuration) {
            // Advance to next mix step
            customMixIdx++;
            customMixStepStart = millis(); // Reset timing for next step
            if (customMixIdx >= st.mixPattern.size()) {
              // Pattern array complete, repeat from beginning
              customMixIdx = 0;
              if (debugSerial) Serial.printf("[MIX] All %d patterns complete, restarting from pattern 0\n", st.mixPattern.size());
            } else {
              if (debugSerial) Serial.printf("[MIX] Advancing to pattern %d\n", customMixIdx);
            }
          }
        } else {
          // Simple single-cycle pattern (legacy behavior)
          if (elapsed < (unsigned long)step.mixSec) {
            setMotor(true);
          } else if (elapsed < (unsigned long)(step.mixSec + step.waitSec)) {
            setMotor(false);
          } else if (elapsed < stepDuration) {
            setMotor(false);
          } else {
            // Advance to next mix step
            customMixIdx++;
            customMixStepStart = millis(); // Reset timing for next step
            if (customMixIdx >= st.mixPattern.size()) {
              // Pattern array complete, repeat from beginning
              customMixIdx = 0;
              if (debugSerial) Serial.printf("[MIX] All %d patterns complete, restarting from pattern 0\n", st.mixPattern.size());
            } else {
              if (debugSerial) Serial.printf("[MIX] Advancing to pattern %d\n", customMixIdx);
            }
          }
        }
      } else {
        // If neither noMix nor mixPattern, motor should be ON for the whole stage
        setMotor(true);
        hasMix = false;
      }
    }
    if (millis() - customStageStart >= (unsigned long)st.min * 60000UL) {
      if (debugSerial) Serial.printf("[STAGE] Stage %d (%s) complete after %lu seconds, advancing to stage %d\n", 
                                    customStageIdx, st.label.c_str(), (millis() - customStageStart) / 1000UL, customStageIdx + 1);
      customStageIdx++;
      customStageStart = millis();
      customMixIdx = 0;
      customMixStepStart = 0;
      // Record actual start time of this stage
      if (customStageIdx < 20) actualStageStartTimes[customStageIdx] = time(nullptr);
      if (customStageIdx >= p.customStages.size()) {
        if (debugSerial) Serial.println("[STAGE] All stages complete, stopping program");
        stopBreadmaker(); customStageIdx = 0; customStageStart = 0; clearResumeState(); yield(); delay(1); return;
      } else {
        if (debugSerial) Serial.printf("[STAGE] Starting stage %d (%s)\n", customStageIdx, p.customStages[customStageIdx].label.c_str());
      }
    }
    yield(); delay(1); return;
  }
  // Housekeeping: auto-off light, buzzer
  if (!manualMode) {
    bool customLightOn = false;
    if (isRunning && programs.count(activeProgramName) > 0) {
      Program &p = programs[activeProgramName];
      if (!p.customStages.empty() && customStageIdx < p.customStages.size()) {
        CustomStage &st = p.customStages[customStageIdx];
        customLightOn = (st.light == "on");
      }
    }
    yield(); delay(1);
    if (!customLightOn && lightState && (millis() - lightOnTime > 30000)) setLight(false);
  }
  yield(); delay(1);
  if (buzzActive && millis() - buzzStart >= 200) setBuzzer(false);
  yield(); delay(1);
  if (scheduledStart && !isRunning) {
    if (time(nullptr) >= scheduledStart) {
      scheduledStart = 0;
      isRunning = true;
      customStageIdx = 0;
      customMixIdx = 0;
      customStageStart = millis();
      customMixStepStart = 0;
      programStartTime = time(nullptr);
      // Initialize actual stage start times array for scheduled start
      for (int i = 0; i < 20; i++) actualStageStartTimes[i] = 0;
      actualStageStartTimes[0] = programStartTime;
      saveResumeState(); // Save on scheduled start
    } else {
      yield(); delay(100);
      return;
    }
  }
  temp = getAveragedTemperature(); // Update temp instead of declaring new one
  if (fabs(temp - lastTemp) >= 0.1) {
    lastTemp = temp;
    broadcastStatusWSIfChanged();
  }
  yield(); delay(1);
}

// --- Temperature averaging functions ---
void updateTemperatureSampling() {
  unsigned long now = millis();
  
  // Sample at the configured interval
  if (now - lastTempSample >= tempSampleInterval) {
    lastTempSample = now;
    
    // Take a new temperature sample
    float rawTemp = readTemperature(); // This calls the calibrated temperature reading
    tempSamples[tempSampleIndex] = rawTemp;
    tempSampleIndex = (tempSampleIndex + 1) % tempSampleCount;
    
    // Mark samples as ready once we've filled the array at least once
    if (tempSampleIndex == 0 && !tempSamplesReady) {
      tempSamplesReady = true;
    }
    
    // Calculate averaged temperature if we have enough samples
    if (tempSamplesReady && tempSampleCount > 0) {
      // Ensure we have enough samples after rejection
      int effectiveSamples = tempSampleCount - (2 * tempRejectCount);
      if (effectiveSamples < 3) {
        // Not enough samples for advanced filtering, use simple average
        float sum = 0.0;
        for (int i = 0; i < tempSampleCount; i++) {
          sum += tempSamples[i];
        }
        averagedTemperature = sum / tempSampleCount;
      } else {
        // ADAPTIVE TREND-AWARE FILTERING
        
        // Step 1: Copy and sort samples for outlier rejection
        float sortedSamples[MAX_TEMP_SAMPLES];
        for (int i = 0; i < tempSampleCount; i++) {
          sortedSamples[i] = tempSamples[i];
        }
        
        // Simple bubble sort
        for (int i = 0; i < tempSampleCount - 1; i++) {
          for (int j = 0; j < tempSampleCount - i - 1; j++) {
            if (sortedSamples[j] > sortedSamples[j + 1]) {
              float temp = sortedSamples[j];
              sortedSamples[j] = sortedSamples[j + 1];
              sortedSamples[j + 1] = temp;
            }
          }
        }
        
        // Step 2: Get clean samples (reject outliers)
        float cleanSamples[MAX_TEMP_SAMPLES];
        int cleanCount = 0;
        for (int i = tempRejectCount; i < tempSampleCount - tempRejectCount; i++) {
          cleanSamples[cleanCount++] = sortedSamples[i];
        }
        
        // Step 3: Detect trend direction and strength
        float firstHalf = 0.0, secondHalf = 0.0;
        int halfSize = cleanCount / 2;
        
        // Average first half vs second half to detect trend
        for (int i = 0; i < halfSize; i++) {
          firstHalf += cleanSamples[i];
        }
        for (int i = halfSize; i < cleanCount; i++) {
          secondHalf += cleanSamples[i];
        }
        firstHalf /= halfSize;
        secondHalf /= (cleanCount - halfSize);
        
        float trendStrength = secondHalf - firstHalf; // Positive = rising, Negative = falling
        float absTrendStrength = fabs(trendStrength);
        
        // Step 4: Apply adaptive weighting based on trend
        float weightedSum = 0.0;
        float totalWeight = 0.0;
        
        if (absTrendStrength > 0.2) { // Significant trend detected (>0.2°C change across buffer)
          // TREND MODE: Weight recent samples more heavily to reduce lag
          for (int i = 0; i < cleanCount; i++) {
            // Exponential weighting: newer samples get more weight
            // Weight increases from 0.5 to 2.0 across the buffer
            float weight = 0.5 + (1.5 * i) / (cleanCount - 1);
            weightedSum += cleanSamples[i] * weight;
            totalWeight += weight;
          }
          
          if (debugSerial && (now % 30000 < tempSampleInterval)) {
            Serial.printf("[TEMP_TREND] Trend=%.2f°C, using weighted average for responsiveness\n", trendStrength);
          }
        } else {
          // STABLE MODE: Use balanced weighting for maximum smoothness
          for (int i = 0; i < cleanCount; i++) {
            // Slight preference for recent samples, but much more balanced
            float weight = 0.8 + (0.4 * i) / (cleanCount - 1);
            weightedSum += cleanSamples[i] * weight;
            totalWeight += weight;
          }
        }
        
        averagedTemperature = weightedSum / totalWeight;
        
        // Debug output occasionally
        if (debugSerial && (now % 30000 < tempSampleInterval)) { // Every 30 seconds
          Serial.printf("[TEMP_AVG] Raw: %.2f°C, Filtered: %.2f°C, Trend: %.2f°C (from %d samples, mode: %s)\n", 
                       rawTemp, averagedTemperature, trendStrength, cleanCount,
                       (absTrendStrength > 0.2) ? "TREND" : "STABLE");
        }
      }
    } else {
      // Not enough samples yet, use the raw reading
      averagedTemperature = rawTemp;
    }
  }
}

float getAveragedTemperature() {
  return averagedTemperature;
}

// --- Time-proportional PID control function ---
void updateTimeProportionalHeater() {
  unsigned long now = millis();
  
  // Initialize window start time if not set
  if (windowStartTime == 0) {
    windowStartTime = now;
    lastPIDOutput = Output; // Initialize last PID output
  }
  
  // Check for dynamic window restart conditions
  bool shouldRestartWindow = false;
  unsigned long elapsed = now - windowStartTime;
  
  // Dynamic window restart logic:
  // If PID output changes significantly AND we've been in this window for at least MIN_WINDOW_TIME
  if (elapsed >= MIN_WINDOW_TIME) {
    double outputChange = fabs(Output - lastPIDOutput);
    
    // Case 1: Large increase in output (e.g., needs more heat quickly)
    if (outputChange >= PID_CHANGE_THRESHOLD && Output > lastPIDOutput) {
      // If we're currently in the OFF part of the window and need much more heat
      if (elapsed >= onTime && Output > 0.7) { // Need >70% output but heater is OFF
        shouldRestartWindow = true;
        if (debugSerial) {
          Serial.printf("[PID-DYNAMIC] Restarting window: Output jumped from %.2f to %.2f (need more heat)\n", 
                       lastPIDOutput, Output);
        }
      }
    }
    
    // Case 2: Large decrease in output (e.g., overheating, need to turn off quickly)
    else if (outputChange >= PID_CHANGE_THRESHOLD && Output < lastPIDOutput) {
      // If we're currently in the ON part of the window and need much less heat
      if (elapsed < onTime && Output < 0.3) { // Need <30% output but heater is ON
        shouldRestartWindow = true;
        if (debugSerial) {
          Serial.printf("[PID-DYNAMIC] Restarting window: Output dropped from %.2f to %.2f (reduce heat)\n", 
                       lastPIDOutput, Output);
        }
      }
    }
  }
  
  // Check if we need to start a new window (normal timing or dynamic restart)
  if (now - windowStartTime >= windowSize || shouldRestartWindow) {
    // Track dynamic restart events
    if (shouldRestartWindow) {
      lastDynamicRestart = now;
      dynamicRestartCount++;
      
      // Determine restart reason
      if (Output > lastPIDOutput && Output > 0.7) {
        lastDynamicRestartReason = "Need more heat (output increased to " + String((Output*100), 1) + "%)";
      } else if (Output < lastPIDOutput && Output < 0.3) {
        lastDynamicRestartReason = "Reduce heat (output decreased to " + String((Output*100), 1) + "%)";
      } else {
        lastDynamicRestartReason = "Output change (from " + String((lastPIDOutput*100), 1) + "% to " + String((Output*100), 1) + "%)";
      }
      
      if (debugSerial) {
        Serial.printf("[PID-DYNAMIC] Restart #%u: %s (elapsed: %lums)\n", 
                     dynamicRestartCount, lastDynamicRestartReason.c_str(), elapsed);
      }
    }
    
    windowStartTime = now;
    lastPIDOutput = Output; // Update the last known PID output
    
    // Calculate ON time for this window based on PID output (0.0 to 1.0)
    onTime = (unsigned long)(Output * windowSize);
    
    // Relay protection: smart minimum ON/OFF times
    const unsigned long minOnTime = 1000;   // Minimum 1 second ON (was 5s - too aggressive!)
    const unsigned long minOffTime = 1000;  // Minimum 1 second OFF 
    const float minOutputThreshold = 0.02;  // Below 2% output, turn OFF completely
    
    // If output is very low, just turn OFF completely (avoid excessive short cycling)
    if (Output < minOutputThreshold) {
      onTime = 0;  // Complete OFF
    }
    // Otherwise apply minimum ON time only if above threshold
    else if (onTime > 0 && onTime < minOnTime) {
      onTime = minOnTime;  // Extend very short ON periods to protect relay
    }
    
    // Ensure minimum OFF time
    if (onTime > 0 && (windowSize - onTime) < minOffTime) {
      onTime = windowSize - minOffTime;  // Ensure minimum OFF time
    }
    
    if (debugSerial && millis() % 15000 < 50) {  // Debug every 15 seconds
      Serial.printf("[PID-RELAY] Setpoint: %.1f°C, Input: %.1f°C, Output: %.2f, OnTime: %lums/%lums (%.1f%%) %s\n", 
                   Setpoint, Input, Output, onTime, windowSize, (onTime * 100.0 / windowSize),
                   shouldRestartWindow ? "[DYNAMIC]" : "[NORMAL]");
    }
  }
  
  // Determine heater state based on position within current window
  elapsed = now - windowStartTime; // Recalculate elapsed after potential window restart
  bool heaterShouldBeOn = (elapsed < onTime);
  setHeater(heaterShouldBeOn);
}

// --- Enhanced heater status reporting for debugging ---
String getHeaterDebugStatus() {
  unsigned long now = millis();
  unsigned long elapsed = (windowStartTime > 0) ? (now - windowStartTime) : 0;
  float windowProgress = (windowSize > 0) ? ((float)elapsed / windowSize) * 100 : 0;
  float onTimePercent = (windowSize > 0) ? ((float)onTime / windowSize) * 100 : 0;
  
  DynamicJsonDocument doc(768); // Increased size for dynamic restart info
  doc["heater_physical_state"] = heaterState;
  doc["pid_output_percent"] = Output * 100;
  doc["setpoint"] = Setpoint;
  doc["manual_mode"] = manualMode;
  doc["is_running"] = isRunning;
  
  // Window timing details
  doc["window_size_ms"] = windowSize;
  doc["window_elapsed_ms"] = elapsed;
  doc["window_progress_percent"] = windowProgress;
  doc["calculated_on_time_ms"] = onTime;
  doc["on_time_percent"] = onTimePercent;
  doc["should_be_on"] = (elapsed < onTime);
  
  // Dynamic restart information
  doc["dynamic_restart_count"] = dynamicRestartCount;
  doc["last_restart_reason"] = lastDynamicRestartReason;
  doc["last_restart_ago_ms"] = (lastDynamicRestart > 0) ? (now - lastDynamicRestart) : 0;
  doc["last_pid_output"] = lastPIDOutput;
  doc["pid_output_change"] = abs(Output - lastPIDOutput);
  doc["change_threshold"] = PID_CHANGE_THRESHOLD;
  
  // Dynamic restart conditions analysis
  bool canRestartNow = (elapsed >= MIN_WINDOW_TIME);
  bool significantChange = (abs(Output - lastPIDOutput) >= PID_CHANGE_THRESHOLD);
  bool needMoreHeat = (Output > lastPIDOutput && elapsed >= onTime && Output > 0.7);
  bool needLessHeat = (Output < lastPIDOutput && elapsed < onTime && Output < 0.3);
  
  doc["can_restart_now"] = canRestartNow;
  doc["significant_change"] = significantChange;
  doc["restart_condition_more_heat"] = needMoreHeat;
  doc["restart_condition_less_heat"] = needLessHeat;
  
  // Safety overrides
  doc["setpoint_override"] = (Setpoint <= 0);
  doc["running_override"] = !isRunning;
  doc["manual_override"] = manualMode;
  
  // Relay protection analysis
  unsigned long theoreticalOnTime = (unsigned long)(Output * windowSize);
  unsigned long minOffTime =  5000;
  bool relayProtectionActive = (theoreticalOnTime > 0 && (windowSize - theoreticalOnTime) < minOffTime);
  doc["relay_protection_active"] = relayProtectionActive;
  doc["theoretical_on_time_ms"] = theoreticalOnTime;
  doc["enforced_min_off_time_ms"] = minOffTime;
  
  String result;
  serializeJson(doc, result);
  return result;
}

// --- Startup delay check for temperature sensor stabilization ---
bool isStartupDelayComplete() {
  unsigned long elapsed = millis() - startupTime;
  return elapsed >= STARTUP_DELAY_MS;
}

void stopBreadmaker() {
  if (debugSerial) Serial.println("[ACTION] stopBreadmaker() called");
  isRunning = false;
  setHeater(false);
  setMotor(false);
  setLight(false);
  setBuzzer(true); // Buzz for 200ms (handled non-blocking in loop)
  clearResumeState();
}

void loadSettings() {
  Serial.println("[loadSettings] Loading settings...");
  File f = LittleFS.open(SETTINGS_FILE, "r");
  if (!f) {
    Serial.println("[loadSettings] settings.json not found, using defaults.");
    debugSerial = true;
    return;
  }
  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, f);
  if (err) {
    Serial.print("[loadSettings] Failed to parse settings.json: ");
    Serial.println(err.c_str());
    f.close();
    debugSerial = true;
    return;
  }
  const char* mode = doc["outputMode"] | "analog";
  outputMode = (strcmp(mode, "digital") == 0) ? OUTPUT_DIGITAL : OUTPUT_ANALOG;
  debugSerial = doc["debugSerial"] | true;
  
  // Load PID parameters
  if (doc.containsKey("pidKp")) {
    Kp = doc["pidKp"] | 2.0;
    Ki = doc["pidKi"] | 5.0;
    Kd = doc["pidKd"] | 1.0;
    pidSampleTime = doc["pidSampleTime"] | 1000;
    windowSize = doc["pidWindowSize"] | 30000;
    
    // Apply loaded PID parameters
    myPID.SetTunings(Kp, Ki, Kd);
    myPID.SetSampleTime(pidSampleTime);
    
    Serial.printf("[loadSettings] PID parameters loaded: Kp=%.6f, Ki=%.6f, Kd=%.6f\n", Kp, Ki, Kd);
    Serial.printf("[loadSettings] Timing parameters loaded: SampleTime=%lums, WindowSize=%lums\n", pidSampleTime, windowSize);
  }
  
  // Load temperature averaging parameters
  if (doc.containsKey("tempSampleCount")) {
    tempSampleCount = doc["tempSampleCount"] | 10;
    tempRejectCount = doc["tempRejectCount"] | 2;
    tempSampleInterval = doc["tempSampleInterval"] | 500;
    
    // Validate parameters
    if (tempSampleCount < 5) tempSampleCount = 5;
    if (tempSampleCount > MAX_TEMP_SAMPLES) tempSampleCount = MAX_TEMP_SAMPLES;
    if (tempRejectCount < 0) tempRejectCount = 0;
    
    // Ensure we have at least 3 samples after rejection
    int effectiveSamples = tempSampleCount - (2 * tempRejectCount);
    if (effectiveSamples < 3) {
      tempRejectCount = (tempSampleCount - 3) / 2;
    }
    
    // Reset sampling system when parameters change
    tempSamplesReady = false;
    tempSampleIndex = 0;
    
    Serial.printf("[loadSettings] Temperature averaging loaded: Samples=%d, Reject=%d, Interval=%lums\n", 
                 tempSampleCount, tempRejectCount, tempSampleInterval);
  }
  
  // Restore last selected program if present
  if (doc.containsKey("lastProgram")) {
    strncpy(activeProgramName, doc["lastProgram"], sizeof(activeProgramName)-1);
    activeProgramName[sizeof(activeProgramName)-1] = '\0';
    Serial.print("[loadSettings] lastProgram loaded: ");
    Serial.println(activeProgramName);
  }
  Serial.print("[loadSettings] outputMode loaded: ");
  Serial.println(mode);
  Serial.print("[loadSettings] debugSerial: ");
  Serial.println(debugSerial ? "true" : "false");
  f.close();
  Serial.println("[loadSettings] Settings loaded.");
}
void saveSettings() {
  Serial.println("[saveSettings] Saving settings...");
  DynamicJsonDocument doc(768);
  doc["outputMode"] = (outputMode == OUTPUT_DIGITAL) ? "digital" : "analog";
  doc["debugSerial"] = debugSerial;
  doc["lastProgram"] = activeProgramName;
  // Save PID parameters
  doc["pidKp"] = Kp;
  doc["pidKi"] = Ki;
  doc["pidKd"] = Kd;
  doc["pidSampleTime"] = pidSampleTime;
  doc["pidWindowSize"] = windowSize;
  // Save temperature averaging parameters
  doc["tempSampleCount"] = tempSampleCount;
  doc["tempRejectCount"] = tempRejectCount;
  doc["tempSampleInterval"] = tempSampleInterval;
  File f = LittleFS.open(SETTINGS_FILE, "w");
  if (!f) {
    Serial.println("[saveSettings] Failed to open settings.json for writing!");
    return;
  }
  serializeJson(doc, f);
  f.close();
  Serial.println("[saveSettings] Settings saved with PID and temperature averaging parameters.");
}

// --- Last sent status cache ---
String lastStatusJson;

// --- Helper: Serialize current status to JSON string ---
String getStatusJson() {
  DynamicJsonDocument d(2048);
 
 
  d["scheduledStart"] = (uint32_t)scheduledStart;
  d["running"] = isRunning;
  d["program"] = activeProgramName;
  unsigned long nowMs = millis();
  time_t now = time(nullptr);
  unsigned long es = 0;
  int stageLeft = 0;
  time_t stageReadyAt = 0;
  time_t programReadyAt = 0;
  if (programs.count(activeProgramName)) {
    Program &p = programs[activeProgramName];
    // If not running, set stage to "Idle"
    if (!isRunning) {
      d["stage"] = "Idle";
    } else if (customStageIdx < p.customStages.size()) {
      d["stage"] = p.customStages[customStageIdx].label.c_str();
    } else {
      d["stage"] = "";
    }
    // Custom stages: calculate stage start times based on durations and programStartTime
    JsonArray arr = d.createNestedArray("stageStartTimes");
    time_t t = programStartTime;
    for (size_t i = 0; i < p.customStages.size(); ++i) {
      arr.add((uint32_t)t);
      t += (time_t)p.customStages[i].min * 60;
    }
    d["programStart"] = (uint32_t)programStartTime;
    // Elapsed time in current stage
    es = (customStageStart == 0) ? 0 : (nowMs - customStageStart) / 1000;
    // Calculate stageReadyAt and programReadyAt based on actual stage start times
    stageReadyAt = 0;
    programReadyAt = 0;
    
    if (customStageIdx < p.customStages.size()) {
      // Current stage ready time = actual start time + duration
      if (actualStageStartTimes[customStageIdx] > 0) {
        stageReadyAt = actualStageStartTimes[customStageIdx] + (time_t)p.customStages[customStageIdx].min * 60;
      }
      
      // Program ready time calculation: current time + time left in current stage + remaining stages
      if (actualStageStartTimes[customStageIdx] > 0) {
        // Calculate time left in current stage based on actual elapsed time
        unsigned long actualElapsedInStage = (millis() - customStageStart) / 1000UL;
        unsigned long plannedStageDuration = (unsigned long)p.customStages[customStageIdx].min * 60UL;
        unsigned long timeLeftInCurrentStage = (actualElapsedInStage < plannedStageDuration) ? 
                                               (plannedStageDuration - actualElapsedInStage) : 0;
        
        if (debugSerial) {
          Serial.printf("[TIMING] Stage %d: elapsed=%lus, planned=%lus, timeLeft=%lus\n", 
                       customStageIdx, actualElapsedInStage, plannedStageDuration, timeLeftInCurrentStage);
        }
        
        // Start from current time + actual time left in current stage
        programReadyAt = now + (time_t)timeLeftInCurrentStage;
        // Add durations of all remaining stages after current stage
        for (size_t i = customStageIdx + 1; i < p.customStages.size(); ++i) {
          programReadyAt += (time_t)p.customStages[i].min * 60;
        }
        
        if (debugSerial) {
          Serial.printf("[TIMING] ProgramReadyAt: %lu (in %ld seconds from now)\n", 
                       programReadyAt, (programReadyAt - now));
        }
      } else {
        // Fallback to programStartTime if no actual times recorded
        programReadyAt = programStartTime;
        for (size_t i = 0; i < p.customStages.size(); ++i) {
          programReadyAt += (time_t)p.customStages[i].min * 60;
        }
      }
    }
    // Time left in current stage
    if (customStageIdx < p.customStages.size()) {
      stageLeft = max(0, (int)(stageReadyAt - now));
    }
  }
  d["elapsed"] = es;
  d["setTemp"] = Setpoint;
  d["timeLeft"] = stageLeft;
  d["stageReadyAt"] = stageReadyAt;
  d["programReadyAt"] = programReadyAt;
  d["temp"] = getAveragedTemperature();
  d["motor"] = motorState;
  d["light"] = lightState;
  d["buzzer"] = buzzerState;
  d["stageStartTime"] = (uint32_t)customStageStart;
  d["manualMode"] = manualMode;
  
  // Add startup delay information
  d["startupDelayComplete"] = isStartupDelayComplete();
  if (!isStartupDelayComplete()) {
    d["startupDelayRemainingMs"] = STARTUP_DELAY_MS - (millis() - startupTime);
  }
  
  // Add actual stage start times (when stages actually started)
  JsonArray actualStarts = d.createNestedArray("actualStageStartTimes");
  for (int i = 0; i < 20; i++) {
    actualStarts.add((uint32_t)actualStageStartTimes[i]);
  }
  
  JsonArray progs = d.createNestedArray("programList");
  for (auto it = programs.begin(); it != programs.end(); ++it) progs.add(it->first);
  String json;
  serializeJson(d, json);
  return json;
}

// --- WebSocket event handler ---
void wsStatusEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    // Send current status on connect
    String status = getStatusJson();
    wsStatusServer.sendTXT(num, status);
  }
}

// --- Broadcast status if changed ---
void broadcastStatusWSIfChanged() {
  String current = getStatusJson();
  if (current != lastStatusJson) {
    lastStatusJson = current;
    wsStatusServer.broadcastTXT(current);
  }
}

