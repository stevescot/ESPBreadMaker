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

// --- ENUM & STAGE HELPERS ---
// Enum for all breadmaking stages
enum Stage {
  STAGE_IDLE,         // Not running
  STAGE_AUTOLYSE,     // Autolyse rest (now replaces delay)
  STAGE_MIX,          // Mixing
  STAGE_BULK,         // Bulk fermentation
  STAGE_KNOCKDOWN,    // Knockdown
  STAGE_RISE,         // Final rise
  STAGE_BAKE1,        // First bake
  STAGE_BAKE2,        // Second bake
  STAGE_COOL,         // Cooling
  STAGE_COUNT         // Number of stages
};

// --- Web server and OTA ---
AsyncWebServer server(80); // Main web server
ESPAsyncHTTPUpdateServer httpUpdater; // OTA update server

// --- Pin assignments ---
const int PIN_HEATER = D1;     // Heater (PWM ~1V ON, 0V OFF)
const int PIN_MOTOR  = D2;     // Mixer motor (PWM ~1V ON, 0V OFF)
const int PIN_RTD    = A0;     // RTD analog input
const int PIN_LIGHT  = D5;     // Light (PWM ~1V ON, 0V OFF)
const int PIN_BUZZER = D6;     // Buzzer (PWM ~1V ON, 0V OFF)

// --- PID parameters for temperature control ---
double Setpoint, Input, Output;
double Kp=2.0, Ki=5.0, Kd=1.0;
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);
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

// --- Light/Buzzer management ---
unsigned long lightOnTime = 0;    // When was the light turned on?
unsigned long buzzStart = 0;      // When was the buzzer turned on?
bool buzzActive = false;          // Is the buzzer currently active?

/// --- Captive portal DNS and AP ---
DNSServer dnsServer;
const char* apSSID = "BreadmakerSetup";

// --- Stage name helpers ---
const char* stageNames[] = {
  "Idle", "autolyse", "mix", "bulk", "knockDown", "rise", "bake1", "bake2", "cool", "COUNT"
};
// Convert string to Stage enum
Stage stageFromString(const char* s) {
  for (int i = 0; i < STAGE_COUNT; ++i) if (strcmp(s, stageNames[i]) == 0) return (Stage)i;
  return STAGE_IDLE;
}
// Convert Stage enum to string
const char* stringFromStage(Stage st) {
  if (st >= 0 && st < STAGE_COUNT) return stageNames[st];
  return "Idle";
}
// Get next stage
Stage nextStage(Stage st) {
  if (st >= 0 && st < STAGE_COOL) return (Stage)(st + 1);
  return STAGE_COOL;
}
// Get previous stage
Stage prevStage(Stage st) {
  if (st > STAGE_AUTOLYSE && st < STAGE_COUNT) return (Stage)(st - 1);
  return STAGE_AUTOLYSE;
}
// --- END ENUM & HELPERS ---

// --- Output mode setting ---
enum OutputMode { OUTPUT_DIGITAL, OUTPUT_ANALOG };
OutputMode outputMode = OUTPUT_ANALOG; // Default to analog (PWM)
const char* SETTINGS_FILE = "/settings.json";

// --- Debug serial setting ---
bool debugSerial = true; // Debug serial output (default: true)

// --- Function prototypes ---
void loadSettings();
void saveSettings();

// --- Forward declarations for output control ---
inline void setHeater(bool on);
inline void setMotor(bool on);
inline void setLight(bool on);
inline void setBuzzer(bool on);
inline bool heaterOn();
inline bool motorOn();
inline bool lightOn();
inline bool buzzerOn();

// --- Forward declaration for stopBreadmaker (must be before any use) ---
void stopBreadmaker();

