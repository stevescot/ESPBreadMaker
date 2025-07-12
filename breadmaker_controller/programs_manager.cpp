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
  for (JsonObject pobj : doc.as<JsonArray>()) {
    Program p;
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
    programs.push_back(p);
  }
}

