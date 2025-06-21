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
// Storage file name
const char* CALIB_FILE = "/calibration.json";
struct CalibPoint { int raw; float temp; };
std::vector<CalibPoint> rtdCalibTable;
// Wi-Fi credentials (no longer needed, now handled by captive portal)
// const char* ssid = "RawInternet";
// const char* password = "36F54F984C";

// --- ENUM & STAGE HELPERS ---
enum Stage {
  STAGE_IDLE,
  STAGE_AUTOLYSE,   // Autolyse rest (now replaces delay)
  STAGE_MIX,
  STAGE_BULK,
  STAGE_KNOCKDOWN,
  STAGE_RISE,
  STAGE_BAKE1,
  STAGE_BAKE2,
  STAGE_COOL,
  STAGE_COUNT
};

// Web server
AsyncWebServer server(80);
ESPAsyncHTTPUpdateServer httpUpdater;

// Pin assignments
const int PIN_HEATER = D1;     // Heater (PWM ~1V ON, 0V OFF)
const int PIN_MOTOR  = D2;     // Mixer motor (PWM ~1V ON, 0V OFF)
const int PIN_RTD    = A0;     // RTD analog input
const int PIN_LIGHT  = D5;     // Light (PWM ~1V ON, 0V OFF)
const int PIN_BUZZER = D6;     // Buzzer (PWM ~1V ON, 0V OFF)

// PID parameters
double Setpoint, Input, Output;
double Kp=2.0, Ki=5.0, Kd=1.0;
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);
time_t scheduledStart = 0;

// State variables
bool isRunning = false;
unsigned long stageStartTime = 0;
Stage currentStage = STAGE_IDLE;
String activeProgramName = "default";

// Light/Buzzer management
unsigned long lightOnTime = 0;
unsigned long buzzStart = 0;
bool buzzActive = false;

// Program definition
struct Program {
  int delayStart;
  int mixTime;
  float bulkTemp;
  int bulkTime;
  int knockDownTime;
  float riseTemp;
  int riseTime;
  float bake1Temp;
  int bake1Time;
  float bake2Temp;
  int bake2Time;
  int coolTime;
};
std::map<String, Program> programs;
float calibrationSlope = 1.0f, calibrationOffset = 0.0f;

// WiFi credentials file
const char* WIFI_FILE = "/wifi.json";

// Loads WiFi credentials from LittleFS. Returns true if found, false otherwise.
bool loadWiFiCreds(String &ssid, String &pass) {
  File f = LittleFS.open(WIFI_FILE, "r");
  if (!f) return false;
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, f)) { f.close(); return false; }
  f.close();
  if (!doc.containsKey("ssid") || !doc.containsKey("pass")) return false;
  ssid = String((const char*)doc["ssid"]);
  pass = String((const char*)doc["pass"]);
  return ssid.length() > 0;
}

DNSServer dnsServer;
const byte DNS_PORT = 53;
const char* apSSID = "BreadmakerSetup";

void startCaptivePortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  // Scan for WiFi networks
  int n = WiFi.scanNetworks();
  String options = "";
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    options += "<option value='" + ssid + "'>" + ssid + "</option>";
  }

  // Read captive portal HTML from LittleFS
  String page;
  File f = LittleFS.open("/wifi.html", "r");
  if (f) {
    while (f.available()) page += (char)f.read();
    f.close();
    page.replace("{{options}}", options);
  } else {
    page = "<h2>WiFi Setup</h2><form method='POST' action='/save'><label>SSID: <select name='ssid'>" + options + "</select></label><br><label>Password: <input name='pass' type='password' placeholder='Password'></label><br><button>Save</button></form>";
  }

  auto sendPortal = [page](AsyncWebServerRequest* req) {
    req->send(200, "text/html", page);
  };

  // Main and fallback
  server.on("/", HTTP_GET, sendPortal);
  server.onNotFound(sendPortal);

  // Captive portal detection endpoints
  const char* captiveEndpoints[] = {
    "/generate_204", "/fwlink", "/hotspot-detect.html", "/ncsi.txt", "/connecttest.txt", "/wpad.dat"
  };
  for (auto ep : captiveEndpoints) {
    server.on(ep, HTTP_GET, sendPortal);
  }

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest* req){
    String ssid = req->arg("ssid");
    String pass = req->arg("pass");
    DynamicJsonDocument doc(256);
    doc["ssid"] = ssid;
    doc["pass"] = pass;
    File f = LittleFS.open(WIFI_FILE, "w");
    if (f) { serializeJson(doc, f); f.close(); }
    req->send(200, "text/plain", "Saved! Connecting to WiFi...\nThe device will reboot and disconnect from this network.\nYou may need to reconnect to the new WiFi.");
    delay(1000);
    ESP.restart();
  });
  server.begin();
}

