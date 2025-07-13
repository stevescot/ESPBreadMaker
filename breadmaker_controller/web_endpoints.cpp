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

// Forward declarations for helpers and global variables used in endpoints
extern bool debugSerial;
// extern unsigned int activeProgramId; // Now in programState

// Only keep externs for actual variables, not macros/constants from globals.h
extern bool debugSerial;
#include "globals.h" // For ProgramState struct
extern ProgramState programState;
#include "globals.h" // For PIDControl struct
extern PIDControl pid;
extern unsigned long windowSize, onTime, startupTime;
// Use tempAvg and fermentState structs from globals.h
extern TemperatureAveragingState tempAvg;
extern FermentationState fermentState;
extern bool thermalRunawayDetected, sensorFaultDetected;
extern time_t scheduledStart;
extern int scheduledStartStage;
extern void streamStatusJson(Print& out);
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
extern float getAveragedTemperature();
extern float readTemperature();

extern void sendJsonError(AsyncWebServerRequest*, const String&, const String&, int);
void deleteFolderRecursive(const String& path);

extern std::vector<Program> programs;
extern unsigned long lightOnTime;
extern unsigned long windowStartTime;
// Now accessed via pid.pidP, pid.pidI, pid.pidD, pid.lastInput, pid.lastITerm
extern OutputStates outputStates;
// Use fermentState struct fields

// Now accessed via pid.controller
// Use tempAvg struct fields
#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__
#endif
void stateMachineEndpoints(AsyncWebServer& server);
void manualOutputEndpoints(AsyncWebServer& server);
void pidControlEndpoints(AsyncWebServer& server);
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
    homeAssistantEndpoint(server);
    calibrationEndpoints(server);
    fileEndPoints(server);
}

// --- Move the full definitions of these functions from your .ino here ---

