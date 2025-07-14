#include <functional>


// Maximum number of program stages supported (now in globals.cpp)
#include "globals.h"

// --- Temperature safety flags ---
bool thermalRunawayDetected = false;
bool sensorFaultDetected = false;
// --- Active program index (ID) now in programState ---
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
#include "web_endpoints.h"

// --- Firmware build date ---
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// --- Web server---
AsyncWebServer server(80); // Main web server

// --- Pin assignments ---
// PIN_RTD now defined in globals.cpp
// (Output pins are defined in outputs_manager.cpp)


#define DEFAULT_KP 0.11
#define DEFAULT_KI 0.00005
#define DEFAULT_KD 10.0
#define DEFAULT_WINDOW_MS 30000

// In setup(), initialize the PID controller:
// pid.controller = new PID(&pid.Input, &pid.Output, &pid.Setpoint, pid.Kp, pid.Ki, pid.Kd, DIRECT);

// --- Time-proportional control variables ---
unsigned long windowStartTime = 0;
unsigned long windowSize = 30000;        // 30 second window in milliseconds (relay-friendly, now configurable)
unsigned long onTime = 0;                // ON time in milliseconds within current window
double lastPIDOutput = 0.0;              // Previous PID output for dynamic window adjustment
const double PID_CHANGE_THRESHOLD = 0.3; // Threshold for significant PID output change (30%)
const unsigned long MIN_WINDOW_TIME = 5000; // Minimum time before allowing dynamic restart (5 seconds)

time_t scheduledStart = 0; // Scheduled start time (epoch)
int scheduledStartStage = -1; // Stage to start at when scheduled (-1 = start from beginning)

// --- Custom stage state variables now in programState ---
// Use programState.customStageIdx, programState.customMixIdx, programState.customStageStart, programState.customMixStepStart, programState.programStartTime, programState.actualStageStartTimes, etc.
// --- Active program variables now in programState ---
// Use programState.activeProgramId, programState.customProgram, programState.maxCustomStages

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

// --- Startup delay to let temperature sensor stabilize ---
unsigned long startupTime = 0;           // Time when system started (millis())
// STARTUP_DELAY_MS now defined in globals.cpp

// --- Function prototypes ---
void loadSettings();
void saveSettings();
void loadPIDProfiles();
void savePIDProfiles();
PIDProfile* findProfileForTemperature(float temperature);
void switchToProfile(const String& profileName);
void checkAndSwitchPIDProfile();
void updateTemperatureSampling();
float getAveragedTemperature();
void streamStatusJson(Print& out);

// --- Forward declaration for stopBreadmaker (must be before any use) ---
void stopBreadmaker();

// --- Forward declaration for startup delay check ---
bool isStartupDelayComplete();

// --- Delete Folder API endpoint ---
// Recursively deletes a folder and all its contents from the LittleFS filesystem.
void deleteFolderRecursive(const String& path) {
  if (!LittleFS.exists(path)) {
    if (debugSerial) Serial.printf("[deleteFolderRecursive] WARNING: Path '%s' does not exist.\n", path.c_str());
    return;
  }
  Dir dir = LittleFS.openDir(path);
  while (dir.next()) {
    String filePath = path + "/" + dir.fileName();
    if (dir.isDirectory()) {
      deleteFolderRecursive(filePath);
    } else {
      if (!LittleFS.remove(filePath) && debugSerial) {
        Serial.printf("[deleteFolderRecursive] ERROR: Failed to remove file '%s'\n", filePath.c_str());
      }
    }
  }
  if (!LittleFS.rmdir(path) && debugSerial) {
    Serial.printf("[deleteFolderRecursive] ERROR: Failed to remove directory '%s'\n", path.c_str());
  }
}

// Helper function to send a JSON error response
// Sends a JSON-formatted error message to the client with a specified HTTP status code.
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

// Initializes all hardware, outputs, file systems, and loads configuration and calibration data.
// Also sets up WiFi, mDNS, and captive portal as needed.
void initialState(){
  // --- Ensure all outputs are OFF immediately on boot/reset ---
  setHeater(false);
  setMotor(false);
  setLight(false);
  setBuzzer(false);
  outputsManagerInit(); // Initialize output pins and set all outputs OFF

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
}


// Arduino setup function. Initializes system state, registers web endpoints, starts the web server,
// configures PID and temperature averaging, and waits for NTP time sync.
void setup() {
  initialState();
  registerWebEndpoints(server);

  
  server.serveStatic("/", LittleFS, "/");
  server.begin();
  Serial.println(F("HTTP server started"));
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  // --- Initialize PID controller ---
  if (pid.controller) {
    pid.controller->SetMode(AUTOMATIC);
    pid.controller->SetOutputLimits(0, 1);  // Output range: 0 (0% duty) to 1 (100% duty)
    pid.controller->SetSampleTime(pid.sampleTime);    // Use loaded sample time (relay-friendly)
  }
  pid.Setpoint = 0;  // Default to no heating
  windowStartTime = millis();   // Initialize time-proportional control
  lastPIDOutput = 0.0;          // Initialize last PID output for dynamic window adjustment
  
  if (debugSerial) {
    Serial.printf("[PID] Initialized with Kp=%.6f, Ki=%.6f, Kd=%.6f, SampleTime=%lums\n", 
                  pid.Kp, pid.Ki, pid.Kd, pid.sampleTime);
  }
  
  // --- Initialize temperature averaging system ---
  for (int i = 0; i < MAX_TEMP_SAMPLES; i++) {
    tempAvg.tempSamples[i] = 0.0;
  }
  tempAvg.tempSampleIndex = 0;
  tempAvg.tempSamplesReady = false;
  tempAvg.lastTempSample = 0;
  tempAvg.averagedTemperature = readTemperature(); // Initialize with first reading
  
  Serial.println(F("PID controller initialized with relay-friendly time-proportional control"));
  Serial.println(F("Temperature averaging system initialized (10 samples, 0.5s interval, reject top/bottom 2)"));
  
  // Wait for time sync with timeout to avoid hanging forever
  unsigned long ntpStart = millis();
  const unsigned long ntpTimeout = 15000; // 15 seconds max wait
  while (time(nullptr) < 100000 && (millis() - ntpStart < ntpTimeout)) delay(200);
  if (time(nullptr) < 100000 && debugSerial) {
    Serial.println("[setup] WARNING: NTP time sync timed out after 15 seconds.");
  }
}

const char* RESUME_FILE = "/resume.json";
unsigned long lastResumeSave = 0;

// Saves the current breadmaker state (program, stage, timing, etc.) to LittleFS for resume after reboot.
// Includes throttling to prevent excessive writes under load conditions.
void saveResumeState() {
  // Throttle saves: minimum 2 seconds between saves to reduce wear and memory pressure
  unsigned long now = millis();
  if (lastResumeSave > 0 && (now - lastResumeSave) < 2000) {
    return; // Skip this save to reduce load
  }
  lastResumeSave = now;
  
  File f = LittleFS.open(RESUME_FILE, "w");
  if (!f) {
    if (debugSerial) Serial.println("[saveResumeState] ERROR: Failed to open resume file for writing!");
    return;
  }
  serializeResumeStateJson(f);
  f.close();
}

