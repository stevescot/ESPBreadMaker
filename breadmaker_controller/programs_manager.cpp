#include "programs_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

std::map<String, Program> programs;

void loadPrograms() {
  File f = LittleFS.open("/programs.json", "r");
  if (!f) return;
  DynamicJsonDocument d(8192);
  if (deserializeJson(d, f)) { f.close(); return; }
  f.close();
  programs.clear();
  if (d.is<JsonArray>()) {
    // New array format
    for (JsonObject v : d.as<JsonArray>()) {
      Program p;
      p.name = v["name"] | "";
      p.autolyseTime = v.containsKey("autolyseTime") ? int(v["autolyseTime"]) : (v.containsKey("delayStart") ? int(v["delayStart"]) : 0);
      p.mixTime = v["mixTime"] | 0;
      p.bulkTemp = v["bulkTemp"] | 0;
      p.bulkTime = v["bulkTime"] | 0;
      p.knockDownTime = v["knockDownTime"] | 0;
      p.riseTemp = v["riseTemp"] | 0;
      p.riseTime = v["riseTime"] | 0;
      p.bake1Temp = v["bake1Temp"] | 0;
      p.bake1Time = v["bake1Time"] | 0;
      p.bake2Temp = v["bake2Temp"] | 0;
      p.bake2Time = v["bake2Time"] | 0;
      p.coolTime = v["coolTime"] | 0;
      // Parse customStages if present
      p.customStages.clear();
      if (v.containsKey("customStages")) {
        for (JsonObject cs : v["customStages"].as<JsonArray>()) {
          CustomStage stage;
          stage.label = cs["label"] | "";
          stage.min = cs["min"] | 0;
          stage.temp = cs["temp"] | 0;
          stage.heater = cs["heater"] | "off";
          stage.light = cs["light"] | "off";
          stage.buzzer = cs["buzzer"] | "off";
          stage.instructions = cs["instructions"] | "";
          stage.mixPattern.clear();
          if (cs.containsKey("mixPattern")) {
            for (JsonObject mp : cs["mixPattern"].as<JsonArray>()) {
              MixStep step;
              step.action = mp["action"] | "wait";
              step.durationSec = mp["durationSec"] | 0;
              stage.mixPattern.push_back(step);
            }
          }
          p.customStages.push_back(stage);
        }
      }
      if (p.name.length() > 0) programs[p.name] = p;
    }
  } else if (d.containsKey("programs")) {
    // Old object format
    for (JsonPair kv : d["programs"].as<JsonObject>()) {
      JsonObject v = kv.value().as<JsonObject>();
      Program p;
      p.name = kv.key().c_str();
      p.autolyseTime = v.containsKey("autolyseTime") ? int(v["autolyseTime"]) : (v.containsKey("delayStart") ? int(v["delayStart"]) : 0);
      p.mixTime = v["mixTime"] | 0;
      p.bulkTemp = v["bulkTemp"] | 0;
      p.bulkTime = v["bulkTime"] | 0;
      p.knockDownTime = v["knockDownTime"] | 0;
      p.riseTemp = v["riseTemp"] | 0;
      p.riseTime = v["riseTime"] | 0;
      p.bake1Temp = v["bake1Temp"] | 0;
      p.bake1Time = v["bake1Time"] | 0;
      p.bake2Temp = v["bake2Temp"] | 0;
      p.bake2Time = v["bake2Time"] | 0;
      p.coolTime = v["coolTime"] | 0;
      // No customStages in old format
      if (p.name.length() > 0) programs[p.name] = p;
    }
  }
}

void savePrograms() {
  File f=LittleFS.open("/programs.json","r"); if(!f) return;
  DynamicJsonDocument d(8192); if(deserializeJson(d,f)){ f.close(); return;} f.close();
  d["calibration"]["slope"]=calibrationSlope; d["calibration"]["offset"]=calibrationOffset;
  File fw=LittleFS.open("/programs.json","w"); if(!fw)return;
  serializeJson(d,fw); fw.close();
}