void saveCalibration() {
  DynamicJsonDocument doc(1024 + 128 * rtdCalibTable.size());
  JsonArray arr = doc.createNestedArray("table");
  for(auto& pt : rtdCalibTable) {
    JsonObject o = arr.createNestedObject();
    o["raw"] = pt.raw; o["temp"] = pt.temp;
  }
  File f = LittleFS.open(CALIB_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

void loadCalibration() {
  rtdCalibTable.clear();
  File f = LittleFS.open(CALIB_FILE, "r");
  if (!f) return;
  DynamicJsonDocument doc(4096);
  if (!deserializeJson(doc, f)) {
    for(JsonObject o : doc["table"].as<JsonArray>()) {
      CalibPoint pt = { o["raw"], o["temp"] };
      rtdCalibTable.push_back(pt);
    }
  }
  f.close();
}


float readTemperature() {
  int raw = analogRead(PIN_RTD);
  return tempFromRaw(raw);
}
// Prototypes
void loadPrograms();
void savePrograms();
void startBreadmaker();
void stopBreadmaker();


const char* stageNames[] = {
  "Idle", "autolyse", "mix", "bulk", "knockDown", "rise", "bake1", "bake2", "cool", "COUNT"
};
Stage stageFromString(const String& s) {
  for (int i = 0; i < STAGE_COUNT; ++i) if (s == stageNames[i]) return (Stage)i;
  return STAGE_IDLE;
}
String stringFromStage(Stage st) {
  if (st >= 0 && st < STAGE_COUNT) return stageNames[st];
  return "Idle";
}
Stage nextStage(Stage st) {
  if (st >= 0 && st < STAGE_COOL) return (Stage)(st + 1);
  return STAGE_COOL;
}
Stage prevStage(Stage st) {
  if (st > STAGE_AUTOLYSE && st < STAGE_COUNT) return (Stage)(st - 1);
  return STAGE_AUTOLYSE;
}
// --- END ENUM & HELPERS ---

void setup() {
  Serial.begin(115200);
  pinMode(PIN_HEATER, OUTPUT);
  pinMode(PIN_MOTOR,  OUTPUT);
  pinMode(PIN_LIGHT,  OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  analogWriteRange(255);
  loadCalibration();
  setHeater(false); setMotor(false); setLight(false); setBuzzer(false);

  if (!LittleFS.begin()) { Serial.println("LittleFS mount failed"); return; }
  loadPrograms();

  myPID.SetMode(AUTOMATIC);
  myPID.SetOutputLimits(0,255);

  String ssid, pass;
  if (loadWiFiCreds(ssid, pass)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("Connecting to %s\n", ssid.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print('.');
      if (millis() - t0 > 20000) {
        Serial.println("\nWi-Fi connect failed, starting captive portal");
        startCaptivePortal();
        while (true) { dnsServer.processNextRequest(); delay(10); }
      }
    }
    Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("No WiFi creds, starting captive portal");
    startCaptivePortal();
    while (true) { dnsServer.processNextRequest(); delay(10); }
  }

  // OTA update server
  httpUpdater.setup(&server, "/update");

  // Static files
  server.serveStatic("/style.css",LittleFS,"/style.css");
  server.serveStatic("/script.js",LittleFS,"/script.js");
  server.serveStatic("/programs.js",LittleFS,"/programs.js");
  server.serveStatic("/calibrate",LittleFS,"/calibration.html");
  server.serveStatic("/programs",LittleFS,"/programs.html");
  server.serveStatic("/config",LittleFS,"/config.html");
  server.serveStatic("/breadmaker.jpg", LittleFS, "/breadmaker.jpg");
  server.serveStatic("/sourdough", LittleFS, "/sourdough.html");
  server.onNotFound([](AsyncWebServerRequest* req){
    if (req->method()==HTTP_GET)
      req->send(LittleFS,"/index.html","text/html");
    else
      req->send(404);
  });

  // Output toggles
  server.on("/motor", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) { String v = req->getParam("on")->value(); setMotor(v == "1"); req->send(200, "text/plain", "Motor set"); return; }
    req->send(400, "text/plain", "Missing on param");
  });
  server.on("/heater", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) { String v = req->getParam("on")->value(); setHeater(v == "1"); req->send(200, "text/plain", "Heater set"); return; }
    req->send(400, "text/plain", "Missing on param");
  });
  server.on("/light", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("on")) { String v = req->getParam("on")->value(); setLight(v == "1"); req->send(200, "text/plain", "Light set"); return; }
    req->send(400, "text/plain", "Missing on param");
  });
  server.on("/buzz", HTTP_GET, [](AsyncWebServerRequest* req){
    setBuzzer(true); req->send(200, "text/plain", "Buzzed");
  });

  // Core state machine
  server.on("/start", HTTP_GET, [](AsyncWebServerRequest*req){
    isRunning = true;
    currentStage = STAGE_AUTOLYSE;
    stageStartTime = millis();
    req->send(200, "text/plain", "Started");
  });
  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest*req){
    stopBreadmaker();
    req->send(200, "text/plain", "Stopped");
  });
  server.on("/pause", HTTP_GET, [](AsyncWebServerRequest* req){
    isRunning = false;
    setMotor(false);
    setHeater(false);
    setLight(false);
    req->send(200, "text/plain", "Paused");
  });
  server.on("/resume", HTTP_GET, [](AsyncWebServerRequest* req){
    isRunning = true;
    stageStartTime = millis();
    req->send(200, "text/plain", "Resumed");
  });
  server.on("/advance", HTTP_GET, [](AsyncWebServerRequest* req){
    if (programs.count(activeProgramName) == 0) { stopBreadmaker(); req->send(200,"text/plain","No program"); return; }
    if (currentStage == STAGE_IDLE || currentStage == STAGE_COUNT) currentStage = STAGE_AUTOLYSE;
    else if (currentStage == STAGE_COOL) { stopBreadmaker(); req->send(200,"text/plain","Stopped at cool"); return; }
    else currentStage = nextStage(currentStage);
    stageStartTime = millis();
    isRunning = true;
    req->send(200,"text/plain","Advanced");
  });
  server.on("/back", HTTP_GET, [](AsyncWebServerRequest* req){
    if (programs.count(activeProgramName) == 0) { stopBreadmaker(); req->send(200,"text/plain","No program"); return; }
    if (currentStage == STAGE_IDLE || currentStage == STAGE_AUTOLYSE) currentStage = STAGE_AUTOLYSE;
    else currentStage = prevStage(currentStage);
    stageStartTime = millis();
    isRunning = true;
    req->send(200,"text/plain","Went back");
  });
  server.on("/select", HTTP_GET, [](AsyncWebServerRequest* req){
    if (req->hasParam("name")) {
      String name = req->getParam("name")->value();
      if (programs.count(name)) {
        activeProgramName = name;
        currentStage = STAGE_IDLE;
        stageStartTime = 0;
        isRunning = false;
        req->send(200, "text/plain", "Selected");
        return;
      }
    }
    req->send(400, "text/plain", "Bad program name");
  });

  // Status endpoint
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest*req){
    DynamicJsonDocument d(1024);
    d["scheduledStart"] = (uint32_t)scheduledStart;
    d["running"] = isRunning;
    d["stage"] = stringFromStage(currentStage);
    d["program"] = activeProgramName;
    unsigned long es = (millis() - stageStartTime) / 1000;
    d["elapsed"] = es;
    int dur = 0;
    if (programs.count(activeProgramName)) {
      Program &p = programs[activeProgramName];
      switch (currentStage) {
        case STAGE_AUTOLYSE: dur = p.delayStart*60; break;
        case STAGE_MIX: dur = p.mixTime*60; break;
        case STAGE_BULK: dur = p.bulkTime*60; break;
        case STAGE_KNOCKDOWN: dur = p.knockDownTime*60; break;
        case STAGE_RISE: dur = p.riseTime*60; break;
        case STAGE_BAKE1: dur = p.bake1Time*60; break;
        case STAGE_BAKE2: dur = p.bake2Time*60; break;
        case STAGE_COOL: dur = p.coolTime*60; break;
        default: dur = 0;
      }
    }
    d["timeLeft"] = max(0, dur - (int)es);
    d["temp"] = readTemperature();
    d["heater"] = heaterOn();
    d["motor"] = motorOn();
    d["light"] = lightOn();
    d["buzzer"] = buzzerOn();
    JsonArray progs = d.createNestedArray("programList");
    for (auto it = programs.begin(); it != programs.end(); ++it) progs.add(it->first);
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
    saveCalibration();
    req->send(200,"text/plain","OK");
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

  server.begin();
  Serial.println("HTTP server started");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  // Wait for time sync
  while (time(nullptr) < 100000) delay(200);
}
float tempFromRaw(int raw) {
  if (rtdCalibTable.empty()) return 0;
  if (raw <= rtdCalibTable.front().raw) return rtdCalibTable.front().temp;
  if (raw >= rtdCalibTable.back().raw) return rtdCalibTable.back().temp;
  for(size_t i=1; i<rtdCalibTable.size(); ++i) {
    if (raw < rtdCalibTable[i].raw) {
      auto &a = rtdCalibTable[i-1], &b = rtdCalibTable[i];
      float t = a.temp + (raw - a.raw) * (b.temp - a.temp) / (b.raw - a.raw);
      return t;
    }
  }
  return rtdCalibTable.back().temp;
}
void loop() {
  if(!isRunning) { yield(); delay(1); return; }
  if (programs.count(activeProgramName) == 0) {
    stopBreadmaker(); return;
  }
  Program &p = programs[activeProgramName];
  unsigned long elapsedMs = millis() - stageStartTime;
  unsigned long elapsedS = elapsedMs / 1000;
  switch (currentStage) {
    case STAGE_AUTOLYSE:
      // Autolyse logic (rest, temp control)
      setMotor(false); setHeater(false); setLight(true);
      if (elapsedMs >= p.delayStart * 60000UL) { // Use delayStart as autolyse duration
        currentStage = STAGE_MIX; stageStartTime = millis();
      }
      break;
    case STAGE_MIX:
    case STAGE_KNOCKDOWN: {
      // First 1 min: burst (200ms/1.2s)
      if (elapsedS < 60) {
        const unsigned long period = 1200, ontime = 200;
        unsigned long t = elapsedMs % period;
        setMotor(t < ontime);
      } else {
        // After burst: 2min mixing, 30s pause, repeat
        unsigned long t = elapsedS - 60;
        unsigned long cycle = t % 150; // 150s = 2min + 30s
        if (cycle < 120) setMotor(true);
        else setMotor(false);
      }
      unsigned long stageLength = (currentStage == STAGE_MIX) ? p.mixTime : p.knockDownTime;
      if (elapsedMs >= stageLength * 60000UL) {
        setMotor(false);
        if (currentStage == STAGE_MIX) {
          currentStage = STAGE_BULK;
          stageStartTime = millis();
        } else {
          currentStage = STAGE_RISE;
          stageStartTime = millis();
        }
      }
      break;
    }
    case STAGE_BULK:
      Setpoint = p.bulkTemp;
      Input = readTemperature();
      myPID.Compute();
      setHeater(Output > 64); // Turn heater ON if PID output is high enough
      setMotor(false);
      if (elapsedMs >= p.bulkTime * 60000UL) {
        setHeater(false);
        currentStage = STAGE_KNOCKDOWN; stageStartTime = millis();
      }
      break;
    case STAGE_RISE:
      Setpoint = p.riseTemp;
      Input = readTemperature();
      myPID.Compute();
      setHeater(Output > 64);
      setMotor(false);
      if (elapsedMs >= p.riseTime * 60000UL) {
        setHeater(false);
        currentStage = STAGE_BAKE1; stageStartTime = millis();
      }
      break;
    case STAGE_BAKE1:
      Setpoint = p.bake1Temp;
      Input = readTemperature();
      myPID.Compute();
      setHeater(Output > 64);
      if (elapsedMs >= p.bake1Time * 60000UL) {
        setHeater(false);
        currentStage = STAGE_BAKE2; stageStartTime = millis();
      }
      break;
    case STAGE_BAKE2:
      Setpoint = p.bake2Temp;
      Input = readTemperature();
      myPID.Compute();
      setHeater(Output > 64);
      if (elapsedMs >= p.bake2Time * 60000UL) {
        setHeater(false);
        currentStage = STAGE_COOL; stageStartTime = millis();
      }
      break;
    case STAGE_COOL:
      setHeater(false); setMotor(false);
      if (elapsedMs >= p.coolTime * 60000UL) {
        stopBreadmaker();
      }
      break;
    default:
      break;
  }
  // Light auto-off after 30s
  if (lightOn() && (millis() - lightOnTime > 30000)) {
    setLight(false);
  }
  // Non-blocking buzzer: 200ms ON
  if (buzzActive && millis() - buzzStart >= 200) {
    setBuzzer(false);
  }
  if (scheduledStart && !isRunning) {
    time_t now = time(nullptr);
    if (now >= scheduledStart) {
      // Time to start!
      scheduledStart = 0;
      isRunning = true;
      currentStage = STAGE_MIX; // or STAGE_DELAY if you want to keep original delay
      stageStartTime = millis();
    } else {
      // Waiting: optionally blink a light, etc.
      yield(); delay(100);
      return;
    }
  }
  yield(); delay(1);
}