// Helper to serialize resume state as JSON using memory-efficient streaming
void serializeResumeStateJson(Print& f) {
  // Calculate elapsed times once to avoid multiple calculations
  unsigned long stageSec = (programState.customStageStart > 0) ? (millis() - programState.customStageStart) / 1000UL : 0UL;
  unsigned long mixSec = (programState.customMixStepStart > 0) ? (millis() - programState.customMixStepStart) / 1000UL : 0UL;
  
  // Stream JSON directly to file - no large buffers needed
  f.print("{\n");
  f.printf("  \"programIdx\":%u,\n", (unsigned)programState.activeProgramId);
  f.printf("  \"customStageIdx\":%u,\n", (unsigned)programState.customStageIdx);
  f.printf("  \"customMixIdx\":%u,\n", (unsigned)programState.customMixIdx);
  f.printf("  \"elapsedStageSec\":%lu,\n", stageSec);
  f.printf("  \"elapsedMixSec\":%lu,\n", mixSec);
  f.printf("  \"programStartTime\":%lu,\n", (unsigned long)programState.programStartTime);
  f.printf("  \"isRunning\":%s,\n", programState.isRunning ? "true" : "false");
  f.print("  \"actualStageStartTimes\":[");
  for (int i = 0; i < 20; i++) {
    if (i > 0) f.print(",");
    f.printf("%lu", (unsigned long)programState.actualStageStartTimes[i]);
  }
  f.print("]\n");
  f.print("}\n");
}

// Removes the saved resume state file from LittleFS.
void clearResumeState() {
  LittleFS.remove(RESUME_FILE);
}

// Loads the breadmaker's previous state from LittleFS and restores program, stage, and timing.
// Optimized for memory efficiency and better error handling.
void loadResumeState() {
  File f = LittleFS.open(RESUME_FILE, "r");
  if (!f) return;
  
  // Use a smaller buffer since resume files are typically <400 bytes
  DynamicJsonDocument doc(768); // Reduced from 1024 to 768 bytes
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  
  if (err) {
    if (debugSerial) {
      Serial.print("[loadResumeState] JSON parse error: ");
      Serial.println(err.c_str());
    }
    return;
  }
  
  // Restore program by id with better validation
  int progIdx = doc["programIdx"] | -1;
  if (progIdx >= 0 && progIdx < (int)programs.size() && 
      programs[progIdx].id == progIdx && programs[progIdx].id != -1) {
    programState.activeProgramId = progIdx;
    updateActiveProgramVars();
  } else {
    // Fallback: select first valid program (id!=-1) or 0 if none
    programState.activeProgramId = 0;
    for (size_t i = 0; i < programs.size(); ++i) {
      if (programs[i].id != -1) { 
        programState.activeProgramId = i; 
        break; 
      }
    }
    updateActiveProgramVars();
  }
  
  // Load basic state with bounds checking
  programState.customStageIdx = constrain(doc["customStageIdx"] | 0, 0, 19);
  programState.customMixIdx = constrain(doc["customMixIdx"] | 0, 0, 9);
  unsigned long elapsedStageSec = doc["elapsedStageSec"] | 0;
  unsigned long elapsedMixSec = doc["elapsedMixSec"] | 0;
  programState.programStartTime = doc["programStartTime"] | 0;
  bool wasRunning = doc["isRunning"] | false;

  // Check startup delay before resuming
  if (wasRunning && !isStartupDelayComplete()) {
    // Don't resume immediately - wait for startup delay
    programState.isRunning = false;
    if (debugSerial) {
      unsigned long remaining = STARTUP_DELAY_MS - (millis() - startupTime);
      Serial.printf("[RESUME] Delaying resume for %lu ms (temperature sensor stabilization)\n", remaining);
    }
  } else {
    programState.isRunning = wasRunning;
  }

  // Load actual stage start times
  for (int i = 0; i < 20; i++) programState.actualStageStartTimes[i] = 0;
  if (doc.containsKey("actualStageStartTimes") && doc["actualStageStartTimes"].is<JsonArray>()) {
    JsonArray stageStartArray = doc["actualStageStartTimes"];
    for (int i = 0; i < min(20, (int)stageStartArray.size()); i++) {
      programState.actualStageStartTimes[i] = stageStartArray[i];
    }
  }

  // Ensure indices are always valid after resume
  if (programState.activeProgramId < programs.size() && programs[programState.activeProgramId].id != -1) {
    Program &p = programs[programState.activeProgramId];
    if (programState.customStageIdx >= p.customStages.size()) programState.customStageIdx = 0;
    if (!p.customStages.empty()) {
      CustomStage &st = p.customStages[programState.customStageIdx];
      if (!st.mixPattern.empty() && programState.customMixIdx >= st.mixPattern.size()) programState.customMixIdx = 0;
    } else {
      programState.customMixIdx = 0;
    }
  } else {
    programState.customStageIdx = 0;
    programState.customMixIdx = 0;
  }

  // Fast-forward logic: if elapsed time > stage/mix durations, advance indices
  if (programState.isRunning && programState.activeProgramId < programs.size() && programs[programState.activeProgramId].id != -1) {
    Program &p = programs[programState.activeProgramId];
    // Fast-forward stages
    size_t stageIdx = programState.customStageIdx;
    unsigned long remainStageSec = elapsedStageSec;
    while (stageIdx < p.customStages.size()) {
      unsigned long stageDurSec = p.customStages[stageIdx].min * 60UL;
      if (remainStageSec < stageDurSec) break;
      remainStageSec -= stageDurSec;
      stageIdx++;
    }
    if (stageIdx >= p.customStages.size()) {
      // Program finished
      programState.customStageIdx = 0;
      programState.customMixIdx = 0;
      programState.customStageStart = 0;
      programState.customMixStepStart = 0;
      programState.isRunning = false;
      clearResumeState();
      return;
    }
    programState.customStageIdx = stageIdx;
    programState.customStageStart = millis() - remainStageSec * 1000UL;
    if (programState.customStageStart == 0) programState.customStageStart = millis(); // Ensure nonzero for UI
    // Fast-forward mix steps if mixPattern exists
    CustomStage &st = p.customStages[programState.customStageIdx];
    if (!st.mixPattern.empty()) {
      size_t mixIdx = programState.customMixIdx;
      unsigned long remainMixSec = elapsedMixSec;
      while (mixIdx < st.mixPattern.size()) {
        unsigned long mixDurSec = st.mixPattern[mixIdx].mixSec + st.mixPattern[mixIdx].waitSec;
        if (remainMixSec < mixDurSec) break;
        remainMixSec -= mixDurSec;
        mixIdx++;
      }
      if (mixIdx >= st.mixPattern.size()) mixIdx = 0;
      programState.customMixIdx = mixIdx;
      programState.customMixStepStart = millis() - remainMixSec * 1000UL;
      if (programState.customMixStepStart == 0) programState.customMixStepStart = millis(); // Ensure nonzero for UI
    } else {
      programState.customMixIdx = 0;
      programState.customMixStepStart = millis(); // Ensure nonzero for UI
    }
  } else {
    // Not running or invalid program
    programState.customStageStart = millis() - elapsedStageSec * 1000UL;
    programState.customMixStepStart = millis() - elapsedMixSec * 1000UL;
    // Ensure all outputs are OFF if not running
    setHeater(false);
    setMotor(false);
    setLight(false);
    setBuzzer(false);
  }
}

// Helper to reset fermentation tracking variables
// Resets all fermentation tracking variables to initial values for a new fermentation stage.
void resetFermentationTracking(float temp) {
  fermentState.initialFermentTemp = 0.0;
  fermentState.fermentationFactor = 1.0;
  fermentState.predictedCompleteTime = 0;
  fermentState.lastFermentAdjust = 0;
  fermentState.fermentLastTemp = temp;
  fermentState.fermentLastFactor = 1.0;
  fermentState.fermentLastUpdateMs = 0;
  fermentState.fermentWeightedSec = 0.0;
}

// --- Helper function declarations ---
// Updates fermentation timing and advances stage if weighted time is met.
void updateFermentationTiming(bool &stageJustAdvanced);
// Checks if a delayed resume is needed after startup delay.
void checkDelayedResume();
// Handles manual mode operation (direct user control).
void handleManualMode();
// Handles scheduled program start logic.
void handleScheduledStart(bool &scheduledStartTriggered);
// Handles the main custom stage logic for the breadmaker program.
void handleCustomStages(bool &stageJustAdvanced);

