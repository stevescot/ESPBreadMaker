/*
ESP32 TTGO T-Display Breadmaker Controller
==========================================

FEATURES:
 - Replaces breadmaker logic board: controls motor, heater, light
 - Manual icon toggles (motor, heater, light, buzzer)
 - FFat for persistent storage (programs, UI)
 - Status shows stage, time left (d:h:m:s), state of outputs, temperature (RTD analog input)
 - Built-in TFT display for local status and control
 - Enhanced memory capacity for larger program collections

*/

#include <Arduino.h>
#include <functional>       // For std::function
#include <WiFi.h>          // ESP32 WiFi library
#include <ESPmDNS.h>       // ESP32 mDNS library
#include <ArduinoOTA.h>    // ESP32 OTA updates
#include <FFat.h>      // ESP32 FATFS support for 16MB
#include <WebServer.h>     // Standard ESP32 WebServer (stable alternative to AsyncWebServer)
#include <ArduinoJson.h>
#include <PID_v1.h>
#include <map>
#include <vector>
#include <time.h>
#include <EEPROM.h>

#include "globals.h"       // Must be included for global definitions
#include "display_manager.h"  // LovyanGFX display management
#include "outputs_manager.h"  // Output control functions
#include "missing_stubs.h"    // Missing function implementations
// #include "capacitive_buttons.h" // REMOVED: Capacitive touch buttons (GPIO conflicts)
#include "calibration.h"
#include "programs_manager.h"
#include "wifi_manager.h"
#include "outputs_manager.h"
#include "web_endpoints.h"
#include "ota_manager.h"   // OTA update support

// --- Firmware build date ---
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// --- Web server---
WebServer server(80); // Standard ESP32 WebServer (stable, no crashes)

// --- Temperature safety flags ---
bool thermalRunawayDetected = false;
bool sensorFaultDetected = false;

// --- Pin assignments ---
// PIN_RTD now defined in globals.h (GPIO34 for ESP32)
// Output pins are defined in outputs_manager.cpp
// Display pins are handled by LovyanGFX library

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

// --- Fermentation rate limiting ---
unsigned long lastFermentUpdate = 0;  // Rate limiting for fermentation updates

/// --- WiFi Configuration ---
// WiFi credentials and connectivity management
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
double getAveragedTemperature();
void streamStatusJson(Print& out);
void updateActiveProgramVars();
void updateTimeProportionalHeater();
void createDefaultPIDProfiles();
bool ensureProgramLoaded(int programId);
// setupOTA removed - OTA is handled by ota_manager.cpp

// --- Forward declaration for cache invalidation ---
extern void invalidateStatusCache();

// --- Forward declaration for stopBreadmaker (must be before any use) ---
void stopBreadmaker();

// --- Forward declaration for startup delay check ---
bool isStartupDelayComplete();

// --- Delete Folder API endpoint ---
// Recursively deletes a folder and all its contents from the FFat filesystem.
void deleteFolderRecursive(const String& path) {
  if (!FFat.exists(path)) {
    if (debugSerial) Serial.printf("[deleteFolderRecursive] WARNING: Path '%s' does not exist.\n", path.c_str());
    return;
  }
  File root = FFat.open(path);
  if (root && root.isDirectory()) {
    File file = root.openNextFile();
    while (file) {
      // Use pre-allocated buffer to avoid String concatenation
      char filePath[128];
      snprintf(filePath, sizeof(filePath), "%s/%s", path.c_str(), file.name());
      
      if (file.isDirectory()) {
        deleteFolderRecursive(String(filePath));
      } else {
        if (!FFat.remove(filePath) && debugSerial) {
          Serial.printf("[deleteFolderRecursive] ERROR: Failed to remove file '%s'\n", filePath);
        }
      }
      file = root.openNextFile();
    }
  }
  if (!FFat.rmdir(path) && debugSerial) {
    Serial.printf("[deleteFolderRecursive] ERROR: Failed to remove directory '%s'\n", path.c_str());
  }
}

// Helper function to send a JSON error response (WebServer compatible)
// Sends a JSON-formatted error message to the client with a specified HTTP status code.
// Optimized to use streaming to avoid String concatenation and reduce memory usage.
void sendJsonError(WebServer& server, const String& error, const String& message, int code = 400) {
  // Calculate approximate response size to minimize reallocations
  size_t responseSize = 50 + error.length() + message.length();
  String response;
  response.reserve(responseSize);
  
  // Build JSON response with minimal String operations
  response = F("{\"error\":\"");
  response += error;
  response += F("\",\"message\":\"");
  response += message;
  response += F("\"}");
  
  server.send(code, F("application/json"), response);
}

// Initializes all hardware, outputs, file systems, and loads configuration and calibration data.
// Also sets up WiFi, mDNS, and captive portal as needed.
void initialState(){
  startupTime = millis(); // Record startup time for sensor stabilization delay
  Serial.begin(115200);
  delay(100);
  Serial.println(F("[setup] Booting..."));
  
  // --- CRITICAL: Force all output GPIOs to LOW state immediately ---
  // This must happen BEFORE any other initialization to prevent brief power-on heating
  digitalWrite(32, LOW);  // PIN_HEATER
  digitalWrite(33, LOW);  // PIN_MOTOR  
  digitalWrite(25, LOW);  // PIN_LIGHT
  digitalWrite(26, LOW);  // PIN_BUZZER
  Serial.println(F("[setup] Emergency GPIO LOW state set"));
  
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
  // COMMENTED OUT: Needs to be ported from AsyncWebServer to standard WebServer
  /*
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
    if (!FFat.exists(path)) {
      req->send(404, "text/plain", "File not found");
      return;
    }
    String contentType = "text/plain";
    if (filename.endsWith(".yaml")) contentType = "text/yaml";
    else if (filename.endsWith(".json")) contentType = "application/json";
    else if (filename.endsWith(".html")) contentType = "text/html";
    req->send(FFat, path, contentType);
  });
  */
  
  // Initialize FFat with better error handling
  Serial.println(F("[setup] Initializing FFat filesystem..."));
  if (!FFat.begin(false)) {
    Serial.println(F("[setup] FFat mount failed, attempting format..."));
    if (!FFat.begin(true)) {
      Serial.println(F("[setup] FFat format failed! Continuing without filesystem."));
      // Don't halt - continue without FFat for debugging
    } else {
      Serial.println(F("[setup] FFat formatted and mounted successfully."));
    }
  } else {
    Serial.println(F("[setup] FFat mounted successfully."));
  }
  Serial.println(F("[setup] FFat mounted."));
  Serial.println(F("[setup] Loading programs..."));
  loadProgramMetadata();
  Serial.print(F("[setup] Programs loaded. Count: "));
  Serial.println(getProgramCount());
  updateActiveProgramVars(); // Initialize program variables after loading
  loadCalibration();
  Serial.println(F("[setup] Calibration loaded."));
  
  // Take some initial temperature samples before fermentation tracking
  Serial.println(F("[setup] Taking initial temperature samples..."));
  for (int i = 0; i < 5; i++) {
    updateTemperatureSampling();
    delay(100); // Small delay between samples
  }
  
  // Initialize fermentation tracking with actual temperature
  double initialTemp = getAveragedTemperature();
  resetFermentationTracking(initialTemp);
  Serial.printf("[setup] Fermentation tracking initialized: temp=%.1f°C, factor=%.3fx\n", 
                initialTemp, fermentState.fermentationFactor);
  
  loadSettings();
  Serial.println(F("[setup] Settings loaded."));
  loadResumeState();
  Serial.println(F("[setup] Resume state loaded."));
}

