#include "web_endpoints.h"
#include <Arduino.h>
#include "globals.h"
#include <ArduinoJson.h>
#include "programs_manager.h"
#include "outputs_manager.h"
#include <PID_v1.h>
#include <WiFi.h>
#include "calibration.h"
#include <LittleFS.h>
#include "ota_manager.h"

#ifdef ESP32
#include <esp_system.h>
#endif
// Add other includes as needed

// Performance tracking variables
static unsigned long loopCount = 0;
static unsigned long lastLoopTime = 0;
static unsigned long totalLoopTime = 0;
static unsigned long maxLoopTime = 0;
static unsigned long minFreeHeap = 0xFFFFFFFF;
static unsigned long wifiReconnectCount = 0;
static unsigned long lastWifiStatus = WL_CONNECTED;

// Caching variables for frequently accessed endpoints
static unsigned long lastStatusUpdate = 0;
static unsigned long lastHaUpdate = 0;
static const unsigned long STATUS_CACHE_MS = 1000;  // Cache for 1 second
static const unsigned long HA_CACHE_MS = 3000;      // Cache HA for 3 seconds

// Cache invalidation function - call this when state changes
void invalidateStatusCache() {
  lastStatusUpdate = 0;
  lastHaUpdate = 0;
}

// Forward declarations for helpers and global variables used in endpoints
extern bool debugSerial;
extern unsigned long windowSize, onTime, startupTime;
extern bool thermalRunawayDetected, sensorFaultDetected;
extern time_t scheduledStart;
extern int scheduledStartStage;
extern bool heaterState, motorState, lightState, buzzerState;
extern void streamStatusJson(Print& out);
extern void resetFermentationTracking(float temp);
extern float getAveragedTemperature();
extern void saveSettings();
extern void saveResumeState();
extern void updateActiveProgramVars();
extern void stopBreadmaker();
extern bool isStartupDelayComplete();
extern void setHeater(bool);
extern void setMotor(bool);
extern void setLight(bool);
extern void setBuzzer(bool);
extern void shortBeep();
extern void startBuzzerTone(float, float, unsigned long);
extern float readTemperature();
extern void switchToProfile(const String& profileName);
extern void loadPIDProfiles();
extern void savePIDProfiles();

extern void sendJsonError(AsyncWebServerRequest*, const String&, const String&, int);
void deleteFolderRecursive(const String& path);

extern std::vector<Program> programs;
extern unsigned long lightOnTime;
extern unsigned long windowStartTime;
// Now accessed via pid.pidP, pid.pidI, pid.pidD, pid.lastInput, pid.lastITerm
// Use fermentState struct fields

// Now accessed via pid.controller
// Use tempAvg struct fields
#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__
#endif
void stateMachineEndpoints(AsyncWebServer& server);
void manualOutputEndpoints(AsyncWebServer& server);
void pidControlEndpoints(AsyncWebServer& server);
void pidProfileEndpoints(AsyncWebServer& server);
void homeAssistantEndpoint(AsyncWebServer& server);
void calibrationEndpoints(AsyncWebServer& server);
void fileEndPoints(AsyncWebServer& server);
void programsEndpoints(AsyncWebServer& server);
void otaEndpoints(AsyncWebServer& server);

// Core endpoints previously in setup()
void coreEndpoints(AsyncWebServer& server) {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
        req->send(LittleFS, "/index.html", "text/html");
    });
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest* req){
        if (debugSerial) Serial.println(F("[DEBUG] /status requested (streaming)"));
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        streamStatusJson(*response);
        req->send(response);
    });
    server.on("/api/firmware_info", HTTP_GET, [](AsyncWebServerRequest* req){
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        response->print("{");
        response->print("\"build\":\"");
        response->print(FIRMWARE_BUILD_DATE);
        response->print("\"}");
        req->send(response);
    });
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* req){
        req->send(200, "application/json", "{\"status\":\"restarting\"}");
        delay(200);
        ESP.restart();
    });
    server.on("/api/output_mode", HTTP_GET, [](AsyncWebServerRequest* req){
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        response->print("{");
        response->print("\"mode\":\"digital\"");
        response->print("}");
        req->send(response);
    });
    server.on("/api/output_mode", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            DynamicJsonDocument doc(128);
            if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }
            // Always set to digital mode (analog mode removed)
            outputMode = OUTPUT_DIGITAL;
            saveSettings();
            AsyncResponseStream *response = req->beginResponseStream("application/json");
            response->print("{");
            response->print("\"mode\":\"digital\"");
            response->print("}");
            req->send(response);
        }
    );
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

}

void registerWebEndpoints(AsyncWebServer& server) {
    coreEndpoints(server);
    stateMachineEndpoints(server);
    manualOutputEndpoints(server);
    pidControlEndpoints(server);
    pidProfileEndpoints(server);
    homeAssistantEndpoint(server);
    calibrationEndpoints(server);
    fileEndPoints(server);
    programsEndpoints(server);
    otaEndpoints(server);
}

// --- Move the full definitions of these functions from your .ino here ---