// --- Output control function definitions (moved up for linker) ---
inline void setHeater(bool on) {
  if (outputMode == OUTPUT_DIGITAL) digitalWrite(PIN_HEATER, on ? HIGH : LOW);
  else analogWrite(PIN_HEATER, on ? 77 : 0);
}
inline void setMotor(bool on) {
  if (outputMode == OUTPUT_DIGITAL) digitalWrite(PIN_MOTOR, on ? HIGH : LOW);
  else analogWrite(PIN_MOTOR, on ? 77 : 0);
}
inline void setLight(bool on) {
  // Always use digitalWrite for light to avoid PWM timer conflicts
  digitalWrite(PIN_LIGHT, on ? HIGH : LOW);
  if (on) lightOnTime = millis();
}
inline void setBuzzer(bool on) {
  if (outputMode == OUTPUT_DIGITAL) digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
  else analogWrite(PIN_BUZZER, on ? 77 : 0);
  buzzActive = on; if (on) buzzStart = millis();
}
inline bool heaterOn() { return (outputMode == OUTPUT_DIGITAL) ? digitalRead(PIN_HEATER) == HIGH : analogRead(PIN_HEATER) > 5; }
inline bool motorOn()  { return (outputMode == OUTPUT_DIGITAL) ? digitalRead(PIN_MOTOR)  == HIGH : analogRead(PIN_MOTOR)  > 5; }
inline bool lightOn()  { return (outputMode == OUTPUT_DIGITAL) ? digitalRead(PIN_LIGHT)  == HIGH : analogRead(PIN_LIGHT)  > 5; }
inline bool buzzerOn() { return (outputMode == OUTPUT_DIGITAL) ? digitalRead(PIN_BUZZER) == HIGH : analogRead(PIN_BUZZER) > 5; }