void stateMachineEndpoints(AsyncWebServer& server)
{
  // --- Core state machine endpoints ---
  server.on("/start", HTTP_GET, [](AsyncWebServerRequest*req){
    if (debugSerial) Serial.println(F("[ACTION] /start called"));
    updateActiveProgramVars();
    // Debug: print active program info
    if (debugSerial && programState.activeProgramId < programs.size()) Serial.printf_P(PSTR("[START] activeProgram='%s' (id=%u), customProgram=%p\n"), programs[programState.activeProgramId].name.c_str(), (unsigned)programState.activeProgramId, (void*)programState.customProgram);

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

    programState.isRunning = true;
    programState.customStageIdx = 0;
    programState.customMixIdx = 0;
    programState.customStageStart = millis();
    programState.customMixStepStart = 0;
    programState.programStartTime = time(nullptr);
    // Initialize actual stage start times array
    for (int i = 0; i < MAX_PROGRAM_STAGES; i++) programState.actualStageStartTimes[i] = 0;
    programState.actualStageStartTimes[0] = programState.programStartTime; // Record actual start of first stage

    // --- Fermentation tracking: set immediately if first stage is fermentation ---
    fermentState.initialFermentTemp = 0.0;
    fermentState.fermentationFactor = 1.0;
    fermentState.predictedCompleteTime = 0;
    fermentState.lastFermentAdjust = 0;
    if (programState.customProgram && !programState.customProgram->customStages.empty()) {
      CustomStage &st = programState.customProgram->customStages[0];
      if (st.isFermentation) {
        float baseline = programState.customProgram->fermentBaselineTemp > 0 ? programState.customProgram->fermentBaselineTemp : 20.0;
        float q10 = programState.customProgram->fermentQ10 > 0 ? programState.customProgram->fermentQ10 : 2.0;
        float actualTemp = getAveragedTemperature();
        fermentState.initialFermentTemp = actualTemp;
        fermentState.fermentationFactor = pow(q10, (baseline - actualTemp) / 10.0);
        unsigned long plannedStageMs = (unsigned long)st.min * 60000UL;
        unsigned long adjustedStageMs = plannedStageMs * fermentState.fermentationFactor;
        fermentState.predictedCompleteTime = millis() + adjustedStageMs;
        fermentState.lastFermentAdjust = millis();
      }
    }
    saveResumeState();
    if (debugSerial) Serial.printf_P(PSTR("[STAGE] Entering custom stage: %d\n"), programState.customStageIdx);
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
    programState.isRunning = false;
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
    if (programState.activeProgramId >= programs.size()) { stopBreadmaker(); AsyncResponseStream *response = req->beginResponseStream("application/json"); streamStatusJson(*response); req->send(response); return; }
    Program &p = programs[programState.activeProgramId];
    programState.customStageIdx++;
    if (programState.customStageIdx >= p.customStages.size()) {
      stopBreadmaker(); programState.customStageIdx = 0; programState.customStageStart = 0; AsyncResponseStream *response = req->beginResponseStream("application/json"); streamStatusJson(*response); req->send(response); return;
    }
    programState.customStageStart = millis();
    programState.customMixIdx = 0;
    programState.customMixStepStart = 0;
    programState.isRunning = true;
    if (programState.customStageIdx == 0) programState.programStartTime = time(nullptr);
    // Record actual start time of this stage
    if (programState.customStageIdx < MAX_PROGRAM_STAGES) programState.actualStageStartTimes[programState.customStageIdx] = time(nullptr);

    // --- Fermentation tracking: set immediately if new stage is fermentation ---
    fermentState.initialFermentTemp = 0.0;
    fermentState.fermentationFactor = 1.0;
    fermentState.predictedCompleteTime = 0;
    fermentState.lastFermentAdjust = 0;
    if (programState.activeProgramId < programs.size()) {
      Program &p = programs[programState.activeProgramId];
      if (programState.customStageIdx < p.customStages.size()) {
        CustomStage &st = p.customStages[programState.customStageIdx];
        if (st.isFermentation) {
          float baseline = p.fermentBaselineTemp > 0 ? p.fermentBaselineTemp : 20.0;
          float q10 = p.fermentQ10 > 0 ? p.fermentQ10 : 2.0;
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
    if (!programState.customProgram) {
        Serial.println(F("[ERROR] /back: customProgram is null"));
        stopBreadmaker();
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        streamStatusJson(*response);
        req->send(response);
        return;
    }
    if (programState.activeProgramId >= programs.size()) {
        Serial.printf_P(PSTR("[ERROR] /back: activeProgramId %u out of range\n"), (unsigned)programState.activeProgramId);
        stopBreadmaker();
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        streamStatusJson(*response);
        req->send(response);
        return;
    }
    Program &p = programs[programState.activeProgramId];
    size_t numStages = p.customStages.size();
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
    // Record actual start time of this stage when going back
    if (programState.customStageIdx < numStages && programState.customStageIdx < MAX_PROGRAM_STAGES) programState.actualStageStartTimes[programState.customStageIdx] = time(nullptr);
    saveResumeState();
    Serial.printf_P(PSTR("[STAGE] Entering custom stage: %d\n"), programState.customStageIdx);
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
  });
  server.on("/select", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("idx")) {
      int idx = req->getParam("idx")->value().toInt();
      if (idx >= 0 && idx < (int)programs.size()) {
        programState.activeProgramId = idx;
        updateActiveProgramVars(); // Update program variables
        programState.isRunning = false;
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
        programState.activeProgramId = foundIdx;
        updateActiveProgramVars(); // Update program variables
        programState.isRunning = false;
        saveSettings();
        saveResumeState(); // <--- Save resume state on program select (by name)
        req->send(200, "text/plain", "Selected");
        return;
      }
    }
    req->send(400, "text/plain", "Bad program name or index");
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

server.on("/start_at_stage", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println(F("[ACTION] /start_at_stage called"));
    if (!req->hasParam("stage")) {
      sendJsonError(req, "Missing stage parameter", "Please specify ?stage=N", 400);
      return;
    }
    updateActiveProgramVars();
    String stageStr = req->getParam("stage")->value();
    bool validStage = true;
    for (size_t i = 0; i < stageStr.length(); ++i) {
      if (!isDigit(stageStr[i]) && !(i == 0 && stageStr[i] == '-')) {
        validStage = false;
        break;
      }
    }
    if (!validStage || stageStr.length() == 0) {
      sendJsonError(req, "Invalid stage parameter", "Stage must be an integer", 400);
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
    if (!programState.customProgram) {
      sendJsonError(req, "No program selected", "Please select a program before starting at a stage.", 400);
      return;
    }

    size_t numStages = programState.customProgram->customStages.size();
    if (numStages == 0) {
      sendJsonError(req, "No stages defined", "The selected program has no stages.", 400);
      return;
    }

    // Validate stage index against actual number of stages
    if (stageIdx < 0 || (size_t)stageIdx >= numStages) {
      String msg = "Stage index must be between 0 and " + String(numStages - 1);
      sendJsonError(req, "Invalid stage index", msg, 400);
      return;
    }

    // Start the program at the specified stage
    programState.isRunning = true;
    programState.customStageIdx = stageIdx;
    programState.customMixIdx = 0;
    programState.customStageStart = millis();
    programState.customMixStepStart = 0;
    programState.programStartTime = time(nullptr);

    // Patch: Do NOT reset all actualStageStartTimes. Mark previous as completed, set current if not set.
    for (int i = 0; i < MAX_PROGRAM_STAGES; i++) {
      if (i < stageIdx) {
        // Mark previous stages as completed (set to a nonzero dummy value, e.g., programStartTime - 1000 * (stageIdx - i))
        if (programState.actualStageStartTimes[i] == 0) programState.actualStageStartTimes[i] = programState.programStartTime - 1000 * (stageIdx - i);
      } else if (i == stageIdx) {
        if (programState.actualStageStartTimes[i] == 0) programState.actualStageStartTimes[i] = programState.programStartTime;
      }
      // Leave future stages untouched (remain 0)
    }

    // --- Fermentation tracking: set immediately if this stage is fermentation ---
    fermentState.initialFermentTemp = 0.0;
    fermentState.fermentationFactor = 1.0;
    fermentState.predictedCompleteTime = 0;
    fermentState.lastFermentAdjust = 0;
    if (programState.customProgram && (size_t)stageIdx < programState.customProgram->customStages.size()) {
      CustomStage &st = programState.customProgram->customStages[stageIdx];
      float baseline = programState.customProgram->fermentBaselineTemp > 0 ? programState.customProgram->fermentBaselineTemp : 20.0;
      float q10 = programState.customProgram->fermentQ10 > 0 ? programState.customProgram->fermentQ10 : 2.0;
      if (st.isFermentation) {
        float actualTemp = getAveragedTemperature();
        fermentState.initialFermentTemp = actualTemp;
        fermentState.fermentationFactor = pow(q10, (baseline - actualTemp) / 10.0);
        unsigned long plannedStageMs = (unsigned long)st.min * 60000UL;
        unsigned long adjustedStageMs = plannedStageMs * fermentState.fermentationFactor;
        fermentState.predictedCompleteTime = millis() + adjustedStageMs;
        fermentState.lastFermentAdjust = millis();
      }
    }

    saveResumeState();
    // --- Clear any pending scheduled start to prevent repeated retriggering ---
    scheduledStart = 0;
    scheduledStartStage = -1;
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
      if (stageIdx < 0 || stageIdx >= programState.maxCustomStages) {
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
      response->printf("\"kd\":%.6f,", pid.Kd);
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

void homeAssistantEndpoint(AsyncWebServer& server)
{// --- Home Assistant-friendly endpoint (lightweight - uses cached values only) ---
  server.on("/ha", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->printf("\"state\":\"%s\",", programState.isRunning ? "on" : "off");
    response->printf("\"stage\":\"");
    if (programState.activeProgramId < programs.size() && programState.isRunning && programState.customStageIdx < programs[programState.activeProgramId].customStages.size()) {
      response->print(programs[programState.activeProgramId].customStages[programState.customStageIdx].label.c_str());
    } else {
      response->print("Idle");
    }
    response->print("\",");
    if (programState.activeProgramId < programs.size()) {
      response->printf("\"program\":\"%s\",", programs[programState.activeProgramId].name.c_str());
    } else {
      response->print("\"program\":\"\",");
    }
    response->printf("\"programId\":%u,", (unsigned)programState.activeProgramId);
    response->printf("\"temperature\":%.2f,", tempAvg.averagedTemperature);
    response->printf("\"setpoint\":%.2f,", pid.Setpoint);
    response->printf("\"heater\":%s,", heaterState ? "true" : "false");
    response->printf("\"motor\":%s,", motorState ? "true" : "false");
    response->printf("\"light\":%s,", lightState ? "true" : "false");
    response->printf("\"buzzer\":%s,", buzzerState ? "true" : "false");
    response->printf("\"manual_mode\":%s,", programState.manualMode ? "true" : "false");
    response->printf("\"stageIdx\":%u,", (unsigned)programState.customStageIdx);
    response->printf("\"mixIdx\":%u,", (unsigned)programState.customMixIdx);
    // Use the same logic as streamStatusJson for stage_ready_at and program_ready_at
    unsigned long nowMs = millis();
    time_t now = time(nullptr);
    unsigned long stageReadyAt = 0;
    unsigned long programReadyAt = 0;
    if (programState.activeProgramId < programs.size() && programState.isRunning && programState.customStageIdx < programs[programState.activeProgramId].customStages.size()) {
        Program &p = programs[programState.activeProgramId];
        CustomStage &st = p.customStages[programState.customStageIdx];
        if (st.isFermentation) {
            double plannedStageSec = (double)st.min * 60.0;
            double elapsedSec = fermentState.fermentWeightedSec;
            double realElapsedSec = (nowMs - fermentState.fermentLastUpdateMs) / 1000.0;
            elapsedSec += realElapsedSec * fermentState.fermentLastFactor;
            double remainSec = plannedStageSec - (elapsedSec / fermentState.fermentLastFactor);
            if (remainSec < 0) remainSec = 0;
            stageReadyAt = now + (unsigned long)(remainSec * fermentState.fermentLastFactor);
        } else if (programState.actualStageStartTimes[programState.customStageIdx] > 0) {
            stageReadyAt = programState.actualStageStartTimes[programState.customStageIdx] + (time_t)st.min * 60;
        }
        // Program ready at: predictedCompleteTime if running
        programReadyAt = (unsigned long)fermentState.predictedCompleteTime;
    }
    response->printf("\"stage_time_left\":%ld,", 0L); // TODO: implement if needed
    response->printf("\"stage_ready_at\":%lu,", stageReadyAt);
    response->printf("\"program_ready_at\":%lu,", programReadyAt);
    response->printf("\"startupDelayComplete\":%s,", isStartupDelayComplete() ? "true" : "false");
    response->printf("\"startupDelayRemainingMs\":%lu,", isStartupDelayComplete() ? 0UL : (STARTUP_DELAY_MS - (millis() - startupTime)));
    response->printf("\"fermentationFactor\":%.2f,", fermentState.fermentationFactor);
    response->printf("\"initialFermentTemp\":%.2f,", fermentState.initialFermentTemp);
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
    response->printf("\"input\":%.2f,", tempAvg.averagedTemperature); // Averaged temp for legacy
    response->printf("\"pid_input\":%.2f,", pid.Input); // PID input (raw value used by PID)
    response->printf("\"output\":%.2f,", pid.Output * 100.0); // Output as percent
    response->printf("\"pid_output\":%.6f,", pid.Output); // PID output (0-1 float)
    response->printf("\"raw_temp\":%.2f,", readTemperature());
    response->printf("\"thermal_runaway\":%s,", (thermalRunawayDetected ? "true" : "false"));
    response->printf("\"sensor_fault\":%s,", (sensorFaultDetected ? "true" : "false"));
    // Use the outer nowMs if already declared, otherwise declare here
    unsigned long elapsed = (windowStartTime > 0) ? (nowMs - windowStartTime) : 0;
    response->printf("\"window_elapsed_ms\":%lu,", elapsed);
    response->printf("\"window_size_ms\":%lu,", windowSize);
    response->printf("\"on_time_ms\":%lu,", onTime);
    response->printf("\"duty_cycle_percent\":%.2f,", pid.Output * 100.0);
    // --- PID parameters for Home Assistant ---
    response->printf("\"kp\":%.6f,", pid.Kp);
    response->printf("\"ki\":%.6f,", pid.Ki);
    response->printf("\"kd\":%.6f,", pid.Kd);
    response->printf("\"sample_time_ms\":%lu,", pid.sampleTime);
    response->printf("\"temp_sample_count\":%d,", tempAvg.tempSampleCount);
    response->printf("\"temp_reject_count\":%d,", tempAvg.tempRejectCount);
    response->printf("\"temp_sample_interval_ms\":%lu", tempAvg.tempSampleInterval);
    response->print("}");
    // --- Add any missing properties for Home Assistant integration ---
    // Add any additional properties here as needed for HA sensors
    response->print("}"); // end health
    response->print("}"); // end root
    req->send(response);
  });// --- Home Assistant-friendly endpoint (lightweight - uses cached values only) ---
  server.on("/ha", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    response->print("{");
    response->printf("\"state\":\"%s\",", programState.isRunning ? "on" : "off");
    response->printf("\"stage\":\"");
    if (programState.activeProgramId < programs.size() && programState.isRunning && programState.customStageIdx < programs[programState.activeProgramId].customStages.size()) {
      response->print(programs[programState.activeProgramId].customStages[programState.customStageIdx].label.c_str());
    } else {
      response->print("Idle");
    }
    response->print("\",");
    if (programState.activeProgramId < programs.size()) {
      response->printf("\"program\":\"%s\",", programs[programState.activeProgramId].name.c_str());
    } else {
      response->print("\"program\":\"\",");
    }
    response->printf("\"programId\":%u,", (unsigned)programState.activeProgramId);
    response->printf("\"temperature\":%.2f,", tempAvg.averagedTemperature);
    response->printf("\"setpoint\":%.2f,", pid.Setpoint);
    response->printf("\"heater\":%s,", heaterState ? "true" : "false");
    response->printf("\"motor\":%s,", motorState ? "true" : "false");
    response->printf("\"light\":%s,", lightState ? "true" : "false");
    response->printf("\"buzzer\":%s,", buzzerState ? "true" : "false");
    response->printf("\"manual_mode\":%s,", programState.manualMode ? "true" : "false");
    response->printf("\"stageIdx\":%u,", (unsigned)programState.customStageIdx);
    response->printf("\"mixIdx\":%u,", (unsigned)programState.customMixIdx);
    // Use the same logic as streamStatusJson for stage_ready_at and program_ready_at
    unsigned long nowMs = millis();
    time_t now = time(nullptr);
    unsigned long stageReadyAt = 0;
    unsigned long programReadyAt = 0;
    if (programState.activeProgramId < programs.size() && programState.isRunning && programState.customStageIdx < programs[programState.activeProgramId].customStages.size()) {
        Program &p = programs[programState.activeProgramId];
        CustomStage &st = p.customStages[programState.customStageIdx];
        if (st.isFermentation) {
            double plannedStageSec = (double)st.min * 60.0;
            double elapsedSec = fermentState.fermentWeightedSec;
            double realElapsedSec = (nowMs - fermentState.fermentLastUpdateMs) / 1000.0;
            elapsedSec += realElapsedSec * fermentState.fermentLastFactor;
            double remainSec = plannedStageSec - (elapsedSec / fermentState.fermentLastFactor);
            if (remainSec < 0) remainSec = 0;
            stageReadyAt = now + (unsigned long)(remainSec * fermentState.fermentLastFactor);
        } else if (programState.actualStageStartTimes[programState.customStageIdx] > 0) {
            stageReadyAt = programState.actualStageStartTimes[programState.customStageIdx] + (time_t)st.min * 60;
        }
        // Program ready at: predictedCompleteTime if running
        programReadyAt = (unsigned long)fermentState.predictedCompleteTime;
    }
    response->printf("\"stage_time_left\":%ld,", 0L); // TODO: implement if needed
    response->printf("\"stage_ready_at\":%lu,", stageReadyAt);
    response->printf("\"program_ready_at\":%lu,", programReadyAt);
    response->printf("\"startupDelayComplete\":%s,", isStartupDelayComplete() ? "true" : "false");
    response->printf("\"startupDelayRemainingMs\":%lu,", isStartupDelayComplete() ? 0UL : (STARTUP_DELAY_MS - (millis() - startupTime)));
    response->printf("\"fermentationFactor\":%.2f,", fermentState.fermentationFactor);
    response->printf("\"initialFermentTemp\":%.2f,", fermentState.initialFermentTemp);
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
    response->printf("\"input\":%.2f,", tempAvg.averagedTemperature); // Averaged temp for legacy
    response->printf("\"pid_input\":%.2f,", pid.Input); // PID input (raw value used by PID)
    response->printf("\"output\":%.2f,", pid.Output * 100.0); // Output as percent
    response->printf("\"pid_output\":%.6f,", pid.Output); // PID output (0-1 float)
    response->printf("\"raw_temp\":%.2f,", readTemperature());
    response->printf("\"thermal_runaway\":%s,", (thermalRunawayDetected ? "true" : "false"));
    response->printf("\"sensor_fault\":%s,", (sensorFaultDetected ? "true" : "false"));
    // Use the outer nowMs if already declared, otherwise declare here
    unsigned long elapsed = (windowStartTime > 0) ? (nowMs - windowStartTime) : 0;
    response->printf("\"window_elapsed_ms\":%lu,", elapsed);
    response->printf("\"window_size_ms\":%lu,", windowSize);
    response->printf("\"on_time_ms\":%lu,", onTime);
    response->printf("\"duty_cycle_percent\":%.2f,", pid.Output * 100.0);
    // --- PID parameters for Home Assistant ---
    response->printf("\"kp\":%.6f,", pid.Kp);
    response->printf("\"ki\":%.6f,", pid.Ki);
    response->printf("\"kd\":%.6f,", pid.Kd);
    response->printf("\"sample_time_ms\":%lu,", pid.sampleTime);
    response->printf("\"temp_sample_count\":%d,", tempAvg.tempSampleCount);
    response->printf("\"temp_reject_count\":%d,", tempAvg.tempRejectCount);
    response->printf("\"temp_sample_interval_ms\":%lu", tempAvg.tempSampleInterval);
    response->print("}");
    // --- Add any missing properties for Home Assistant integration ---
    // Add any additional properties here as needed for HA sensors
    response->print("}"); // end health
    response->print("}"); // end root
    req->send(response);
  });
}

void calibrationEndpoints(AsyncWebServer& server)
{
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
}

void fileEndPoints(AsyncWebServer& server)
{
  
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
}

// --- Streaming status JSON function ---
void streamStatusJson(Print& out) {
  out.print("{");
  // scheduledStart
  out.printf("\"scheduledStart\":%lu,", (unsigned long)scheduledStart);
  // running
  out.printf("\"running\":%s,", programState.isRunning ? "true" : "false");
  // program
  if (programState.activeProgramId < programs.size()) {
    out.printf("\"program\":\"%s\",", programs[programState.activeProgramId].name.c_str());
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
  if (programState.activeProgramId < programs.size()) {
    Program &p = programs[programState.activeProgramId];
    // Determine preview or running mode
    bool previewMode = !programState.isRunning;
    // Stage label
    if (programState.isRunning && programState.customStageIdx < p.customStages.size()) {
      out.print("\"stage\":\"");
      out.print(p.customStages[programState.customStageIdx].label.c_str());
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
    unsigned long programStartUnix = (unsigned long)(programState.isRunning ? programState.programStartTime : time(nullptr));
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
    fermentState.fermentationFactor = fermentationFactorForProgram;
    // --- Predicted (temperature-adjusted) program end time ---
    out.printf("\"predictedProgramEnd\":%lu,", (unsigned long)(programStartUnix + (unsigned long)(cumulativePredictedSec)));
    // --- Legacy: stageStartTimes array (planned, not adjusted) ---
    out.print("\"stageStartTimes\":[");
    time_t t = programState.isRunning ? programState.programStartTime : time(nullptr);
    for (size_t i = 0; i < p.customStages.size(); ++i) {
      if (i > 0) out.print(",");
      out.printf("%lu", (unsigned long)t);
      t += (time_t)p.customStages[i].min * 60;
    }
    out.print("],");
    out.printf("\"programStart\":%lu,", (unsigned long)programStartUnix);
    // elapsed
    es = (programState.customStageStart == 0) ? 0 : (nowMs - programState.customStageStart) / 1000;
    // Calculate stageReadyAt and programReadyAt (legacy, not adjusted)
    stageReadyAt = 0;
    programReadyAt = 0;
    if (programState.customStageIdx < p.customStages.size()) {
      CustomStage &st = p.customStages[programState.customStageIdx];
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
          stageReadyAt = programState.actualStageStartTimes[programState.customStageIdx] + (time_t)p.customStages[programState.customStageIdx].min * 60;
        }
        if (programState.actualStageStartTimes[programState.customStageIdx] > 0) {
          unsigned long actualElapsedInStage = (millis() - programState.customStageStart) / 1000UL;
          unsigned long plannedStageDuration = (unsigned long)p.customStages[programState.customStageIdx].min * 60UL;
          unsigned long timeLeftInCurrentStage = (actualElapsedInStage < plannedStageDuration) ? (plannedStageDuration - actualElapsedInStage) : 0;
          stageLeft = (int)timeLeftInCurrentStage;
          programReadyAt = now + (time_t)timeLeftInCurrentStage;
          for (size_t i = programState.customStageIdx + 1; i < p.customStages.size(); ++i) {
            programReadyAt += (time_t)p.customStages[i].min * 60;
          }
        } else {
          programReadyAt = programState.isRunning ? programState.programStartTime : time(nullptr);
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