void stateMachineEndpoints(AsyncWebServer& server)
{
  // --- Unified start endpoint - handles immediate start, stage selection, and scheduling ---
  server.on("/start", HTTP_GET, [](AsyncWebServerRequest*req){
    if (debugSerial) Serial.println(F("[ACTION] /start called"));
    
    // Check for time parameter - if present, schedule the start
    if (req->hasParam("time")) {
      String t = req->getParam("time")->value(); // format: "HH:MM"
      
      // Parse and validate time
      struct tm nowTm;
      time_t now = time(nullptr);
      localtime_r(&now, &nowTm);
      int hh, mm;
      if (sscanf(t.c_str(), "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
        req->send(400, "application/json", "{\"error\":\"Invalid time format. Use HH:MM\"}");
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
      
      // Handle optional stage parameter for scheduled start
      if (req->hasParam("stage")) {
        int stageIdx = req->getParam("stage")->value().toInt();
        
        // Basic validation - more detailed validation will happen when scheduled start triggers
        if (stageIdx < 0) {
          req->send(400, "application/json", "{\"error\":\"Invalid stage index\"}");
          return;
        }
        
        scheduledStartStage = stageIdx;
        req->send(200, "application/json", "{\"status\":\"Scheduled to start at stage " + String(stageIdx + 1) + " at " + t + "\"}");
        if (debugSerial) Serial.printf("[START] Scheduled to start at stage %d at %s\n", stageIdx, t.c_str());
      } else {
        // Time only (start from beginning)
        scheduledStartStage = -1; // -1 means start from beginning
        req->send(200, "application/json", "{\"status\":\"Scheduled to start at " + t + "\"}");
        if (debugSerial) Serial.printf("[START] Scheduled to start at %s\n", t.c_str());
      }
      return;
    }
    
    // No time parameter - immediate start
    updateActiveProgramVars();
    
    // Debug: print active program info
    if (debugSerial && programState.activeProgramId < getProgramCount() && isProgramValid(programState.activeProgramId)) {
      Serial.printf_P(PSTR("[START] activeProgram='%s' (id=%u), customProgram=%p\n"), 
                     getProgramName(programState.activeProgramId).c_str(), 
                     (unsigned)programState.activeProgramId, (void*)programState.customProgram);
    }

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
      req->send(response);
      return;
    }

    // Check if a program is selected and customProgram is valid
    if (!programState.customProgram) {
      AsyncResponseStream *response = req->beginResponseStream("application/json");
      response->print("{");
      response->print("\"error\":\"No program selected\",");
      response->print("\"message\":\"Please select a program before starting.\"");
      response->print("}");
      req->send(response);
      return;
    }
    
    size_t numStages = programState.customProgram->customStages.size();
    if (numStages == 0) {
      AsyncResponseStream *response = req->beginResponseStream("application/json");
      response->print("{");
      response->print("\"error\":\"No stages defined\",");
      response->print("\"message\":\"The selected program has no stages.\"");
      response->print("}");
      req->send(response);
      return;
    }

    // Handle optional stage parameter for immediate start
    int startStageIdx = 0;
    if (req->hasParam("stage")) {
      startStageIdx = req->getParam("stage")->value().toInt();
      
      // Validate stage index
      if (startStageIdx < 0 || (size_t)startStageIdx >= numStages) {
        String msg = "Stage index must be between 0 and " + String(numStages - 1);
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        response->print("{");
        response->print("\"error\":\"Invalid stage index\",");
        response->printf("\"message\":\"%s\"", msg.c_str());
        response->print("}");
        req->send(response);
        return;
      }
      
      if (debugSerial) Serial.printf("[START] Starting immediately at stage %d\n", startStageIdx);
    } else {
      if (debugSerial) Serial.println("[START] Starting immediately from beginning");
    }

    // Start the program
    programState.isRunning = true;
    programState.customStageIdx = startStageIdx;
    programState.customMixIdx = 0;
    programState.customStageStart = millis();
    programState.customMixStepStart = 0;
    programState.programStartTime = time(nullptr);
    
    // Invalidate cache when state changes
    invalidateStatusCache();
    
    // Initialize actual stage start times array
    for (int i = 0; i < MAX_PROGRAM_STAGES; i++) {
      if (i < startStageIdx) {
        // Mark previous stages as completed with dummy timestamps
        programState.actualStageStartTimes[i] = programState.programStartTime - 1000 * (startStageIdx - i);
      } else if (i == startStageIdx) {
        programState.actualStageStartTimes[i] = programState.programStartTime;
      } else {
        programState.actualStageStartTimes[i] = 0; // Future stages
      }
    }

    // --- Fermentation tracking: reset and initialize for current stage ---
    resetFermentationTracking(getAveragedTemperature());
    
    if (programState.customProgram && (size_t)startStageIdx < programState.customProgram->customStages.size()) {
      CustomStage &st = programState.customProgram->customStages[startStageIdx];
      if (st.isFermentation) {
        float baseline = programState.customProgram->fermentBaselineTemp > 0 ? programState.customProgram->fermentBaselineTemp : 20.0;
        float q10 = programState.customProgram->fermentQ10 > 0 ? programState.customProgram->fermentQ10 : 2.0;
        float actualTemp = getAveragedTemperature();
        fermentState.initialFermentTemp = actualTemp;
        fermentState.fermentationFactor = pow(q10, (baseline - actualTemp) / 10.0);
        fermentState.fermentLastTemp = actualTemp;
        fermentState.fermentLastFactor = fermentState.fermentationFactor;
        fermentState.fermentLastUpdateMs = millis();
        fermentState.fermentWeightedSec = 0.0;
        if (debugSerial) {
          Serial.printf("[START] Fermentation stage %d: baseline=%.1f, temp=%.1f, factor=%.3f\n", 
                        startStageIdx, baseline, actualTemp, fermentState.fermentationFactor);
        }
      }
    }
    
    saveResumeState();
    
    // Clear any pending scheduled start
    scheduledStart = 0;
    scheduledStartStage = -1;
    
    if (debugSerial) Serial.printf_P(PSTR("[STAGE] Entering custom stage: %d\n"), programState.customStageIdx);
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest*req){
    if (debugSerial) Serial.println(F("[ACTION] /stop called"));
    updateActiveProgramVars();
    stopBreadmaker();
    invalidateStatusCache();
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/pause", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println(F("[ACTION] /pause called"));
    programState.isRunning = false;
    invalidateStatusCache();
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
    programState.isRunning = true;
    invalidateStatusCache();
    // Do NOT reset actualStageStartTimes[customStageIdx] on resume if it was already set.
    // Only set it if it was never set (i.e., just started this stage for the first time).
    if (programState.customStageIdx < MAX_PROGRAM_STAGES && programState.actualStageStartTimes[programState.customStageIdx] == 0) {
      programState.actualStageStartTimes[programState.customStageIdx] = time(nullptr);
      if (debugSerial) Serial.printf_P(PSTR("[RESUME PATCH] Set actualStageStartTimes[%u] to now (%ld) on /resume (first entry to this stage).\n"), (unsigned)programState.customStageIdx, (long)programState.actualStageStartTimes[programState.customStageIdx]);
    } else {
      if (debugSerial) Serial.printf_P(PSTR("[RESUME PATCH] actualStageStartTimes[%u] already set (%ld), not resetting on resume.\n"), (unsigned)programState.customStageIdx, (long)programState.actualStageStartTimes[programState.customStageIdx]);
    }
    saveResumeState();
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/advance", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println(F("[ACTION] /advance called"));
    if (programState.activeProgramId >= getProgramCount()) { stopBreadmaker(); AsyncResponseStream *response = req->beginResponseStream("application/json"); streamStatusJson(*response); req->send(response); return; }
    Program *p = getActiveProgramMutable();
    if (!p) { stopBreadmaker(); AsyncResponseStream *response = req->beginResponseStream("application/json"); streamStatusJson(*response); req->send(response); return; }
    programState.customStageIdx++;
    if (programState.customStageIdx >= p->customStages.size()) {
      stopBreadmaker(); programState.customStageIdx = 0; programState.customStageStart = 0; invalidateStatusCache(); AsyncResponseStream *response = req->beginResponseStream("application/json"); streamStatusJson(*response); req->send(response); return;
    }
    programState.customStageStart = millis();
    programState.customMixIdx = 0;
    programState.customMixStepStart = 0;
    programState.isRunning = true;
    if (programState.customStageIdx == 0) programState.programStartTime = time(nullptr);
    invalidateStatusCache();
    // Record actual start time of this stage
    if (programState.customStageIdx < MAX_PROGRAM_STAGES) programState.actualStageStartTimes[programState.customStageIdx] = time(nullptr);

    // --- Fermentation tracking: set immediately if new stage is fermentation ---
    fermentState.initialFermentTemp = 0.0;
    fermentState.fermentationFactor = 1.0;
    fermentState.predictedCompleteTime = 0;
    fermentState.lastFermentAdjust = 0;
    if (programState.activeProgramId < getProgramCount()) {
      Program *p = getActiveProgramMutable();
      if (p && programState.customStageIdx < p->customStages.size()) {
        CustomStage &st = p->customStages[programState.customStageIdx];
        if (st.isFermentation) {
          float baseline = p->fermentBaselineTemp > 0 ? p->fermentBaselineTemp : 20.0;
          float q10 = p->fermentQ10 > 0 ? p->fermentQ10 : 2.0;
          float actualTemp = getAveragedTemperature();
          fermentState.initialFermentTemp = actualTemp;
          fermentState.fermentationFactor = pow(q10, (baseline - actualTemp) / 10.0);
          unsigned long plannedStageMs = (unsigned long)st.min * 60000UL;
          unsigned long adjustedStageMs = plannedStageMs * fermentState.fermentationFactor;
          fermentState.predictedCompleteTime = millis() + adjustedStageMs;
          fermentState.lastFermentAdjust = millis();
        }
      }
    }
    saveResumeState();
    if (debugSerial) Serial.printf("[STAGE] Entering custom stage: %d\n", programState.customStageIdx);
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/back", HTTP_GET, [](AsyncWebServerRequest* req){
    Serial.println(F("[DEBUG] /back endpoint handler entered"));
    if (debugSerial) Serial.println(F("[ACTION] /back called"));
    updateActiveProgramVars();
    // Robust null and bounds checks
    // If current program is a dummy slot, clamp to first valid program
    if (programState.activeProgramId >= getProgramCount() || !isProgramValid(programState.activeProgramId)) {
        // Find first valid program
        size_t firstValid = 0;
        for (size_t i = 0; i < getProgramCount(); ++i) {
            if (isProgramValid(i)) { firstValid = i; break; }
        }
        programState.activeProgramId = firstValid;
        updateActiveProgramVars();
        if (!programState.customProgram) {
            Serial.println(F("[ERROR] /back: customProgram is null after clamping"));
            stopBreadmaker();
            AsyncResponseStream *response = req->beginResponseStream("application/json");
            streamStatusJson(*response);
            req->send(response);
            return;
        }
    }
    if (!programState.customProgram) {
        Serial.println(F("[ERROR] /back: customProgram is null"));
        stopBreadmaker();
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        streamStatusJson(*response);
        req->send(response);
        return;
    }
    Program *p = getActiveProgramMutable();
    if (!p) {
        Serial.printf_P(PSTR("[ERROR] /back: Unable to get active program\n"));
        stopBreadmaker();
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        streamStatusJson(*response);
        req->send(response);
        return;
    }
    size_t numStages = p->customStages.size();
    if (numStages == 0) {
        Serial.printf_P(PSTR("[ERROR] /back: Program at id %u has zero stages\n"), (unsigned)programState.activeProgramId);
        stopBreadmaker();
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        streamStatusJson(*response);
        req->send(response);
        return;
    }
    // Clamp customStageIdx to valid range
    if (programState.customStageIdx >= numStages) {
        Serial.printf_P(PSTR("[WARN] /back: customStageIdx (%u) >= numStages (%u), clamping\n"), (unsigned)programState.customStageIdx, (unsigned)numStages);
        programState.customStageIdx = numStages - 1;
    }
    if (programState.customStageIdx > 0) {
        programState.customStageIdx--;
        Serial.printf_P(PSTR("[INFO] /back: Decremented customStageIdx to %u\n"), (unsigned)programState.customStageIdx);
    } else {
        programState.customStageIdx = 0;
        Serial.println(F("[INFO] /back: Already at first stage, staying at 0"));
    }
    programState.customStageStart = millis();
    programState.customMixIdx = 0;
    programState.customMixStepStart = 0;
    programState.isRunning = true;
    if (programState.customStageIdx == 0) programState.programStartTime = time(nullptr);
    invalidateStatusCache();
    // Record actual start time of this stage when going back
    if (programState.customStageIdx < numStages && programState.customStageIdx < MAX_PROGRAM_STAGES) programState.actualStageStartTimes[programState.customStageIdx] = time(nullptr);
    saveResumeState();
    Serial.printf_P(PSTR("[STAGE] Entering custom stage: %d\n"), programState.customStageIdx);
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/select", HTTP_GET, [](AsyncWebServerRequest* req){
    // Select by id (preferred)
    if (req->hasParam("idx")) {
      int id = req->getParam("idx")->value().toInt();
      
      if (debugSerial) {
        Serial.printf("[DEBUG] /select called with ID %d, programMetadata size: %zu\n", id, programMetadata.size());
        for (const auto& meta : programMetadata) {
          Serial.printf("[DEBUG] Available program ID: %d, Name: %s\n", meta.id, meta.name.c_str());
        }
      }
      
      // Check if program ID exists in metadata
      bool programExists = false;
      for (const auto& meta : programMetadata) {
        if (meta.id == id) {
          programExists = true;
          break;
        }
      }
      
      if (programExists) {
        if (debugSerial) Serial.printf("[DEBUG] Program ID %d exists in metadata, attempting to load...\n", id);
        // Load the specific program on-demand
        if (ensureProgramLoaded(id)) {
          programState.activeProgramId = id;
          updateActiveProgramVars();
          programState.isRunning = false;
          invalidateStatusCache();
          saveSettings();
          saveResumeState();
          
          Serial.printf("[INFO] Selected program ID %d, memory usage: %zu bytes\n", id, getAvailableMemory());
          req->send(200, "text/plain", "Selected");
          return;
        } else {
          if (debugSerial) Serial.printf("[ERROR] Failed to load program ID %d\n", id);
          req->send(500, "text/plain", "Failed to load program");
          return;
        }
      }
      if (debugSerial) Serial.printf("[ERROR] Program ID %d not found in metadata\n", id);
      req->send(400, "text/plain", "Bad program id");
      return;
    }
    
    // Fallback: select by name (legacy) - search metadata first
    if (req->hasParam("name")) {
      const char* name = req->getParam("name")->value().c_str();
      int foundIdx = -1;
      
      // Search in metadata first
      for (const auto& meta : programMetadata) {
        if (meta.name == name) {
          foundIdx = meta.id;
          break;
        }
      }
      
      if (foundIdx >= 0) {
        // Load the specific program on-demand
        if (ensureProgramLoaded(foundIdx)) {
          programState.activeProgramId = foundIdx;
          updateActiveProgramVars();
          programState.isRunning = false;
          saveSettings();
          saveResumeState();
          
          Serial.printf("[INFO] Selected program '%s' (ID %d), memory usage: %zu bytes\n", 
                        name, foundIdx, getAvailableMemory());
          req->send(200, "text/plain", "Selected");
          return;
        } else {
          req->send(500, "text/plain", "Failed to load program");
          return;
        }
      }
    }
    req->send(400, "text/plain", "Bad program name or id");
  });

}

void manualOutputEndpoints(AsyncWebServer& server)
{
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
      invalidateStatusCache();
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
      invalidateStatusCache();
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
      programState.manualMode = (on != 0);
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
        pid.Setpoint = temp;
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
      response->printf("\"setpoint\":%.6f,", pid.Setpoint);
      response->printf("\"input\":%.6f,", pid.Input);
      response->printf("\"output\":%.6f,", pid.Output);
      response->printf("\"current_temp\":%.6f,", getAveragedTemperature());
      response->printf("\"raw_temp\":%.6f,", readTemperature());
      response->printf("\"heater_state\":%s,", heaterState ? "true" : "false");
      response->printf("\"manual_mode\":%s,", programState.manualMode ? "true" : "false");
      response->printf("\"is_running\":%s,", programState.isRunning ? "true" : "false");
      response->printf("\"kp\":%.6f,", pid.Kp);
      response->printf("\"ki\":%.6f,", pid.Ki);
      response->printf("\"kd\":%.6f", pid.Kd);
      response->printf("\"sample_time_ms\":%lu,", pid.sampleTime);
      response->printf("\"temp_sample_count\":%d,", tempAvg.tempSampleCount);
      response->printf("\"temp_reject_count\":%d,", tempAvg.tempRejectCount);
      response->printf("\"temp_sample_interval_ms\":%lu,", tempAvg.tempSampleInterval);
      unsigned long nowMs = millis();
      unsigned long elapsed = (windowStartTime > 0) ? (nowMs - windowStartTime) : 0;
      response->printf("\"window_size_ms\":%lu,", windowSize);
      response->printf("\"window_elapsed_ms\":%lu,", elapsed);
      response->printf("\"on_time_ms\":%lu,", onTime);
      response->printf("\"duty_cycle_percent\":%.2f,", (pid.Output * 100.0));
      // --- PID component terms ---
      response->printf("\"pid_p\":%.6f,", pid.pidP);
      response->printf("\"pid_i\":%.6f,", pid.pidI);
      response->printf("\"pid_d\":%.6f", pid.pidD);
      response->print("}");
      req->send(response);
    });
}