// Arduino main loop. Handles temperature sampling, fermentation, manual mode, scheduled start,
// and custom stage logic for breadmaker operation.
void loop() {
  updateTemperatureSampling();
  updateBuzzerTone();
  float temp = getAveragedTemperature();
  static bool stageJustAdvanced = false;
  static bool scheduledStartTriggered = false;

  // Check and switch PID profile based on temperature
  checkAndSwitchPIDProfile();

  updateFermentationTiming(stageJustAdvanced);

  // --- Check for stage advancement and log ---
  if (stageJustAdvanced && programs.size() > 0 && programState.activeProgramId < programs.size() && programState.customStageIdx < programs[programState.activeProgramId].customStages.size()) {
    Program &p = programs[programState.activeProgramId];
    CustomStage &st = p.customStages[programState.customStageIdx];
    if (st.isFermentation) {
      if (debugSerial) Serial.printf("[STAGE ADVANCE] Fermentation stage advanced to %d\n", (int)programState.customStageIdx);
    } else {
      if (debugSerial) Serial.printf("[STAGE ADVANCE] Non-fermentation stage advanced to %d\n", (int)programState.customStageIdx);
    }
  }

  if (!programState.isRunning) {
    stageJustAdvanced = false;
    checkDelayedResume();
    handleManualMode();
    handleScheduledStart(scheduledStartTriggered);
    yield(); delay(1); return;
  }
  if (programs.size() == 0 || programState.activeProgramId >= programs.size()) {
    stageJustAdvanced = false;
    stopBreadmaker(); yield(); delay(1); return;
  }
  handleCustomStages(stageJustAdvanced);
  temp = getAveragedTemperature();
  yield();
  delay(1);
}