// Initialize WiFi connection after core systems are ready
void initializeWiFi() {
  Serial.println(F("[wifi] Starting WiFi initialization..."));
  
  // Add significant delay for ESP32 WiFi subsystem stability
  delay(2000);
  
  // Check for any issues before WiFi init
  Serial.println(F("[wifi] Pre-WiFi system check..."));
  Serial.printf("[wifi] Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // Very simple WiFi initialization to avoid TCP stack issues
  try {
    Serial.println(F("[wifi] Setting WiFi mode to OFF..."));
    WiFi.mode(WIFI_OFF);
    delay(1000);
    
    // Check if wifi.json exists
    Serial.println(F("[wifi] Checking for WiFi configuration..."));
    File wf = FFat.open("/wifi.json", "r");
    if (!wf) {
      Serial.println(F("[wifi] No WiFi config found. Starting WiFiManager captive portal."));
      Serial.println(F("[wifi] Note: Web server temporarily disabled to save memory during WiFi setup"));
      
      // Start reliable WiFiManager captive portal (uses tzapu library)
      startWiFiManagerPortal();
      Serial.println(F("[wifi] WiFiManager completed - device should now be connected"));
      
      // After WiFi setup, we could restart to enable full web server
      Serial.println(F("[wifi] Restarting to enable full web interface..."));
      delay(2000);
      ESP.restart();
      return;
    } else {
      // Try to connect to saved network
      DynamicJsonDocument wdoc(256);
      DeserializationError err = deserializeJson(wdoc, wf);
      wf.close();
      
      if (!err) {
        const char* ssid = wdoc["ssid"] | "";
        const char* pass = wdoc["pass"] | "";
        
        if (strlen(ssid) > 0) {
          Serial.printf("[wifi] Connecting to: %s\n", ssid);
          
          WiFi.mode(WIFI_STA);
          delay(300);
          WiFi.begin(ssid, pass);
          
          // Simple connection attempt with timeout
          int attempts = 0;
          while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
          }
          Serial.println();
          
          if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[wifi] Connected! IP: %s\n", wifiCache.getIPString().c_str());
            
            // Initialize OTA only after successful connection
            delay(500);
            otaManagerInit();
            
            Serial.println(F("[wifi] WiFi connected successfully - WebServer already started"));
          } else {
            Serial.println(F("[wifi] Connection failed, starting AP mode"));
            WiFi.mode(WIFI_AP);
            delay(300);
            WiFi.softAP("BreadmakerSetup", "breadmaker123");
            Serial.printf("[wifi] AP mode IP: %s\n", WiFi.softAPIP().toString().c_str());
          }
        }
      }
    }
    
    // Start mDNS after WiFi is stable
    delay(500);
    if (MDNS.begin("breadmaker")) {
      Serial.println(F("[mDNS] mDNS started: http://breadmaker.local/"));
    }
    
  } catch (...) {
    Serial.println(F("[wifi] WiFi initialization failed, continuing without network"));
  }
  
  Serial.println(F("[wifi] WiFi initialization complete"));
}


// Arduino setup function. Initializes system state, registers web endpoints, starts the web server,
// configures PID and temperature averaging, and waits for NTP time sync.
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("=== ESP32 TTGO T-Display Breadmaker Controller ==="));
  Serial.println(F("[setup] Starting setup() function..."));
  Serial.printf("[setup] Free heap at start: %d bytes\n", ESP.getFreeHeap());
  
  initialState();
  
  // Initialize TFT display
  Serial.println(F("[setup] Initializing display..."));
  try {
    displayManagerInit();
    Serial.println(F("[setup] Display initialized successfully"));
  } catch (...) {
    Serial.println(F("[setup] Display initialization failed, continuing..."));
  }
  
  // Initialize capacitive touch buttons
  // REMOVED: capacitiveButtonsInit(); // Capacitive touch buttons disabled due to GPIO boot conflicts
  
  // Give the system time to stabilize after hardware init
  delay(1000);
  Serial.println(F("[setup] Hardware initialization complete, starting services..."));
  
  Serial.println(F("[setup] Starting WiFi initialization first..."));
  initializeWiFi();
  
  // Initialize WebServer AFTER WiFi is stable to prevent LWIP crashes
  Serial.println(F("[setup] WiFi initialization complete, now starting WebServer..."));
  registerWebEndpoints(server);  // Now compatible with standard WebServer
  // server.serveStatic("/", FFat, "/");  // Static files handled by onNotFound in registerWebEndpoints
  // Note: server.begin() is now called inside registerWebEndpoints()
  Serial.println(F("[setup] Standard WebServer started successfully!"));
  
  delay(1000);  // Give WiFi time to stabilize
  
  Serial.println(F("[setup] Starting OTA services..."));
  // OTA initialization handled by otaManagerInit() in WiFi setup

  // Set timezone to Central European Time (CET/CEST)
  // This handles both standard time (UTC+1) and daylight saving time (UTC+2)
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  // --- Initialize PID controller ---
  // Load PID profiles first
  loadPIDProfiles();
  
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
  
  // --- Initialize temperature EMA system ---
  tempAvg.smoothedTemperature = readTemperature(); // Initialize with first reading
  tempAvg.initialized = false; // Will become valid after first few readings
  tempAvg.lastUpdate = millis();
  tempAvg.sampleCount = 0;
  
  Serial.printf("[TEMP-EMA] Initialized with alpha=%.3f, spike threshold=%.1f°C\n", 
                tempAvg.alpha, tempAvg.spikeThreshold);
  Serial.println(F("EMA temperature averaging system initialized (memory optimized)"));
  
  // --- Initialize safety monitoring system ---
  safetySystem.init();
  Serial.println(F("Safety monitoring system initialized with oven limits (235°C max)"));
  
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

// Saves the current breadmaker state (program, stage, timing, etc.) to FFat for resume after reboot.
// Optimized for minimal memory usage and reduced performance impact during critical operations.
void saveResumeState() {
  // Throttle saves: minimum 2 seconds between saves to reduce wear and memory pressure
  unsigned long now = millis();
  if (lastResumeSave > 0 && (now - lastResumeSave) < 2000) {
    return; // Skip this save to reduce load
  }
  
  // FIXED: Don't skip saves during temperature control - this is critical for resume functionality!
  // The original code was preventing saves during active programs, breaking resume after OTA updates.
  // Instead, we'll save regularly but with throttling to reduce filesystem pressure.
  
  lastResumeSave = now;
  
  // Open file with minimal error handling to reduce code size
  File f = FFat.open(RESUME_FILE, "w");
  if (!f) {
    if (debugSerial) Serial.println(F("[saveResumeState] ERROR: Failed to open resume file"));
    return;
  }
  
  // Stream directly to file - no intermediate buffers
  serializeResumeStateJson(f);
  f.close();
  
  // Optional: Track save performance for debugging
  if (debugSerial && ((now - lastResumeSave) > 100)) {
    Serial.printf("[saveResumeState] Save took %lums\n", now - lastResumeSave);
  }
}

// Helper to serialize resume state as JSON using memory-efficient streaming
// Optimized to minimize memory allocation and reduce processing overhead
void serializeResumeStateJson(Print& f) {
  // Calculate elapsed times once to avoid multiple calculations during streaming
  unsigned long nowMs = millis();
  unsigned long stageSec = (programState.customStageStart > 0) ? (nowMs - programState.customStageStart) / 1000UL : 0UL;
  unsigned long mixSec = (programState.customMixStepStart > 0) ? (nowMs - programState.customMixStepStart) / 1000UL : 0UL;
  
  // Stream JSON directly to file - completely avoids string buffers and heap allocation
  // Use print() for static strings and printf() only when necessary for formatting
  f.print(F("{\"programIdx\":"));
  f.print((unsigned)programState.activeProgramId);
  f.print(F(",\"customStageIdx\":"));
  f.print((unsigned)programState.customStageIdx);
  f.print(F(",\"customMixIdx\":"));
  f.print((unsigned)programState.customMixIdx);
  f.print(F(",\"elapsedStageSec\":"));
  f.print(stageSec);
  f.print(F(",\"elapsedMixSec\":"));
  f.print(mixSec);
  f.print(F(",\"programStartTime\":"));
  f.print((unsigned long)programState.programStartTime);
  f.print(F(",\"customStageStart\":"));
  f.print((unsigned long)programState.customStageStart);
  f.print(F(",\"isRunning\":"));
  f.print(programState.isRunning ? F("true") : F("false"));
  f.print(F(",\"actualStageStartTimes\":["));
  
  // Stream array elements directly without intermediate formatting
  for (int i = 0; i < 20; i++) {
    if (i > 0) f.print(',');
    f.print((unsigned long)programState.actualStageStartTimes[i]);
  }
  f.print(F("],\"actualStageEndTimes\":["));
  
  // Add actual stage end times for complete stage duration tracking
  for (int i = 0; i < 20; i++) {
    if (i > 0) f.print(',');
    f.print((unsigned long)programState.actualStageEndTimes[i]);
  }
  f.print(F("],\"fermentationFactor\":"));
  f.print(fermentState.fermentationFactor, 3);
  f.print(F(",\"scheduledElapsedSeconds\":"));
  f.print(fermentState.scheduledElapsedSeconds, 1);
  f.print(F(",\"realElapsedSeconds\":"));
  f.print(fermentState.realElapsedSeconds, 1);
  f.print(F(",\"accumulatedFermentMinutes\":"));
  f.print(fermentState.accumulatedFermentMinutes, 2);
  f.print(F(",\"predictedCompleteTime\":"));
  f.print((unsigned long)fermentState.predictedCompleteTime);
  f.print(F(",\"lastFermentAdjust\":"));
  f.print((unsigned long)fermentState.lastFermentAdjust);
  f.print(F("}\n"));
}

