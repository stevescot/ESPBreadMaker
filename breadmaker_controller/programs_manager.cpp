#include <LittleFS.h>
#include <ArduinoJson.h>
#include "programs_manager.h"


// --- Programs map ---
std::vector<Program> programs;

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
  int maxId = -1;
  std::vector<Program> loadedPrograms;
  for (JsonObject pobj : doc.as<JsonArray>()) {
    Program p;
    // Read id if present, else assign sequentially
    if (pobj.containsKey("id")) {
      p.id = pobj["id"];
    } else {
      continue; // skip programs without id
    }
    if (p.id > maxId) maxId = p.id;
    p.name = pobj["name"].as<String>();
    p.notes = pobj["notes"] | String("");
    p.icon = pobj["icon"] | String("");
    p.fermentBaselineTemp = pobj["fermentBaselineTemp"] | 20.0f;
    p.fermentQ10 = pobj["fermentQ10"] | 2.0f;
    p.customStages.clear();
    for (JsonObject st : pobj["customStages"].as<JsonArray>()) {
      CustomStage cs;
      cs.label = st["label"].as<String>();
      cs.min = st["min"] | 0;
      cs.temp = st["temp"] | 0.0;
      cs.noMix = st["noMix"] | false;
      cs.isFermentation = st["isFermentation"] | false;
      cs.instructions = st["instructions"] | String("");
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
    loadedPrograms.push_back(p);
  }
  // Now, create a vector sized to maxId+1, fill with dummy programs (id=-1)
  programs.clear();
  programs.resize(maxId+1);
  for (auto& p : programs) p.id = -1;
  for (const auto& p : loadedPrograms) {
    if (p.id >= 0 && p.id < (int)programs.size()) {
      programs[p.id] = p;
    }
  }
}

// Save programs to LittleFS (including id)
void savePrograms() {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();
  for (const Program& p : programs) {
    JsonObject pobj = arr.createNestedObject();
    pobj["id"] = p.id;
    pobj["name"] = p.name;
    pobj["notes"] = p.notes;
    pobj["icon"] = p.icon;
    pobj["fermentBaselineTemp"] = p.fermentBaselineTemp;
    pobj["fermentQ10"] = p.fermentQ10;
    JsonArray stages = pobj.createNestedArray("customStages");
    for (const CustomStage& cs : p.customStages) {
      JsonObject st = stages.createNestedObject();
      st["label"] = cs.label;
      st["min"] = cs.min;
      st["temp"] = cs.temp;
      st["noMix"] = cs.noMix;
      st["isFermentation"] = cs.isFermentation;
      st["instructions"] = cs.instructions;
      st["light"] = cs.light;
      st["buzzer"] = cs.buzzer;
      JsonArray mixArr = st.createNestedArray("mixPattern");
      for (const MixStep& ms : cs.mixPattern) {
        JsonObject m = mixArr.createNestedObject();
        m["mixSec"] = ms.mixSec;
        m["waitSec"] = ms.waitSec;
        m["durationSec"] = ms.durationSec;
      }
    }
  }
  File f = LittleFS.open("/programs.json", "w");
  if (f) {
    serializeJsonPretty(doc, f);
    f.close();
  }
}


