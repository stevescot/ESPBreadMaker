#include "calibration.h"
#include "globals.h"  // For PIN_RTD definition
#include <FFat.h>
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
  File f = FFat.open(CALIB_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

void loadCalibration() {
  rtdCalibTable.clear();
  File f = FFat.open(CALIB_FILE, "r");
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
  int raw = analogRead(PIN_RTD);  // RTD on ESP32 analog pin
  float temp = tempFromRaw(raw);
  
  // Temperature sensor fault detection
  if (temp > 200.0f || temp < -40.0f) {
    Serial.println("WARNING: Temperature sensor fault detected - extreme reading: " + String(temp) + "°C");
    Serial.println("Raw ADC value: " + String(raw));
    return 25.0f;  // Return safe room temperature default
  }
  
  return temp;
}