// --- Core state machine endpoints ---
void setup() {
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

  // --- Core state machine endpoints ---
  server.on("/start", HTTP_GET, [](AsyncWebServerRequest*req){
    if (debugSerial) Serial.println("[ACTION] /start called");
    isRunning = true;
    customStageIdx = 0;
    customMixIdx = 0;
    customStageStart = millis(); // Always set to millis() on start
    customMixStepStart = 0;
    programStartTime = time(nullptr); // Set absolute program start time
    saveResumeState(); // <--- Save resume state immediately on start
    if (debugSerial) Serial.printf("[STAGE] Entering custom stage: %d\n", customStageIdx);
    req->send(200, "text/plain", "Started");
  });
  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest*req){
    if (debugSerial) Serial.println("[ACTION] /stop called");
    stopBreadmaker();
    req->send(200, "text/plain", "Stopped");
  });
  server.on("/pause", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println("[ACTION] /pause called");
    isRunning = false;
    setMotor(false);
    setHeater(false);
    setLight(false);
    saveResumeState(); // Save on pause
    req->send(200, "text/plain", "Paused");
  });
  server.on("/resume", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println("[ACTION] /resume called");
    isRunning = true;
    saveResumeState(); // Save on resume
    req->send(200, "text/plain", "Resumed");
  });
  server.on("/advance", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println("[ACTION] /advance called");
    if (programs.count(activeProgramName) == 0) { stopBreadmaker(); req->send(200,"text/plain","No program"); return; }
    Program &p = programs[activeProgramName];
    customStageIdx++;
    if (customStageIdx >= p.customStages.size()) {
      stopBreadmaker(); customStageIdx = 0; customStageStart = 0; req->send(200,"text/plain","Stopped at end"); return;
    }
    customStageStart = millis(); // Always set to millis() on advance
    customMixIdx = 0;
    customMixStepStart = 0;
    isRunning = true;
    if (customStageIdx == 0) programStartTime = time(nullptr); // Reset programStartTime if looping
    saveResumeState(); // Save on advance
    if (debugSerial) Serial.printf("[STAGE] Entering custom stage: %d\n", customStageIdx);
    req->send(200,"text/plain","Advanced");
  });
  server.on("/back", HTTP_GET, [](AsyncWebServerRequest* req){
    if (debugSerial) Serial.println("[ACTION] /back called");
    if (programs.count(activeProgramName) == 0) { stopBreadmaker(); req->send(200,"text/plain","No program"); return; }
    Program &p = programs[activeProgramName];
    if (customStageIdx > 0) customStageIdx--;
    customStageStart = millis(); // Always set to millis() on back
    customMixIdx = 0;
    customMixStepStart = 0;
    isRunning = true;
    if (customStageIdx == 0) programStartTime = time(nullptr); // Reset programStartTime if going back to start
    saveResumeState(); // Save on back
    if (debugSerial) Serial.printf("[STAGE] Entering custom stage: %d\n", customStageIdx);
    req->send(200,"text/plain","Went back");
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
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(LittleFS, "/index.html", "text/html");
  });
  // --- Status endpoint for UI polling ---
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest*req){
    if (debugSerial) Serial.println("[DEBUG] /status requested");
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
      // --- FIX: Use programStartTime for absolute ready times ---
      // Calculate stageReadyAt and programReadyAt based on programStartTime and stage durations
      stageReadyAt = programStartTime;
      for (size_t i = 0; i <= customStageIdx && i < p.customStages.size(); ++i) {
        stageReadyAt += (time_t)p.customStages[i].min * 60;
      }
      programReadyAt = programStartTime;
      for (size_t i = 0; i < p.customStages.size(); ++i) {
        programReadyAt += (time_t)p.customStages[i].min * 60;
      }
      // Time left in current stage
      if (customStageIdx < p.customStages.size()) {
        stageLeft = max(0, (int)(stageReadyAt - now));
      }
      // Program left
      // (programReadyAt - now) can be used if needed
    }
    d["elapsed"] = es;
    d["setTemp"] = Setpoint;
    d["timeLeft"] = stageLeft;
    d["stageReadyAt"] = stageReadyAt;
    d["programReadyAt"] = programReadyAt;
    d["temp"] = readTemperature();
    d["heater"] = heaterOn();
    d["motor"] = motorOn();
    d["light"] = lightOn();
    d["buzzer"] = buzzerOn();
    d["stageStartTime"] = (uint32_t)customStageStart;
    JsonArray progs = d.createNestedArray("programList");
    for (auto it = programs.begin(); it != programs.end(); ++it) progs.add(it->first);
    char buf[1024];
    serializeJson(d, buf, sizeof(buf));
    req->send(200, "application/json", buf);
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
    d["temperature"] = readTemperature();
    d["heater"] = heaterOn();
    d["motor"] = motorOn();
    d["light"] = lightOn();
    d["buzzer"] = buzzerOn();
    d["stage_time_left"] = (int)(d["timeLeft"] | 0);
    d["stage_ready_at"] = (int)(d["stageReadyAt"] | 0);
    d["program_ready_at"] = (int)(d["programReadyAt"] | 0);
    char buf[512];
    serializeJson(d, buf, sizeof(buf));
    req->send(200, "application/json", buf);
  });

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
    DynamicJsonDocument doc(4096);
    if(deserializeJson(doc, data, len)) { req->send(400,"text/plain","Bad JSON"); return; }
    rtdCalibTable.clear();
    for(JsonObject o : doc["table"].as<JsonArray>()) {
      CalibPoint pt = { o["raw"], o["temp"] };
      rtdCalibTable.push_back(pt);
    }
    // Save to LittleFS here if desired
    req->send(200,"text/plain","OK");
  });
  // Programs/cals as before...
  server.on("/api/programs",HTTP_GET,[](AsyncWebServerRequest*req){
    auto r = req->beginResponse(LittleFS,"/programs.json","application/json");
    r->addHeader("Cache-Control","no-cache");
    req->send(r);
  });
  server.on("/api/programs", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
      DynamicJsonDocument doc(8192);
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) { req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
      File f = LittleFS.open("/programs.json", "w");
      if (!f) { req->send(500, "application/json", "{\"error\":\"FS fail\"}"); return; }
      serializeJson(doc, f); f.close(); loadPrograms();
      req->send(200, "application/json", "{\"status\":\"ok\"}");
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
  Serial.println("HTTP server started");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  // Wait for time sync
  while (time(nullptr) < 100000) delay(200);
}

// Remove duplicate loadPrograms() implementation from this file. Use the one in programs_manager.cpp.

// --- Resume state management ---
const char* RESUME_FILE = "/resume.json";
unsigned long lastResumeSave = 0;