void pidControlEndpoints(AsyncWebServer& server)
{
  // --- PID parameter tuning endpoint ---
  server.on("/api/pid_params", HTTP_GET, [](AsyncWebServerRequest* req){
    bool updated = false;
    bool pidTuningsChanged = false;
    String errors = "";
    
    // Store original values to check if they actually changed
    float originalKp = pid.Kp, originalKi = pid.Ki, originalKd = pid.Kd;
    unsigned long originalSampleTime = pid.sampleTime;
    
    if (req->hasParam("kp")) {
      float kp = req->getParam("kp")->value().toFloat();
      if (kp >= 0 && kp <= 100) {
        if (abs(pid.Kp - kp) > 0.000001) { // Only update if actually different
          pid.Kp = kp;
          pidTuningsChanged = true;
          updated = true;
          if (debugSerial) Serial.printf("[PID] Kp updated %.6f→%.6f\n", originalKp, pid.Kp);
        } else {
          if (debugSerial) Serial.printf("[PID] Kp unchanged (%.6f)\n", pid.Kp);
        }
      } else {
        errors += "Kp out of range (0-100); ";
      }
    }
    if (req->hasParam("ki")) {
      float ki = req->getParam("ki")->value().toFloat();
      if (ki >= 0 && ki <= 50) {
        if (abs(pid.Ki - ki) > 0.000001) { // Only update if actually different
          pid.Ki = ki;
          pidTuningsChanged = true;
          updated = true;
          if (debugSerial) Serial.printf("[PID] Ki updated %.6f→%.6f\n", originalKi, pid.Ki);
        } else {
          if (debugSerial) Serial.printf("[PID] Ki unchanged (%.6f)\n", pid.Ki);
        }
      } else {
        errors += "Ki out of range (0-50); ";
      }
    }
    if (req->hasParam("kd")) {
      float kd = req->getParam("kd")->value().toFloat();
      if (kd >= 0 && kd <= 100) {
        if (abs(pid.Kd - kd) > 0.000001) { // Only update if actually different
          pid.Kd = kd;
          pidTuningsChanged = true;
          updated = true;
          if (debugSerial) Serial.printf("[PID] Kd updated %.6f→%.6f\n", originalKd, pid.Kd);
        } else {
          if (debugSerial) Serial.printf("[PID] Kd unchanged (%.6f)\n", pid.Kd);
        }
      } else {
        errors += "Kd out of range (0-100); ";
      }
    }
    
    // Only call SetTunings if PID parameters actually changed
    if (pidTuningsChanged) {
      if (pid.controller) pid.controller->SetTunings(pid.Kp, pid.Ki, pid.Kd);
      if (debugSerial) Serial.printf("[PID] Tunings applied: Kp=%.6f, Ki=%.6f, Kd=%.6f\n", pid.Kp, pid.Ki, pid.Kd);
    }
    
    if (req->hasParam("sample_time")) {
      int sampleTime = req->getParam("sample_time")->value().toInt();
      if (sampleTime >= 100 && sampleTime <= 120000) {  // Allow up to 2 minutes (120 seconds)
        if (pid.sampleTime != (unsigned long)sampleTime) { // Only update if different
          pid.sampleTime = (unsigned long)sampleTime;
          if (pid.controller) pid.controller->SetSampleTime(pid.sampleTime);
          updated = true;
          if (debugSerial) Serial.printf("[PID] Sample time updated %lums→%lums\n", originalSampleTime, pid.sampleTime);
        } else {
          if (debugSerial) Serial.printf("[PID] Sample time unchanged (%lums)\n", pid.sampleTime);
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
        int oldTempSamples = tempAvg.tempSampleCount;
        tempAvg.tempSampleCount = newTempSamples;
        updated = true;
        
        // Intelligent reset logic - only reset if necessary
        if (newTempSamples != oldTempSamples) {
          if (newTempSamples < oldTempSamples) {
            // Reducing sample count - keep existing data if we have enough
            if (tempAvg.tempSamplesReady && tempAvg.tempSampleIndex >= newTempSamples) {
              // We have enough samples, just adjust the count
              if (debugSerial) Serial.printf("[TEMP] Sample count reduced %d→%d, keeping existing data\n", oldTempSamples, newTempSamples);
            } else {
              // Not enough samples yet, reset to rebuild with new count
              tempAvg.tempSamplesReady = false;
              tempAvg.tempSampleIndex = 0;
              if (debugSerial) Serial.printf("[TEMP] Sample count reduced %d→%d, reset required\n", oldTempSamples, newTempSamples);
            }
          } else {
            // Increasing sample count - keep existing data, just need more samples
            if (debugSerial) Serial.printf("[TEMP] Sample count increased %d→%d, keeping existing data\n", oldTempSamples, newTempSamples);
            tempAvg.tempSamplesReady = false; // Need to fill the larger array
          }
        } else {
          if (debugSerial) Serial.printf("[TEMP] Sample count unchanged (%d), preserving data\n", tempAvg.tempSampleCount);
        }
      } else {
        errors += String("Temp samples out of range (5-") + MAX_TEMP_SAMPLES + "); ";
      }
    }
    if (req->hasParam("temp_reject")) {
      int newTempReject = req->getParam("temp_reject")->value().toInt();
      if (newTempReject >= 0 && newTempReject <= 10) {
        // Ensure we have at least 3 samples after rejection
        int effectiveSamples = tempAvg.tempSampleCount - (2 * newTempReject);
        if (effectiveSamples >= 3) {
          int oldTempReject = tempAvg.tempRejectCount;
          tempAvg.tempRejectCount = newTempReject;
          updated = true;
          
          // Intelligent reset - only if reject count actually changed
          if (newTempReject != oldTempReject) {
            if (debugSerial) Serial.printf("[TEMP] Reject count changed %d→%d, data preserved\n", oldTempReject, newTempReject);
            // No reset needed - rejection is applied during calculation, not storage
          } else {
            if (debugSerial) Serial.printf("[TEMP] Reject count unchanged (%d), data preserved\n", tempAvg.tempRejectCount);
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
        unsigned long oldTempInterval = tempAvg.tempSampleInterval;
        tempAvg.tempSampleInterval = (unsigned long)newTempInterval;
        updated = true;
        
        // Intelligent reset - timing change affects sample collection rhythm
        if (newTempInterval != oldTempInterval) {
          if (debugSerial) Serial.printf("[TEMP] Sample interval changed %lums→%lums, preserving existing data\n", oldTempInterval, tempAvg.tempSampleInterval);
          // Keep existing data - new interval will apply to future samples
          // No reset needed unless we want to maintain exact timing consistency
        } else {
          if (debugSerial) Serial.printf("[TEMP] Sample interval unchanged (%lums), data preserved\n", tempAvg.tempSampleInterval);
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
    response->printf("\"kp\":%.6f,", pid.Kp);
    response->printf("\"ki\":%.6f,", pid.Ki);
    response->printf("\"kd\":%.6f,", pid.Kd);
    response->printf("\"sample_time_ms\":%lu,", pid.sampleTime);
    response->printf("\"window_size_ms\":%lu,", windowSize);
      response->printf("\"temp_sample_count\":%d,", tempAvg.tempSampleCount);
      response->printf("\"temp_reject_count\":%d,", tempAvg.tempRejectCount);
      response->printf("\"temp_sample_interval_ms\":%lu,", tempAvg.tempSampleInterval);
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
      if (tempAvg.tempSamplesReady && !tempStructuralChange) {
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
  
}

void pidProfileEndpoints(AsyncWebServer& server)
{
  // --- PID profile management endpoints ---
  server.on("/api/pid_profiles", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{\"profiles\":[");
    // List all available profiles
    for (size_t i = 0; i < pid.profiles.size(); ++i) {
      if (i > 0) response->print(",");
      response->printf("{\"id\":%d,\"name\":\"%s\",\"minTemp\":%.1f,\"maxTemp\":%.1f}", 
                       (int)i, pid.profiles[i].name.c_str(), pid.profiles[i].minTemp, pid.profiles[i].maxTemp);
    }
    response->printf("],\"activeProfile\":\"%s\",\"autoSwitching\":%s}", 
                     pid.activeProfile.c_str(), pid.autoSwitching ? "true" : "false");
    req->send(response);
  });
  
  server.on("/api/pid_profile", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!req->hasParam("id")) {
      req->send(400, "application/json", "{\"error\":\"Missing id parameter\"}");
      return;
    }
    int id = req->getParam("id")->value().toInt();
    if (id < 0 || id >= (int)pid.profiles.size()) {
      req->send(404, "application/json", "{\"error\":\"Profile not found\"}");
      return;
    }
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->printf("\"id\":%d,", id);
    response->printf("\"name\":\"%s\",", pid.profiles[id].name.c_str());
    response->printf("\"minTemp\":%.1f,", pid.profiles[id].minTemp);
    response->printf("\"maxTemp\":%.1f,", pid.profiles[id].maxTemp);
    response->print("\"params\":{");
    response->printf("\"kp\":%.6f,", pid.profiles[id].kp);
    response->printf("\"ki\":%.6f,", pid.profiles[id].ki);
    response->printf("\"kd\":%.6f,", pid.profiles[id].kd);
    response->printf("\"windowMs\":%lu", pid.profiles[id].windowMs);
    response->print("},");
    response->printf("\"description\":\"%s\",", pid.profiles[id].description.c_str());
    response->printf("\"isActive\":%s", (pid.profiles[id].name == pid.activeProfile) ? "true" : "false");
    response->print("}");
    req->send(response);
  });
  
  // Switch to a specific profile
  server.on("/api/pid_profile/switch", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      DynamicJsonDocument doc(128);
      DeserializationError error = deserializeJson(doc, data, len);
      if (error) { 
        req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); 
        return; 
      }
      
      if (!doc.containsKey("name")) {
        req->send(400, "application/json", "{\"error\":\"Missing name field\"}");
        return;
      }
      
      String profileName = doc["name"];
      
      // Find and switch to the profile
      bool found = false;
      for (const PIDProfile& profile : pid.profiles) {
        if (profile.name == profileName) {
          switchToProfile(profileName);
          found = true;
          break;
        }
      }
      
      if (!found) {
        req->send(404, "application/json", "{\"error\":\"Profile not found\"}");
        return;
      }
      
      req->send(200, "application/json", "{\"status\":\"Profile switched\"}");
    }
  );
  
  // Enable/disable auto-switching
  server.on("/api/pid_profile/auto_switch", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      DynamicJsonDocument doc(128);
      DeserializationError error = deserializeJson(doc, data, len);
      if (error) { 
        req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); 
        return; 
      }
      
      if (doc.containsKey("enabled")) {
        pid.autoSwitching = doc["enabled"];
        saveSettings();
        
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        response->print("{");
        response->printf("\"autoSwitching\":%s,", pid.autoSwitching ? "true" : "false");
        response->print("\"status\":\"Auto-switching updated\"");
        response->print("}");
        req->send(response);
      } else {
        req->send(400, "application/json", "{\"error\":\"Missing enabled field\"}");
      }
    }
  );
}

// Programs management endpoints - bridge between programs editor and hybrid system
void programsEndpoints(AsyncWebServer& server) {
  // Legacy programs.json endpoint - synthesizes a single JSON from all program files
  server.on("/programs.json", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("[");
    
    bool first = true;
    for (const auto& meta : programMetadata) {
      if (!first) response->print(",");
      first = false;
      
      // Load the specific program data
      String programFileName = "/programs/program_" + String(meta.id) + ".json";
      File f = LittleFS.open(programFileName, "r");
      if (f && f.size() > 0) {
        // Stream the program JSON directly
        while (f.available()) {
          response->write(f.read());
        }
        f.close();
      } else {
        // Fallback: create minimal program structure from metadata
        response->printf("{\"id\":%d,\"name\":\"%s\",\"notes\":\"%s\",\"icon\":\"%s\",\"fermentBaselineTemp\":%.1f,\"fermentQ10\":%.1f,\"customStages\":[]}",
                        meta.id, meta.name.c_str(), meta.notes.c_str(), meta.icon.c_str(), meta.fermentBaselineTemp, meta.fermentQ10);
      }
    }
    
    response->print("]");
    req->send(response);
  });
  
  // Save programs.json - splits into individual program files and updates index
  server.on("/programs.json", HTTP_POST, [](AsyncWebServerRequest* req){
    req->send(200, "application/json", "{\"status\":\"saved\"}");
  }, NULL, [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
    DynamicJsonDocument doc(32768); // 32KB buffer for all programs
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
      req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    
    // Clear existing program files
    for (const auto& meta : programMetadata) {
      String programFileName = "/programs/program_" + String(meta.id) + ".json";
      LittleFS.remove(programFileName);
    }
    
    // Clear metadata
    programMetadata.clear();
    
    // Process each program in the array
    JsonArray programsArray = doc.as<JsonArray>();
    for (JsonObject prog : programsArray) {
      int id = prog["id"];
      String name = prog["name"].as<String>();
      
      // Create individual program file
      String programFileName = "/programs/program_" + String(id) + ".json";
      File f = LittleFS.open(programFileName, "w");
      if (f) {
        serializeJson(prog, f);
        f.close();
      }
      
      // Update metadata
      ProgramMetadata meta;
      meta.id = id;
      meta.name = name;
      meta.notes = prog["notes"] | String("");
      meta.icon = prog["icon"] | String("");
      meta.fermentBaselineTemp = prog["fermentBaselineTemp"] | 20.0f;
      meta.fermentQ10 = prog["fermentQ10"] | 2.0f;
      
      // Count stages
      if (prog.containsKey("customStages")) {
        meta.stageCount = prog["customStages"].size();
      }
      
      programMetadata.push_back(meta);
    }
    
    // Update programs index
    File indexFile = LittleFS.open("/programs_index.json", "w");
    if (indexFile) {
      indexFile.print("[");
      for (size_t i = 0; i < programMetadata.size(); ++i) {
        if (i > 0) indexFile.print(",");
        indexFile.printf("{\"id\":%d,\"name\":\"%s\",\"notes\":\"%s\",\"icon\":\"%s\",\"fermentBaselineTemp\":%.1f,\"fermentQ10\":%.1f}",
                        programMetadata[i].id, programMetadata[i].name.c_str(), programMetadata[i].notes.c_str(), 
                        programMetadata[i].icon.c_str(), programMetadata[i].fermentBaselineTemp, programMetadata[i].fermentQ10);
      }
      indexFile.print("]");
      indexFile.close();
    }
    
    Serial.printf("[PROGRAMS] Saved %zu programs to individual files\n", programMetadata.size());
  });
  
  // Get individual program by ID
  server.on("/api/programs", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!req->hasParam("id")) {
      // Return programs list (metadata only)
      AsyncResponseStream *response = req->beginResponseStream("application/json");
      response->print("[");
      for (size_t i = 0; i < programMetadata.size(); ++i) {
        if (i > 0) response->print(",");
        response->printf("{\"id\":%d,\"name\":\"%s\",\"notes\":\"%s\",\"icon\":\"%s\",\"fermentBaselineTemp\":%.1f,\"fermentQ10\":%.1f,\"stageCount\":%zu}",
                        programMetadata[i].id, programMetadata[i].name.c_str(), programMetadata[i].notes.c_str(), 
                        programMetadata[i].icon.c_str(), programMetadata[i].fermentBaselineTemp, programMetadata[i].fermentQ10, programMetadata[i].stageCount);
      }
      response->print("]");
      req->send(response);
    } else {
      // Return specific program
      int id = req->getParam("id")->value().toInt();
      String programFileName = "/programs/program_" + String(id) + ".json";
      req->send(LittleFS, programFileName, "application/json");
    }
  });
  
  // Save individual program
  server.on("/api/programs", HTTP_POST, [](AsyncWebServerRequest* req){
    req->send(200, "application/json", "{\"status\":\"saved\"}");
  }, NULL, [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
    DynamicJsonDocument doc(8192); // 8KB buffer for single program
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
      req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    
    int id = doc["id"];
    String name = doc["name"].as<String>();
    
    // Save individual program file
    String programFileName = "/programs/program_" + String(id) + ".json";
    File f = LittleFS.open(programFileName, "w");
    if (f) {
      serializeJson(doc, f);
      f.close();
    }
    
    // Update metadata
    bool found = false;
    for (auto& meta : programMetadata) {
      if (meta.id == id) {
        meta.name = name;
        meta.notes = doc["notes"] | String("");
        meta.icon = doc["icon"] | String("");
        meta.fermentBaselineTemp = doc["fermentBaselineTemp"] | 20.0f;
        meta.fermentQ10 = doc["fermentQ10"] | 2.0f;
        if (doc.containsKey("customStages")) {
          meta.stageCount = doc["customStages"].size();
        }
        found = true;
        break;
      }
    }
    
    if (!found) {
      // Add new program metadata
      ProgramMetadata meta;
      meta.id = id;
      meta.name = name;
      meta.notes = doc["notes"] | String("");
      meta.icon = doc["icon"] | String("");
      meta.fermentBaselineTemp = doc["fermentBaselineTemp"] | 20.0f;
      meta.fermentQ10 = doc["fermentQ10"] | 2.0f;
      if (doc.containsKey("customStages")) {
        meta.stageCount = doc["customStages"].size();
      }
      programMetadata.push_back(meta);
    }
    
    // Update programs index
    File indexFile = LittleFS.open("/programs_index.json", "w");
    if (indexFile) {
      indexFile.print("[");
      for (size_t i = 0; i < programMetadata.size(); ++i) {
        if (i > 0) indexFile.print(",");
        indexFile.printf("{\"id\":%d,\"name\":\"%s\",\"notes\":\"%s\",\"icon\":\"%s\",\"fermentBaselineTemp\":%.1f,\"fermentQ10\":%.1f}",
                        programMetadata[i].id, programMetadata[i].name.c_str(), programMetadata[i].notes.c_str(), 
                        programMetadata[i].icon.c_str(), programMetadata[i].fermentBaselineTemp, programMetadata[i].fermentQ10);
      }
      indexFile.print("]");
      indexFile.close();
    }
    
    Serial.printf("[PROGRAMS] Saved program %d: %s\n", id, name.c_str());
  });
  
  // Delete program
  server.on("/api/programs", HTTP_DELETE, [](AsyncWebServerRequest* req){
    if (!req->hasParam("id")) {
      req->send(400, "application/json", "{\"error\":\"Missing id parameter\"}");
      return;
    }
    
    int id = req->getParam("id")->value().toInt();
    
    // Delete program file
    String programFileName = "/programs/program_" + String(id) + ".json";
    if (LittleFS.remove(programFileName)) {
      // Remove from metadata
      for (auto it = programMetadata.begin(); it != programMetadata.end(); ++it) {
        if (it->id == id) {
          programMetadata.erase(it);
          break;
        }
      }
      
      // Update programs index
      File indexFile = LittleFS.open("/programs_index.json", "w");
      if (indexFile) {
        indexFile.print("[");
        for (size_t i = 0; i < programMetadata.size(); ++i) {
          if (i > 0) indexFile.print(",");
          indexFile.printf("{\"id\":%d,\"name\":\"%s\",\"notes\":\"%s\",\"icon\":\"%s\",\"fermentBaselineTemp\":%.1f,\"fermentQ10\":%.1f}",
                          programMetadata[i].id, programMetadata[i].name.c_str(), programMetadata[i].notes.c_str(), 
                          programMetadata[i].icon.c_str(), programMetadata[i].fermentBaselineTemp, programMetadata[i].fermentQ10);
        }
        indexFile.print("]");
        indexFile.close();
      }
      
      Serial.printf("[PROGRAMS] Deleted program %d\n", id);
      req->send(200, "application/json", "{\"status\":\"deleted\"}");
    } else {
      req->send(404, "application/json", "{\"error\":\"Program not found\"}");
    }
  });
}

