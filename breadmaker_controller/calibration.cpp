#include "calibration.h"
#include "globals.h"  // For PIN_RTD definition
#include <FFat.h>
#include <ArduinoJson.h>

const char* CALIB_FILE = "/calibration.json";
std::vector<CalibPoint> rtdCalibTable;
float calibrationSlope = 1.0f;
float calibrationOffset = 0.0f;

void saveCalibration() {
  // MEMORY OPTIMIZATION: Use streaming to avoid large DynamicJsonDocument allocation
  File f = FFat.open(CALIB_FILE, "w");
  if (!f) return;
  
  // Stream JSON directly to file instead of building in memory
  f.print("{\"table\":[");
  for(size_t i = 0; i < rtdCalibTable.size(); i++) {
    if (i > 0) f.print(',');
    f.print("{\"raw\":");
    f.print(rtdCalibTable[i].raw);
    f.print(",\"temp\":");
    f.print(rtdCalibTable[i].temp, 2);
    f.print('}');
  }
  f.print("]}");
  f.close();
}

void loadCalibration() {
  rtdCalibTable.clear();
  File f = FFat.open(CALIB_FILE, "r");
  if (!f) return;
  
  // MEMORY OPTIMIZATION: Reduced from 4096 to 1024 bytes (75% reduction)
  DynamicJsonDocument doc(1024);
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
  
  // CRITICAL SAFETY CHECK: If raw reading is zero, immediately turn off heater
  if (raw == 0) {
    Serial.println("CRITICAL SAFETY ALERT: Raw temperature reading is ZERO - sensor failure detected!");
    Serial.println("Immediately shutting off heater for safety");
    // Turn off heater immediately for safety
    extern void setHeater(bool);  // Forward declaration
    setHeater(false);
    // SAFETY FIX: Return HIGH temperature to prevent PID from heating!
    return 999.0f;  // Return HIGH error value - PID will think it's too hot and stop heating
  }
  
  float temp = tempFromRaw(raw);
  
  // Temperature sensor fault detection
  if (temp > 250.0f || temp < -40.0f) {
    Serial.println("WARNING: Temperature sensor fault detected - extreme reading: " + String(temp) + "°C");
    Serial.println("Raw ADC value: " + String(raw));
    return 25.0f;  // Return safe room temperature default
  }
  
  return temp;
}