// Helper: set outputs with PWM (77 = ~1V, 0 = OFF)
inline void setHeater(bool on) { analogWrite(PIN_HEATER, on ? 77 : 0); }
inline void setMotor(bool on)  { analogWrite(PIN_MOTOR,  on ? 77 : 0); }
inline void setLight(bool on)  { analogWrite(PIN_LIGHT,  on ? 77 : 0); if (on) lightOnTime = millis(); }
inline void setBuzzer(bool on) { analogWrite(PIN_BUZZER, on ? 77 : 0); buzzActive = on; if (on) buzzStart = millis(); }
inline bool heaterOn() { return analogRead(PIN_HEATER) > 5; }
inline bool motorOn()  { return analogRead(PIN_MOTOR)  > 5; }
inline bool lightOn()  { return analogRead(PIN_LIGHT)  > 5; }
inline bool buzzerOn() { return analogRead(PIN_BUZZER) > 5; }

void loadPrograms() {
  File f=LittleFS.open("/programs.json","r"); if(!f) return;
  DynamicJsonDocument d(8192); if(deserializeJson(d,f)){ f.close(); return;} f.close();
  if(d.containsKey("calibration")){
    calibrationSlope  = d["calibration"]["slope"].as<float>();
    calibrationOffset = d["calibration"]["offset"].as<float>();
  }
  programs.clear();
  for(JsonPair kv: d["programs"].as<JsonObject>()){
    JsonObject v=kv.value().as<JsonObject>();
    Program p={
      v["delayStart"]  |0,
      v["mixTime"]     |0,
      v["bulkTemp"]    |0.0f,
      v["bulkTime"]    |0,
      v["knockDownTime"]|0,
      v["riseTemp"]    |0.0f,
      v["riseTime"]    |0,
      v["bake1Temp"]   |0.0f,
      v["bake1Time"]   |0,
      v["bake2Temp"]   |0.0f,
      v["bake2Time"]   |0,
      v["coolTime"]    |0
    };
    programs[kv.key().c_str()] = p;
  }
}

void savePrograms() {
  File f=LittleFS.open("/programs.json","r"); if(!f) return;
  DynamicJsonDocument d(8192); if(deserializeJson(d,f)){ f.close(); return;} f.close();
  d["calibration"]["slope"]=calibrationSlope; d["calibration"]["offset"]=calibrationOffset;
  File fw=LittleFS.open("/programs.json","w"); if(!fw)return;
  serializeJson(d,fw); fw.close();
}

void stopBreadmaker() {
  isRunning = false;
  setHeater(false);
  setMotor(false);
  setLight(false);
  setBuzzer(true); // Buzz for 200ms (handled non-blocking in loop)
  currentStage = STAGE_IDLE;
}
