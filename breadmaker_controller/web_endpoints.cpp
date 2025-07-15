#include "web_endpoints.h"
#include <Arduino.h>
#include "globals.h"
#include <ArduinoJson.h>
#include "programs_manager.h"
#include "outputs_manager.h"
#include <PID_v1.h>
#include <ESP8266WiFi.h>
#include "calibration.h"
#include <LittleFS.h>
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
      
      // Check if program ID exists in metadata
      bool programExists = false;
      for (const auto& meta : programMetadata) {
        if (meta.id == id) {
          programExists = true;
          break;
        }
      }
      
      if (programExists) {
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
          req->send(500, "text/plain", "Failed to load program");
          return;
        }
      }
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

// --- Streaming status JSON function ---
void streamStatusJson(Print& out) {
  out.print("{");
  // scheduledStart
  out.printf("\"scheduledStart\":%lu,", (unsigned long)scheduledStart);
  // running
  out.printf("\"running\":%s,", programState.isRunning ? "true" : "false");
  // program
  if (programState.activeProgramId < getProgramCount()) {
    out.printf("\"program\":\"%s\",", getProgramName(programState.activeProgramId).c_str());
    out.printf("\"programId\":%u,", (unsigned)programState.activeProgramId);
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
  if (programState.activeProgramId < getProgramCount() && isProgramValid(programState.activeProgramId)) {
    Program *p = getActiveProgramMutable();
    if (p) {
    
    // Ensure the program stages are loaded for preview calculations
    if (p->customStages.empty()) {
      Serial.println("DEBUG: Program stages empty, loading program " + String(programState.activeProgramId));
      ensureProgramLoaded(programState.activeProgramId);
    } else {
      Serial.println("DEBUG: Program " + String(programState.activeProgramId) + " has " + String(p->customStages.size()) + " stages");
    }
    
    // Determine preview or running mode
    bool previewMode = !programState.isRunning;
    // Stage label
    if (programState.isRunning && programState.customStageIdx < p->customStages.size()) {
      out.print("\"stage\":\"");
      out.print(p->customStages[programState.customStageIdx].label.c_str());
      out.print("\",");
    } else {
      out.print("\"stage\":\"Idle\",");
    }
    // --- Predicted (temperature-adjusted) stage end times ---
    out.print("\"predictedStageEndTimes\":[");
    float actualTemp = getAveragedTemperature();
    float baseline = p->fermentBaselineTemp > 0 ? p->fermentBaselineTemp : 20.0;
    float q10 = p->fermentQ10 > 0 ? p->fermentQ10 : 2.0;
    float fermentationFactorForProgram = pow(q10, (baseline - actualTemp) / 10.0);
    // Use programStartTime if running, otherwise use now as preview start
    unsigned long programStartUnix = (unsigned long)(programState.isRunning ? programState.programStartTime : time(nullptr));
    double cumulativePredictedSec = 0.0;
    for (size_t i = 0; i < p->customStages.size(); ++i) {
      CustomStage &stage = p->customStages[i];
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
    fermentState.fermentationFactor = fermentationFactorForProgram;
    // --- Predicted (temperature-adjusted) program end time ---
    out.printf("\"predictedProgramEnd\":%lu,", (unsigned long)(programStartUnix + (unsigned long)(cumulativePredictedSec)));
    // --- Legacy: stageStartTimes array (now fermentation-adjusted for consistency) ---
    out.print("\"stageStartTimes\":[");
    time_t t = programState.isRunning ? programState.programStartTime : time(nullptr);
    for (size_t i = 0; i < p->customStages.size(); ++i) {
      if (i > 0) out.print(",");
      out.printf("%lu", (unsigned long)t);
      CustomStage &stage = p->customStages[i];
      double plannedStageSec = (double)stage.min * 60.0;
      double adjustedStageSec = plannedStageSec;
      if (stage.isFermentation) {
        adjustedStageSec = plannedStageSec * fermentationFactorForProgram;
      }
      t += (time_t)adjustedStageSec;
    }
    out.print("],");
    out.printf("\"programStart\":%lu,", (unsigned long)programStartUnix);
    // elapsed
    es = (programState.customStageStart == 0) ? 0 : (nowMs - programState.customStageStart) / 1000;
    // Calculate stageReadyAt and programReadyAt (legacy, not adjusted)
    stageReadyAt = 0;
    programReadyAt = 0;
    if (programState.customStageIdx < p->customStages.size()) {
      CustomStage &st = p->customStages[programState.customStageIdx];
      if (st.isFermentation) {
        // Calculate temperature-adjusted time left for fermentation stage
        double plannedStageSec = (double)st.min * 60.0;
        double elapsedSec = fermentState.fermentWeightedSec;
        double realElapsedSec = (nowMs - fermentState.fermentLastUpdateMs) / 1000.0;
        elapsedSec += realElapsedSec * fermentState.fermentLastFactor;
        double remainSec = plannedStageSec - (elapsedSec / fermentState.fermentLastFactor);
        if (remainSec < 0) remainSec = 0;
        fermentationStageTimeLeft = remainSec * fermentState.fermentLastFactor;
        stageLeft = (int)fermentationStageTimeLeft;
      } else {
        if (programState.actualStageStartTimes[programState.customStageIdx] > 0) {
          // Running mode - apply fermentation factor to current stage
          double plannedStageSec = (double)st.min * 60.0;
          double adjustedStageSec = plannedStageSec;
          if (st.isFermentation) {
            adjustedStageSec = plannedStageSec * fermentationFactorForProgram;
          }
          stageReadyAt = programState.actualStageStartTimes[programState.customStageIdx] + (time_t)adjustedStageSec;
        } else {
          // Preview mode - calculate fermentation-adjusted stage ready time
          time_t previewStart = programState.isRunning ? programState.programStartTime : time(nullptr);
          time_t cumulativeTime = previewStart;
          for (size_t i = 0; i <= programState.customStageIdx && i < p->customStages.size(); ++i) {
            CustomStage &stage = p->customStages[i];
            double plannedStageSec = (double)stage.min * 60.0;
            double adjustedStageSec = plannedStageSec;
            if (stage.isFermentation) {
              adjustedStageSec = plannedStageSec * fermentationFactorForProgram;
            }
            cumulativeTime += (time_t)adjustedStageSec;
          }
          stageReadyAt = cumulativeTime;
        }
        
        if (programState.actualStageStartTimes[programState.customStageIdx] > 0) {
          unsigned long actualElapsedInStage = (millis() - programState.customStageStart) / 1000UL;
          unsigned long plannedStageDuration = (unsigned long)p->customStages[programState.customStageIdx].min * 60UL;
          unsigned long timeLeftInCurrentStage = (actualElapsedInStage < plannedStageDuration) ? (plannedStageDuration - actualElapsedInStage) : 0;
          stageLeft = (int)timeLeftInCurrentStage;
          programReadyAt = now + (time_t)timeLeftInCurrentStage;
          for (size_t i = programState.customStageIdx + 1; i < p->customStages.size(); ++i) {
            CustomStage &stage = p->customStages[i];
            double plannedStageSec = (double)stage.min * 60.0;
            double adjustedStageSec = plannedStageSec;
            if (stage.isFermentation) {
              adjustedStageSec = plannedStageSec * fermentationFactorForProgram;
            }
            programReadyAt += (time_t)adjustedStageSec;
          }
        } else {
          // Preview mode or start of program - use fermentation-adjusted times
          programReadyAt = programState.isRunning ? programState.programStartTime : time(nullptr);
          for (size_t i = 0; i < p->customStages.size(); ++i) {
            CustomStage &stage = p->customStages[i];
            double plannedStageSec = (double)stage.min * 60.0;
            double adjustedStageSec = plannedStageSec;
            if (stage.isFermentation) {
              adjustedStageSec = plannedStageSec * fermentationFactorForProgram;
            }
            programReadyAt += (time_t)adjustedStageSec;
          }
          
          // For preview mode, calculate stage time left
          if (!programState.isRunning && programState.customStageIdx < p->customStages.size()) {
            CustomStage &currentStage = p->customStages[programState.customStageIdx];
            double plannedStageSec = (double)currentStage.min * 60.0;
            double adjustedStageSec = plannedStageSec;
            if (currentStage.isFermentation) {
              adjustedStageSec = plannedStageSec * fermentationFactorForProgram;
            }
            stageLeft = (int)adjustedStageSec;
          }
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
  out.printf("\"setTemp\":%.2f,", pid.Setpoint);
  out.printf("\"timeLeft\":%d,", stageLeft);
  out.printf("\"stageReadyAt\":%lu,", (unsigned long)stageReadyAt);
  out.printf("\"programReadyAt\":%lu,", (unsigned long)programReadyAt);
  out.printf("\"temp\":%.2f,", tempAvg.averagedTemperature);
  out.printf("\"motor\":%s,", motorState ? "true" : "false");
  out.printf("\"light\":%s,", lightState ? "true" : "false");
  out.printf("\"buzzer\":%s,", buzzerState ? "true" : "false");
  out.printf("\"stageStartTime\":%lu,", (unsigned long)programState.customStageStart);
  out.printf("\"manualMode\":%s,", programState.manualMode ? "true" : "false");
  // PID status
  out.printf("\"input\":%.2f,", pid.Input);
  out.printf("\"output\":%.2f,", pid.Output * 100.0);
  out.printf("\"pid_output\":%.6f,", pid.Output);
  out.printf("\"kp\":%.6f,", pid.Kp);
  out.printf("\"ki\":%.6f,", pid.Ki);
  out.printf("\"kd\":%.6f,", pid.Kd);
  out.printf("\"sample_time_ms\":%lu,", pid.sampleTime);
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
    out.printf("%lu", (unsigned long)programState.actualStageStartTimes[i]);
  }
  out.print("],");
  // stageIdx, mixIdx, heater, buzzer
  out.printf("\"stageIdx\":%u,", (unsigned)programState.customStageIdx);
  out.printf("\"mixIdx\":%u,", (unsigned)programState.customMixIdx);
  out.printf("\"heater\":%s,", heaterState ? "true" : "false");
  out.printf("\"buzzer\":%s,", buzzerState ? "true" : "false");
  // fermentation info
  out.printf("\"fermentationFactor\":%.2f,", fermentState.fermentationFactor);
  out.printf("\"predictedCompleteTime\":%lu,", (unsigned long)fermentState.predictedCompleteTime);
  // Also update programReadyAt to match predictedCompleteTime if running
  if (programState.isRunning) {
    out.printf("\"programReadyAt\":%lu,", (unsigned long)fermentState.predictedCompleteTime);
  }
  // --- WiFi signal strength ---
  out.printf("\"wifiRssi\":%d,", WiFi.RSSI());
  out.printf("\"wifiConnected\":%s,", (WiFi.status() == WL_CONNECTED) ? "true" : "false");
  out.printf("\"initialFermentTemp\":%.2f", fermentState.initialFermentTemp);
  out.print("}");
}

void homeAssistantEndpoint(AsyncWebServer& server)
{
  // --- Home Assistant integration endpoint (matches template configuration.yaml) ---
  server.on("/ha", HTTP_GET, [](AsyncWebServerRequest* req){
    unsigned long now = millis();
    
    // Check if we can use cached values
    if (now - lastHaUpdate < HA_CACHE_MS) {
      // Return cached basic state values without expensive calculations
      AsyncResponseStream *response = req->beginResponseStream("application/json");
      response->print("{");
      response->printf("\"state\":\"%s\",", programState.isRunning ? "running" : "idle");
      response->printf("\"temperature\":%.1f,", tempAvg.averagedTemperature);
      response->printf("\"setpoint\":%.1f,", pid.Setpoint);
      response->printf("\"motor\":%s,", outputStates.motor ? "true" : "false");
      response->printf("\"light\":%s,", outputStates.light ? "true" : "false");
      response->printf("\"buzzer\":%s,", outputStates.buzzer ? "true" : "false");
      response->printf("\"heater\":%s,", outputStates.heater ? "true" : "false");
      response->printf("\"manual_mode\":%s,", programState.manualMode ? "true" : "false");
      response->printf("\"cached\":true");
      response->print("}");
      req->send(response);
      return;
    }
    
    // Update cache timestamp and return full detailed response
    lastHaUpdate = now;
    
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    
    // Basic status - matches template expectations
    response->printf("\"state\":\"%s\",", programState.isRunning ? "running" : "idle");
    response->printf("\"temperature\":%.1f,", tempAvg.averagedTemperature);
    response->printf("\"setpoint\":%.1f,", pid.Setpoint);
    
    // Output states (direct boolean values as expected)
    response->printf("\"motor\":%s,", outputStates.motor ? "true" : "false");
    response->printf("\"light\":%s,", outputStates.light ? "true" : "false");
    response->printf("\"buzzer\":%s,", outputStates.buzzer ? "true" : "false");
    response->printf("\"heater\":%s,", outputStates.heater ? "true" : "false");
    response->printf("\"manual_mode\":%s,", programState.manualMode ? "true" : "false");
    
    // Program and stage info
    if (programState.activeProgramId < getProgramCount()) {
      response->printf("\"program\":\"%s\",", getProgramName(programState.activeProgramId).c_str());
      
      Program *p = getActiveProgramMutable();
      if (programState.isRunning && p && programState.customStageIdx < p->customStages.size()) {
        response->printf("\"stage\":\"%s\",", p->customStages[programState.customStageIdx].label.c_str());
        
        // Calculate stage time left in minutes (as expected by template)
        unsigned long elapsed = (programState.customStageStart == 0) ? 0 : (millis() - programState.customStageStart) / 1000;
        int stageTimeMin = p->customStages[programState.customStageIdx].min;
        int timeLeftMin = stageTimeMin - (elapsed / 60);
        if (timeLeftMin < 0) timeLeftMin = 0;
        response->printf("\"stage_time_left\":%d,", timeLeftMin);
      } else {
        response->print("\"stage\":\"Idle\",");
        response->print("\"stage_time_left\":0,");
      }
    } else {
      response->print("\"program\":\"\",");
      response->print("\"stage\":\"Idle\",");
      response->print("\"stage_time_left\":0,");
    }
    
    // Timing information (Unix timestamps as expected)
    time_t currentTime = time(nullptr);
    time_t stageReadyAt = 0;
    time_t programReadyAt = 0;
    
    if (programState.isRunning && programState.activeProgramId < getProgramCount()) {
      Program *p = getActiveProgramMutable();
      if (p && programState.customStageIdx < p->customStages.size()) {
        unsigned long elapsed = (programState.customStageStart == 0) ? 0 : (millis() - programState.customStageStart) / 1000;
        int stageTimeMin = p->customStages[programState.customStageIdx].min;
        int timeLeftSec = (stageTimeMin * 60) - elapsed;
        if (timeLeftSec < 0) timeLeftSec = 0;
        
        stageReadyAt = currentTime + timeLeftSec;
        
        // Calculate total program time remaining
        programReadyAt = stageReadyAt;
        for (size_t i = programState.customStageIdx + 1; i < p->customStages.size(); ++i) {
          programReadyAt += p->customStages[i].min * 60;
        }
      }
    }
    
    response->printf("\"stage_ready_at\":%lu,", (unsigned long)stageReadyAt);
    response->printf("\"program_ready_at\":%lu,", (unsigned long)programReadyAt);
    
    // Health section (comprehensive system information as expected by template)
    response->print("\"health\":{");
    
    // System info
    response->printf("\"uptime_sec\":%lu,", millis() / 1000);
    response->printf("\"firmware_version\":\"%s\",", FIRMWARE_BUILD_DATE);
    response->printf("\"build_date\":\"%s\",", FIRMWARE_BUILD_DATE);
    response->printf("\"reset_reason\":\"%s\",", ESP.getResetReason().c_str());
    response->printf("\"chip_id\":\"%08X\",", ESP.getChipId());
    
    // Memory information
    response->print("\"memory\":{");
    response->printf("\"free_heap\":%u,", ESP.getFreeHeap());
    response->printf("\"max_free_block\":%u,", ESP.getMaxFreeBlockSize());
    response->printf("\"min_free_heap\":%lu,", minFreeHeap == 0xFFFFFFFF ? ESP.getFreeHeap() : minFreeHeap);
    response->printf("\"fragmentation\":%.1f", (ESP.getHeapFragmentation()));
    response->print("},");
    
    // Performance information (real-time tracking)
    response->print("\"performance\":{");
    
    // Calculate CPU usage estimate and average loop time
    float avgLoopTime = loopCount > 0 ? (float)totalLoopTime / loopCount : 0.0;
    float cpuUsage = avgLoopTime > 0 ? (avgLoopTime / 1000.0) * 100.0 : 0.0; // Rough CPU usage estimation
    if (cpuUsage > 100.0) cpuUsage = 100.0;
    
    response->printf("\"cpu_usage\":%.2f,", cpuUsage);
    response->printf("\"avg_loop_time_us\":%.0f,", avgLoopTime);
    response->printf("\"max_loop_time_us\":%lu,", maxLoopTime);
    response->printf("\"loop_count\":%lu", loopCount);
    response->print("},");
    
    // Network information
    response->print("\"network\":{");
    response->printf("\"connected\":%s,", (WiFi.status() == WL_CONNECTED) ? "true" : "false");
    response->printf("\"rssi\":%d,", WiFi.RSSI());
    response->print("\"reconnect_count\":");
    response->print(wifiReconnectCount);
    response->print(",");
    response->printf("\"ip\":\"%s\"", WiFi.localIP().toString().c_str());
    response->print("},");
    
    // Filesystem information
    response->print("\"filesystem\":{");
    FSInfo fsInfo;
    LittleFS.info(fsInfo);
    response->printf("\"usedBytes\":%u,", fsInfo.usedBytes);
    response->printf("\"totalBytes\":%u", fsInfo.totalBytes);
    response->print("}");
    
    response->print("}"); // Close health section
    response->print("}"); // Close main JSON
    req->send(response);
  });
}

// --- Calibration endpoints ---
void calibrationEndpoints(AsyncWebServer& server) {
  // Temperature calibration endpoints
  server.on("/api/calibration", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->printf("\"raw\":%d,", analogRead(A0));  // Current raw RTD reading
    response->printf("\"temp\":%.2f,", readTemperature());  // Current calibrated temperature
    response->print("\"table\":[");
    for (size_t i = 0; i < rtdCalibTable.size(); ++i) {
      if (i > 0) response->print(",");
      response->printf("{\"raw\":%d,\"temp\":%.2f}", rtdCalibTable[i].raw, rtdCalibTable[i].temp);
    }
    response->print("]}");
    req->send(response);
  });
  
  server.on("/api/calibration", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t *data, size_t len, size_t, size_t){
      DynamicJsonDocument doc(2048);
      if (deserializeJson(doc, data, len)) { 
        req->send(400, "application/json", "{\"error\":\"Bad JSON\"}"); 
        return; 
      }
      
      rtdCalibTable.clear();
      for (JsonObject pt : doc["table"].as<JsonArray>()) {
        CalibPoint p = { pt["raw"], pt["temp"] };
        rtdCalibTable.push_back(p);
      }
      saveCalibration();
      req->send(200, "application/json", "{\"status\":\"saved\"}");
    }
  );
}

// --- File management endpoints ---
void fileEndPoints(AsyncWebServer& server) {
  // Serve static files from LittleFS
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  
  // File upload endpoint
  server.on("/api/upload", HTTP_POST, [](AsyncWebServerRequest* req){
    req->send(200, "application/json", "{\"status\":\"upload_complete\"}");
  }, [](AsyncWebServerRequest* req, String filename, size_t index, uint8_t *data, size_t len, bool final){
    static File uploadFile;
    static String uploadPath;
    
    if (index == 0) {
      // Start of upload - create file
      String folder = "/";
      if (req->hasParam("folder", true)) {
        folder = req->getParam("folder", true)->value();
        if (!folder.startsWith("/")) folder = "/" + folder;
        if (!folder.endsWith("/")) folder += "/";
      }
      
      uploadPath = folder + filename;
      Serial.printf("Starting upload: %s\n", uploadPath.c_str());
      
      // Ensure directory structure exists by creating any missing parent directories
      String dir = uploadPath.substring(0, uploadPath.lastIndexOf('/'));
      if (dir.length() > 1) {
        // Create directory structure by creating a dummy file and removing it
        String dummyFile = dir + "/.dummy";
        File tempFile = LittleFS.open(dummyFile, "w");
        if (tempFile) {
          tempFile.close();
          LittleFS.remove(dummyFile);
        }
      }
      
      uploadFile = LittleFS.open(uploadPath, "w");
      if (!uploadFile) {
        Serial.printf("Failed to create file: %s\n", uploadPath.c_str());
        return;
      }
    }
    
    // Write data chunk
    if (len) {
      uploadFile.write(data, len);
    }
    
    if (final) {
      // End of upload - close file
      uploadFile.close();
      Serial.printf("Upload complete: %s (%d bytes)\n", uploadPath.c_str(), index + len);
    }
  });

  // Enhanced file listing with folder support
  server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest* req){
    String folder = "/";
    if (req->hasParam("folder")) {
      folder = req->getParam("folder")->value();
      if (!folder.startsWith("/")) folder = "/" + folder;
      if (!folder.endsWith("/")) folder += "/";
    }
    
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{\"folders\":[");
    
    // List directories first
    Dir dir = LittleFS.openDir(folder);
    bool firstFolder = true;
    while (dir.next()) {
      if (dir.isDirectory()) {
        if (!firstFolder) response->print(",");
        firstFolder = false;
        response->printf("\"%s\"", dir.fileName().c_str());
      }
    }
    
    response->print("],\"files\":[");
    
    // List files
    dir = LittleFS.openDir(folder);
    bool firstFile = true;
    while (dir.next()) {
      if (dir.isFile()) {
        if (!firstFile) response->print(",");
        firstFile = false;
        response->printf("{\"name\":\"%s\",\"size\":%u}", dir.fileName().c_str(), dir.fileSize());
      }
    }
    
    response->print("]}");
    req->send(response);
  });

  // File delete endpoint (enhanced for folder support)
  server.on("/api/delete", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, [](AsyncWebServerRequest* req, uint8_t *data, size_t len, size_t index, size_t total){
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, (char*)data);
    
    String filename = doc["filename"];
    String folder = doc["folder"] | "/";
    
    if (!folder.endsWith("/")) folder += "/";
          String fullPath = folder + filename;
    
    if (LittleFS.remove(fullPath)) {
      req->send(200, "application/json", "{\"status\":\"deleted\"}");
    } else {
      req->send(404, "application/json", "{\"error\":\"File not found\"}");
    }
  });

  // Create folder endpoint
  server.on("/api/create_folder", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, [](AsyncWebServerRequest* req, uint8_t *data, size_t len, size_t index, size_t total){
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, (char*)data);
    
    String parent = doc["parent"] | "/";
    String name = doc["name"];
    
    if (!parent.endsWith("/")) parent += "/";
    String fullPath = parent + name;
    
    
    // Create a dummy file to ensure the directory exists, then remove it
    // This is a workaround for ESP8266 LittleFS which doesn't have true directory support
    String dummyFile = fullPath + "/.dummy";
    File f = LittleFS.open(dummyFile, "w");
    if (f) {
      f.close();
      LittleFS.remove(dummyFile);
      req->send(200, "application/json", "{\"status\":\"created\"}");
    } else {
      req->send(500, "application/json", "{\"error\":\"Failed to create folder\"}");
    }
  });

  // Delete folder endpoint
  server.on("/api/delete_folder", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, [](AsyncWebServerRequest* req, uint8_t *data, size_t len, size_t index, size_t total){
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, (char*)data);
    
    String folder = doc["folder"] | "/";
    String name = doc["name"];
    
    if (!folder.endsWith("/")) folder += "/";
    String folderPath = folder + name + "/";
    
    // Delete all files in the folder (ESP8266 LittleFS doesn't have true directories)
    Dir dir = LittleFS.openDir(folderPath);
    bool success = true;
    while (dir.next()) {
      String filePath = folderPath + dir.fileName();
      if (!LittleFS.remove(filePath)) {
        success = false;
      }
    }
    
    if (success) {
      req->send(200, "application/json", "{\"status\":\"deleted\"}");
    } else {
      req->send(500, "application/json", "{\"error\":\"Failed to delete all files in folder\"}");
    }
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