// Removes the saved resume state file from FFat.
void clearResumeState() {
  FFat.remove(RESUME_FILE);
}

// Loads the breadmaker's previous state from FFat and restores program, stage, and timing.
// Optimized for memory efficiency and better error handling.
void loadResumeState() {
  File f = FFat.open(RESUME_FILE, "r");
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
  if (progIdx >= 0 && progIdx < (int)getProgramCount() && 
      isProgramValid(progIdx)) {
    programState.activeProgramId = progIdx;
    // Ensure the program is loaded into memory
    ensureProgramLoaded(progIdx);
    updateActiveProgramVars();
  } else {
    // Fallback: select first valid program (id!=-1) or 0 if none
    programState.activeProgramId = 0;
    for (size_t i = 0; i < getProgramCount(); ++i) {
      if (isProgramValid(i)) { 
        programState.activeProgramId = i; 
        break; 
      }
    }
    // Ensure the fallback program is loaded into memory
    ensureProgramLoaded(programState.activeProgramId);
    updateActiveProgramVars();
  }
  
  // Load basic state with bounds checking
  programState.customStageIdx = constrain(doc["customStageIdx"] | 0, 0, 19);
  programState.customMixIdx = constrain(doc["customMixIdx"] | 0, 0, 9);
  unsigned long elapsedStageSec = doc["elapsedStageSec"] | 0;
  unsigned long elapsedMixSec = doc["elapsedMixSec"] | 0;
  programState.programStartTime = doc["programStartTime"] | 0;
  programState.customStageStart = doc["customStageStart"] | 0;
  bool wasRunning = doc["isRunning"] | false;

  // Load fermentation state with fallback defaults
  fermentState.fermentationFactor = doc["fermentationFactor"] | 1.0f;
  fermentState.scheduledElapsedSeconds = doc["scheduledElapsedSeconds"] | 0.0;
  fermentState.realElapsedSeconds = doc["realElapsedSeconds"] | 0.0;
  fermentState.accumulatedFermentMinutes = doc["accumulatedFermentMinutes"] | 0.0;
  fermentState.predictedCompleteTime = doc["predictedCompleteTime"] | 0UL;
  fermentState.lastFermentAdjust = doc["lastFermentAdjust"] | 0UL;
  
  if (debugSerial && fermentState.fermentationFactor != 1.0f) {
    Serial.printf("[RESUME] Restored fermentation state: factor=%.3f, scheduled=%.1fs, real=%.1fs, accum=%.2fmin\n",
                  fermentState.fermentationFactor, fermentState.scheduledElapsedSeconds, 
                  fermentState.realElapsedSeconds, fermentState.accumulatedFermentMinutes);
  }

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

  // Load actual stage end times
  for (int i = 0; i < 20; i++) programState.actualStageEndTimes[i] = 0;
  if (doc.containsKey("actualStageEndTimes") && doc["actualStageEndTimes"].is<JsonArray>()) {
    JsonArray stageEndArray = doc["actualStageEndTimes"];
    for (int i = 0; i < min(20, (int)stageEndArray.size()); i++) {
      programState.actualStageEndTimes[i] = stageEndArray[i];
    }
  }

  // Ensure indices are always valid after resume
  if (programState.activeProgramId >= 0 && isProgramValid(programState.activeProgramId)) {
    if (ensureProgramLoaded(programState.activeProgramId)) {
      Program* p = getActiveProgramMutable();
      if (p != nullptr) {
        if (programState.customStageIdx >= p->customStages.size()) programState.customStageIdx = 0;
        if (!p->customStages.empty()) {
          CustomStage &st = p->customStages[programState.customStageIdx];
          if (!st.mixPattern.empty() && programState.customMixIdx >= st.mixPattern.size()) programState.customMixIdx = 0;
        } else {
          programState.customMixIdx = 0;
        }
      }
    }
  } else {
    programState.customStageIdx = 0;
    programState.customMixIdx = 0;
  }

  // Fast-forward logic: if elapsed time > stage/mix durations, advance indices
  if (programState.isRunning && programState.activeProgramId >= 0 && isProgramValid(programState.activeProgramId)) {
    if (ensureProgramLoaded(programState.activeProgramId)) {
      Program* p = getActiveProgramMutable();
      if (p != nullptr) {
        // Fast-forward stages
        size_t stageIdx = programState.customStageIdx;
        unsigned long remainStageSec = elapsedStageSec;
        while (stageIdx < p->customStages.size()) {
          unsigned long stageDurSec = p->customStages[stageIdx].min * 60UL;
          if (remainStageSec < stageDurSec) break;
          remainStageSec -= stageDurSec;
          stageIdx++;
        }
        if (stageIdx >= p->customStages.size()) {
          // Program finished
          programState.customStageIdx = 0;
          programState.customMixIdx = 0;
          programState.customStageStart = 0;
          programState.customMixStepStart = 0;
          programState.isRunning = false;
          invalidateStatusCache();
          clearResumeState();
          return;
        }
        programState.customStageIdx = stageIdx;
        
        // Restore stage start time if we have it, otherwise calculate it
        if (programState.customStageStart > 0) {
          // Use saved stage start time
          if (debugSerial) {
            Serial.printf("[RESUME] Restored stage start time: %lu\n", programState.customStageStart);
          }
        } else {
          // Calculate stage start time from elapsed time (fallback)
          programState.customStageStart = millis() - remainStageSec * 1000UL;
          if (programState.customStageStart == 0) programState.customStageStart = millis(); // Ensure nonzero for UI
          if (debugSerial) {
            Serial.printf("[RESUME] Calculated stage start time: %lu (remain: %lus)\n", 
                         programState.customStageStart, remainStageSec);
          }
        }
        // Fast-forward mix steps if mixPattern exists
        CustomStage &st = p->customStages[programState.customStageIdx];
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
      }
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
  // Add rate limiting to prevent excessive resets
  static unsigned long lastResetTime = 0;
  unsigned long now = millis();
  if (now - lastResetTime < 1000) {
    return; // Skip reset if less than 1 second since last reset
  }
  lastResetTime = now;
  
  if (debugSerial) Serial.printf("[FERMENT] Tracking reset: temp=%.1f, previous scheduled=%.1fs, clearing all timers\n", temp, fermentState.scheduledElapsedSeconds);
  
  fermentState.initialFermentTemp = temp;
  fermentState.fermentationFactor = calculateFermentationFactor(temp); // Calculate proper factor
  fermentState.predictedCompleteTime = 0;
  fermentState.lastFermentAdjust = 0;
  fermentState.fermentLastTemp = temp;
  fermentState.fermentLastFactor = fermentState.fermentationFactor;
  fermentState.fermentLastUpdateMs = now;
  // Initialize new time tracking system
  fermentState.scheduledElapsedSeconds = 0.0;
  fermentState.realElapsedSeconds = 0.0;
  fermentState.accumulatedFermentMinutes = 0.0;
  
  // Reset rate limiting to allow immediate update for new stage
  lastFermentUpdate = 0;
  
  if (debugSerial) Serial.printf("[FERMENT] Tracking reset complete: factor=%.3fx\n", fermentState.fermentationFactor);
}

// Helper to reset fermentation rate limiting
void resetFermentationRateLimit() {
  // This function allows external code to reset the rate limiting
  // when needed (e.g., when starting a new fermentation stage)
  extern unsigned long lastFermentUpdate;
  // We'll use a different approach - make the rate limiting variable global
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

// Low-impact safety monitoring with emergency shutdown capabilities
void performSafetyChecks() {
  unsigned long now = millis();
  
  // Only check every second to minimize loop impact
  if (now - safetySystem.lastSafetyCheck < SafetySystem::SAFETY_CHECK_INTERVAL) {
    return;
  }
  safetySystem.lastSafetyCheck = now;
  
  // If safety system is disabled, skip all checks (for testing without heater)
  if (!safetySystem.safetyEnabled) {
    if (debugSerial && (now - safetySystem.lastSafetyCheck) > 30000) { // Log once every 30 seconds
      Serial.println(F("[SAFETY] Safety system DISABLED - no temperature or heating checks"));
    }
    return;
  }
  
  // If already in emergency shutdown, don't do further checks
  if (safetySystem.emergencyShutdown) {
    return;
  }
  
  double currentTemp = getAveragedTemperature();
  
  // --- Critical Temperature Checks ---
  
  // Emergency temperature shutdown (immediate)
  if (safetySystem.isEmergencyShutdownNeeded(currentTemp)) {
    triggerEmergencyShutdown(F("Emergency: Temperature exceeds 240°C"));
    return;
  }
  
  // Temperature sensor validation
  if (!safetySystem.isTemperatureValid(currentTemp)) {
    safetySystem.invalidTempCount++;
    if (currentTemp == 0.0) {
      safetySystem.zeroTempCount++;
    }
    
    // Multiple consecutive invalid readings trigger shutdown
    if (safetySystem.invalidTempCount >= SafetySystem::MAX_INVALID_TEMP || 
        safetySystem.zeroTempCount >= SafetySystem::MAX_ZERO_TEMP) {
      triggerEmergencyShutdown(F("Shutdown: Temperature sensor fault detected"));
      return;
    }
  } else {
    // Valid temperature - reset counters and update tracking
    safetySystem.invalidTempCount = 0;
    safetySystem.zeroTempCount = 0;
    safetySystem.lastValidTempTime = now;
    safetySystem.lastValidTemperature = currentTemp;
    safetySystem.temperatureValid = true;
  }
  
  // Temperature timeout check (no valid readings for too long)
  if (now - safetySystem.lastValidTempTime > SafetySystem::TEMP_TIMEOUT_MS) {
    triggerEmergencyShutdown(F("Shutdown: Temperature sensor timeout"));
    return;
  }
  
  // Safe operating temperature check
  if (!safetySystem.isTemperatureSafe(currentTemp)) {
    triggerEmergencyShutdown(F("Shutdown: Temperature exceeds 235°C limit"));
    return;
  }
  
  // --- Heating Effectiveness Monitoring ---
  
  // Check if heater is running
  bool heaterActive = outputStates.heater || (pid.Output > 0.5); // 50% threshold
  
  if (heaterActive) {
    if (safetySystem.heatingStartTime == 0) {
      // Start tracking heating effectiveness
      safetySystem.heatingStartTime = now;
      safetySystem.heatingStartTemp = currentTemp;
      safetySystem.lastTempRiseTime = now;
    } else {
      // Check if heating is effective
      unsigned long heatingDuration = now - safetySystem.heatingStartTime;
      float tempRise = currentTemp - safetySystem.heatingStartTemp;
      unsigned long sinceLastRise = now - safetySystem.lastTempRiseTime;
      
      // Update last rise time if temperature increased
      if (tempRise >= SafetySystem::MIN_TEMP_RISE) {
        safetySystem.lastTempRiseTime = now;
        safetySystem.heatingEffective = true;
      }
      
      // Check for heating timeout (heater on too long without effect)
      if (heatingDuration > SafetySystem::MAX_HEATING_TIME && tempRise < SafetySystem::MIN_TEMP_RISE) {
        triggerEmergencyShutdown(F("Shutdown: Heater ineffective (5min no rise)"));
        return;
      }
      
      // Check for no temperature rise in 30 seconds during heating
      if (sinceLastRise > SafetySystem::HEATING_CHECK_INTERVAL && tempRise < SafetySystem::MIN_TEMP_RISE) {
        safetySystem.heatingEffective = false;
        // Don't shutdown immediately - could be normal during some phases
      }
    }
  } else {
    // Reset heating tracking when heater is off
    safetySystem.heatingStartTime = 0;
    safetySystem.heatingEffective = true;
  }
  
  // --- PID Saturation Monitoring ---
  
  // Check if PID is saturated (normal during heat-up)
  if (pid.Output >= 0.95) { // 95% considered saturated
    if (safetySystem.pidSaturationStart == 0) {
      safetySystem.pidSaturationStart = now;
    }
    safetySystem.pidSaturated = true;
    
    // Extended saturation could indicate problems
    if (now - safetySystem.pidSaturationStart > SafetySystem::MAX_PID_SATURATION) {
      // Don't shutdown for PID saturation alone - just log it
      if (debugSerial) {
        Serial.println(F("[SAFETY] PID saturated >10min - possible issue"));
      }
    }
  } else {
    safetySystem.pidSaturated = false;
    safetySystem.pidSaturationStart = 0;
  }
}

// Emergency shutdown function with display notification
void triggerEmergencyShutdown(const String& reason) {
  safetySystem.emergencyShutdown = true;
  safetySystem.shutdownReason = reason;
  safetySystem.shutdownTime = millis();
  
  // Immediately disable all outputs
  outputStates.heater = false;
  outputStates.motor = false;
  outputStates.buzzer = true; // Sound alarm
  
  // Stop any running program
  if (programState.isRunning) {
    stopBreadmaker();
  }
  
  // Reset PID to prevent any heating
  pid.Setpoint = 0;
  pid.Output = 0;
  
  // Log the emergency shutdown
  Serial.print(F("[EMERGENCY SHUTDOWN] "));
  Serial.println(reason);
  
  // Update display to show shutdown reason
  displayError(reason);
  
  // Force immediate output update
  setHeater(outputStates.heater);
  setMotor(outputStates.motor);
  setLight(outputStates.light);
  setBuzzer(outputStates.buzzer);
}

// Arduino main loop. Handles temperature sampling, fermentation, manual mode, scheduled start,
// and custom stage logic for breadmaker operation.
void loop() {
  // --- Handle deferred settings save ---
  if (pendingSettingsSaveTime > 0 && millis() >= pendingSettingsSaveTime) {
    pendingSettingsSaveTime = 0;
    if (debugSerial) Serial.println("[DEBUG] Executing deferred settings save...");
    
    // Use try/catch equivalent for ESP32
    bool saveSuccess = false;
    try {
      saveSettings();
      saveSuccess = true;
      if (debugSerial) Serial.println("[DEBUG] Deferred settings save completed successfully");
    } catch (...) {
      if (debugSerial) Serial.println("[ERROR] Settings save failed with exception");
    }
    
    if (!saveSuccess) {
      if (debugSerial) Serial.println("[ERROR] Settings save failed");
    }
  }
  
  // --- Safety monitoring (low-impact, every 1 second) ---
  safetySystem.loopStartTime = micros();
  performSafetyChecks();
  checkHeaterWatchdog(); // CRITICAL: Check heater safety watchdog every loop
  
  updatePerformanceMetrics(); // Track performance for Home Assistant endpoint
  updateTemperatureSampling();
  // REMOVED: updateFermentationFactor(); // Redundant - fermentation handled in updateFermentationTiming()
  updateBuzzerTone();
  updateDisplay(); // Update TFT display
  otaManagerLoop(); // Handle OTA updates via OTA manager
  server.handleClient(); // Handle web server requests - CRITICAL for web interface!
  // REMOVED: capacitiveButtonsUpdate(); // Capacitive touch buttons disabled due to GPIO boot conflicts
  checkSerialWifiConfig(); // Check for serial WiFi configuration commands
  
  // WiFiManager handles DNS internally, no need for manual processing
  
  double temp = getAveragedTemperature();
  static bool stageJustAdvanced = false;
  static bool scheduledStartTriggered = false;
  
  // Rate limiting for main loop - IMPROVED for heating responsiveness
  static unsigned long lastMainLoopUpdate = 0;
  unsigned long nowMs = millis();
  
  // Dynamic loop timing: faster for web responsiveness, moderate when heating
  unsigned long loopInterval = 20; // Default 20ms for good web responsiveness
  if (programState.isRunning && outputStates.heater) {
    loopInterval = 15; // 15ms when heater is on for excellent PID responsiveness  
  } else if (!programState.isRunning) {
    loopInterval = 50; // 50ms when idle (reduced from 100ms)
  }
  
  if (nowMs - lastMainLoopUpdate < loopInterval) {
    yield();
    // Removed delay(5) for better web responsiveness
    return;
  }
  lastMainLoopUpdate = nowMs;

  // Check and switch PID profile based on temperature
  checkAndSwitchPIDProfile();

  updateFermentationTiming(stageJustAdvanced);
  
  // --- Periodic resume state saving during program execution ---
  static unsigned long lastResumeStateSave = 0;
  if (programState.isRunning && (nowMs - lastResumeStateSave) >= 30000) { // Save every 30 seconds during execution
    lastResumeStateSave = nowMs;
    saveResumeState();
    if (debugSerial) Serial.println("[RESUME] Periodic state save during program execution");
  }

  // --- Check for stage advancement and log ---
  if (stageJustAdvanced && getProgramCount() > 0 && programState.activeProgramId < getProgramCount()) {
    Program *p = getActiveProgramMutable();
    if (p && programState.customStageIdx < p->customStages.size()) {
      CustomStage &st = p->customStages[programState.customStageIdx];
      if (st.isFermentation) {
        if (debugSerial) Serial.printf("[STAGE ADVANCE] Fermentation stage advanced to %d\n", (int)programState.customStageIdx);
      } else {
        if (debugSerial) Serial.printf("[STAGE ADVANCE] Non-fermentation stage advanced to %d\n", (int)programState.customStageIdx);
      }
    }
  }

  if (!programState.isRunning) {
    stageJustAdvanced = false;
    checkDelayedResume();
    handleManualMode();
    handleScheduledStart(scheduledStartTriggered);
    yield(); return;  // Removed delay(100) for better web responsiveness
  }
  if (getProgramCount() == 0 || programState.activeProgramId >= getProgramCount()) {
    stageJustAdvanced = false;
    stopBreadmaker(); yield(); return;  // Removed delay(100) for better web responsiveness
  }
  handleCustomStages(stageJustAdvanced);
  temp = getAveragedTemperature();
  
  // --- Track loop performance (low-impact) ---
  if (safetySystem.loopStartTime > 0) {
    unsigned long loopTime = micros() - safetySystem.loopStartTime;
    safetySystem.totalLoopTime += loopTime;
    safetySystem.loopCount++;
    if (loopTime > safetySystem.maxLoopTime) {
      safetySystem.maxLoopTime = loopTime;
    }
    
    // Warn about critical loop times (but don't shutdown)
    if (loopTime > SafetySystem::CRITICAL_LOOP_TIME * 1000) { // Convert to microseconds
      if (debugSerial) {
        Serial.printf("[SAFETY] Critical loop time: %lu μs\n", loopTime);
      }
    }
  }
  
  yield();
  // Removed final delay - timing now handled by rate limiting above for better responsiveness
}

// --- Helper function definitions ---
// Updates fermentation timing, calculates weighted time, and advances stage if needed.
// NOTE: This is the ONLY place where fermentation stages should advance to prevent race conditions.
void updateFermentationTiming(bool &stageJustAdvanced) {
  if (getProgramCount() > 0 && programState.activeProgramId < getProgramCount()) {
    Program *p = getActiveProgramMutable();
    if (p && programState.customStageIdx < p->customStages.size()) {
      CustomStage &st = p->customStages[programState.customStageIdx];
      if (st.isFermentation) {
        float baseline = p->fermentBaselineTemp > 0 ? p->fermentBaselineTemp : 20.0;
        float q10 = p->fermentQ10 > 0 ? p->fermentQ10 : 2.0;
        double actualTemp = getAveragedTemperature();
        
        // Debug output to see actual values being used
        Serial.printf("[FERMENT-TIMING] Program: %s, baseline=%.1f (raw=%.1f), Q10=%.1f (raw=%.1f), actualTemp=%.1f\n", 
                     p->name.c_str(), baseline, p->fermentBaselineTemp, q10, p->fermentQ10, actualTemp);
        
        unsigned long nowMs = millis();
        
        // Rate limiting for fermentation updates to prevent excessive processing
        if (nowMs - lastFermentUpdate < 5000 && fermentState.fermentLastUpdateMs > 0) {
          // Skip update if less than 5 seconds since last update
          return;
        }
        lastFermentUpdate = nowMs;
        
        if (fermentState.fermentLastUpdateMs == 0) {
          fermentState.fermentLastTemp = actualTemp;
          fermentState.fermentLastFactor = calculateFermentationFactorWithStage(actualTemp, st.temp, st.isFermentation); // Use stage-aware biological calculation
          Serial.printf("[FERMENT-TIMING-CALC] Initial factor calculation: temp=%.1f°C -> factor=%.3f\n", 
                       actualTemp, fermentState.fermentLastFactor);
          fermentState.fermentLastUpdateMs = nowMs;
          // Initialize new time tracking system
          fermentState.scheduledElapsedSeconds = 0.0;
          fermentState.realElapsedSeconds = 0.0;
          fermentState.accumulatedFermentMinutes = 0.0;
          if (debugSerial) Serial.printf("[FERMENT] Stage %d (%s) initialized: temp=%.1f, baseline=%.1f, q10=%.1f, factor=%.3f, planned=%.1fs (%.1f hours)\n", 
                                        programState.customStageIdx, st.label.c_str(), actualTemp, baseline, q10, 
                                        fermentState.fermentLastFactor, (double)st.min * 60.0, (double)st.min / 60.0);
        } else if (programState.isRunning) {
          // Prevent overflow: Check if millis() has wrapped around
          if (nowMs < fermentState.fermentLastUpdateMs) {
            if (debugSerial) Serial.println("[FERMENT] WARNING: millis() overflow detected, resetting tracking");
            fermentState.fermentLastUpdateMs = nowMs;
            return;
          }
          
          double realElapsedSec = (nowMs - fermentState.fermentLastUpdateMs) / 1000.0;
          // Sanity check: prevent accumulation of unreasonably large time periods
          if (realElapsedSec > 1800.0) { // More than 30 minutes since last update
            if (debugSerial) Serial.printf("[FERMENT] WARNING: Large time gap detected (%.1fs), capping at 1800s\n", realElapsedSec);
            realElapsedSec = 1800.0;
          }
          
          // Update real elapsed time
          fermentState.realElapsedSeconds += realElapsedSec;
          
          // FIXED: Calculate accumulated minutes using PREVIOUS factor FIRST (for time that passed at previous temperature)
          // NEW APPROACH: Every (fermentationFactor * 60) real seconds = 1 minute of scheduled fermentation
          // Example: factor 0.84 means every 50.4 real seconds = 1 scheduled minute (faster fermentation)
          // Example: factor 1.2 means every 72 real seconds = 1 scheduled minute (slower fermentation)
          double secondsPerScheduledMinute = fermentState.fermentLastFactor * 60.0;
          fermentState.accumulatedFermentMinutes += realElapsedSec / secondsPerScheduledMinute;
          
          // THEN update fermentation factor based on current temperature for NEXT interval
          fermentState.fermentLastTemp = actualTemp;
          fermentState.fermentLastFactor = calculateFermentationFactorWithStage(actualTemp, st.temp, st.isFermentation); // Use stage-aware biological calculation
          Serial.printf("[FERMENT-TIMING-UPDATE] Used previous factor %.3f for elapsed %.1fs, updated to new factor %.3f for temp %.1f°C\n", 
                       secondsPerScheduledMinute / 60.0, realElapsedSec, fermentState.fermentLastFactor, actualTemp);
          
          // Convert accumulated minutes back to scheduled seconds for comparison
          double previousScheduledSec = fermentState.scheduledElapsedSeconds;
          fermentState.scheduledElapsedSeconds = fermentState.accumulatedFermentMinutes * 60.0;
          
          fermentState.fermentLastUpdateMs = nowMs;
          
          // Enhanced debug output with clear real vs scheduled time
          if (debugSerial) {
            Serial.printf("[FERMENT] Stage %d (%s): real_elapsed=%.1fs, sched_elapsed=%.1fs, factor=%.3f, secs_per_sched_min=%.1f, temp=%.1f, target=%.1fs (%.1f%% complete)\n", 
                         programState.customStageIdx, st.label.c_str(), 
                         fermentState.realElapsedSeconds, fermentState.scheduledElapsedSeconds, 
                         fermentState.fermentLastFactor, secondsPerScheduledMinute, actualTemp, 
                         (double)st.min * 60.0, (fermentState.scheduledElapsedSeconds / ((double)st.min * 60.0)) * 100.0);
          }
        }
        fermentState.fermentationFactor = fermentState.fermentLastFactor; // For reference: multiply planned time by this factor for Q10
        double plannedStageSec = (double)st.min * 60.0;
        double epsilon = 0.05;
        if (!stageJustAdvanced && (fermentState.scheduledElapsedSeconds + epsilon >= plannedStageSec)) {
          if (debugSerial) Serial.printf("[FERMENT] Auto-advance: Stage %d (%s) COMPLETE - scheduled %.1fs >= planned %.1fs (%.1f%% complete, real %.1fs actual, ratio %.2f)\n", 
                                        programState.customStageIdx, st.label.c_str(), fermentState.scheduledElapsedSeconds, plannedStageSec, 
                                        (fermentState.scheduledElapsedSeconds / plannedStageSec) * 100.0, fermentState.realElapsedSeconds,
                                        fermentState.realElapsedSeconds / fermentState.scheduledElapsedSeconds);
          
          // Record when the current stage ended (BEFORE advancing)
          time_t now = time(nullptr);
          if (now > 1640995200 && programState.customStageIdx < 20) { // Valid NTP time and within bounds
            programState.actualStageEndTimes[programState.customStageIdx] = now;
            if (debugSerial) Serial.printf("[TIMING] Stage %d ended at %lu\n", programState.customStageIdx, (unsigned long)now);
          }
          
          programState.customStageIdx++;
          programState.customStageStart = millis();
          
          // Record when the new stage started for timing display
          if (now > 1640995200 && programState.customStageIdx < 20) { // Valid NTP time and within bounds
            programState.actualStageStartTimes[programState.customStageIdx] = now;
            if (debugSerial) Serial.printf("[TIMING] Stage %d started at %lu\n", programState.customStageIdx, (unsigned long)now);
          }
          
          // Optimize fermentation stage transition: batch operations and add yield points
          yield(); // Allow other tasks to run
          resetFermentationTracking(actualTemp);
          yield(); // Allow other tasks to run
          invalidateStatusCache();
          yield(); // Allow other tasks to run
          saveResumeState();
          yield(); // Allow other tasks to run
          
          stageJustAdvanced = true;
        }
      } else {
        // Don't continuously reset fermentation tracking for non-fermentation stages
        // This was causing continuous loop in debug output
      }
    } else {
      // Don't continuously reset fermentation tracking when no valid stage
      // This was causing continuous loop in debug output
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
    File f = FFat.open(RESUME_FILE, "r");
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
    // Rate limit PID calculations to configured sample time
    static unsigned long lastPIDCalculation = 0;
    unsigned long nowMs = millis();
    
    if (nowMs - lastPIDCalculation >= pid.sampleTime) {
      lastPIDCalculation = nowMs;
      
      pid.Input = getAveragedTemperature();
      
      // Initialize PID on first run to prevent D term spikes
      if (!pid.initialized) {
        pid.lastInput = pid.Input;
        pid.lastITerm = 0;
        pid.initialized = true;
        Serial.printf("[PID] Initialized: Input=%.2f°C, preventing D term startup spike\n", pid.Input);
      }
      
      double error = pid.Setpoint - pid.Input;
      double dInput = pid.Input - pid.lastInput;
      double kpUsed = pid.Kp, kiUsed = pid.Ki, kdUsed = pid.Kd;
      double sampleTimeSec = pid.sampleTime / 1000.0;
      pid.pidP = kpUsed * error;
      pid.lastITerm += kiUsed * error * sampleTimeSec;
      pid.pidI = pid.lastITerm;
      pid.pidD = -kdUsed * dInput / sampleTimeSec;
      pid.lastInput = pid.Input;
      // Calculate PID output (custom implementation since pid.controller is commented out)
      pid.Output = pid.pidP + pid.pidI + pid.pidD;
      // Clamp output to valid range [0, 1]
      if (pid.Output < 0) pid.Output = 0;
      if (pid.Output > 1) pid.Output = 1;
      if (pid.controller) pid.controller->Compute();
      updatePIDTerms(); // Update individual PID terms for monitoring
    }
    
    // Always update heater control (this can run more frequently than PID)
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
    
    invalidateStatusCache();
    saveResumeState();
    scheduledStart = 0;
    scheduledStartStage = -1;
  }
  if (!scheduledStart) scheduledStartTriggered = false;
}

// Handles the execution and advancement of custom program stages, including mixing and fermentation.
void handleCustomStages(bool &stageJustAdvanced) {
  Program *p = getActiveProgramMutable();
  if (!p || p->customStages.empty()) {
    stageJustAdvanced = false;
    return;
  }
  
  stageJustAdvanced = false;
  if (programState.customStageIdx >= p->customStages.size()) {
    stageJustAdvanced = false;
    stopBreadmaker();
    programState.customStageIdx = 0;
    programState.customStageStart = 0;
    clearResumeState();
    yield();
    delay(1);
    return;
  }
  CustomStage &st = p->customStages[programState.customStageIdx];
    pid.Setpoint = st.temp;
    checkAndSwitchPIDProfile(); // Auto-switch profile when stage changes temperature
    pid.Input = getAveragedTemperature();
    
    // Track fermentation stage transitions to prevent continuous reset calls
    static bool wasLastStageFermentation = false;
    
    if (st.isFermentation) {
      // Already handled at top of loop
    } else {
      // Only reset fermentation tracking once when entering a non-fermentation stage
      if (wasLastStageFermentation) {
        resetFermentationTracking(getAveragedTemperature());
        wasLastStageFermentation = false;
      }
    }
    
    // Update tracking flag for next iteration
    wasLastStageFermentation = st.isFermentation;
    if (fermentState.lastFermentAdjust == 0 || millis() - fermentState.lastFermentAdjust > 600000) {
      fermentState.lastFermentAdjust = millis();
      unsigned long nowMs = millis();
      unsigned long elapsedInCurrentStage = (programState.customStageStart > 0) ? (nowMs - programState.customStageStart) : 0UL;
      double cumulativePredictedSec = 0.0;
      for (size_t i = 0; i < p->customStages.size(); ++i) {
        CustomStage &stage = p->customStages[i];
        double plannedStageSec = (double)stage.min * 60.0;
        double adjustedStageSec = plannedStageSec;
        if (stage.isFermentation) {
          if (i < programState.customStageIdx) {
            float baseline = p->fermentBaselineTemp > 0 ? p->fermentBaselineTemp : 20.0;
            float q10 = p->fermentQ10 > 0 ? p->fermentQ10 : 2.0;
            float temp = fermentState.fermentLastTemp;
            float factor = pow(q10, (baseline - temp) / 10.0); // Q10: factor > 1 means slower at cooler temp
            cumulativePredictedSec += plannedStageSec * factor; // Multiply: cooler temp = longer real time needed
          } else if (i == programState.customStageIdx) {
            // Current fermentation stage: use scheduled elapsed time
            double elapsedScheduledSec = fermentState.scheduledElapsedSeconds;
            
            // Remaining scheduled time needed
            double remainingScheduledSec = plannedStageSec - elapsedScheduledSec;
            if (remainingScheduledSec < 0) remainingScheduledSec = 0;
            
            // Convert to real time using current factor
            double factor = fermentState.fermentLastFactor > 0 ? fermentState.fermentLastFactor : 1.0;
            double elapsedRealSec = fermentState.realElapsedSeconds;
            double remainingRealSec = remainingScheduledSec * factor; // Multiply: cooler temp = more real time
            cumulativePredictedSec += elapsedRealSec + remainingRealSec;
          } else {
            // Future fermentation stages: predict using current temperature
            cumulativePredictedSec += plannedStageSec * fermentState.fermentLastFactor; // Multiply: cooler temp = longer real time
          }
        } else {
          cumulativePredictedSec += plannedStageSec;
        }
      }
      // Check if NTP time is valid before setting predicted complete time
      time_t now = time(nullptr);
      bool ntpValid = (now > 1640995200); // Jan 1, 2022 - if before this, NTP failed
      if (ntpValid && programState.programStartTime > 1640995200) {
        fermentState.predictedCompleteTime = (unsigned long)(programState.programStartTime + (unsigned long)(cumulativePredictedSec)); // All predicted times use Q10 multiply logic
      } else {
        fermentState.predictedCompleteTime = 0; // NTP not synced, disable timestamp predictions
      }
    }
    if (st.temp > 0 || programState.manualMode) {
      // Rate limit PID calculations to configured sample time
      static unsigned long lastCustomStagePIDCalculation = 0;
      unsigned long nowMs = millis();
      bool shouldCalculatePID = (nowMs - lastCustomStagePIDCalculation >= pid.sampleTime);
      
      if (programState.manualMode && pid.Setpoint > 0) {
        if (shouldCalculatePID) {
          lastCustomStagePIDCalculation = nowMs;
          
          pid.Input = getAveragedTemperature();
          
          // Initialize PID on first run to prevent D term spikes
          if (!pid.initialized) {
            pid.lastInput = pid.Input;
            pid.lastITerm = 0;
            pid.initialized = true;
            Serial.printf("[PID] Initialized (manual): Input=%.2f°C\n", pid.Input);
          }
          
          double error = pid.Setpoint - pid.Input;
          double dInput = pid.Input - pid.lastInput;
          double kpUsed = pid.Kp, kiUsed = pid.Ki, kdUsed = pid.Kd;
          double sampleTimeSec = pid.sampleTime / 1000.0;
          pid.pidP = kpUsed * error;
          pid.lastITerm += kiUsed * error * sampleTimeSec;
          pid.pidI = pid.lastITerm;
          pid.pidD = -kdUsed * dInput / sampleTimeSec;
          pid.lastInput = pid.Input;
          // Calculate PID output (custom implementation since pid.controller is commented out)
          pid.Output = pid.pidP + pid.pidI + pid.pidD;
          // Clamp output to valid range [0, 1]
          if (pid.Output < 0) pid.Output = 0;
          if (pid.Output > 1) pid.Output = 1;
          if (pid.controller) pid.controller->Compute();
        }
        // Always update heater control (this can run more frequently than PID)
        updateTimeProportionalHeater();
      } else if (!programState.manualMode && st.temp > 0) {
        if (shouldCalculatePID) {
          lastCustomStagePIDCalculation = nowMs;
          
          pid.Input = getAveragedTemperature();
          
          // Initialize PID on first run to prevent D term spikes
          if (!pid.initialized) {
            pid.lastInput = pid.Input;
            pid.lastITerm = 0;
            pid.initialized = true;
            Serial.printf("[PID] Initialized (auto): Input=%.2f°C\n", pid.Input);
          }
          
          double error = pid.Setpoint - pid.Input;
          double dInput = pid.Input - pid.lastInput;
          double kpUsed = pid.Kp, kiUsed = pid.Ki, kdUsed = pid.Kd;
          double sampleTimeSec = pid.sampleTime / 1000.0;
          pid.pidP = kpUsed * error;
          pid.lastITerm += kiUsed * error * sampleTimeSec;
          pid.pidI = pid.lastITerm;
          pid.pidD = -kdUsed * dInput / sampleTimeSec;
          pid.lastInput = pid.Input;
          // Calculate PID output (custom implementation since pid.controller is commented out)
          pid.Output = pid.pidP + pid.pidI + pid.pidD;
          // Clamp output to valid range [0, 1]
          if (pid.Output < 0) pid.Output = 0;
          if (pid.Output > 1) pid.Output = 1;
          if (pid.controller) pid.controller->Compute();
        }
        // Always update heater control (this can run more frequently than PID)
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
      // Safety limit: allow up to 4x the planned stage duration
      unsigned long maxStageMs = (unsigned long)st.min * 60000UL * 4; // 4x expected time as safety limit
      // For very long stages, cap at 24 hours (1440 minutes) to prevent overflow
      if (st.min > 360) { // If stage is > 6 hours, cap safety at 24 hours
        maxStageMs = 1440UL * 60000UL; // 24 hours maximum
        if (debugSerial) {
          Serial.printf("[WARN] Fermentation stage %u minutes is very long, capping safety timeout at 24 hours\n", st.min);
        }
      }
      if (maxStageMs > 0 && elapsedMs > maxStageMs && !stageComplete) {
        if (debugSerial) Serial.printf("[FORCE ADVANCE] Forcing advancement of stuck fermentation stage %d after %lu minutes (4x limit reached: %lu min planned, %lu min safety limit)\n", 
                                      (int)programState.customStageIdx, elapsedMs / 60000UL, (unsigned long)st.min, maxStageMs / 60000UL);
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
        double remainScheduledSec = plannedStageSec - fermentState.scheduledElapsedSeconds;
        if (remainScheduledSec < 0) remainScheduledSec = 0;
        double factor = fermentState.fermentLastFactor > 0 ? fermentState.fermentLastFactor : 1.0; // Q10: factor < 1 means faster
        time_t now = time(nullptr);
        // Check if NTP time is valid (reasonable epoch time)
        bool ntpValid = (now > 1640995200); // Jan 1, 2022 - if before this, NTP failed
        if (ntpValid) {
          time_t predicted = now + (time_t)(remainScheduledSec * factor); // Multiply: higher temp = less time
          fermentState.predictedCompleteTime = predicted;
          if (debugSerial) Serial.printf("[FERMENT] Stage advanced, predictedCompleteTime set to %lu (now=%lu, remainScheduledSec=%.2f, factor=%.3f) [MULTIPLY]\n", (unsigned long)predicted, (unsigned long)now, remainScheduledSec, factor);
        } else {
          fermentState.predictedCompleteTime = 0; // NTP not synced, disable timestamp predictions
          if (debugSerial) Serial.printf("[FERMENT] Stage advanced, NTP not synced (now=%lu), predictedCompleteTime disabled\n", (unsigned long)now);
        }
      }
      programState.customStageIdx++;
      programState.customStageStart = millis();
      programState.customMixIdx = 0;
      programState.customMixStepStart = 0;
      
      // Record when the new stage started for timing display
      time_t now = time(nullptr);
      if (now > 1640995200 && programState.customStageIdx < 20) { // Valid NTP time and within bounds
        programState.actualStageStartTimes[programState.customStageIdx] = now;
      }
      
      // Optimize stage transition: batch operations and add yield points
      yield(); // Allow other tasks to run
      saveResumeState();
      yield(); // Allow other tasks to run
      invalidateStatusCache();
      yield(); // Allow other tasks to run
      
      stageJustAdvanced = true;
      if (debugSerial) Serial.printf("[ADVANCE] Stage advanced to %d\n", programState.customStageIdx);
      
      // Reduce CPU load during transition
      yield();
      delay(50); // Increased delay to reduce CPU spike
      return;
    } else {
      yield();
      delay(100);
      return;
    }
}

// Serial WiFi configuration helper
void checkSerialWifiConfig() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd.startsWith("wifi:")) {
      // Format: wifi:SSID,PASSWORD
      int commaIndex = cmd.indexOf(',', 5);
      if (commaIndex > 5) {
        String ssid = cmd.substring(5, commaIndex);
        String pass = cmd.substring(commaIndex + 1);
        
        Serial.printf("[WIFI CONFIG] Setting up WiFi: %s\n", ssid.c_str());
        
        // Save to wifi.json
        DynamicJsonDocument doc(256);
        doc["ssid"] = ssid;
        doc["pass"] = pass;
        
        File f = FFat.open("/wifi.json", "w");
        if (f) {
          serializeJson(doc, f);
          f.close();
          Serial.println("[WIFI CONFIG] WiFi configuration saved!");
          Serial.println("[WIFI CONFIG] Restarting to apply new WiFi settings...");
          delay(1000);
          ESP.restart();
        } else {
          Serial.println("[WIFI CONFIG] ERROR: Failed to save WiFi configuration");
        }
      } else {
        Serial.println("[WIFI CONFIG] Invalid format. Use: wifi:SSID,PASSWORD");
      }
    } else if (cmd == "wifi:help") {
      Serial.println(F("[WIFI CONFIG] WiFi Configuration Commands:"));
      Serial.println(F("[WIFI CONFIG] wifi:SSID,PASSWORD - Set WiFi credentials"));
      Serial.println(F("[WIFI CONFIG] wifi:status - Show current WiFi status"));
      Serial.println(F("[WIFI CONFIG] wifi:reset - Reset WiFi configuration"));
      Serial.println(F("[WIFI CONFIG] wifi:help - Show this help"));
    } else if (cmd == "wifi:status") {
      Serial.printf("[WIFI CONFIG] WiFi Status: %s\n", wifiCache.isConnected() ? "Connected" : "Disconnected");
      if (wifiCache.isConnected()) {
        Serial.printf("[WIFI CONFIG] IP: %s\n", wifiCache.getIPString().c_str());
        Serial.printf("[WIFI CONFIG] SSID: %s\n", wifiCache.getSSID().c_str());
      } else if (WiFi.getMode() == WIFI_AP) {
        Serial.printf("[WIFI CONFIG] AP Mode IP: %s\n", WiFi.softAPIP().toString().c_str());
      }
    } else if (cmd == "wifi:reset") {
      if (FFat.remove("/wifi.json")) {
        Serial.println("[WIFI CONFIG] WiFi configuration reset. Restarting...");
        delay(1000);
        ESP.restart();
      } else {
        Serial.println("[WIFI CONFIG] No WiFi configuration to reset");
      }
    }
  }
}

// === OTA Update Support ===
// setupOTA function removed - OTA is now handled by ota_manager.cpp
// This eliminates conflicts between multiple OTA implementations
// OTA functionality is provided by otaManagerInit() and otaManagerLoop()

// === Settings Management ===
// Loads breadmaker settings (PID, temperature, program selection, etc.) from FFat.
void loadSettings() {
  if (debugSerial) Serial.println("[loadSettings] Loading settings...");
  File f = FFat.open(SETTINGS_FILE, "r");
  if (!f) {
    if (debugSerial) Serial.println("[loadSettings] settings.json not found, using defaults.");
    debugSerial = true;
    return;
  }
  
  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, f);
  if (err) {
    if (debugSerial) {
      Serial.print("[loadSettings] Failed to parse settings.json: ");
      Serial.println(err.c_str());
    }
    f.close();
    debugSerial = true;
    return;
  }
  
  // outputMode removed - system is always digital
  debugSerial = doc["debugSerial"] | true;
  
  // Load safety system settings (default to disabled for testing)
  safetySystem.safetyEnabled = doc["safetyEnabled"] | false;
  
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
  }
  
  // Restore last selected program by ID if present, fallback to name for backward compatibility
  if (doc.containsKey("lastProgramId")) {
    int lastId = doc["lastProgramId"];
    if (lastId >= 0 && lastId < (int)getProgramCount()) {
      programState.activeProgramId = lastId;
      if (debugSerial) {
        Serial.print("[loadSettings] lastProgramId loaded: ");
        Serial.println(lastId);
      }
    }
  } else if (doc.containsKey("lastProgram")) {
    // Deprecated: fallback to name for backward compatibility
    String lastProg = doc["lastProgram"].as<String>();
    for (size_t i = 0; i < getProgramCount(); ++i) {
      if (getProgramName(i) == lastProg) {
        programState.activeProgramId = i;
        if (debugSerial) {
          Serial.print("[loadSettings] lastProgram loaded (deprecated): ");
          Serial.println(lastProg);
        }
        break;
      }
    }
  }
  
  if (debugSerial) {
    Serial.print("[loadSettings] debugSerial: ");
    Serial.println(debugSerial ? "true" : "false");
  }
  f.close();
  if (debugSerial) Serial.println("[loadSettings] Settings loaded.");
}

