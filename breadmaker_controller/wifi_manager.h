#pragma once
#include <Arduino.h>

extern const byte DNS_PORT;
extern const char* WIFI_FILE;

bool loadWiFiCreds(String &ssid, String &pass);
void startCaptivePortal();
void processCaptivePortalDNS();
void cleanupCaptivePortalEndpoints();

// Simple WiFiManager-based captive portal  
void startWiFiManagerPortal();
