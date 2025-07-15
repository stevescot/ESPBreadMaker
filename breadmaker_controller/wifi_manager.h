#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

extern const byte DNS_PORT;
extern const char* WIFI_FILE;

bool loadWiFiCreds(String &ssid, String &pass);
void startCaptivePortal();
void cleanupCaptivePortalEndpoints();
