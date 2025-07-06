#include <LittleFS.h>
#include <ArduinoJson.h>
#include "programs_manager.h"

// --- Programs map ---
std::map<String, Program> programs;

// --- Program management implementation ---

// Load programs from LittleFS
void loadPrograms() {
  programs.clear();
  File f = LittleFS.open("/programs.json", "r");
  if (!f || f.size() == 0) {
    if (f) f.close();
    return;
  }
  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  for (JsonObject pobj : doc.as<JsonArray>()) {
    Program p;
    p.name = pobj["name"].as<String>();
    p.notes = pobj["notes"] | String("");
    p.icon = pobj["icon"] | String("");
    p.customStages.clear();
    for (JsonObject st : pobj["customStages"].as<JsonArray>()) {
      CustomStage cs;
      cs.label = st["label"].as<String>();
      cs.min = st["min"] | 0;
      cs.temp = st["temp"] | 0.0;
      cs.noMix = st["noMix"] | false;
      cs.light = st["light"] | String("");
      cs.buzzer = st["buzzer"] | String("");
      cs.mixPattern.clear();
      for (JsonObject m : st["mixPattern"].as<JsonArray>()) {
        MixStep ms;
        ms.mixSec = m["mixSec"] | 0;
        ms.waitSec = m["waitSec"] | 0;
        ms.durationSec = m["durationSec"] | 0;
        cs.mixPattern.push_back(ms);
      }
      p.customStages.push_back(cs);
    }
    programs[p.name] = p;
  }
}

// Save programs to LittleFS
void savePrograms() {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();
  for (auto &kv : programs) {
    Program &p = kv.second;
    JsonObject pobj = arr.createNestedObject();
    pobj["name"] = p.name;
    pobj["notes"] = p.notes;
    pobj["icon"] = p.icon;
    JsonArray stagesArr = pobj.createNestedArray("customStages");
    for (auto &cs : p.customStages) {
      JsonObject st = stagesArr.createNestedObject();
      st["label"] = cs.label;
      st["min"] = cs.min;
      st["temp"] = cs.temp;
      st["noMix"] = cs.noMix;
      st["light"] = cs.light;
      st["buzzer"] = cs.buzzer;
      JsonArray mixArr = st.createNestedArray("mixPattern");
      for (auto &ms : cs.mixPattern) {
        JsonObject m = mixArr.createNestedObject();
        m["mixSec"] = ms.mixSec;
        m["waitSec"] = ms.waitSec;
        m["durationSec"] = ms.durationSec;
      }
    }
  }
  File f = LittleFS.open("/programs.json", "w");
  if (f) { serializeJson(doc, f); f.close(); }
}
