#include <functional>

// Helper function to send a JSON error response
void sendJsonError(AsyncWebServerRequest* req, const String& error, const String& message, int code = 400) {
  AsyncResponseStream *response = req->beginResponseStream("application/json");
  response->print("{");
  response->print("\"error\":\"");
  response->print(error);
  response->print("\",");
  response->print("\"message\":\"");
  response->print(message);
  response->print("\"}");
  req->send(response);
}
// Maximum number of program stages supported
#ifndef MAX_PROGRAM_STAGES
#define MAX_PROGRAM_STAGES 20
#endif


// (Predicted times will be moved to the status endpoint instead)
// --- Temperature safety flags ---
bool thermalRunawayDetected = false;
bool sensorFaultDetected = false;
// --- Active program index (ID) ---
unsigned int activeProgramId = 0; // Index of the active program in programNamesOrdered
/*
FEATURES:
 - Replaces breadmaker logic board: controls motor, heater, light
 - Manual icon toggles (motor, heater, light, buzzer)
 - LittleFS for persistent storage (programs, UI)
 - Status shows stage, time left (d:h:m:s), state of outputs, temperature (RTD analog input on A0)

*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
//#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

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
//#include <WebSocketsServer.h>

// --- Firmware build date ---
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// --- Web server and OTA ---
AsyncWebServer server(80); // Main web server

//WebSocketsServer wsStatusServer = WebSocketsServer(81); // WebSocket server for status changes

// --- Pin assignments ---
const int PIN_RTD = A0;     // RTD analog input
// (Output pins are defined in outputs_manager.cpp)

// --- PID parameters for temperature control ---
double Setpoint, Input, Output;
double Kp=2.0, Ki=5.0, Kd=1.0;
unsigned long pidSampleTime = 1000;  // PID sample time in milliseconds
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);
// --- PID component terms for debug/graphing ---
double pidP = 0.0, pidI = 0.0, pidD = 0.0;
double lastInput = 0.0; // For D calculation
double lastITerm = 0.0; // For I calculation

// --- Time-proportional control variables ---
unsigned long windowStartTime = 0;
unsigned long windowSize = 30000;        // 30 second window in milliseconds (relay-friendly, now configurable)
unsigned long onTime = 0;                // ON time in milliseconds within current window
double lastPIDOutput = 0.0;              // Previous PID output for dynamic window adjustment
const double PID_CHANGE_THRESHOLD = 0.3; // Threshold for significant PID output change (30%)
const unsigned long MIN_WINDOW_TIME = 5000; // Minimum time before allowing dynamic restart (5 seconds)

time_t scheduledStart = 0; // Scheduled start time (epoch)
int scheduledStartStage = -1; // Stage to start at when scheduled (-1 = start from beginning)

// --- State variables ---
bool isRunning = false;           // Is the breadmaker running?
// Removed activeProgramName and programNamesOrdered; use activeProgramId and programs[activeProgramId].name instead

// --- Custom stage state variables (global, for customStages logic) ---
size_t customStageIdx = 0;         // Index of current custom stage
size_t customMixIdx = 0;           // Index of current mix step within stage
unsigned long customStageStart = 0;    // Start time (ms) of current custom stage
unsigned long customMixStepStart = 0;  // Start time (ms) of current mix step
time_t programStartTime = 0;           // Absolute program start time (seconds since epoch)
time_t actualStageStartTimes[MAX_PROGRAM_STAGES];      // Array to store actual start times of each stage (max 20 stages)

// --- Active program variables ---
Program* customProgram = nullptr;  // Pointer to current active program
size_t maxCustomStages = 0;        // Number of stages in current program

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

// --- Fermentation tracking variables ---
float initialFermentTemp = 0.0;
float fermentationFactor = 1.0;
unsigned long lastFermentAdjust = 0;
unsigned long predictedCompleteTime = 0;
// New: Weighted fermentation time tracking
float fermentLastTemp = 0.0;
float fermentLastFactor = 1.0;
unsigned long fermentLastUpdateMs = 0;
double fermentWeightedSec = 0.0;

// --- Function prototypes ---
void loadSettings();
void saveSettings();
void updateTemperatureSampling();
float getAveragedTemperature();
void streamStatusJson(Print& out);

// --- Forward declaration for stopBreadmaker (must be before any use) ---
void stopBreadmaker();

// --- Forward declaration for startup delay check ---
bool isStartupDelayComplete();

// --- Delete Folder API endpoint ---
void deleteFolderRecursive(const String& path) {
  Dir dir = LittleFS.openDir(path);
  while (dir.next()) {
    String filePath = path + "/" + dir.fileName();
    if (dir.isDirectory()) {
      deleteFolderRecursive(filePath);
    } else {
      LittleFS.remove(filePath);
    }
  }
  LittleFS.rmdir(path);
}


// --- Core state machine endpoints ---
void setup() {
  // Start mDNS responder for breadmaker.local
  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin("breadmaker")) {
      Serial.println(F("[mDNS] mDNS responder started: http://breadmaker.local/"));
    } else {
      Serial.println(F("[mDNS] Error setting up MDNS responder!"));
    }
  }
  // --- Serve files from /templates/ for Home Assistant YAML downloads ---
  server.on("/templates/", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("file")) {
      req->send(400, "text/plain", "Missing file parameter");
      return;
    }
    String filename = req->getParam("file")->value();
    if (filename.indexOf("..") >= 0 || filename.indexOf('/') >= 0) {
      req->send(400, "text/plain", "Invalid filename");
      return;
    }
    String path = "/templates/" + filename;
    if (!LittleFS.exists(path)) {
      req->send(404, "text/plain", "File not found");
      return;
    }
    String contentType = "text/plain";
    if (filename.endsWith(".yaml")) contentType = "text/yaml";
    else if (filename.endsWith(".json")) contentType = "application/json";
    else if (filename.endsWith(".html")) contentType = "text/html";
    req->send(LittleFS, path, contentType);
  });
  // --- Ensure all outputs are OFF immediately on boot/reset ---
  setHeater(false);
  setMotor(false);
  setLight(false);
  setBuzzer(false);

  startupTime = millis(); // Record startup time for sensor stabilization delay
  Serial.begin(115200);
  delay(100);
  Serial.println(F("[setup] Booting..."));
  if (!LittleFS.begin()) {
    Serial.println(F("[setup] LittleFS mount failed! Halting."));
    while (1) delay(1000);
  }
  Serial.println(F("[setup] LittleFS mounted."));
  Serial.println(F("[setup] Loading programs..."));
  loadPrograms();
  Serial.print(F("[setup] Programs loaded. Count: "));
  Serial.println(programs.size());
  updateActiveProgramVars(); // Initialize program variables after loading
  loadCalibration();
  Serial.println(F("[setup] Calibration loaded."));
  loadSettings();
  Serial.println(F("[setup] Settings loaded."));
  loadResumeState();
  Serial.println(F("[setup] Resume state loaded."));

  // --- Patch: Only set actualStageStartTimes[customStageIdx] if it was never set (i.e., new stage, not resume) ---
  // Do NOT reset actualStageStartTimes[customStageIdx] on resume, so stage time left is preserved.
  // (No action needed here; handled in /resume endpoint)

  // --- WiFi connection debug output ---
  Serial.println(F("[wifi] Reading WiFi config..."));
  // --- Load disableHotspot setting ---
  bool disableHotspot = false;
  {
    File sf = LittleFS.open("/settings.json", "r");
    if (sf) {
      DynamicJsonDocument sdoc(512);
      if (!deserializeJson(sdoc, sf)) {
        disableHotspot = sdoc["disableHotspot"] | false;
      }
      sf.close();
    }
  }
  File wf = LittleFS.open("/wifi.json", "r");
  if (!wf) {
    Serial.println(F("[wifi] wifi.json not found! Starting AP mode."));
    if (!disableHotspot) {
      WiFi.mode(WIFI_AP);
      startCaptivePortal();
    }
  } else {
    DynamicJsonDocument wdoc(256);
    DeserializationError werr = deserializeJson(wdoc, wf);
    if (werr) {
      Serial.print(F("[wifi] Failed to parse wifi.json: "));
      Serial.println(werr.c_str());
      if (!disableHotspot) {
        WiFi.mode(WIFI_AP);
        startCaptivePortal();
      }
    } else {
      const char* ssid = wdoc["ssid"] | "";
      const char* pass = wdoc["pass"] | "";
      Serial.print(F("[wifi] Connecting to SSID: "));
      Serial.println(ssid);
      WiFi.mode(disableHotspot ? WIFI_STA : WIFI_AP_STA); // Only AP+STA if not disabled
      WiFi.begin(ssid, pass);
      int tries = 0;
      while (WiFi.status() != WL_CONNECTED && tries < 40) {
        delay(250);
        Serial.print(F("."));
        tries++;
      }
      Serial.println();
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print(F("[wifi] Connected! IP address: "));
        Serial.println(WiFi.localIP());
      } else {
        Serial.println(F("[wifi] Failed to connect. Starting AP mode."));
        if (!disableHotspot) {
          WiFi.mode(WIFI_AP);
          startCaptivePortal();
        }
      }
    }
    wf.close();
  }

  // --- API endpoint to disable AP/hotspot ---
  server.on("/api/disable_ap", HTTP_POST, [](AsyncWebServerRequest* req){
    // Save disableHotspot=true to settings.json
    File sf = LittleFS.open("/settings.json", "r");
    DynamicJsonDocument sdoc(512);
    if (sf) {
      DeserializationError err = deserializeJson(sdoc, sf);
      sf.close();
      if (err) sdoc.clear();
    }
    sdoc["disableHotspot"] = true;
    File sfw = LittleFS.open("/settings.json", "w");
    if (sfw) {
      serializeJson(sdoc, sfw);
      sfw.close();
    }
    WiFi.softAPdisconnect(true); // Disable AP
    WiFi.mode(WIFI_STA); // Switch to STA only
    req->send(200, "application/json", "{\"status\":\"ap_disabled\"}");
  });
  // --- API endpoint: WiFi status (current SSID, IP, available networks) ---
  server.on("/api/wifi_status", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->printf("\"connected\":%s,", (WiFi.status() == WL_CONNECTED) ? "true" : "false");
    response->printf("\"ip\":\"%s\",", WiFi.localIP().toString().c_str());
    response->printf("\"ssid\":\"%s\",", WiFi.SSID().c_str());
    response->printf("\"bssid\":\"%s\",", WiFi.BSSIDstr().c_str());
    response->printf("\"rssi\":%d,", WiFi.RSSI());
    // Scan for available networks (always, even in STA mode)
    int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
    response->print("\"networks\":[");
    for (int i = 0; i < n; ++i) {
      if (i > 0) response->print(",");
      response->print("{");
      response->printf("\"ssid\":\"%s\",", WiFi.SSID(i).c_str());
      response->printf("\"rssi\":%d,", WiFi.RSSI(i));
      response->printf("\"bssid\":\"%s\",", WiFi.BSSIDstr(i).c_str());
      response->printf("\"secure\":%s", WiFi.encryptionType(i) != ENC_TYPE_NONE ? "true" : "false");
      response->print("}");
    }
    response->print("]}");
    req->send(response);
  });
  // --- Ensure all outputs are OFF at boot ---
  setHeater(false);
  setMotor(false);
  setLight(false);
  setBuzzer(false);
  outputsManagerInit(); // Initialize output pins and set all outputs OFF

  // --- Core state machine endpoints ---
  server.on("/start", HTTP_GET, [](AsyncWebServerRequest*req){
    if (debugSerial) Serial.println(F("[ACTION] /start called"));
    
    // Debug: print active program info
    if (debugSerial && activeProgramId < programs.size()) Serial.printf_P(PSTR("[START] activeProgram='%s' (id=%u), customProgram=%p\n"), programs[activeProgramId].name.c_str(), (unsigned)activeProgramId, (void*)customProgram);

    // Check if startup delay has completed
    if (!isStartupDelayComplete()) {
      unsigned long remaining = STARTUP_DELAY_MS - (millis() - startupTime);
      if (debugSerial) Serial.printf_P(PSTR("[START] Startup delay: %lu ms remaining\n"), remaining);
      AsyncResponseStream *response = req->beginResponseStream("application/json");
      response->print("{");
      response->print("\"error\":\"Startup delay active\",");
      response->print("\"message\":\"Please wait for temperature sensor to stabilize\",");
      response->printf("\"remainingMs\":%lu", remaining);
      response->print("}");
      req->send(response); // Correct usage for AsyncResponseStream*
      return;
    }

    // Check if a program is selected and customProgram is valid
    if (!customProgram) {

      AsyncResponseStream *response = req->beginResponseStream("application/json");
      response->print("{");
      response->print("\"error\":\"No program selected\",");
      response->print("\"message\":\"Please select a program before starting.\"");
      response->print("}");
      req->send(response);
      return;
    }
    size_t numStages = customProgram->customStages.size();
    if (numStages == 0) {
      AsyncResponseStream *response = req->beginResponseStream("application/json");
      response->print("{");
      response->print("\"error\":\"No stages defined\",");
      response->print("\"message\":\"The selected program has no stages.\"");
      response->print("}");
      req->send(response);
      return;
    }

    isRunning = true;
    customStageIdx = 0;
    customMixIdx = 0;
    customStageStart = millis();
    customMixStepStart = 0;
    programStartTime = time(nullptr);
    // Initialize actual stage start times array
    for (int i = 0; i < MAX_PROGRAM_STAGES; i++) actualStageStartTimes[i] = 0;
    actualStageStartTimes[0] = programStartTime; // Record actual start of first stage

    // --- Fermentation tracking: set immediately if first stage is fermentation ---
    initialFermentTemp = 0.0;
    fermentationFactor = 1.0;
    predictedCompleteTime = 0;
    lastFermentAdjust = 0;
    if (customProgram && !customProgram->customStages.empty()) {
      CustomStage &st = customProgram->customStages[0];
      if (st.isFermentation) {
        float baseline = customProgram->fermentBaselineTemp > 0 ? customProgram->fermentBaselineTemp : 20.0;
        float q10 = customProgram->fermentQ10 > 0 ? customProgram->fermentQ10 : 2.0;
        float actualTemp = getAveragedTemperature();
        initialFermentTemp = actualTemp;
        fermentationFactor = pow(q10, (baseline - actualTemp) / 10.0);
        unsigned long plannedStageMs = (unsigned long)st.min * 60000UL;
        unsigned long adjustedStageMs = plannedStageMs * fermentationFactor;
        predictedCompleteTime = millis() + adjustedStageMs;
        lastFermentAdjust = millis();
      }
    }
    saveResumeState();
    if (debugSerial) Serial.printf_P(PSTR("[STAGE] Entering custom stage: %d\n"), customStageIdx);
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest*req){
    if (debugSerial) Serial.println(F("[ACTION] /stop called"));
    updateActiveProgramVars();
    stopBreadmaker();
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/pause", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println(F("[ACTION] /pause called"));
    isRunning = false;
    setMotor(false);
    setHeater(false);
    setLight(false);
    saveResumeState();
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/resume", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println(F("[ACTION] /resume called"));
    isRunning = true;
    // Do NOT reset actualStageStartTimes[customStageIdx] on resume if it was already set.
    // Only set it if it was never set (i.e., just started this stage for the first time).
    if (customStageIdx < MAX_PROGRAM_STAGES && actualStageStartTimes[customStageIdx] == 0) {
      actualStageStartTimes[customStageIdx] = time(nullptr);
      if (debugSerial) Serial.printf_P(PSTR("[RESUME PATCH] Set actualStageStartTimes[%u] to now (%ld) on /resume (first entry to this stage).\n"), (unsigned)customStageIdx, (long)actualStageStartTimes[customStageIdx]);
    } else {
      if (debugSerial) Serial.printf_P(PSTR("[RESUME PATCH] actualStageStartTimes[%u] already set (%ld), not resetting on resume.\n"), (unsigned)customStageIdx, (long)actualStageStartTimes[customStageIdx]);
    }
    saveResumeState();
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/advance", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println(F("[ACTION] /advance called"));
    if (activeProgramId >= programs.size()) { stopBreadmaker(); AsyncResponseStream *response = req->beginResponseStream("application/json"); streamStatusJson(*response); req->send(response); return; }
    Program &p = programs[activeProgramId];
    customStageIdx++;
    if (customStageIdx >= p.customStages.size()) {
      stopBreadmaker(); customStageIdx = 0; customStageStart = 0; AsyncResponseStream *response = req->beginResponseStream("application/json"); streamStatusJson(*response); req->send(response); return;
    }
    customStageStart = millis();
    customMixIdx = 0;
    customMixStepStart = 0;
    isRunning = true;
    if (customStageIdx == 0) programStartTime = time(nullptr);
    // Record actual start time of this stage
    if (customStageIdx < MAX_PROGRAM_STAGES) actualStageStartTimes[customStageIdx] = time(nullptr);

    // --- Fermentation tracking: set immediately if new stage is fermentation ---
    initialFermentTemp = 0.0;
    fermentationFactor = 1.0;
    predictedCompleteTime = 0;
    lastFermentAdjust = 0;
    if (activeProgramId < programs.size()) {
      Program &p = programs[activeProgramId];
      if (customStageIdx < p.customStages.size()) {
        CustomStage &st = p.customStages[customStageIdx];
        if (st.isFermentation) {
          float baseline = p.fermentBaselineTemp > 0 ? p.fermentBaselineTemp : 20.0;
          float q10 = p.fermentQ10 > 0 ? p.fermentQ10 : 2.0;
          float actualTemp = getAveragedTemperature();
          initialFermentTemp = actualTemp;
          fermentationFactor = pow(q10, (baseline - actualTemp) / 10.0);
          unsigned long plannedStageMs = (unsigned long)st.min * 60000UL;
          unsigned long adjustedStageMs = plannedStageMs * fermentationFactor;
          predictedCompleteTime = millis() + adjustedStageMs;
          lastFermentAdjust = millis();
        }
      }
    }
    saveResumeState();
    if (debugSerial) Serial.printf("[STAGE] Entering custom stage: %d\n", customStageIdx);
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/back", HTTP_GET, [](AsyncWebServerRequest* req){
    Serial.println(F("[DEBUG] /back endpoint handler entered"));
    if (debugSerial) Serial.println(F("[ACTION] /back called"));
    updateActiveProgramVars();
    // Robust null and bounds checks
    if (!customProgram) {
        Serial.println(F("[ERROR] /back: customProgram is null"));
        stopBreadmaker();
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        streamStatusJson(*response);
        req->send(response);
        return;
    }
    if (activeProgramId >= programs.size()) {
        Serial.printf_P(PSTR("[ERROR] /back: activeProgramId %u out of range\n"), (unsigned)activeProgramId);
        stopBreadmaker();
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        streamStatusJson(*response);
        req->send(response);
        return;
    }
    Program &p = programs[activeProgramId];
    size_t numStages = p.customStages.size();
    if (numStages == 0) {
        Serial.printf_P(PSTR("[ERROR] /back: Program at id %u has zero stages\n"), (unsigned)activeProgramId);
        stopBreadmaker();
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        streamStatusJson(*response);
        req->send(response);
        return;
    }
    // Clamp customStageIdx to valid range
    if (customStageIdx >= numStages) {
        Serial.printf_P(PSTR("[WARN] /back: customStageIdx (%u) >= numStages (%u), clamping\n"), (unsigned)customStageIdx, (unsigned)numStages);
        customStageIdx = numStages - 1;
    }
    if (customStageIdx > 0) {
        customStageIdx--;
        Serial.printf_P(PSTR("[INFO] /back: Decremented customStageIdx to %u\n"), (unsigned)customStageIdx);
    } else {
        customStageIdx = 0;
        Serial.println(F("[INFO] /back: Already at first stage, staying at 0"));
    }
    customStageStart = millis();
    customMixIdx = 0;
    customMixStepStart = 0;
    isRunning = true;
    if (customStageIdx == 0) programStartTime = time(nullptr);
    // Record actual start time of this stage when going back
    if (customStageIdx < numStages && customStageIdx < MAX_PROGRAM_STAGES) actualStageStartTimes[customStageIdx] = time(nullptr);
    saveResumeState();
    Serial.printf_P(PSTR("[STAGE] Entering custom stage: %d\n"), customStageIdx);
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/select", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("idx")) {
      int idx = req->getParam("idx")->value().toInt();
      if (idx >= 0 && idx < (int)programs.size()) {
        activeProgramId = idx;
        updateActiveProgramVars(); // Update program variables
        isRunning = false;
        saveSettings();
        saveResumeState(); // <--- Save resume state on program select (by idx)
        req->send(200, "text/plain", "Selected");
        return;
      }
      req->send(400, "text/plain", "Bad program index");
      return;
    }
    if (req->hasParam("name")) {
      const char* name = req->getParam("name")->value().c_str();
      // Find index by name
      int foundIdx = -1;
      for (size_t i = 0; i < programs.size(); ++i) {
        if (programs[i].name == name) { foundIdx = i; break; }
      }
      if (foundIdx >= 0) {
        activeProgramId = foundIdx;
        updateActiveProgramVars(); // Update program variables
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
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/api/motor", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) {
      int on = req->getParam("on")->value().toInt();
      setMotor(on != 0);
    }
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/api/light", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) {
      int on = req->getParam("on")->value().toInt();
      setLight(on != 0);
      if (on) lightOnTime = millis();
    }
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/api/buzzer", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) {
      int on = req->getParam("on")->value().toInt();
      setBuzzer(on != 0);
    }
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  
  // Buzzer tone test endpoint
  server.on("/api/buzzer_tone", HTTP_GET, [](AsyncWebServerRequest* req){
    float frequency = 1000.0;
    float amplitude = 0.3;
    unsigned long duration = 200;
    
    if (req->hasParam("freq")) {
      frequency = req->getParam("freq")->value().toFloat();
    }
    if (req->hasParam("amp")) {
      amplitude = req->getParam("amp")->value().toFloat();
    }
    if (req->hasParam("dur")) {
      duration = req->getParam("dur")->value().toInt();
    }
    
    startBuzzerTone(frequency, amplitude, duration);
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  
  // Short beep test endpoint
  server.on("/api/short_beep", HTTP_GET, [](AsyncWebServerRequest* req){
    shortBeep();
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  
  // --- Short beep API endpoint ---
  server.on("/api/beep", HTTP_GET, [](AsyncWebServerRequest* req){
    shortBeep();
    req->send(200, "application/json", "{\"status\":\"beep_completed\"}");
  });
  
  // --- Manual mode API endpoint ---
  server.on("/api/manual_mode", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) {
      int on = req->getParam("on")->value().toInt();
      manualMode = (on != 0);
    }
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  // --- Manual temperature setpoint API endpoint ---
  server.on("/api/temperature", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("setpoint")) {
      float temp = req->getParam("setpoint")->value().toFloat();
      if (temp >= 0 && temp <= 250) {  // Reasonable temperature range
        Setpoint = temp;
      }
    }
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
    // --- PID debugging endpoint ---
    server.on("/api/pid_debug", HTTP_GET, [](AsyncWebServerRequest* req){
      AsyncResponseStream *response = req->beginResponseStream("application/json");
      response->print("{");
      response->printf("\"setpoint\":%.6f,", Setpoint);
      response->printf("\"input\":%.6f,", Input);
      response->printf("\"output\":%.6f,", Output);
      response->printf("\"current_temp\":%.6f,", getAveragedTemperature());
      response->printf("\"raw_temp\":%.6f,", readTemperature());
      response->printf("\"heater_state\":%s,", heaterState ? "true" : "false");
      response->printf("\"manual_mode\":%s,", manualMode ? "true" : "false");
      response->printf("\"is_running\":%s,", isRunning ? "true" : "false");
      response->printf("\"kp\":%.6f,", Kp);
      response->printf("\"ki\":%.6f,", Ki);
      response->printf("\"kd\":%.6f,", Kd);
      response->printf("\"sample_time_ms\":%lu,", pidSampleTime);
      response->printf("\"temp_sample_count\":%d,", tempSampleCount);
      response->printf("\"temp_reject_count\":%d,", tempRejectCount);
      response->printf("\"temp_sample_interval_ms\":%lu,", tempSampleInterval);
      unsigned long nowMs = millis();
      unsigned long elapsed = (windowStartTime > 0) ? (nowMs - windowStartTime) : 0;
      response->printf("\"window_size_ms\":%lu,", windowSize);
      response->printf("\"window_elapsed_ms\":%lu,", elapsed);
      response->printf("\"on_time_ms\":%lu,", onTime);
      response->printf("\"duty_cycle_percent\":%.2f,", (Output * 100.0));
      // --- PID component terms ---
      response->printf("\"pid_p\":%.6f,", pidP);
      response->printf("\"pid_i\":%.6f,", pidI);
      response->printf("\"pid_d\":%.6f", pidD);
      response->print("}");
      req->send(response);
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
    
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->printf("\"kp\":%.6f,", Kp);
    response->printf("\"ki\":%.6f,", Ki);
    response->printf("\"kd\":%.6f,", Kd);
    response->printf("\"sample_time_ms\":%lu,", pidSampleTime);
    response->printf("\"window_size_ms\":%lu,", windowSize);
    response->printf("\"temp_sample_count\":%d,", tempSampleCount);
    response->printf("\"temp_reject_count\":%d,", tempRejectCount);
    response->printf("\"temp_sample_interval_ms\":%lu,", tempSampleInterval);
    response->printf("\"updated\":%s,", updated ? "true" : "false");
    // Add intelligent update status information
    String updateDetails = "";
    bool pidChanged = pidTuningsChanged;
    bool tempStructuralChange = false;
    bool tempParametersChanged = false;
    if (req->hasParam("temp_samples")) tempStructuralChange = true;
    if (req->hasParam("temp_reject") || req->hasParam("temp_interval")) tempParametersChanged = true;
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
      response->printf("\"update_details\":\"%s\",", updateDetails.c_str());
      if (tempSamplesReady && !tempStructuralChange) {
        response->print("\"data_preservation\":\"Temperature history preserved\",");
      } else if (tempStructuralChange) {
        response->print("\"data_preservation\":\"Intelligent data preservation applied\",");
      }
      if (debugSerial) {
        Serial.printf("[API] Update summary: %s (PID changed: %s, Temp structural: %s, Temp params: %s)\n", 
                     updateDetails.c_str(), 
                     pidChanged ? "yes" : "no",
                     tempStructuralChange ? "yes" : "no", 
                     tempParametersChanged ? "yes" : "no");
      }
    } else {
      response->print("\"update_details\":\"No changes required - all parameters already at requested values\",");
      response->print("\"data_preservation\":\"All existing data preserved\",");
    }
    if (errors.length() > 0) {
      response->printf("\"errors\":\"%s\"", errors.c_str());
      response->print("}");
      req->send(response);
    } else {
      response->print("\"errors\":null}");
      req->send(response);
    }
  });
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(LittleFS, "/index.html", "text/html");
  });
// --- Status endpoint for UI polling ---
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println(F("[DEBUG] /status requested (streaming)"));
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });

  // --- Firmware info endpoint ---
  server.on("/api/firmware_info", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->print("\"build\":\"");
    response->print(FIRMWARE_BUILD_DATE);
    response->print("\"}");
    req->send(response);
  });

  // --- Restart endpoint ---
  server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* req){
    req->send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(200);
    ESP.restart();
  });

  // --- Home Assistant-friendly endpoint (lightweight - uses cached values only) ---
  server.on("/ha", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->printf("\"state\":\"%s\",", isRunning ? "on" : "off");
    response->printf("\"stage\":\"");
    if (activeProgramId < programs.size() && isRunning && customStageIdx < programs[activeProgramId].customStages.size()) {
      response->print(programs[activeProgramId].customStages[customStageIdx].label.c_str());
    } else {
      response->print("Idle");
    }
    response->print("\",");
    if (activeProgramId < programs.size()) {
      response->printf("\"program\":\"%s\",", programs[activeProgramId].name.c_str());
    } else {
      response->print("\"program\":\"\",");
    }
    response->printf("\"programId\":%u,", (unsigned)activeProgramId);
    response->printf("\"temperature\":%.2f,", averagedTemperature);
    response->printf("\"setpoint\":%.2f,", Setpoint);
    response->printf("\"heater\":%s,", heaterState ? "true" : "false");
    response->printf("\"motor\":%s,", motorState ? "true" : "false");
    response->printf("\"light\":%s,", lightState ? "true" : "false");
    response->printf("\"buzzer\":%s,", buzzerState ? "true" : "false");
    response->printf("\"manual_mode\":%s,", manualMode ? "true" : "false");
    response->printf("\"stageIdx\":%u,", (unsigned)customStageIdx);
    response->printf("\"mixIdx\":%u,", (unsigned)customMixIdx);
    // Use the same logic as streamStatusJson for stage_ready_at and program_ready_at
    unsigned long nowMs = millis();
    time_t now = time(nullptr);
    unsigned long stageReadyAt = 0;
    unsigned long programReadyAt = 0;
    if (activeProgramId < programs.size() && isRunning && customStageIdx < programs[activeProgramId].customStages.size()) {
        Program &p = programs[activeProgramId];
        CustomStage &st = p.customStages[customStageIdx];
        if (st.isFermentation) {
            double plannedStageSec = (double)st.min * 60.0;
            double elapsedSec = fermentWeightedSec;
            double realElapsedSec = (nowMs - fermentLastUpdateMs) / 1000.0;
            elapsedSec += realElapsedSec * fermentLastFactor;
            double remainSec = plannedStageSec - (elapsedSec / fermentLastFactor);
            if (remainSec < 0) remainSec = 0;
            stageReadyAt = now + (unsigned long)(remainSec * fermentLastFactor);
        } else if (actualStageStartTimes[customStageIdx] > 0) {
            stageReadyAt = actualStageStartTimes[customStageIdx] + (time_t)st.min * 60;
        }
        // Program ready at: predictedCompleteTime if running
        programReadyAt = (unsigned long)predictedCompleteTime;
    }
    response->printf("\"stage_time_left\":%ld,", 0L); // TODO: implement if needed
    response->printf("\"stage_ready_at\":%lu,", stageReadyAt);
    response->printf("\"program_ready_at\":%lu,", programReadyAt);
    response->printf("\"startupDelayComplete\":%s,", isStartupDelayComplete() ? "true" : "false");
    response->printf("\"startupDelayRemainingMs\":%lu,", isStartupDelayComplete() ? 0UL : (STARTUP_DELAY_MS - (millis() - startupTime)));
    response->printf("\"fermentationFactor\":%.2f,", fermentationFactor);
    response->printf("\"initialFermentTemp\":%.2f,", initialFermentTemp);
    // Health info
    response->print("\"health\":{");
    response->printf("\"uptime_sec\":%lu,", millis() / 1000UL);
    response->printf("\"firmware_version\":\"%s\",", FIRMWARE_BUILD_DATE);
    response->printf("\"build_date\":\"%s\",", FIRMWARE_BUILD_DATE);
    response->printf("\"reset_reason\":\"%s\",", ESP.getResetReason().c_str());
    response->printf("\"chip_id\":%lu,", ESP.getChipId());
    response->print("\"watchdog_enabled\":true,");
    response->printf("\"startup_complete\":%s,", (millis() - startupTime) >= STARTUP_DELAY_MS ? "true" : "false");
    response->printf("\"watchdog_last_feed\":%lu,", millis() % 1000);
    response->print("\"watchdog_timeout_ms\":8000,");
    // Memory info
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t maxFreeBlock = ESP.getMaxFreeBlockSize();
    response->print("\"memory\":{");
    response->printf("\"free_heap\":%lu,", freeHeap);
    response->printf("\"max_free_block\":%lu,", maxFreeBlock);
    response->printf("\"min_free_heap\":%lu,", freeHeap);
    response->printf("\"fragmentation\":%.2f", (freeHeap > 0) ? ((maxFreeBlock * 100.0) / freeHeap) : 0.0);
    response->print("},");
    // Performance info
    response->print("\"performance\":{");
    response->print("\"cpu_usage\":0,");
    response->print("\"avg_loop_time_us\":1000,");
    response->print("\"max_loop_time_us\":5000,");
    response->printf("\"loop_count\":%lu", millis() / 10);
    response->print("},");
    // Network info
    response->print("\"network\":{");
    response->printf("\"connected\":%s,", (WiFi.status() == WL_CONNECTED) ? "true" : "false");
    response->printf("\"rssi\":%d,", WiFi.RSSI());
    response->print("\"reconnect_count\":0,");
    response->printf("\"ip\":\"%s\"", WiFi.localIP().toString().c_str());
    response->print("},");
    // Filesystem info (real values)
    FSInfo fsinfo;
    LittleFS.info(fsinfo);
    response->print("\"filesystem\":{");
    response->printf("\"usedBytes\":%lu,", fsinfo.usedBytes);
    response->printf("\"totalBytes\":%lu,", fsinfo.totalBytes);
    response->printf("\"freeBytes\":%lu,", fsinfo.totalBytes - fsinfo.usedBytes);
    float fsUtil = (fsinfo.totalBytes > 0) ? (fsinfo.usedBytes * 100.0 / fsinfo.totalBytes) : 0.0;
    response->printf("\"utilization\":%.2f", fsUtil);
    response->print("},");
    // Error counts (placeholder)
    response->print("\"error_counts\":[0,0,0,0,0,0],");
    // Temperature control info
    response->print("\"temperature_control\":{");
    response->printf("\"input\":%.2f,", averagedTemperature); // Averaged temp for legacy
    response->printf("\"pid_input\":%.2f,", Input); // PID input (raw value used by PID)
    response->printf("\"output\":%.2f,", Output * 100.0); // Output as percent
    response->printf("\"pid_output\":%.6f,", Output); // PID output (0-1 float)
    response->printf("\"raw_temp\":%.2f,", readTemperature());
    response->printf("\"thermal_runaway\":%s,", (thermalRunawayDetected ? "true" : "false"));
    response->printf("\"sensor_fault\":%s,", (sensorFaultDetected ? "true" : "false"));
    // Use the outer nowMs if already declared, otherwise declare here
    unsigned long elapsed = (windowStartTime > 0) ? (nowMs - windowStartTime) : 0;
    response->printf("\"window_elapsed_ms\":%lu,", elapsed);
    response->printf("\"window_size_ms\":%lu,", windowSize);
    response->printf("\"on_time_ms\":%lu,", onTime);
    response->printf("\"duty_cycle_percent\":%.2f,", Output * 100.0);
    // --- PID parameters for Home Assistant ---
    response->printf("\"kp\":%.6f,", Kp);
    response->printf("\"ki\":%.6f,", Ki);
    response->printf("\"kd\":%.6f,", Kd);
    response->printf("\"sample_time_ms\":%lu,", pidSampleTime);
    response->printf("\"temp_sample_count\":%d,", tempSampleCount);
    response->printf("\"temp_reject_count\":%d,", tempRejectCount);
    response->printf("\"temp_sample_interval_ms\":%lu", tempSampleInterval);
    response->print("}");
    // --- Add any missing properties for Home Assistant integration ---
    // Add any additional properties here as needed for HA sensors
    response->print("}"); // end health
    response->print("}"); // end root
    req->send(response);
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

  // --- Create Folder API endpoint ---
server.on("/api/create_folder", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
  [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
    DynamicJsonDocument doc(128);
    if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }
    String parent = doc["parent"] | "/";
    String name = doc["name"] | "";
    if (name.length() == 0) { req->send(400, "application/json", "{\"error\":\"No folder name provided\"}"); return; }
    if (!parent.endsWith("/")) parent += "/";
    String path = parent + name;
    if (!path.startsWith("/")) path = "/" + path;
    if (LittleFS.mkdir(path)) {
      req->send(200, "application/json", "{\"status\":\"created\"}");
    } else {
      req->send(500, "application/json", "{\"error\":\"Failed to create folder\"}");
    }
  }
);

server.on("/api/delete_folder", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
  [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
    DynamicJsonDocument doc(128);
    if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }
    String parent = doc["folder"] | "/";
    String name = doc["name"] | "";
    if (name.length() == 0) { req->send(400, "application/json", "{\"error\":\"No folder name provided\"}"); return; }
    if (!parent.endsWith("/")) parent += "/";
    String path = parent + name;
    if (!path.startsWith("/")) path = "/" + path;
    if (LittleFS.exists(path)) {
      deleteFolderRecursive(path);
      req->send(200, "application/json", "{\"status\":\"deleted\"}");
    } else {
      req->send(404, "application/json", "{\"error\":\"Folder not found\"}");
    }
  }
);

// --- Updated File List API endpoint ---
server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest* req){
  String folder = "/";
  if (req->hasParam("folder")) {
    folder = req->getParam("folder")->value();
    if (!folder.startsWith("/")) folder = "/" + folder;
    if (!folder.endsWith("/")) folder += "/";
  }
  AsyncResponseStream *response = req->beginResponseStream("application/json");
  response->print("{\"folders\":[");
  Dir dir = LittleFS.openDir(folder);
  bool firstFolder = true, firstFile = true;
  String filesJson = "],\"files\":[";
  while (dir.next()) {
    if (dir.isDirectory()) {
      if (!firstFolder) response->print(",");
      response->printf("\"%s\"", dir.fileName().c_str());
      firstFolder = false;
    } else {
      if (!firstFile) filesJson += ",";
      filesJson += "{";
      filesJson += "\"name\":\"" + String(dir.fileName()) + "\",\"size\":" + String((unsigned long)dir.fileSize());
      filesJson += "}";
      firstFile = false;
    }
  }
  response->print(filesJson);
  response->print("]}");
  req->send(response);
});

  server.on("/api/calibration", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    int raw = analogRead(PIN_RTD);
    response->print("{");
    response->printf("\"raw\":%d,\"table\":[", raw);
    for (size_t i = 0; i < rtdCalibTable.size(); ++i) {
      response->printf("{\"raw\":%d,\"temp\":%.2f}", rtdCalibTable[i].raw, rtdCalibTable[i].temp);
      if (i + 1 < rtdCalibTable.size()) response->print(",");
    }
    response->print("]}");
    req->send(response);
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

  // --- Start at specific stage endpoint ---
  server.on("/start_at_stage", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println(F("[ACTION] /start_at_stage called"));



    if (!req->hasParam("stage")) {
      sendJsonError(req, "Missing stage parameter", "Please specify ?stage=N");
      return;
    }

    String stageStr = req->getParam("stage")->value();
    bool validStage = true;
    for (size_t i = 0; i < stageStr.length(); ++i) {
      if (!isDigit(stageStr[i]) && !(i == 0 && stageStr[i] == '-')) {
        validStage = false;
        break;
      }
    }
    if (!validStage || stageStr.length() == 0) {
      sendJsonError(req, "Invalid stage parameter", "Stage must be an integer");
      return;
    }
    int stageIdx = stageStr.toInt();

    // Check if startup delay has completed
    if (!isStartupDelayComplete()) {
      unsigned long remaining = STARTUP_DELAY_MS - (millis() - startupTime);
      if (debugSerial) Serial.printf("[START_AT_STAGE] Startup delay: %lu ms remaining\n", remaining);
      AsyncResponseStream *response = req->beginResponseStream("application/json");
      response->print("{");
      response->print("\"error\":\"Startup delay active\",");
      response->print("\"message\":\"Please wait for temperature sensor to stabilize\",");
      response->printf("\"remainingMs\":%lu", remaining);
      response->print("}");
      req->send(response);
      return;
    }

    // Always update active program vars to ensure customProgram/maxCustomStages are set
    updateActiveProgramVars();
    // Ensure a program is selected and customProgram is valid
    if (!customProgram) {
      sendJsonError(req, "No program selected", "Please select a program before starting at a stage.");
      return;
    }

    size_t numStages = customProgram->customStages.size();
    if (numStages == 0) {
      sendJsonError(req, "No stages defined", "The selected program has no stages.");
      return;
    }

    // Validate stage index against actual number of stages
    if (stageIdx < 0 || (size_t)stageIdx >= numStages) {
      String msg = "Stage index must be between 0 and " + String(numStages - 1);
      sendJsonError(req, "Invalid stage index", msg);
      return;
    }


    // Start the program at the specified stage
    isRunning = true;
    customStageIdx = stageIdx;
    customMixIdx = 0;
    customStageStart = millis();
    customMixStepStart = 0;
    programStartTime = time(nullptr);

    // Patch: Do NOT reset all actualStageStartTimes. Mark previous as completed, set current if not set.
    for (int i = 0; i < MAX_PROGRAM_STAGES; i++) {
      if (i < stageIdx) {
        // Mark previous stages as completed (set to a nonzero dummy value, e.g., programStartTime - 1000 * (stageIdx - i))
        if (actualStageStartTimes[i] == 0) actualStageStartTimes[i] = programStartTime - 1000 * (stageIdx - i);
      } else if (i == stageIdx) {
        if (actualStageStartTimes[i] == 0) actualStageStartTimes[i] = programStartTime;
      }
      // Leave future stages untouched (remain 0)
    }

    // --- Fermentation tracking: set immediately if this stage is fermentation ---
    initialFermentTemp = 0.0;
    fermentationFactor = 1.0;
    predictedCompleteTime = 0;
    lastFermentAdjust = 0;
    if (customProgram && (size_t)stageIdx < customProgram->customStages.size()) {
      CustomStage &st = customProgram->customStages[stageIdx];
      float baseline = customProgram->fermentBaselineTemp > 0 ? customProgram->fermentBaselineTemp : 20.0;
      float q10 = customProgram->fermentQ10 > 0 ? customProgram->fermentQ10 : 2.0;
      if (st.isFermentation) {
        float actualTemp = getAveragedTemperature();
        initialFermentTemp = actualTemp;
        fermentationFactor = pow(q10, (baseline - actualTemp) / 10.0);
        unsigned long plannedStageMs = (unsigned long)st.min * 60000UL;
        unsigned long adjustedStageMs = plannedStageMs * fermentationFactor;
        predictedCompleteTime = millis() + adjustedStageMs;
        lastFermentAdjust = millis();
      }
    }

    saveResumeState();
    if (debugSerial) Serial.printf("[STAGE] Starting at custom stage: %d\n", customStageIdx);

    response->print("\"status\":\"started\",");
    response->printf("\"stage\":%d,", stageIdx);
    if (customProgram && (size_t)stageIdx < customProgram->customStages.size()) {
      response->print("\"stageName\":\"");
      response->print(customProgram->customStages[stageIdx].label.c_str());
      response->print("\"");
    }
    response->print("}");
    req->send(response);
  });

  // --- Combined timed start at stage endpoint ---
  server.on("/setStartAtStage", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println(F("[ACTION] /setStartAtStage called"));
    
    if (!req->hasParam("time")) {
      req->send(400, "text/plain", "Missing time parameter");
      return;
    }
    
    String t = req->getParam("time")->value(); // format: "HH:MM"
    
    // Parse and validate time
    struct tm nowTm;
    time_t now = time(nullptr);
    localtime_r(&now, &nowTm);
    int hh, mm;
    if (sscanf(t.c_str(), "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
      req->send(400, "text/plain", "Invalid time format. Use HH:MM");
      return;
    }
    
    // Compute next time today or tomorrow if already past
    struct tm startTm = nowTm;
    startTm.tm_hour = hh;
    startTm.tm_min = mm;
    startTm.tm_sec = 0;
    time_t startEpoch = mktime(&startTm);
    if (startEpoch <= now) startEpoch += 24*60*60; // Next day
    
    scheduledStart = startEpoch;
    
    // Handle stage parameter if provided
    if (req->hasParam("stage")) {
      int stageIdx = req->getParam("stage")->value().toInt();
      
      // Validate stage index
      if (stageIdx < 0 || stageIdx >= maxCustomStages) {
        req->send(400, "text/plain", "Invalid stage index");
        return;
      }
      
      // Store the stage to start at (we'll need to add a global variable for this)
      scheduledStartStage = stageIdx;
      
      req->send(200, "text/plain", "Scheduled to start at stage " + String(stageIdx + 1) + " at " + t);
      if (debugSerial) Serial.printf("[TIMED_STAGE] Scheduled to start at stage %d at %s\n", stageIdx, t.c_str());
    } else {
      // Time only (start from beginning)
      scheduledStartStage = -1; // -1 means start from beginning
      req->send(200, "text/plain", "Scheduled to start at " + t);
      if (debugSerial) Serial.printf("[TIMED_START] Scheduled to start at %s\n", t.c_str());
    }
  });

  // --- Output mode API endpoint ---
  server.on("/api/output_mode", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->printf("\"mode\":\"%s\"", (outputMode == OUTPUT_DIGITAL) ? "digital" : "analog");
    response->print("}");
    req->send(response);
  });
  server.on("/api/output_mode", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      DynamicJsonDocument doc(128);
      if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }
      const char* mode = doc["mode"] | "analog";
      outputMode = (strcmp(mode, "digital") == 0) ? OUTPUT_DIGITAL : OUTPUT_ANALOG;
      saveSettings();
      AsyncResponseStream *response = req->beginResponseStream("application/json");
      response->print("{");
      response->printf("\"mode\":\"%s\"", (outputMode == OUTPUT_DIGITAL) ? "digital" : "analog");
      response->print("}");
      req->send(response);
    }
  );

  // --- Debug Serial API endpoint ---
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->printf("\"debugSerial\":%s", debugSerial ? "true" : "false");
    response->print("}");
    req->send(response);
  });
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      DynamicJsonDocument doc(128);
      if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }
      if (doc.containsKey("debugSerial")) debugSerial = doc["debugSerial"];
      saveSettings();
      AsyncResponseStream *response = req->beginResponseStream("application/json");
      response->print("{");
      response->printf("\"debugSerial\":%s", debugSerial ? "true" : "false");
      response->print("}");
      req->send(response);
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
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{\"files\":[");
    Dir dir = LittleFS.openDir("/");
    bool first = true;
    while (dir.next()) {
      if (!first) response->print(",");
      response->print("{");
      response->printf("\"name\":\"%s\",\"size\":%lu", dir.fileName().c_str(), (unsigned long)dir.fileSize());
      response->print("}");
      first = false;
    }
    response->print("]}");
    req->send(response);
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
  server.begin();
  Serial.println(F("HTTP server started"));
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
  
  Serial.println(F("PID controller initialized with relay-friendly time-proportional control"));
  Serial.println(F("Temperature averaging system initialized (10 samples, 0.5s interval, reject top/bottom 2)"));
  
  // Wait for time sync
  while (time(nullptr) < 100000) delay(200);
}

// Remove duplicate loadPrograms() implementation from this file. Use the one in programs_manager.cpp.

// --- Resume state management ---
const char* RESUME_FILE = "/resume.json";
unsigned long lastResumeSave = 0;

void saveResumeState() {
  File f = LittleFS.open(RESUME_FILE, "w");
  if (!f) return;
  f.print("{\n");
  f.printf("  \"programIdx\":%u,\n", (unsigned)activeProgramId);
  f.printf("  \"customStageIdx\":%u,\n", (unsigned)customStageIdx);
  f.printf("  \"customMixIdx\":%u,\n", (unsigned)customMixIdx);
  f.printf("  \"elapsedStageSec\":%lu,\n", (customStageStart > 0) ? (millis() - customStageStart) / 1000UL : 0UL);
  f.printf("  \"elapsedMixSec\":%lu,\n", (customMixStepStart > 0) ? (millis() - customMixStepStart) / 1000UL : 0UL);
  f.printf("  \"programStartTime\":%lu,\n", (unsigned long)programStartTime);
  f.printf("  \"isRunning\":%s,\n", isRunning ? "true" : "false");
   f.print("  \"actualStageStartTimes\":[");
  for (int i = 0; i < 20; i++) {
    f.printf("%s%lu", (i > 0) ? "," : "", (unsigned long)actualStageStartTimes[i]);
  }
  f.print("]\n");
  f.print("}\n");
  f.close();
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
  // Restore program by index
  int progIdx = doc["programIdx"] | -1;
  if (progIdx >= 0 && progIdx < (int)programs.size()) {
    activeProgramId = progIdx;
    updateActiveProgramVars(); // Update program variables after loading
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
  if (isRunning && activeProgramId < programs.size()) {
    Program &p = programs[activeProgramId];
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

// Helper to reset fermentation tracking variables
void resetFermentationTracking(float temp) {
  initialFermentTemp = 0.0;
  fermentationFactor = 1.0;
  predictedCompleteTime = 0;
  lastFermentAdjust = 0;
  fermentLastTemp = temp;
  fermentLastFactor = 1.0;
  fermentLastUpdateMs = 0;
  fermentWeightedSec = 0.0;
}

void loop() {
  // Update temperature sampling (non-blocking, every 0.5s)
  updateTemperatureSampling();
  
  // Update buzzer tone generation (for sine wave output)
  updateBuzzerTone();
    
  // Update temperature for broadcasts
  float temp = getAveragedTemperature();
  static bool stageJustAdvanced = false;
  // --- Live fermentation timing integration and auto-advance ---
  if (programs.size() > 0 && activeProgramId < programs.size() && customStageIdx < programs[activeProgramId].customStages.size()) {
    Program &p = programs[activeProgramId];
    CustomStage &st = p.customStages[customStageIdx];
    if (st.isFermentation) {
      float baseline = p.fermentBaselineTemp > 0 ? p.fermentBaselineTemp : 20.0;
      float q10 = p.fermentQ10 > 0 ? p.fermentQ10 : 2.0;
      float actualTemp = getAveragedTemperature();
      unsigned long nowMs = millis();
      if (fermentLastUpdateMs == 0) {
        fermentLastTemp = actualTemp;
        fermentLastFactor = pow(q10, (baseline - actualTemp) / 10.0);
        fermentLastUpdateMs = nowMs;
        fermentWeightedSec = 0.0;
      } else if (isRunning) {
        double elapsedSec = (nowMs - fermentLastUpdateMs) / 1000.0;
        fermentWeightedSec += elapsedSec * fermentLastFactor;
        fermentLastTemp = actualTemp;
        fermentLastFactor = pow(q10, (baseline - actualTemp) / 10.0);
        fermentLastUpdateMs = nowMs;
      }
      fermentationFactor = fermentLastFactor;
      double plannedStageSec = (double)st.min * 60.0;
      double epsilon = 0.05; // 50ms margin for float compare
      if (!stageJustAdvanced && (fermentWeightedSec + epsilon >= plannedStageSec)) {
        if (debugSerial) Serial.printf("[FERMENT] Auto-advance: weighted %.1fs >= planned %.1fs\n", fermentWeightedSec, plannedStageSec);
        customStageIdx++;
        customStageStart = millis();
        resetFermentationTracking(actualTemp);
        saveResumeState();
        stageJustAdvanced = true;
      }
    } else {
      resetFermentationTracking(getAveragedTemperature());
    }
  } else {
    stageJustAdvanced = false;
  }
// ...existing code...
  if (!isRunning) {
    stageJustAdvanced = false;
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
    
    // Handle manual mode even when program is not running
    if (manualMode && Setpoint > 0) {
      Input = getAveragedTemperature();
      // --- Calculate PID terms manually for debug/graphing ---
      double error = Setpoint - Input;
      double dInput = Input - lastInput;
      double kpUsed = Kp, kiUsed = Ki, kdUsed = Kd;
      double sampleTimeSec = pidSampleTime / 1000.0;
      pidP = kpUsed * error;
      lastITerm += kiUsed * error * sampleTimeSec;
      pidI = lastITerm;
      pidD = -kdUsed * dInput / sampleTimeSec;
      lastInput = Input;
      myPID.Compute();
      updateTimeProportionalHeater();
      // Keep other outputs OFF in manual mode when not running a program
      setMotor(false);
      setLight(false);
      setBuzzer(false);
    } else {
      // Ensure all outputs are OFF when idle and not scheduled to start
      setHeater(false);
      setMotor(false);
      setLight(false);
      setBuzzer(false);
    }
    
    if (scheduledStart && time(nullptr) >= scheduledStart) {
      scheduledStart = 0;
      isRunning = true;
      
      // Handle starting at a specific stage or from the beginning
      if (scheduledStartStage >= 0 && scheduledStartStage < maxCustomStages) {
        customStageIdx = scheduledStartStage;
        if (debugSerial) Serial.printf("[SCHEDULED] Starting at stage %d\n", scheduledStartStage);
      } else {
        customStageIdx = 0;
        if (debugSerial) Serial.printf("[SCHEDULED] Starting from beginning\n");
      }
      scheduledStartStage = -1; // Reset after use
      
      customMixIdx = 0;
      customStageStart = millis();
      customMixStepStart = 0;
      programStartTime = time(nullptr);
      
      // Initialize actual stage start times array for scheduled start
      for (int i = 0; i < 20; i++) actualStageStartTimes[i] = 0;
      actualStageStartTimes[customStageIdx] = programStartTime; // Record start time for the actual starting stage
      
      saveResumeState(); // Save on scheduled start
    }
    yield(); delay(1); return;
  }
  if (programs.size() == 0 || activeProgramId >= programs.size()) {
    stageJustAdvanced = false;
    stopBreadmaker(); yield(); delay(1); return;
  }
  Program &p = programs[activeProgramId];
  // --- Custom Stages Logic ---
  if (!p.customStages.empty()) {
    stageJustAdvanced = false;
    if (customStageIdx >= p.customStages.size()) {
      stageJustAdvanced = false;
      stopBreadmaker(); customStageIdx = 0; customStageStart = 0; clearResumeState(); yield(); delay(1); return;
    }
    CustomStage &st = p.customStages[customStageIdx];
    Setpoint = st.temp;
    Input = getAveragedTemperature();

    // --- Fermentation factor logic (update every 10 min or on temp change >0.1C) ---
    if (st.isFermentation) {
      // Already handled at top of loop
    } else {
      // Reset fermentation tracking when not in fermentation stage
      resetFermentationTracking(getAveragedTemperature());
    }

    // --- Predicted program completion time: adjust all fermentation stages ---
    // Only update every 10 min or at start
    if (lastFermentAdjust == 0 || millis() - lastFermentAdjust > 600000) {
      lastFermentAdjust = millis();
      unsigned long nowMs = millis();
      unsigned long elapsedInCurrentStage = (customStageStart > 0) ? (nowMs - customStageStart) : 0UL;
      double cumulativePredictedSec = 0.0;
      for (size_t i = 0; i < p.customStages.size(); ++i) {
        CustomStage &stage = p.customStages[i];
        double plannedStageSec = (double)stage.min * 60.0;
        double adjustedStageSec = plannedStageSec;
        if (stage.isFermentation) {
          if (i < customStageIdx) {
            // For completed fermentation stages, assume factor at time of completion (approximate)
            float baseline = p.fermentBaselineTemp > 0 ? p.fermentBaselineTemp : 20.0;
            float q10 = p.fermentQ10 > 0 ? p.fermentQ10 : 2.0;
            float temp = fermentLastTemp; // Use last known temp
            float factor = pow(q10, (baseline - temp) / 10.0);
            cumulativePredictedSec += plannedStageSec * factor;
          } else if (i == customStageIdx) {
            // For current fermentation stage, use weighted elapsed and current factor for remaining
            double elapsedSec = fermentWeightedSec;
            double realElapsedSec = (nowMs - fermentLastUpdateMs) / 1000.0;
            elapsedSec += realElapsedSec * fermentLastFactor;
            double remainSec = plannedStageSec - (elapsedSec / fermentLastFactor);
            if (remainSec < 0) remainSec = 0;
            cumulativePredictedSec += elapsedSec + remainSec * fermentLastFactor;
          } else {
            // For future fermentation stages, use current factor
            cumulativePredictedSec += plannedStageSec * fermentLastFactor;
          }
        } else {
          // Non-fermentation stage
          cumulativePredictedSec += plannedStageSec;
        }
      }
      predictedCompleteTime = (unsigned long)(programStartTime + (unsigned long)(cumulativePredictedSec));
    }
    // --- Heater logic: PID control based on stage temperature ---
    // PID should run in both manual and automatic modes
    if (st.temp > 0 || manualMode) {
      // In manual mode, use the manually set Setpoint instead of stage temp
      if (manualMode && Setpoint > 0) {
        Input = getAveragedTemperature();
        // --- Calculate PID terms manually for debug/graphing ---
        double error = Setpoint - Input;
        double dInput = Input - lastInput;
        double kpUsed = Kp, kiUsed = Ki, kdUsed = Kd;
        double sampleTimeSec = pidSampleTime / 1000.0;
        pidP = kpUsed * error;
        lastITerm += kiUsed * error * sampleTimeSec;
        pidI = lastITerm;
        pidD = -kdUsed * dInput / sampleTimeSec;
        lastInput = Input;
        myPID.Compute();
        updateTimeProportionalHeater();
      } else if (!manualMode && st.temp > 0) {
        // Automatic mode: use stage temperature
        // --- Calculate PID terms manually for debug/graphing ---
        double error = Setpoint - Input;
        double dInput = Input - lastInput;
        double kpUsed = Kp, kiUsed = Ki, kdUsed = Kd;
        double sampleTimeSec = pidSampleTime / 1000.0;
        pidP = kpUsed * error;
        lastITerm += kiUsed * error * sampleTimeSec;
        pidI = lastITerm;
        pidD = -kdUsed * dInput / sampleTimeSec;
        lastInput = Input;
        myPID.Compute();
        updateTimeProportionalHeater();
      } else {
        // No temperature control needed
        setHeater(false);
        windowStartTime = 0;
        lastPIDOutput = 0.0;
      }
    } else {
      setHeater(false);  // No heating if temperature not set
      windowStartTime = 0;  // Reset window timing
      lastPIDOutput = 0.0;  // Reset last PID output
    }
    
    // Only control other outputs automatically if not in manual mode
    if (!manualMode) {
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
    // Use fermentWeightedSec for fermentation stages, wall time for others
    bool stageComplete = false;
    if (st.isFermentation) {
      // Add small epsilon to avoid floating-point edge case
      double fermentTargetSec = (double)st.min * 60.0;
      double epsilon = 0.05; // 50ms margin
      if (fermentWeightedSec + epsilon >= fermentTargetSec) {
        stageComplete = true;
        if (debugSerial) {
          Serial.printf("[FERMENT] Advancing: fermentWeightedSec=%.3f, target=%.3f (min=%.2f)\n", fermentWeightedSec, fermentTargetSec, st.min);
        }
      } else if (debugSerial && (fermentTargetSec - fermentWeightedSec) < 2.0) {
        // Log when close to advancing for troubleshooting
        Serial.printf("[FERMENT] Not yet advancing: fermentWeightedSec=%.3f, target=%.3f (min=%.2f)\n", fermentWeightedSec, fermentTargetSec, st.min);
      }
    } else {
      if (millis() - customStageStart >= (unsigned long)st.min * 60000UL) {
        stageComplete = true;
      }
    }
    // (Fermentation timing handled at top of loop; nothing needed here)
      if (scheduledStartStage >= 0 && scheduledStartStage < maxCustomStages) {
        customStageIdx = scheduledStartStage;
        if (debugSerial) Serial.printf("[SCHEDULED] Starting at stage %d\n", scheduledStartStage);
      } else {
        customStageIdx = 0;
        if (debugSerial) Serial.printf("[SCHEDULED] Starting from beginning\n");
      }
      scheduledStartStage = -1; // Reset after use
      
      customMixIdx = 0;
      customStageStart = millis();
      customMixStepStart = 0;
      programStartTime = time(nullptr);
      
      // Initialize actual stage start times array for scheduled start
      for (int i = 0; i < 20; i++) actualStageStartTimes[i] = 0;
      actualStageStartTimes[customStageIdx] = programStartTime; // Record start time for the actual starting stage
      
      saveResumeState(); // Save on scheduled start
    } else {
      yield(); delay(100);
      return;
    }
  
  temp = getAveragedTemperature(); // Update temp instead of declaring new one
  yield(); delay(1);
}

// --- Update active program variables ---
void updateActiveProgramVars() {
  if (programs.size() > 0 && activeProgramId < programs.size()) {
    customProgram = &programs[activeProgramId];
    maxCustomStages = customProgram->customStages.size();
  } else {
    customProgram = nullptr;
    maxCustomStages = 0;
  }
}

// --- Temperature averaging functions ---
void updateTemperatureSampling() {
  unsigned long nowMs = millis();
  
  // Sample at the configured interval
  if (nowMs - lastTempSample >= tempSampleInterval) {
    lastTempSample = nowMs;
    
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
      // ALWAYS USE WEIGHTED AVERAGE - no switching between methods
      
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
      
      // Step 2: Get clean samples (reject outliers if we have enough samples)
      float cleanSamples[MAX_TEMP_SAMPLES];
      int cleanCount = 0;
      int effectiveSamples = tempSampleCount - (2 * tempRejectCount);
      
      if (effectiveSamples >= 3) {
        // Enough samples for outlier rejection
        for (int i = tempRejectCount; i < tempSampleCount - tempRejectCount; i++) {
          cleanSamples[cleanCount++] = sortedSamples[i];
        }
      } else {
        // Not enough samples for outlier rejection, use all samples
        for (int i = 0; i < tempSampleCount; i++) {
          cleanSamples[cleanCount++] = sortedSamples[i];
        }
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
      
      // Step 4: ALWAYS apply weighted average (adaptive weighting based on trend)
      float weightedSum = 0.0;
      float totalWeight = 0.0;
      

        for (int i = 0; i < cleanCount; i++) {
          // Exponential weighting: newer samples get more weight
          // Weight increases from 0.5 to 2.0 across the buffer
          float weight = 0.5 + (1.5 * i) / (cleanCount - 1);
          weightedSum += cleanSamples[i] * weight;
          totalWeight += weight;
        }      
      averagedTemperature = weightedSum / totalWeight;
      
      // Debug output occasionally
  if (debugSerial && (nowMs % 30000 < tempSampleInterval)) { // Every 30 seconds
        Serial.printf("[TEMP_AVG] Raw: %.2f°C, Filtered: %.2f°C, Trend: %.2f°C (from %d samples, mode: %s)\n", 
                     rawTemp, averagedTemperature, trendStrength, cleanCount,
                     (absTrendStrength > 0.2) ? "TREND" : "STABLE");
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
  unsigned long nowMs = millis();
  
  // Initialize window start time if not set
  if (windowStartTime == 0) {
    windowStartTime = nowMs;
    lastPIDOutput = Output; // Initialize last PID output
  }
  
  // Check for dynamic window restart conditions
  bool shouldRestartWindow = false;
  unsigned long elapsed = nowMs - windowStartTime;
  
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
  if (nowMs - windowStartTime >= windowSize || shouldRestartWindow) {
    // Track dynamic restart events
    if (shouldRestartWindow) {
      lastDynamicRestart = nowMs;
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
    
    windowStartTime = nowMs;
    lastPIDOutput = Output; // Update the last known PID output
    
    // Calculate ON time for this window based on PID output (0.0 to 1.0)
    onTime = (unsigned long)(Output * windowSize);
    
    // Relay protection: smart minimum ON/OFF times
    const unsigned long minOnTime = 1000;   // Minimum 1 second ON (was 5s - too aggressive!)
    const unsigned long minOffTime = 1000;  // Minimum 1 second OFF 
    const float minOutputThreshold = 0.02;  // Below 2% output, turn OFF completely
\
    // If output is very low, just turn OFF completely (avoid excessive short cycling)
    if (Output < minOutputThreshold) {
      onTime = 0;  // Complete OFF
    }
    // If output is 100% (or very close), force ON for entire window
    else if (Output >= 0.999f) {
      onTime = windowSize;
    }
    // Otherwise apply minimum ON/OFF time logic
    else {
      if (onTime > 0 && onTime < minOnTime) {
        onTime = minOnTime;  // Extend very short ON periods to protect relay
      }
      // Ensure minimum OFF time, but only if there is actually an OFF period
      if (onTime > 0 && onTime < windowSize && (windowSize - onTime) < minOffTime) {
        onTime = windowSize - minOffTime;  // Ensure minimum OFF time
      }
    }
    
    if (debugSerial && millis() % 15000 < 50) {  // Debug every 15 seconds
      Serial.printf("[PID-RELAY] Setpoint: %.1f°C, Input: %.1f°C, Output: %.2f, OnTime: %lums/%lums (%.1f%%) %s\n", 
                   Setpoint, Input, Output, onTime, windowSize, (onTime * 100.0 / windowSize),
                   shouldRestartWindow ? "[DYNAMIC]" : "[NORMAL]");
    }
  }
  
  // Determine heater state based on position within current window
  elapsed = nowMs - windowStartTime; // Recalculate elapsed after potential window restart
  bool heaterShouldBeOn = (elapsed < onTime);
  setHeater(heaterShouldBeOn);
}

// --- Enhanced heater status reporting for debugging ---
void streamHeaterDebugStatus(Print& out) {
  unsigned long nowMs = millis();
  unsigned long elapsed = (windowStartTime > 0) ? (nowMs - windowStartTime) : 0;
  float windowProgress = (windowSize > 0) ? ((float)elapsed / windowSize) * 100 : 0;
  float onTimePercent = (windowSize > 0) ? ((float)onTime / windowSize) * 100 : 0;

  bool canRestartNow = (elapsed >= MIN_WINDOW_TIME);
  bool significantChange = (abs(Output - lastPIDOutput) >= PID_CHANGE_THRESHOLD);
  bool needMoreHeat = (Output > lastPIDOutput && elapsed >= onTime && Output > 0.7);
  bool needLessHeat = (Output < lastPIDOutput && elapsed < onTime && Output < 0.3);
  unsigned long theoreticalOnTime = (unsigned long)(Output * windowSize);
  unsigned long minOffTime =  5000;
  bool relayProtectionActive = (theoreticalOnTime > 0 && (windowSize - theoreticalOnTime) < minOffTime);

  out.print("{");
  out.printf("\"heater_physical_state\":%s,", heaterState ? "true" : "false");
  out.printf("\"pid_output_percent\":%.2f,", Output * 100);
  out.printf("\"setpoint\":%.2f,", Setpoint);
  out.printf("\"manual_mode\":%s,", manualMode ? "true" : "false");
  out.printf("\"is_running\":%s,", isRunning ? "true" : "false");
  out.printf("\"window_size_ms\":%lu,", windowSize);
  out.printf("\"window_elapsed_ms\":%lu,", elapsed);
  out.printf("\"window_progress_percent\":%.2f,", windowProgress);
  out.printf("\"calculated_on_time_ms\":%lu,", onTime);
  out.printf("\"on_time_percent\":%.2f,", onTimePercent);
  out.printf("\"should_be_on\":%s,", (elapsed < onTime) ? "true" : "false");
  out.printf("\"dynamic_restarts\":%u,", dynamicRestartCount);
  out.printf("\"last_restart_reason\":\"%s\",", lastDynamicRestartReason.c_str());
  out.printf("\"last_restart_ago_ms\":%lu,", (lastDynamicRestart > 0) ? (nowMs - lastDynamicRestart) : 0);
  out.printf("\"last_pid_output\":%.2f,", lastPIDOutput);
  out.printf("\"pid_output_change\":%.2f,", abs(Output - lastPIDOutput));
  out.printf("\"change_threshold\":%.2f,", PID_CHANGE_THRESHOLD);
  out.printf("\"can_restart_now\":%s,", canRestartNow ? "true" : "false");
  out.printf("\"significant_change\":%s,", significantChange ? "true" : "false");
  out.printf("\"restart_condition_more_heat\":%s,", needMoreHeat ? "true" : "false");
  out.printf("\"restart_condition_less_heat\":%s,", needLessHeat ? "true" : "false");
  out.printf("\"setpoint_override\":%s,", (Setpoint <= 0) ? "true" : "false");
  out.printf("\"running_override\":%s,", !isRunning ? "true" : "false");
  out.printf("\"manual_override\":%s,", manualMode ? "true" : "false");
  out.printf("\"relay_protection_active\":%s,", relayProtectionActive ? "true" : "false");
  out.printf("\"theoretical_on_time_ms\":%lu,", theoreticalOnTime);
  out.printf("\"enforced_min_off_time_ms\":%lu", minOffTime);
  out.print("}");
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
  
  // Restore last selected program by ID if present, fallback to name for backward compatibility
  if (doc.containsKey("lastProgramId")) {
    int lastId = doc["lastProgramId"];
    if (lastId >= 0 && lastId < (int)programs.size()) {
      activeProgramId = lastId;
      Serial.print("[loadSettings] lastProgramId loaded: ");
      Serial.println(lastId);
    }
  } else if (doc.containsKey("lastProgram")) {
    // Deprecated: fallback to name for backward compatibility
    String lastProg = doc["lastProgram"].as<String>();
    for (size_t i = 0; i < programs.size(); ++i) {
      if (programs[i].name == lastProg) {
        activeProgramId = i;
        Serial.print("[loadSettings] lastProgram loaded (deprecated): ");
        Serial.println(lastProg);
        break;
      }
    }
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
  if (programs.size() > 0) {
    doc["lastProgramId"] = (int)activeProgramId;
    doc["lastProgram"] = programs[activeProgramId].name; // Deprecated, for backward compatibility
  }
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
// --- Streaming status JSON function ---
void streamStatusJson(Print& out) {
  out.print("{");
  // scheduledStart
  out.printf("\"scheduledStart\":%lu,", (unsigned long)scheduledStart);
  // running
  out.printf("\"running\":%s,", isRunning ? "true" : "false");
  // program
  if (activeProgramId < programs.size()) {
    out.printf("\"program\":\"%s\",", programs[activeProgramId].name.c_str());
    out.printf("\"programId\":%u,", (unsigned)activeProgramId);
  } else {
    out.print("\"program\":\"\",\"programId\":0,");
  }
  // nowMs, now
  unsigned long nowMs = millis();
  time_t now = time(nullptr);
  unsigned long es = 0;
  int stageLeft = 0;
  time_t stageReadyAt = 0;
  time_t programReadyAt = 0;
  double fermentationStageTimeLeft = 0.0;
  // stage, stageStartTimes, programStart, elapsed, stageReadyAt, programReadyAt
  if (activeProgramId < programs.size()) {
    Program &p = programs[activeProgramId];
    // Determine preview or running mode
    bool previewMode = !isRunning;
    // Stage label
    if (isRunning && customStageIdx < p.customStages.size()) {
      out.print("\"stage\":\"");
      out.print(p.customStages[customStageIdx].label.c_str());
      out.print("\",");
    } else {
      out.print("\"stage\":\"Idle\",");
    }
    // --- Predicted (temperature-adjusted) stage end times ---
    out.print("\"predictedStageEndTimes\":[");
    float actualTemp = getAveragedTemperature();
    float baseline = p.fermentBaselineTemp > 0 ? p.fermentBaselineTemp : 20.0;
    float q10 = p.fermentQ10 > 0 ? p.fermentQ10 : 2.0;
    float fermentationFactorForProgram = pow(q10, (baseline - actualTemp) / 10.0);
    // Use programStartTime if running, otherwise use now as preview start
    unsigned long programStartUnix = (unsigned long)(isRunning ? programStartTime : time(nullptr));
    double cumulativePredictedSec = 0.0;
    for (size_t i = 0; i < p.customStages.size(); ++i) {
      CustomStage &stage = p.customStages[i];
      double plannedStageSec = (double)stage.min * 60.0;
      double adjustedStageSec = plannedStageSec;
      if (stage.isFermentation) {
        adjustedStageSec = plannedStageSec * fermentationFactorForProgram;
      }
      cumulativePredictedSec += adjustedStageSec;
      if (i > 0) out.print(",");
      out.printf("%lu", (unsigned long)(programStartUnix + (unsigned long)(cumulativePredictedSec)));
    }
    out.print("],");
    // Always report the program's fermentation factor (for display in UI)
    fermentationFactor = fermentationFactorForProgram;
    // --- Predicted (temperature-adjusted) program end time ---
    out.printf("\"predictedProgramEnd\":%lu,", (unsigned long)(programStartUnix + (unsigned long)(cumulativePredictedSec)));
    // --- Legacy: stageStartTimes array (planned, not adjusted) ---
    out.print("\"stageStartTimes\":[");
    time_t t = isRunning ? programStartTime : time(nullptr);
    for (size_t i = 0; i < p.customStages.size(); ++i) {
      if (i > 0) out.print(",");
      out.printf("%lu", (unsigned long)t);
      t += (time_t)p.customStages[i].min * 60;
    }
    out.print("],");
    out.printf("\"programStart\":%lu,", (unsigned long)programStartUnix);
    // elapsed
    es = (customStageStart == 0) ? 0 : (nowMs - customStageStart) / 1000;
    // Calculate stageReadyAt and programReadyAt (legacy, not adjusted)
    stageReadyAt = 0;
    programReadyAt = 0;
    if (customStageIdx < p.customStages.size()) {
      CustomStage &st = p.customStages[customStageIdx];
      if (st.isFermentation) {
        // Calculate temperature-adjusted time left for fermentation stage
        double plannedStageSec = (double)st.min * 60.0;
        double elapsedSec = fermentWeightedSec;
        double realElapsedSec = (nowMs - fermentLastUpdateMs) / 1000.0;
        elapsedSec += realElapsedSec * fermentLastFactor;
        double remainSec = plannedStageSec - (elapsedSec / fermentLastFactor);
        if (remainSec < 0) remainSec = 0;
        fermentationStageTimeLeft = remainSec * fermentLastFactor;
        stageLeft = (int)fermentationStageTimeLeft;
      } else {
        if (actualStageStartTimes[customStageIdx] > 0) {
          stageReadyAt = actualStageStartTimes[customStageIdx] + (time_t)p.customStages[customStageIdx].min * 60;
        }
        if (actualStageStartTimes[customStageIdx] > 0) {
          unsigned long actualElapsedInStage = (millis() - customStageStart) / 1000UL;
          unsigned long plannedStageDuration = (unsigned long)p.customStages[customStageIdx].min * 60UL;
          unsigned long timeLeftInCurrentStage = (actualElapsedInStage < plannedStageDuration) ? (plannedStageDuration - actualElapsedInStage) : 0;
          stageLeft = (int)timeLeftInCurrentStage;
          programReadyAt = now + (time_t)timeLeftInCurrentStage;
          for (size_t i = customStageIdx + 1; i < p.customStages.size(); ++i) {
            programReadyAt += (time_t)p.customStages[i].min * 60;
          }
        } else {
          programReadyAt = isRunning ? programStartTime : time(nullptr);
          for (size_t i = 0; i < p.customStages.size(); ++i) {
            programReadyAt += (time_t)p.customStages[i].min * 60;
          }
        }
      }
    }
  } else {
    out.print("\"stage\":\"Idle\",");
    out.print("\"predictedStageEndTimes\":[],");
    out.print("\"predictedProgramEnd\":0,");
    out.print("\"stageStartTimes\":[],");
    out.print("\"programStart\":0,");
  }
  out.printf("\"elapsed\":%lu,", es);
  out.printf("\"setTemp\":%.2f,", Setpoint);
  out.printf("\"timeLeft\":%d,", stageLeft);
  out.printf("\"stageReadyAt\":%lu,", (unsigned long)stageReadyAt);
  out.printf("\"programReadyAt\":%lu,", (unsigned long)programReadyAt);
  out.printf("\"temp\":%.2f,", averagedTemperature);
  out.printf("\"motor\":%s,", motorState ? "true" : "false");
  out.printf("\"light\":%s,", lightState ? "true" : "false");
  out.printf("\"buzzer\":%s,", buzzerState ? "true" : "false");
  out.printf("\"stageStartTime\":%lu,", (unsigned long)customStageStart);
  out.printf("\"manualMode\":%s,", manualMode ? "true" : "false");
  // startup delay
  out.printf("\"startupDelayComplete\":%s,", isStartupDelayComplete() ? "true" : "false");
  if (!isStartupDelayComplete()) {
    out.printf("\"startupDelayRemainingMs\":%lu,", STARTUP_DELAY_MS - (millis() - startupTime));
  } else {
    out.print("\"startupDelayRemainingMs\":0,");
  }
  // actualStageStartTimes
  out.print("\"actualStageStartTimes\":[");
  for (int i = 0; i < 20; i++) {
    if (i > 0) out.print(",");
    out.printf("%lu", (unsigned long)actualStageStartTimes[i]);
  }
  out.print("],");
  // stageIdx, mixIdx, heater, buzzer
  out.printf("\"stageIdx\":%u,", (unsigned)customStageIdx);
  out.printf("\"mixIdx\":%u,", (unsigned)customMixIdx);
  out.printf("\"heater\":%s,", heaterState ? "true" : "false");
  out.printf("\"buzzer\":%s,", buzzerState ? "true" : "false");
  // fermentation info
  out.printf("\"fermentationFactor\":%.2f,", fermentationFactor);
  out.printf("\"predictedCompleteTime\":%lu,", (unsigned long)predictedCompleteTime);
  // Also update programReadyAt to match predictedCompleteTime if running
  if (isRunning) {
    out.printf("\"programReadyAt\":%lu,", (unsigned long)predictedCompleteTime);
  }
  // --- WiFi signal strength ---
  out.printf("\"wifiRssi\":%d,", WiFi.RSSI());
  out.printf("\"wifiConnected\":%s,", (WiFi.status() == WL_CONNECTED) ? "true" : "false");
  out.printf("\"initialFermentTemp\":%.2f", initialFermentTemp);
  out.print("}");
}
