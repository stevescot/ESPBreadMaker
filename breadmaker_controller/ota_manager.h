#pragma once
#include <Arduino.h>

// OTA manager for ESP32 breadmaker controller
void otaManagerInit();
void otaManagerLoop();
void setOTAPassword(const String& password);
bool isOTAEnabled();
void enableOTA(bool enabled);
String getOTAHostname();
void setOTAHostname(const String& hostname);

// OTA status for web interface
struct OTAStatus {
  bool enabled = true;
  bool inProgress = false;
  int progress = 0;
  String error = "";
  String hostname = "breadmaker-controller";
};

extern OTAStatus otaStatus;