// OTA endpoints for web interface management
void otaEndpoints(AsyncWebServer& server) {
  // OTA status endpoint
  server.on("/api/ota/status", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->printf("\"enabled\":%s,", otaStatus.enabled ? "true" : "false");
    response->printf("\"inProgress\":%s,", otaStatus.inProgress ? "true" : "false");
    response->printf("\"progress\":%d,", otaStatus.progress);
    response->printf("\"hostname\":\"%s\",", otaStatus.hostname.c_str());
    if (otaStatus.error.length() > 0) {
      response->printf("\"error\":\"%s\",", otaStatus.error.c_str());
    } else {
      response->print("\"error\":null,");
    }
    response->printf("\"wifiConnected\":%s", WiFi.status() == WL_CONNECTED ? "true" : "false");
    response->print("}");
    req->send(response);
  });
  
  // Enable/disable OTA
  server.on("/api/ota/enable", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, data, len)) { 
        req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); 
        return; 
      }
      
      if (doc.containsKey("enabled")) {
        enableOTA(doc["enabled"]);
        saveSettings();
        req->send(200, "application/json", "{\"status\":\"OTA setting updated\"}");
      } else {
        req->send(400, "application/json", "{\"error\":\"Missing enabled field\"}");
      }
    }
  );
  
  // Set OTA password
  server.on("/api/ota/password", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, data, len)) { 
        req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); 
        return; 
      }
      
      if (doc.containsKey("password")) {
        String password = doc["password"].as<String>();
        if (password.length() >= 8) {
          setOTAPassword(password);
          saveSettings();
          req->send(200, "application/json", "{\"status\":\"OTA password updated\"}");
        } else {
          req->send(400, "application/json", "{\"error\":\"Password must be at least 8 characters\"}");
        }
      } else {
        req->send(400, "application/json", "{\"error\":\"Missing password field\"}");
      }
    }
  );
  
  // Set OTA hostname
  server.on("/api/ota/hostname", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, data, len)) { 
        req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); 
        return; 
      }
      
      if (doc.containsKey("hostname")) {
        String hostname = doc["hostname"].as<String>();
        if (hostname.length() > 0 && hostname.length() <= 32) {
          setOTAHostname(hostname);
          saveSettings();
          req->send(200, "application/json", "{\"status\":\"OTA hostname updated\"}");
        } else {
          req->send(400, "application/json", "{\"error\":\"Hostname must be 1-32 characters\"}");
        }
      } else {
        req->send(400, "application/json", "{\"error\":\"Missing hostname field\"}");
      }
    }
  );
  
  // OTA information endpoint
  server.on("/api/ota/info", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->printf("\"instructions\":\"Use Arduino IDE or PlatformIO to upload firmware via OTA\",");
    response->printf("\"hostname\":\"%s\",", otaStatus.hostname.c_str());
    response->printf("\"ip\":\"%s\",", WiFi.localIP().toString().c_str());
    response->printf("\"port\":3232,");
    response->print("\"requirements\":\"Password required for uploads\",");
    response->print("\"compatible\":\"Arduino IDE 1.8.13+ or PlatformIO\"");
    response->print("}");
    req->send(response);
  });
}

// Function to update performance metrics (should be called from main loop)
void updatePerformanceMetrics() {
  unsigned long currentTime = micros();
  if (lastLoopTime > 0) {
    unsigned long loopDuration = currentTime - lastLoopTime;
    totalLoopTime += loopDuration;
    if (loopDuration > maxLoopTime) {
      maxLoopTime = loopDuration;
    }
  }
  lastLoopTime = currentTime;
  loopCount++;
  
  // Track minimum free heap
  uint32_t currentHeap = ESP.getFreeHeap();
  if (currentHeap < minFreeHeap) {
    minFreeHeap = currentHeap;
  }
  
  // Track WiFi reconnections
  unsigned long currentWifiStatus = WiFi.status();
  if (lastWifiStatus != WL_CONNECTED && currentWifiStatus == WL_CONNECTED) {
    wifiReconnectCount++;
  }
  lastWifiStatus = currentWifiStatus;
}