// --- Helper function definitions ---
// Updates fermentation timing, calculates weighted time, and advances stage if needed.
// NOTE: This is the ONLY place where fermentation stages should advance to prevent race conditions.
void updateFermentationTiming(bool &stageJustAdvanced) {
  if (programs.size() > 0 && programState.activeProgramId < programs.size() && programState.customStageIdx < programs[programState.activeProgramId].customStages.size()) {
    Program &p = programs[programState.activeProgramId];
    CustomStage &st = p.customStages[programState.customStageIdx];
    if (st.isFermentation) {
      float baseline = p.fermentBaselineTemp > 0 ? p.fermentBaselineTemp : 20.0;
      float q10 = p.fermentQ10 > 0 ? p.fermentQ10 : 2.0;
      float actualTemp = getAveragedTemperature();
      unsigned long nowMs = millis();
      if (fermentState.fermentLastUpdateMs == 0) {
        fermentState.fermentLastTemp = actualTemp;
        fermentState.fermentLastFactor = pow(q10, (baseline - actualTemp) / 10.0); // Q10: factor < 1 means faster at higher temp
        fermentState.fermentLastUpdateMs = nowMs;
        fermentState.fermentWeightedSec = 0.0;
      } else if (programState.isRunning) {
        double elapsedSec = (nowMs - fermentState.fermentLastUpdateMs) / 1000.0;
        fermentState.fermentWeightedSec += elapsedSec * fermentState.fermentLastFactor;
        fermentState.fermentLastTemp = actualTemp;
        fermentState.fermentLastFactor = pow(q10, (baseline - actualTemp) / 10.0);
        fermentState.fermentLastUpdateMs = nowMs;
      }
      fermentState.fermentationFactor = fermentState.fermentLastFactor; // For reference: multiply planned time by this factor for Q10
      double plannedStageSec = (double)st.min * 60.0;
      double epsilon = 0.05;
      if (!stageJustAdvanced && (fermentState.fermentWeightedSec + epsilon >= plannedStageSec)) {
        if (debugSerial) Serial.printf("[FERMENT] Auto-advance: weighted %.1fs >= planned %.1fs\n", fermentState.fermentWeightedSec, plannedStageSec);
        programState.customStageIdx++;
        programState.customStageStart = millis();
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
}

// Checks if the breadmaker should resume a previous program after the startup delay.
void checkDelayedResume() {
  static bool delayedResumeChecked = false;
  if (!delayedResumeChecked && isStartupDelayComplete()) {
    delayedResumeChecked = true;
    File f = LittleFS.open(RESUME_FILE, "r");
    if (f) {
      DynamicJsonDocument doc(256);
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (!err && (doc["isRunning"] | false)) {
        if (debugSerial) Serial.println("[RESUME] Startup delay complete, resuming program");
        loadResumeState();
      }
    }
  }
}

// Handles manual mode operation, including direct PID and output control.
void handleManualMode() {
  if (programState.manualMode && pid.Setpoint > 0) {
    pid.Input = getAveragedTemperature();
    double error = pid.Setpoint - pid.Input;
    double dInput = pid.Input - pid.lastInput;
    double kpUsed = pid.Kp, kiUsed = pid.Ki, kdUsed = pid.Kd;
    double sampleTimeSec = pid.sampleTime / 1000.0;
    pid.pidP = kpUsed * error;
    pid.lastITerm += kiUsed * error * sampleTimeSec;
    pid.pidI = pid.lastITerm;
    pid.pidD = -kdUsed * dInput / sampleTimeSec;
    pid.lastInput = pid.Input;
    if (pid.controller) pid.controller->Compute();
    updateTimeProportionalHeater();
    setMotor(false);
    setLight(false);
    setBuzzer(false);
  } else {
    setHeater(false);
    setMotor(false);
    setLight(false);
    setBuzzer(false);
  }
}

// Handles logic for starting a scheduled program at a specific time and stage.
void handleScheduledStart(bool &scheduledStartTriggered) {
  if (scheduledStart && time(nullptr) >= scheduledStart && !scheduledStartTriggered) {
    scheduledStartTriggered = true;
    programState.isRunning = true;
    if (scheduledStartStage >= 0 && scheduledStartStage < programState.maxCustomStages) {
      programState.customStageIdx = scheduledStartStage;
      if (debugSerial) Serial.printf("[SCHEDULED] Starting at stage %d\n", scheduledStartStage);
    } else {
      programState.customStageIdx = 0;
      if (debugSerial) Serial.printf("[SCHEDULED] Starting from beginning\n");
    }
    programState.customMixIdx = 0;
    programState.customStageStart = millis();
    programState.customMixStepStart = 0;
    programState.programStartTime = time(nullptr);
    for (int i = 0; i < 20; i++) programState.actualStageStartTimes[i] = 0;
    programState.actualStageStartTimes[programState.customStageIdx] = programState.programStartTime;
    
    // Initialize fermentation tracking for the starting stage
    resetFermentationTracking(getAveragedTemperature());
    
    saveResumeState();
    scheduledStart = 0;
    scheduledStartStage = -1;
  }
  if (!scheduledStart) scheduledStartTriggered = false;
}

// Handles the execution and advancement of custom program stages, including mixing and fermentation.
void handleCustomStages(bool &stageJustAdvanced) {
  Program &p = programs[programState.activeProgramId];
  if (!p.customStages.empty()) {
    stageJustAdvanced = false;
    if (programState.customStageIdx >= p.customStages.size()) {
      stageJustAdvanced = false;
      stopBreadmaker();
      programState.customStageIdx = 0;
      programState.customStageStart = 0;
      clearResumeState();
      yield();
      delay(1);
      return;
    }
    CustomStage &st = p.customStages[programState.customStageIdx];
    pid.Setpoint = st.temp;
    pid.Input = getAveragedTemperature();
    if (st.isFermentation) {
      // Already handled at top of loop
    } else {
      resetFermentationTracking(getAveragedTemperature());
    }
    if (fermentState.lastFermentAdjust == 0 || millis() - fermentState.lastFermentAdjust > 600000) {
      fermentState.lastFermentAdjust = millis();
      unsigned long nowMs = millis();
      unsigned long elapsedInCurrentStage = (programState.customStageStart > 0) ? (nowMs - programState.customStageStart) : 0UL;
      double cumulativePredictedSec = 0.0;
      for (size_t i = 0; i < p.customStages.size(); ++i) {
        CustomStage &stage = p.customStages[i];
        double plannedStageSec = (double)stage.min * 60.0;
        double adjustedStageSec = plannedStageSec;
        if (stage.isFermentation) {
          if (i < programState.customStageIdx) {
            float baseline = p.fermentBaselineTemp > 0 ? p.fermentBaselineTemp : 20.0;
            float q10 = p.fermentQ10 > 0 ? p.fermentQ10 : 2.0;
            float temp = fermentState.fermentLastTemp;
            float factor = pow(q10, (baseline - temp) / 10.0); // Q10: factor < 1 means faster at higher temp
            cumulativePredictedSec += plannedStageSec * factor; // Multiply: higher temp = less time
          } else if (i == programState.customStageIdx) {
            double elapsedSec = fermentState.fermentWeightedSec;
            double realElapsedSec = (nowMs - fermentState.fermentLastUpdateMs) / 1000.0;
            elapsedSec += realElapsedSec * fermentState.fermentLastFactor;
            // For current fermentation stage, estimate remaining time:
            // elapsedSec is already weighted (i.e., elapsed * factor), so to get the equivalent real elapsed time at the current temperature,
            // we divide by the factor. This is the only place division is correct: it "unweights" the elapsed time so we can subtract from planned.
            // All future/remaining time is then multiplied by the factor for Q10 logic.
            double remainSec = plannedStageSec - (elapsedSec / fermentState.fermentLastFactor); // Division: unweight elapsed, then multiply remaining by factor
            if (remainSec < 0) remainSec = 0;
            // Add elapsed (weighted) plus remaining (multiply by factor for Q10 logic)
            cumulativePredictedSec += elapsedSec + remainSec * fermentState.fermentLastFactor; // Multiply: remaining time is shorter at higher temp
          } else {
            cumulativePredictedSec += plannedStageSec * fermentState.fermentLastFactor; // Multiply: higher temp = less time
          }
        } else {
          cumulativePredictedSec += plannedStageSec;
        }
      }
      fermentState.predictedCompleteTime = (unsigned long)(programState.programStartTime + (unsigned long)(cumulativePredictedSec)); // All predicted times use Q10 multiply logic
    }
    if (st.temp > 0 || programState.manualMode) {
      if (programState.manualMode && pid.Setpoint > 0) {
        pid.Input = getAveragedTemperature();
        double error = pid.Setpoint - pid.Input;
        double dInput = pid.Input - pid.lastInput;
        double kpUsed = pid.Kp, kiUsed = pid.Ki, kdUsed = pid.Kd;
        double sampleTimeSec = pid.sampleTime / 1000.0;
        pid.pidP = kpUsed * error;
        pid.lastITerm += kiUsed * error * sampleTimeSec;
        pid.pidI = pid.lastITerm;
        pid.pidD = -kdUsed * dInput / sampleTimeSec;
        pid.lastInput = pid.Input;
        if (pid.controller) pid.controller->Compute();
        updateTimeProportionalHeater();
      } else if (!programState.manualMode && st.temp > 0) {
        double error = pid.Setpoint - pid.Input;
        double dInput = pid.Input - pid.lastInput;
        double kpUsed = pid.Kp, kiUsed = pid.Ki, kdUsed = pid.Kd;
        double sampleTimeSec = pid.sampleTime / 1000.0;
        pid.pidP = kpUsed * error;
        pid.lastITerm += kiUsed * error * sampleTimeSec;
        pid.pidI = pid.lastITerm;
        pid.pidD = -kdUsed * dInput / sampleTimeSec;
        pid.lastInput = pid.Input;
        if (pid.controller) pid.controller->Compute();
        updateTimeProportionalHeater();
      } else {
        setHeater(false);
        windowStartTime = 0;
        lastPIDOutput = 0.0;
      }
    } else {
      setHeater(false);
      windowStartTime = 0;
      lastPIDOutput = 0.0;
    }
    if (!programState.manualMode) {
      setLight(false);
      setBuzzer(false);
    }
    if (!programState.manualMode) {
      bool hasMix = false;
      if (st.noMix) {
        setMotor(false);
        hasMix = false;
      } else if (!st.mixPattern.empty()) {
        hasMix = true;
        if (programState.customMixIdx >= st.mixPattern.size()) {
          programState.customMixIdx = 0;
          if (debugSerial) Serial.printf("[MIX] Index out of bounds, reset to 0 (total patterns: %d)\n", st.mixPattern.size());
        }
        MixStep &step = st.mixPattern[programState.customMixIdx];
        unsigned long stepDuration = (step.durationSec > 0) ? (unsigned long)step.durationSec : (unsigned long)(step.mixSec + step.waitSec);
        if (programState.customMixStepStart == 0) {
          programState.customMixStepStart = millis();
          if (debugSerial) {
            if (stepDuration > (unsigned long)(step.mixSec + step.waitSec)) {
              unsigned long expectedCycles = stepDuration / (step.mixSec + step.waitSec);
              Serial.printf("[MIX] Starting pattern %d/%d: mix=%ds, wait=%ds, duration=%ds (≈%lu cycles)\n", 
                          programState.customMixIdx + 1, st.mixPattern.size(), step.mixSec, step.waitSec, stepDuration, expectedCycles);
            } else {
              Serial.printf("[MIX] Starting pattern %d/%d: mix=%ds, wait=%ds, duration=%ds (single cycle)\n", 
                          programState.customMixIdx + 1, st.mixPattern.size(), step.mixSec, step.waitSec, stepDuration);
            }
          }
        }
        unsigned long elapsed = (millis() - programState.customMixStepStart) / 1000UL;
        if (stepDuration > (unsigned long)(step.mixSec + step.waitSec)) {
          unsigned long cycleTime = step.mixSec + step.waitSec;
          unsigned long cycleElapsed = elapsed % cycleTime;
          if (cycleElapsed < (unsigned long)step.mixSec) {
            setMotor(true);
            if (debugSerial && (elapsed / cycleTime) != ((elapsed - 1) / cycleTime)) {
              Serial.printf("[MIX] Pattern %d cycle %lu: mixing (%lus elapsed)\n", programState.customMixIdx + 1, elapsed / cycleTime + 1, elapsed);
            }
          } else {
            setMotor(false);
          }
          if (elapsed >= stepDuration) {
            programState.customMixIdx++;
            programState.customMixStepStart = millis();
            if (programState.customMixIdx >= st.mixPattern.size()) {
              programState.customMixIdx = 0;
              if (debugSerial) Serial.printf("[MIX] All %d patterns complete, restarting from pattern 0\n", st.mixPattern.size());
            } else {
              if (debugSerial) Serial.printf("[MIX] Advancing to pattern %d\n", programState.customMixIdx);
            }
          }
        } else {
          if (elapsed < (unsigned long)step.mixSec) {
            setMotor(true);
          } else if (elapsed < (unsigned long)(step.mixSec + step.waitSec)) {
            setMotor(false);
          } else if (elapsed < stepDuration) {
            setMotor(false);
          } else {
            programState.customMixIdx++;
            programState.customMixStepStart = millis();
            if (programState.customMixIdx >= st.mixPattern.size()) {
              programState.customMixIdx = 0;
              if (debugSerial) Serial.printf("[MIX] All %d patterns complete, restarting from pattern 0\n", st.mixPattern.size());
            } else {
              if (debugSerial) Serial.printf("[MIX] Advancing to pattern %d\n", programState.customMixIdx);
            }
          }
        }
      } else {
        setMotor(true);
        hasMix = false;
      }
    }
    bool stageComplete = false;
    // Note: Fermentation stage advancement is handled in updateFermentationTiming()
    // to prevent duplicate advancement logic and race conditions
    if (st.isFermentation) {
      // For fermentation stages, completion is handled by updateFermentationTiming()
      // We only set stageComplete here if stageJustAdvanced flag is set
      stageComplete = stageJustAdvanced;
    } else {
      unsigned long elapsedMs = millis() - programState.customStageStart;
      // Prevent integer overflow: limit stage time to ~71 minutes (2^32 / 60000)
      unsigned long safeMin = (st.min > 71) ? 71 : st.min;
      if (st.min > 71 && debugSerial) {
        Serial.printf("[WARN] Stage duration %u minutes exceeds safe limit, capping at 71 minutes\n", st.min);
      }
      unsigned long stageMs = safeMin * 60000UL;
      if (elapsedMs >= stageMs) {
        stageComplete = true;
      } else if (stageMs > 0 && elapsedMs >= stageMs - 1000 && !stageComplete) {
        // Safety: If time left is 0 but not advancing, log warning
        if (debugSerial) Serial.printf("[WARN] Time left is 0 for non-fermentation stage %d but not advancing (elapsed=%lu, stageMs=%lu)\n", (int)programState.customStageIdx, elapsedMs, stageMs);
      }
    }
    // Extra safety mechanisms for stuck stages
    if (!st.isFermentation) {
      unsigned long elapsedMs = millis() - programState.customStageStart;
      // Use same safe calculation to prevent overflow
      unsigned long safeMin = (st.min > 71) ? 71 : st.min;
      unsigned long stageMs = safeMin * 60000UL;
      if (stageMs > 0 && elapsedMs > stageMs + 2000 && !stageComplete) {
        if (debugSerial) Serial.printf("[FORCE ADVANCE] Forcing advancement of non-fermentation stage %d after time expired (elapsed=%lu, stageMs=%lu)\n", (int)programState.customStageIdx, elapsedMs, stageMs);
        stageComplete = true;
      }
    } else {
      // Safety for fermentation stages: if real time is way beyond expected (4x), force advance
      unsigned long elapsedMs = millis() - programState.customStageStart;
      // Prevent overflow: limit calculation and use safe max time
      unsigned long safeMin = (st.min > 17) ? 17 : st.min; // 17 * 4 * 60000 = max safe value
      if (st.min > 17 && debugSerial) {
        Serial.printf("[WARN] Fermentation stage %u minutes too long for 4x safety calc, using 17 min limit\n", st.min);
      }
      unsigned long maxStageMs = safeMin * 60000UL * 4; // 4x expected time as safety limit
      if (maxStageMs > 0 && elapsedMs > maxStageMs && !stageComplete) {
        if (debugSerial) Serial.printf("[FORCE ADVANCE] Forcing advancement of stuck fermentation stage %d after %lu minutes (4x limit reached)\n", (int)programState.customStageIdx, elapsedMs / 60000UL);
        stageComplete = true;
        // Reset fermentation tracking to prevent issues in next stage
        resetFermentationTracking(getAveragedTemperature());
      }
    }
    if (stageComplete) {
      // If this was a fermentation stage, update predictedCompleteTime to now (or recalculate for next stage)
      if (st.isFermentation) {
        // Estimate remaining time for this stage (should be near zero if just advanced, but recalc for next stage)
        double plannedStageSec = (double)st.min * 60.0;
        double epsilon = 0.05;
        double remainWeightedSec = plannedStageSec - fermentState.fermentWeightedSec;
        if (remainWeightedSec < 0) remainWeightedSec = 0;
        double factor = fermentState.fermentLastFactor > 0 ? fermentState.fermentLastFactor : 1.0; // Q10: factor < 1 means faster
        time_t now = time(nullptr);
        time_t predicted = now + (time_t)(remainWeightedSec * factor); // Multiply: higher temp = less time
        fermentState.predictedCompleteTime = predicted;
        if (debugSerial) Serial.printf("[FERMENT] Stage advanced, predictedCompleteTime set to %lu (now=%lu, remainWeightedSec=%.2f, factor=%.3f) [MULTIPLY]\n", (unsigned long)predicted, (unsigned long)now, remainWeightedSec, factor);
      }
      programState.customStageIdx++;
      programState.customStageStart = millis();
      programState.customMixIdx = 0;
      programState.customMixStepStart = 0;
      saveResumeState();
      stageJustAdvanced = true;
      if (debugSerial) Serial.printf("[ADVANCE] Stage advanced to %d\n", programState.customStageIdx);
      getAveragedTemperature();
      yield();
      delay(1);
      return;
    }
  } else {
    yield();
    delay(100);
    return;
  }
}

// --- Update active program variables ---
// Updates pointers and counters for the currently active breadmaker program.
void updateActiveProgramVars() {
  if (programs.size() > 0 && programState.activeProgramId < programs.size()) {
    programState.customProgram = &programs[programState.activeProgramId];
    programState.maxCustomStages = programState.customProgram->customStages.size();
  } else {
    programState.customProgram = nullptr;
    programState.maxCustomStages = 0;
  }
}

// --- Temperature averaging functions ---
// Samples the temperature sensor, applies outlier rejection and weighted averaging, and updates the global temperature value.
void updateTemperatureSampling() {
  unsigned long nowMs = millis();
  
  // Sample at the configured interval
  if (nowMs - tempAvg.lastTempSample >= tempAvg.tempSampleInterval) {
    tempAvg.lastTempSample = nowMs;
    // Take a new temperature sample
    float rawTemp = readTemperature();
    tempAvg.tempSamples[tempAvg.tempSampleIndex] = rawTemp;
    tempAvg.tempSampleIndex = (tempAvg.tempSampleIndex + 1) % tempAvg.tempSampleCount;
    // Mark samples as ready once we've filled the array at least once
    if (tempAvg.tempSampleIndex == 0 && !tempAvg.tempSamplesReady) {
      tempAvg.tempSamplesReady = true;
    }
    // Calculate averaged temperature if we have enough samples
    if (tempAvg.tempSamplesReady && tempAvg.tempSampleCount > 0) {
      // Step 1: Copy and sort samples for outlier rejection
      float sortedSamples[MAX_TEMP_SAMPLES];
      for (int i = 0; i < tempAvg.tempSampleCount; i++) {
        sortedSamples[i] = tempAvg.tempSamples[i];
      }
      // Simple bubble sort
      for (int i = 0; i < tempAvg.tempSampleCount - 1; i++) {
        for (int j = 0; j < tempAvg.tempSampleCount - i - 1; j++) {
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
      int effectiveSamples = tempAvg.tempSampleCount - (2 * tempAvg.tempRejectCount);
      if (effectiveSamples >= 3) {
        for (int i = tempAvg.tempRejectCount; i < tempAvg.tempSampleCount - tempAvg.tempRejectCount; i++) {
          cleanSamples[cleanCount++] = sortedSamples[i];
        }
      } else {
        for (int i = 0; i < tempAvg.tempSampleCount; i++) {
          cleanSamples[cleanCount++] = sortedSamples[i];
        }
      }
      // Step 3: Detect trend direction and strength
      float firstHalf = 0.0, secondHalf = 0.0;
      int halfSize = cleanCount / 2;
      for (int i = 0; i < halfSize; i++) {
        firstHalf += cleanSamples[i];
      }
      for (int i = halfSize; i < cleanCount; i++) {
        secondHalf += cleanSamples[i];
      }
      firstHalf /= halfSize;
      secondHalf /= (cleanCount - halfSize);
      float trendStrength = secondHalf - firstHalf;
      float absTrendStrength = fabs(trendStrength);
      // Step 4: ALWAYS apply weighted average (adaptive weighting based on trend)
      float weightedSum = 0.0;
      float totalWeight = 0.0;
      for (int i = 0; i < cleanCount; i++) {
        float weight = 0.5 + (1.5 * i) / (cleanCount - 1);
        weightedSum += cleanSamples[i] * weight;
        totalWeight += weight;
      }
      tempAvg.averagedTemperature = weightedSum / totalWeight;
      // Debug output occasionally
      if (debugSerial && (nowMs % 30000 < tempAvg.tempSampleInterval)) {
        Serial.printf("[TEMP_AVG] Raw: %.2f°C, Filtered: %.2f°C, Trend: %.2f°C (from %d samples, mode: %s)\n",
                     rawTemp, tempAvg.averagedTemperature, trendStrength, cleanCount,
                     (absTrendStrength > 0.2) ? "TREND" : "STABLE");
      }
    } else {
      tempAvg.averagedTemperature = rawTemp;
    }
  }
}

// Returns the most recently calculated averaged temperature.
float getAveragedTemperature() {
  return tempAvg.averagedTemperature;
}

// --- Time-proportional PID control function ---
// Implements time-proportional relay control for the heater using PID output and dynamic windowing.
void updateTimeProportionalHeater() {
  unsigned long nowMs = millis();
  
  // Initialize window start time if not set
  if (windowStartTime == 0) {
    windowStartTime = nowMs;
    lastPIDOutput = pid.Output; // Initialize last PID output
  }
  
  // Check for dynamic window restart conditions
  bool shouldRestartWindow = false;
  unsigned long elapsed = nowMs - windowStartTime;
  
  // Dynamic window restart logic:
  // If PID output changes significantly AND we've been in this window for at least MIN_WINDOW_TIME
  if (elapsed >= MIN_WINDOW_TIME) {
    double outputChange = fabs(pid.Output - lastPIDOutput);
    
    // Case 1: Large increase in output (e.g., needs more heat quickly)
    if (outputChange >= PID_CHANGE_THRESHOLD && pid.Output > lastPIDOutput) {
      // If we're currently in the OFF part of the window and need much more heat
      if (elapsed >= onTime && pid.Output > 0.7) { // Need >70% output but heater is OFF
        shouldRestartWindow = true;
        if (debugSerial) {
          Serial.printf("[PID-DYNAMIC] Restarting window: Output jumped from %.2f to %.2f (need more heat)\n", 
                       lastPIDOutput, pid.Output);
        }
      }
    }
    
    // Case 2: Large decrease in output (e.g., overheating, need to turn off quickly)
    else if (outputChange >= PID_CHANGE_THRESHOLD && pid.Output < lastPIDOutput) {
      // If we're currently in the ON part of the window and need much less heat
      if (elapsed < onTime && pid.Output < 0.3) { // Need <30% output but heater is ON
        shouldRestartWindow = true;
        if (debugSerial) {
          Serial.printf("[PID-DYNAMIC] Restarting window: Output dropped from %.2f to %.2f (reduce heat)\n", 
                       lastPIDOutput, pid.Output);
        }
      }
    }
  }
  
  // Check if we need to start a new window (normal timing or dynamic restart)
  if (nowMs - windowStartTime >= windowSize || shouldRestartWindow) {
    // Track dynamic restart events
    if (shouldRestartWindow) {
      dynamicRestart.lastDynamicRestart = nowMs;
      dynamicRestart.dynamicRestartCount++;
      // Determine restart reason
      if (pid.Output > lastPIDOutput && pid.Output > 0.7) {
        dynamicRestart.lastDynamicRestartReason = "Need more heat (output increased to " + String((pid.Output*100), 1) + "%)";
      } else if (pid.Output < lastPIDOutput && pid.Output < 0.3) {
        dynamicRestart.lastDynamicRestartReason = "Reduce heat (output decreased to " + String((pid.Output*100), 1) + "%)";
      } else {
        dynamicRestart.lastDynamicRestartReason = "Output change (from " + String((lastPIDOutput*100), 1) + "% to " + String((pid.Output*100), 1) + "%)";
      }
      if (debugSerial) {
        Serial.printf("[PID-DYNAMIC] Restart #%u: %s (elapsed: %lums)\n", 
                     dynamicRestart.dynamicRestartCount, dynamicRestart.lastDynamicRestartReason.c_str(), elapsed);
      }
    }
    
    windowStartTime = nowMs;
    lastPIDOutput = pid.Output; // Update the last known PID output
    
    // Calculate ON time for this window based on PID output (0.0 to 1.0)
    onTime = (unsigned long)(pid.Output * windowSize);
    
    // Relay protection: smart minimum ON/OFF times
    const unsigned long minOnTime = 1000;   // Minimum 1 second ON (was 5s - too aggressive!)
    const unsigned long minOffTime = 1000;  // Minimum 1 second OFF 
    const float minOutputThreshold = 0.02;  // Below 2% output, turn OFF completely
\
    // If output is very low, just turn OFF completely (avoid excessive short cycling)
    if (pid.Output < minOutputThreshold) {
      onTime = 0;  // Complete OFF
    }
    // If output is 100% (or very close), force ON for entire window
    else if (pid.Output >= 0.999f) {
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
                   pid.Setpoint, pid.Input, pid.Output, onTime, windowSize, (onTime * 100.0 / windowSize),
                   shouldRestartWindow ? "[DYNAMIC]" : "[NORMAL]");
    }
  }
  
  // Determine heater state based on position within current window
  elapsed = nowMs - windowStartTime; // Recalculate elapsed after potential window restart
  bool heaterShouldBeOn = (elapsed < onTime);
  setHeater(heaterShouldBeOn);
}

// --- Enhanced heater status reporting for debugging ---
// Streams detailed heater and PID status as JSON for debugging purposes.
void streamHeaterDebugStatus(Print& out) {
  unsigned long nowMs = millis();
  unsigned long elapsed = (windowStartTime > 0) ? (nowMs - windowStartTime) : 0;
  float windowProgress = (windowSize > 0) ? ((float)elapsed / windowSize) * 100 : 0;
  float onTimePercent = (windowSize > 0) ? ((float)onTime / windowSize) * 100 : 0;

  bool canRestartNow = (elapsed >= MIN_WINDOW_TIME);
  bool significantChange = (abs(pid.Output - lastPIDOutput) >= PID_CHANGE_THRESHOLD);
  bool needMoreHeat = (pid.Output > lastPIDOutput && elapsed >= onTime && pid.Output > 0.7);
  bool needLessHeat = (pid.Output < lastPIDOutput && elapsed < onTime && pid.Output < 0.3);
  unsigned long theoreticalOnTime = (unsigned long)(pid.Output * windowSize);
  unsigned long minOffTime =  5000;
  bool relayProtectionActive = (theoreticalOnTime > 0 && (windowSize - theoreticalOnTime) < minOffTime);

  out.print("{");
  out.printf("\"heater_physical_state\":%s,", heaterState ? "true" : "false");
  out.printf("\"pid_output_percent\":%.2f,", pid.Output * 100);
  out.printf("\"setpoint\":%.2f,", pid.Setpoint);
  out.printf("\"manual_mode\":%s,", programState.manualMode ? "true" : "false");
  out.printf("\"is_running\":%s,", programState.isRunning ? "true" : "false");
  out.printf("\"window_size_ms\":%lu,", windowSize);
  out.printf("\"window_elapsed_ms\":%lu,", elapsed);
  out.printf("\"window_progress_percent\":%.2f,", windowProgress);
  out.printf("\"calculated_on_time_ms\":%lu,", onTime);
  out.printf("\"on_time_percent\":%.2f,", onTimePercent);
  out.printf("\"should_be_on\":%s,", (elapsed < onTime) ? "true" : "false");
  out.printf("\"dynamic_restarts\":%u,", dynamicRestart.dynamicRestartCount);
  out.printf("\"last_restart_reason\":\"%s\",", dynamicRestart.lastDynamicRestartReason.c_str());
  out.printf("\"last_restart_ago_ms\":%lu,", (dynamicRestart.lastDynamicRestart > 0) ? (nowMs - dynamicRestart.lastDynamicRestart) : 0);
  out.printf("\"last_pid_output\":%.2f,", lastPIDOutput);
  out.printf("\"pid_output_change\":%.2f,", abs(pid.Output - lastPIDOutput));
  out.printf("\"change_threshold\":%.2f,", PID_CHANGE_THRESHOLD);
  out.printf("\"can_restart_now\":%s,", canRestartNow ? "true" : "false");
  out.printf("\"significant_change\":%s,", significantChange ? "true" : "false");
  out.printf("\"restart_condition_more_heat\":%s,", needMoreHeat ? "true" : "false");
  out.printf("\"restart_condition_less_heat\":%s,", needLessHeat ? "true" : "false");
  out.printf("\"setpoint_override\":%s,", (pid.Setpoint <= 0) ? "true" : "false");
  out.printf("\"running_override\":%s,", !programState.isRunning ? "true" : "false");
  out.printf("\"manual_override\":%s,", programState.manualMode ? "true" : "false");
  out.printf("\"relay_protection_active\":%s,", relayProtectionActive ? "true" : "false");
  out.printf("\"theoretical_on_time_ms\":%lu,", theoreticalOnTime);
  out.printf("\"enforced_min_off_time_ms\":%lu", minOffTime);
  out.print("}");
}

// --- Startup delay check for temperature sensor stabilization ---
// Returns true if the startup delay for temperature sensor stabilization has elapsed.
bool isStartupDelayComplete() {
  unsigned long elapsed = millis() - startupTime;
  return elapsed >= STARTUP_DELAY_MS;
}

// Stops the breadmaker, turns off all outputs, and saves state.
void stopBreadmaker() {
  if (debugSerial) Serial.println("[ACTION] stopBreadmaker() called");
  programState.isRunning = false;
  setHeater(false);
  setMotor(false);
  setLight(false);
  setBuzzer(true); // Buzz for 200ms (handled non-blocking in loop)
  clearResumeState();
}

// Loads breadmaker settings (PID, temperature, program selection, etc.) from LittleFS.
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
  
  // Load PID parameters - backward compatibility
  if (doc.containsKey("pidKp")) {
    pid.Kp = doc["pidKp"] | 2.0;
    pid.Ki = doc["pidKi"] | 5.0;
    pid.Kd = doc["pidKd"] | 1.0;
    pid.sampleTime = doc["pidSampleTime"] | 1000;
    windowSize = doc["pidWindowSize"] | 30000;
    
    // Apply loaded PID parameters
    if (pid.controller) {
      pid.controller->SetTunings(pid.Kp, pid.Ki, pid.Kd);
      pid.controller->SetSampleTime(pid.sampleTime);
    }
    Serial.printf("[loadSettings] Legacy PID parameters loaded: Kp=%.6f, Ki=%.6f, Kd=%.6f\n", pid.Kp, pid.Ki, pid.Kd);
    Serial.printf("[loadSettings] Timing parameters loaded: SampleTime=%lums, WindowSize=%lums\n", pid.sampleTime, windowSize);
  }
  
  // Load temperature-dependent PID profiles
  loadPIDProfiles();
  
  // Load PID control settings
  if (doc.containsKey("activeProfile")) {
    pid.activeProfile = doc["activeProfile"] | "Baking Heat";
  }
  if (doc.containsKey("autoSwitching")) {
    pid.autoSwitching = doc["autoSwitching"] | true;
  }
  
  // Load temperature averaging parameters
  if (doc.containsKey("tempSampleCount")) {
    tempAvg.tempSampleCount = doc["tempSampleCount"] | 10;
    tempAvg.tempRejectCount = doc["tempRejectCount"] | 2;
    tempAvg.tempSampleInterval = doc["tempSampleInterval"] | 500;
    // Validate parameters
    if (tempAvg.tempSampleCount < 5) tempAvg.tempSampleCount = 5;
    if (tempAvg.tempSampleCount > MAX_TEMP_SAMPLES) tempAvg.tempSampleCount = MAX_TEMP_SAMPLES;
    if (tempAvg.tempRejectCount < 0) tempAvg.tempRejectCount = 0;
    // Ensure we have at least 3 samples after rejection
    int effectiveSamples = tempAvg.tempSampleCount - (2 * tempAvg.tempRejectCount);
    if (effectiveSamples < 3) {
      tempAvg.tempRejectCount = (tempAvg.tempSampleCount - 3) / 2;
    }
    // Reset sampling system when parameters change
    tempAvg.tempSamplesReady = false;
    tempAvg.tempSampleIndex = 0;
    Serial.printf("[loadSettings] Temperature averaging loaded: Samples=%d, Reject=%d, Interval=%lums\n", 
                 tempAvg.tempSampleCount, tempAvg.tempRejectCount, tempAvg.tempSampleInterval);
  }
  
  // Restore last selected program by ID if present, fallback to name for backward compatibility
  if (doc.containsKey("lastProgramId")) {
    int lastId = doc["lastProgramId"];
    if (lastId >= 0 && lastId < (int)programs.size()) {
      programState.activeProgramId = lastId;
      Serial.print("[loadSettings] lastProgramId loaded: ");
      Serial.println(lastId);
    }
  } else if (doc.containsKey("lastProgram")) {
    // Deprecated: fallback to name for backward compatibility
    String lastProg = doc["lastProgram"].as<String>();
    for (size_t i = 0; i < programs.size(); ++i) {
      if (programs[i].name == lastProg) {
        programState.activeProgramId = i;
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
// Saves breadmaker settings (PID, temperature, program selection, etc.) to LittleFS.
// Uses direct streaming to minimize memory usage under load.
void saveSettings() {
  if (debugSerial) Serial.println("[saveSettings] Saving settings...");
  
  File f = LittleFS.open(SETTINGS_FILE, "w");
  if (!f) {
    if (debugSerial) Serial.println("[saveSettings] Failed to open settings.json for writing!");
    return;
  }
  
  // Stream JSON directly to file - much more memory efficient than building large strings
  f.print("{\n");
  f.printf("  \"outputMode\":\"%s\",\n", (outputMode == OUTPUT_DIGITAL) ? "digital" : "analog");
  f.printf("  \"debugSerial\":%s,\n", debugSerial ? "true" : "false");
  
  if (programs.size() > 0) {
    f.printf("  \"lastProgramId\":%d,\n", (int)programState.activeProgramId);
    f.print("  \"lastProgram\":\"");
    f.print(programs[programState.activeProgramId].name);
    f.print("\",\n");
  } else {
    f.print("  \"lastProgramId\":0,\n");
    f.print("  \"lastProgram\":\"Unknown\",\n");
  }
  
  // PID parameters for backward compatibility
  f.printf("  \"pidKp\":%.6f,\n", pid.Kp);
  f.printf("  \"pidKi\":%.6f,\n", pid.Ki);
  f.printf("  \"pidKd\":%.6f,\n", pid.Kd);
  f.printf("  \"pidSampleTime\":%lu,\n", pid.sampleTime);
  f.printf("  \"pidWindowSize\":%lu,\n", windowSize);
  
  // PID profile settings
  f.print("  \"activeProfile\":\"");
  f.print(pid.activeProfile);
  f.print("\",\n");
  f.printf("  \"autoSwitching\":%s,\n", pid.autoSwitching ? "true" : "false");
  
  // Temperature averaging parameters
  f.printf("  \"tempSampleCount\":%d,\n", tempAvg.tempSampleCount);
  f.printf("  \"tempRejectCount\":%d,\n", tempAvg.tempRejectCount);
  f.printf("  \"tempSampleInterval\":%lu\n", tempAvg.tempSampleInterval);
  
  f.print("}\n");
  f.close();
  
  if (debugSerial) Serial.println("[saveSettings] Settings saved with direct streaming (low memory usage).");
}

// Load PID profiles from LittleFS
void loadPIDProfiles() {
  Serial.println("[loadPIDProfiles] Loading PID profiles...");
  
  // Clear existing profiles
  pid.profiles.clear();
  
  File f = LittleFS.open("/pid-profiles.json", "r");
  if (!f) {
    Serial.println("[loadPIDProfiles] pid-profiles.json not found, creating defaults.");
    createDefaultPIDProfiles();
    savePIDProfiles();
    return;
  }
  
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  
  if (err) {
    Serial.print("[loadPIDProfiles] Failed to parse pid-profiles.json: ");
    Serial.println(err.c_str());
    createDefaultPIDProfiles();
    return;
  }
  
  JsonArray profileArray = doc["pidProfiles"];
  if (profileArray.isNull()) {
    Serial.println("[loadPIDProfiles] No pidProfiles array found, creating defaults.");
    createDefaultPIDProfiles();
    return;
  }
  
  for (JsonObject profile : profileArray) {
    PIDProfile p;
    p.name = profile["name"] | "Unknown";
    p.minTemp = profile["minTemp"] | 0.0;
    p.maxTemp = profile["maxTemp"] | 100.0;
    p.kp = profile["kp"] | 0.11;
    p.ki = profile["ki"] | 0.00005;
    p.kd = profile["kd"] | 10.0;
    p.windowMs = profile["windowMs"] | 30000;
    p.description = profile["description"] | "";
    pid.profiles.push_back(p);
  }
  
  // Load control settings
  pid.activeProfile = doc["activeProfile"] | "Baking Heat";
  pid.autoSwitching = doc["autoSwitching"] | true;
  
  Serial.printf("[loadPIDProfiles] Loaded %d PID profiles, active: %s, auto-switching: %s\n", 
                pid.profiles.size(), pid.activeProfile.c_str(), pid.autoSwitching ? "ON" : "OFF");
}

// Save PID profiles to LittleFS
void savePIDProfiles() {
  Serial.println("[savePIDProfiles] Saving PID profiles...");
  
  File f = LittleFS.open("/pid-profiles.json", "w");
  if (!f) {
    Serial.println("[savePIDProfiles] Failed to open pid-profiles.json for writing!");
    return;
  }
  
  // Stream JSON directly to file - much more memory efficient than 2048-byte buffer
  f.print("{\n");
  f.print("  \"pidProfiles\":[\n");
  
  for (size_t i = 0; i < pid.profiles.size(); ++i) {
    const PIDProfile& profile = pid.profiles[i];
    if (i > 0) f.print(",\n");
    
    f.print("    {\n");
    f.print("      \"name\":\"");
    f.print(profile.name);
    f.print("\",\n");
    f.printf("      \"minTemp\":%.1f,\n", profile.minTemp);
    f.printf("      \"maxTemp\":%.1f,\n", profile.maxTemp);
    f.printf("      \"kp\":%.6f,\n", profile.kp);
    f.printf("      \"ki\":%.6f,\n", profile.ki);
    f.printf("      \"kd\":%.6f,\n", profile.kd);
    f.printf("      \"windowMs\":%lu,\n", profile.windowMs);
    f.print("      \"description\":\"");
    f.print(profile.description);
    f.print("\"\n");
    f.print("    }");
  }
  
  f.print("\n  ],\n");
  f.print("  \"activeProfile\":\"");
  f.print(pid.activeProfile);
  f.print("\",\n");
  f.printf("  \"autoSwitching\":%s\n", pid.autoSwitching ? "true" : "false");
  f.print("}\n");
  
  f.close();
  Serial.println("[savePIDProfiles] PID profiles saved with direct streaming (low memory usage).");
}

// Create default PID profiles
void createDefaultPIDProfiles() {
  Serial.println("[createDefaultPIDProfiles] Creating default PID profiles...");
  
  pid.profiles.clear();
  
  // Room Temperature - Very gentle control
  pid.profiles.push_back({
    "Room Temperature", 0, 35, 0.5, 0.00001, 2.0, 60000,
    "Very gentle control - minimal heat to prevent long thermal mass rises"
  });
  
  // Low Fermentation - Gentle warming
  pid.profiles.push_back({
    "Low Fermentation", 35, 50, 0.3, 0.00002, 3.0, 45000,
    "Gentle warming prevents thermal mass overshoot"
  });
  
  // Medium Fermentation - Balanced control
  pid.profiles.push_back({
    "Medium Fermentation", 50, 70, 0.2, 0.00005, 5.0, 30000,
    "Balanced control for typical fermentation temps"
  });
  
  // High Fermentation - More responsive
  pid.profiles.push_back({
    "High Fermentation", 70, 100, 0.15, 0.00008, 6.0, 25000,
    "More responsive for higher fermentation temps"
  });
  
  // Baking Heat - Your current settings
  pid.profiles.push_back({
    "Baking Heat", 100, 150, 0.11, 0.00005, 10.0, 15000,
    "Your current range - balanced for baking"
  });
  
  // High Baking - Higher derivative
  pid.profiles.push_back({
    "High Baking", 150, 200, 0.08, 0.00003, 10.0, 15000,
    "Higher derivative to prevent overshoot"
  });
  
  // Extreme Heat - Maximum control
  pid.profiles.push_back({
    "Extreme Heat", 200, 250, 0.015, 0.00015, 10.0, 10000,
    "Your exact tuned settings - maximum derivative control"
  });
  
  Serial.printf("[createDefaultPIDProfiles] Created %d default profiles.\n", pid.profiles.size());
}

// Find the appropriate PID profile for a given temperature
PIDProfile* findProfileForTemperature(float temperature) {
  for (PIDProfile& profile : pid.profiles) {
    if (temperature >= profile.minTemp && temperature < profile.maxTemp) {
      return &profile;
    }
  }
  // Return the last profile if temperature is higher than all ranges
  if (!pid.profiles.empty()) {
    return &pid.profiles.back();
  }
  return nullptr;
}

// Switch to a specific PID profile by name
void switchToProfile(const String& profileName) {
  for (const PIDProfile& profile : pid.profiles) {
    if (profile.name == profileName) {
      // Update active PID parameters
      pid.Kp = profile.kp;
      pid.Ki = profile.ki;
      pid.Kd = profile.kd;
      windowSize = profile.windowMs;
      pid.activeProfile = profileName;
      
      // Apply to PID controller
      if (pid.controller) {
        pid.controller->SetTunings(pid.Kp, pid.Ki, pid.Kd);
      }
      
      Serial.printf("[switchToProfile] Switched to '%s': Kp=%.6f, Ki=%.6f, Kd=%.6f, Window=%lums\n",
                    profileName.c_str(), pid.Kp, pid.Ki, pid.Kd, windowSize);
      return;
    }
  }
  Serial.printf("[switchToProfile] Profile '%s' not found!\n", profileName.c_str());
}

// Check current temperature and switch profile if needed
void checkAndSwitchPIDProfile() {
  // Only check every 5 seconds to avoid excessive switching
  if (millis() - pid.lastProfileCheck < 5000) return;
  pid.lastProfileCheck = millis();
  
  // Skip if auto-switching is disabled
  if (!pid.autoSwitching) return;
  
  // Skip if no valid temperature reading
  if (tempAvg.averagedTemperature <= 0) return;
  
  PIDProfile* targetProfile = findProfileForTemperature(tempAvg.averagedTemperature);
  if (targetProfile && targetProfile->name != pid.activeProfile) {
    Serial.printf("[checkAndSwitchPIDProfile] Temperature %.1f°C - switching from '%s' to '%s'\n",
                  tempAvg.averagedTemperature, pid.activeProfile.c_str(), targetProfile->name.c_str());
    switchToProfile(targetProfile->name);
    
    // Save the change to persist across reboots
    saveSettings();
  }
}