// Saves breadmaker settings (PID, temperature, program selection, etc.) to FFat.
// Uses direct streaming to minimize memory usage under load.
void saveSettings() {
  if (debugSerial) Serial.println("[saveSettings] Saving settings...");
  
  // Check if file system is available
  if (!FFat.begin()) {
    if (debugSerial) Serial.println("[saveSettings] FFat not available!");
    return;
  }
  
  File f = FFat.open(SETTINGS_FILE, "w");
  if (!f) {
    if (debugSerial) Serial.println("[saveSettings] Failed to open settings.json for writing!");
    return;
  }
  
  // Use simple JSON construction to avoid memory issues
  f.print("{\n");
  f.print("  \"outputMode\":\"digital\",\n");
  f.print("  \"debugSerial\":");
  f.print(debugSerial ? "true" : "false");
  f.print(",\n");
  f.print("  \"safetyEnabled\":");
  f.print(safetySystem.safetyEnabled ? "true" : "false");
  f.print(",\n");
  
  // Program information with safety checks
  int programId = 0;
  String programName = "Unknown";
  
  if (getProgramCount() > 0 && programState.activeProgramId >= 0 && programState.activeProgramId < (int)getProgramCount()) {
    programId = programState.activeProgramId;
    String progName = getProgramName(programState.activeProgramId);
    if (progName.length() > 0) {
      programName = progName;
    }
  }
  
  f.print("  \"lastProgramId\":");
  f.print(programId);
  f.print(",\n");
  f.print("  \"lastProgram\":\"");
  f.print(programName);
  f.print("\",\n");
  
  // PID parameters
  f.print("  \"pidKp\":");
  f.print(pid.Kp, 6);
  f.print(",\n");
  f.print("  \"pidKi\":");
  f.print(pid.Ki, 6);
  f.print(",\n");
  f.print("  \"pidKd\":");
  f.print(pid.Kd, 6);
  f.print(",\n");
  f.print("  \"pidSampleTime\":");
  f.print(pid.sampleTime);
  f.print(",\n");
  f.print("  \"pidWindowSize\":");
  f.print(windowSize);
  f.print("\n");
  f.print("}\n");
  
  f.close();
  if (debugSerial) Serial.println("[saveSettings] Settings saved.");
}