void saveResumeState() {
  DynamicJsonDocument doc(512);
  doc["activeProgramName"] = activeProgramName;
  doc["customStageIdx"] = customStageIdx;
  doc["customMixIdx"] = customMixIdx;
  // Save elapsed time in current stage and mix step (in seconds)
  doc["elapsedStageSec"] = (customStageStart > 0) ? (millis() - customStageStart) / 1000UL : 0;
  doc["elapsedMixSec"] = (customMixStepStart > 0) ? (millis() - customMixStepStart) / 1000UL : 0;
  doc["programStartTime"] = programStartTime;
  doc["isRunning"] = isRunning;
  File f = LittleFS.open(RESUME_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

void clearResumeState() {
  LittleFS.remove(RESUME_FILE);
}

void loadResumeState() {
  File f = LittleFS.open(RESUME_FILE, "r");
  if (!f) return;
  DynamicJsonDocument doc(512);
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
  isRunning = doc["isRunning"] | false;
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
        unsigned long mixDurSec = st.mixPattern[mixIdx].durationSec;
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
  }
}

void loop() {
  static unsigned long lastDebugPrint = 0;
  if (debugSerial && millis() - lastDebugPrint > 10000) { // Every 10 seconds
    Serial.printf("[DEBUG] Heap: %lu bytes, isRunning: %d, activeProgram: %s, customStageIdx: %u, customMixIdx: %u\n",
      ESP.getFreeHeap(), isRunning, activeProgramName, (unsigned)customStageIdx, (unsigned)customMixIdx);
    lastDebugPrint = millis();
  }
  yield(); delay(1); // <--- Extra yield/delay at top
  if (!isRunning) {
    if (scheduledStart && time(nullptr) >= scheduledStart) {
      scheduledStart = 0;
      isRunning = true;
      customStageIdx = 0;
      customMixIdx = 0;
      customStageStart = millis();
      customMixStepStart = 0;
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
    Input = readTemperature();
    myPID.Compute();
    setHeater(st.heater == "on");
    // --- Always set light every loop if stage requests it ---
    if (st.light == "on") setLight(true);
    else setLight(false);
    setBuzzer(st.buzzer == "on");
    yield(); delay(1); // <--- Extra yield/delay after output control
    if (!st.mixPattern.empty()) {
      if (customMixStepStart == 0) customMixStepStart = millis();
      MixStep &step = st.mixPattern[customMixIdx];
      if (step.action == "mix") setMotor(true);
      else setMotor(false);
      yield(); delay(1); // <--- Extra yield/delay in mix pattern
      if (millis() - customMixStepStart >= (unsigned long)step.durationSec * 1000UL) {
        customMixIdx++;
        customMixStepStart = millis();
        if (customMixIdx >= st.mixPattern.size()) customMixIdx = 0; // Loop pattern
      }
      yield(); delay(1); // <--- Extra yield/delay after mix step
    } else {
      setMotor(false);
      yield(); delay(1);
    }
    if (millis() - customStageStart >= (unsigned long)st.min * 60000UL) {
      customStageIdx++;
      customStageStart = millis();
      customMixIdx = 0;
      customMixStepStart = 0;
      if (customStageIdx >= p.customStages.size()) {
        stopBreadmaker(); customStageIdx = 0; customStageStart = 0; clearResumeState(); yield(); delay(1); return;
      }
    }
    yield(); delay(1); return;
  }
  // Housekeeping: auto-off light, buzzer
  bool customLightOn = false;
  if (isRunning && programs.count(activeProgramName) > 0) {
    Program &p = programs[activeProgramName];
    if (!p.customStages.empty() && customStageIdx < p.customStages.size()) {
      CustomStage &st = p.customStages[customStageIdx];
      customLightOn = (st.light == "on");
    }
  }
  yield(); delay(1);
  if (!customLightOn && lightOn() && (millis() - lightOnTime > 30000)) setLight(false);
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
      saveResumeState(); // Save on scheduled start
    } else {
      yield(); delay(100);
      return;
    }
  }
  yield(); delay(1);
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
  DynamicJsonDocument doc(256);
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
  DynamicJsonDocument doc(256);
  doc["outputMode"] = (outputMode == OUTPUT_DIGITAL) ? "digital" : "analog";
  doc["debugSerial"] = debugSerial;
  doc["lastProgram"] = activeProgramName;
  File f = LittleFS.open(SETTINGS_FILE, "w");
  if (!f) {
    Serial.println("[saveSettings] Failed to open settings.json for writing!");
    return;
  }
  serializeJson(doc, f);
  f.close();
  Serial.println("[saveSettings] Settings saved.");
}
