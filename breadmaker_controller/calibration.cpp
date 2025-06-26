#include "calibration.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

const char* CALIB_FILE = "/calibration.json";
std::vector<CalibPoint> rtdCalibTable;
float calibrationSlope = 1.0f;
float calibrationOffset = 0.0f;

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

float readTemperature() {
  int raw = analogRead(A0); // PIN_RTD should be defined in main
  return tempFromRaw(raw);
}
