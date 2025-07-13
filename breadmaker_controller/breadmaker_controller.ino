#include <functional>


// Maximum number of program stages supported (now in globals.cpp)
#include "globals.h"

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


// --- PID control struct instance ---
PIDControl pid;

// --- Program state struct instance (needed for web_endpoints.cpp extern reference) ---
ProgramState programState;

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
// STARTUP_DELAY_MS now defined in globals.cpp

// --- Temperature averaging system (encapsulated) ---
TemperatureAveragingState tempAvg;


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
// Recursively deletes a folder and all its contents from the LittleFS filesystem.
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
  
  // Wait for time sync
  while (time(nullptr) < 100000) delay(200);
}

const char* RESUME_FILE = "/resume.json";
unsigned long lastResumeSave = 0;

// Saves the current breadmaker state (program, stage, timing, etc.) to LittleFS for resume after reboot.
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

// Removes the saved resume state file from LittleFS.
void clearResumeState() {
  LittleFS.remove(RESUME_FILE);
}

// Loads the breadmaker's previous state from LittleFS and restores program, stage, and timing.
void loadResumeState() {
  File f = LittleFS.open(RESUME_FILE, "r");
  if (!f) return;
  DynamicJsonDocument doc(1024); // Increased size for stage start times array
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  // Restore program by id (id is now the index in the programs vector, but may be a dummy slot)
  int progIdx = doc["programIdx"] | -1;
  if (progIdx >= 0 && progIdx < (int)programs.size() && programs[progIdx].id == progIdx && programs[progIdx].id != -1) {
    activeProgramId = progIdx;
    updateActiveProgramVars(); // Update program variables after loading
  } else {
    // Fallback: select first valid program (id!=-1) or 0 if none
    activeProgramId = 0;
    for (size_t i = 0; i < programs.size(); ++i) {
      if (programs[i].id != -1) { activeProgramId = i; break; }
    }
    updateActiveProgramVars();
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

  // Ensure indices are always valid after resume
  if (activeProgramId < programs.size() && programs[activeProgramId].id != -1) {
    Program &p = programs[activeProgramId];
    if (customStageIdx >= p.customStages.size()) customStageIdx = 0;
    if (!p.customStages.empty()) {
      CustomStage &st = p.customStages[customStageIdx];
      if (!st.mixPattern.empty() && customMixIdx >= st.mixPattern.size()) customMixIdx = 0;
    } else {
      customMixIdx = 0;
    }
  } else {
    customStageIdx = 0;
    customMixIdx = 0;
  }

  // Fast-forward logic: if elapsed time > stage/mix durations, advance indices
  if (isRunning && activeProgramId < programs.size() && programs[activeProgramId].id != -1) {
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
      size_t mixIdx = customMixIdx;
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

  updateFermentationTiming(stageJustAdvanced);

  // --- Check for stage advancement and log ---
  if (stageJustAdvanced && programs.size() > 0 && activeProgramId < programs.size() && customStageIdx < programs[activeProgramId].customStages.size()) {
    Program &p = programs[activeProgramId];
    CustomStage &st = p.customStages[customStageIdx];
    if (st.isFermentation) {
      if (debugSerial) Serial.printf("[STAGE ADVANCE] Fermentation stage advanced to %d\n", (int)customStageIdx);
    } else {
      if (debugSerial) Serial.printf("[STAGE ADVANCE] Non-fermentation stage advanced to %d\n", (int)customStageIdx);
    }
  }

  if (!isRunning) {
    stageJustAdvanced = false;
    checkDelayedResume();
    handleManualMode();
    handleScheduledStart(scheduledStartTriggered);
    yield(); delay(1); return;
  }
  if (programs.size() == 0 || activeProgramId >= programs.size()) {
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
void updateFermentationTiming(bool &stageJustAdvanced) {
  if (programs.size() > 0 && activeProgramId < programs.size() && customStageIdx < programs[activeProgramId].customStages.size()) {
    Program &p = programs[activeProgramId];
    CustomStage &st = p.customStages[customStageIdx];
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
      } else if (isRunning) {
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
  if (manualMode && pid.Setpoint > 0) {
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
    isRunning = true;
    if (scheduledStartStage >= 0 && scheduledStartStage < maxCustomStages) {
      customStageIdx = scheduledStartStage;
      if (debugSerial) Serial.printf("[SCHEDULED] Starting at stage %d\n", scheduledStartStage);
    } else {
      customStageIdx = 0;
      if (debugSerial) Serial.printf("[SCHEDULED] Starting from beginning\n");
    }
    customMixIdx = 0;
    customStageStart = millis();
    customMixStepStart = 0;
    programStartTime = time(nullptr);
    for (int i = 0; i < 20; i++) actualStageStartTimes[i] = 0;
    actualStageStartTimes[customStageIdx] = programStartTime;
    saveResumeState();
    scheduledStart = 0;
    scheduledStartStage = -1;
  }
  if (!scheduledStart) scheduledStartTriggered = false;
}

// Handles the execution and advancement of custom program stages, including mixing and fermentation.
void handleCustomStages(bool &stageJustAdvanced) {
  Program &p = programs[activeProgramId];
  if (!p.customStages.empty()) {
    stageJustAdvanced = false;
    if (customStageIdx >= p.customStages.size()) {
      stageJustAdvanced = false;
      stopBreadmaker();
      customStageIdx = 0;
      customStageStart = 0;
      clearResumeState();
      yield();
      delay(1);
      return;
    }
    CustomStage &st = p.customStages[customStageIdx];
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
      unsigned long elapsedInCurrentStage = (customStageStart > 0) ? (nowMs - customStageStart) : 0UL;
      double cumulativePredictedSec = 0.0;
      for (size_t i = 0; i < p.customStages.size(); ++i) {
        CustomStage &stage = p.customStages[i];
        double plannedStageSec = (double)stage.min * 60.0;
        double adjustedStageSec = plannedStageSec;
        if (stage.isFermentation) {
          if (i < customStageIdx) {
            float baseline = p.fermentBaselineTemp > 0 ? p.fermentBaselineTemp : 20.0;
            float q10 = p.fermentQ10 > 0 ? p.fermentQ10 : 2.0;
            float temp = fermentState.fermentLastTemp;
            float factor = pow(q10, (baseline - temp) / 10.0); // Q10: factor < 1 means faster at higher temp
            cumulativePredictedSec += plannedStageSec * factor; // Multiply: higher temp = less time
          } else if (i == customStageIdx) {
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
      fermentState.predictedCompleteTime = (unsigned long)(programStartTime + (unsigned long)(cumulativePredictedSec)); // All predicted times use Q10 multiply logic
    }
    if (st.temp > 0 || manualMode) {
      if (manualMode && pid.Setpoint > 0) {
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
      } else if (!manualMode && st.temp > 0) {
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
    if (!manualMode) {
      setLight(false);
      setBuzzer(false);
    }
    if (!manualMode) {
      bool hasMix = false;
      if (st.noMix) {
        setMotor(false);
        hasMix = false;
      } else if (!st.mixPattern.empty()) {
        hasMix = true;
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
          if (elapsed >= stepDuration) {
            customMixIdx++;
            customMixStepStart = millis();
            if (customMixIdx >= st.mixPattern.size()) {
              customMixIdx = 0;
              if (debugSerial) Serial.printf("[MIX] All %d patterns complete, restarting from pattern 0\n", st.mixPattern.size());
            } else {
              if (debugSerial) Serial.printf("[MIX] Advancing to pattern %d\n", customMixIdx);
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
            customMixIdx++;
            customMixStepStart = millis();
            if (customMixIdx >= st.mixPattern.size()) {
              customMixIdx = 0;
              if (debugSerial) Serial.printf("[MIX] All %d patterns complete, restarting from pattern 0\n", st.mixPattern.size());
            } else {
              if (debugSerial) Serial.printf("[MIX] Advancing to pattern %d\n", customMixIdx);
            }
          }
        }
      } else {
        setMotor(true);
        hasMix = false;
      }
    }
    bool stageComplete = false;
    if (st.isFermentation) {
      double fermentTargetSec = (double)st.min * 60.0;
      double epsilon = 0.05;
      if (fermentState.fermentWeightedSec + epsilon >= fermentTargetSec) {
        stageComplete = true;
        if (debugSerial) {
          Serial.printf("[FERMENT] Advancing: fermentWeightedSec=%.3f, target=%.3f (min=%.2f)\n", fermentState.fermentWeightedSec, fermentTargetSec, st.min);
        }
      } else if (debugSerial && (fermentTargetSec - fermentState.fermentWeightedSec) < 2.0) {
        Serial.printf("[FERMENT] Not yet advancing: fermentWeightedSec=%.3f, target=%.3f (min=%.2f)\n", fermentState.fermentWeightedSec, fermentTargetSec, st.min);
      }
    } else {
      unsigned long elapsedMs = millis() - customStageStart;
      unsigned long stageMs = (unsigned long)st.min * 60000UL;
      if (elapsedMs >= stageMs) {
        stageComplete = true;
      } else if (stageMs > 0 && elapsedMs >= stageMs - 1000 && !stageComplete) {
        // Safety: If time left is 0 but not advancing, log warning
        if (debugSerial) Serial.printf("[WARN] Time left is 0 for non-fermentation stage %d but not advancing (elapsed=%lu, stageMs=%lu)\n", (int)customStageIdx, elapsedMs, stageMs);
      }
    }
    // Extra safety: If time left is 0 for non-fermentation and not advancing, force advancement
    if (!st.isFermentation) {
      unsigned long elapsedMs = millis() - customStageStart;
      unsigned long stageMs = (unsigned long)st.min * 60000UL;
      if (stageMs > 0 && elapsedMs > stageMs + 2000 && !stageComplete) {
        if (debugSerial) Serial.printf("[FORCE ADVANCE] Forcing advancement of non-fermentation stage %d after time expired (elapsed=%lu, stageMs=%lu)\n", (int)customStageIdx, elapsedMs, stageMs);
        stageComplete = true;
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
      customStageIdx++;
      customStageStart = millis();
      customMixIdx = 0;
      customMixStepStart = 0;
      saveResumeState();
      stageJustAdvanced = true;
      if (debugSerial) Serial.printf("[ADVANCE] Stage advanced to %d\n", customStageIdx);
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
  if (programs.size() > 0 && activeProgramId < programs.size()) {
    customProgram = &programs[activeProgramId];
    maxCustomStages = customProgram->customStages.size();
  } else {
    customProgram = nullptr;
    maxCustomStages = 0;
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
  out.printf("\"manual_mode\":%s,", manualMode ? "true" : "false");
  out.printf("\"is_running\":%s,", isRunning ? "true" : "false");
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
  out.printf("\"running_override\":%s,", !isRunning ? "true" : "false");
  out.printf("\"manual_override\":%s,", manualMode ? "true" : "false");
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
  isRunning = false;
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
  
  // Load PID parameters
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
    Serial.printf("[loadSettings] PID parameters loaded: Kp=%.6f, Ki=%.6f, Kd=%.6f\n", pid.Kp, pid.Ki, pid.Kd);
    Serial.printf("[loadSettings] Timing parameters loaded: SampleTime=%lums, WindowSize=%lums\n", pid.sampleTime, windowSize);
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
// Saves breadmaker settings (PID, temperature, program selection, etc.) to LittleFS.
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
  doc["pidKp"] = pid.Kp;
  doc["pidKi"] = pid.Ki;
  doc["pidKd"] = pid.Kd;
  doc["pidSampleTime"] = pid.sampleTime;
  doc["pidWindowSize"] = windowSize;
  // Save temperature averaging parameters
  doc["tempSampleCount"] = tempAvg.tempSampleCount;
  doc["tempRejectCount"] = tempAvg.tempRejectCount;
  doc["tempSampleInterval"] = tempAvg.tempSampleInterval;
  File f = LittleFS.open(SETTINGS_FILE, "w");
  if (!f) {
    Serial.println("[saveSettings] Failed to open settings.json for writing!");
    return;
  }
  serializeJson(doc, f);
  f.close();
  Serial.println("[saveSettings] Settings saved with PID and temperature averaging parameters.");
}
