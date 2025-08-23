#include "wifi_manager.h"
#include <FFat.h>           // Use FATFS instead of LittleFS
#include <ArduinoJson.h>
#include <WiFi.h>           // ESP32 WiFi library
#include <WiFiManager.h>    // tzapu WiFiManager library

// External variables
extern bool debugSerial;

const byte DNS_PORT = 53;
const char* WIFI_FILE = "/wifi.json";

bool loadWiFiCreds(String &ssid, String &pass) {
  File f = FFat.open(WIFI_FILE, "r");
  if (!f) return false;
  // OPTIMIZATION: Use StaticJsonDocument instead of DynamicJsonDocument for small JSON
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, f)) { f.close(); return false; }
  f.close();
  if (!doc.containsKey("ssid") || !doc.containsKey("pass")) return false;
  ssid = String((const char*)doc["ssid"]);
  pass = String((const char*)doc["pass"]);
  return ssid.length() > 0;
}

void startWiFiManagerPortal() {
  if (debugSerial) Serial.println("[WiFi] Starting WiFiManager captive portal...");
  
  WiFiManager wm;
  
  // Reset settings if needed - uncomment for debugging
  // wm.resetSettings();
  
  // Set configuration portal timeout (5 minutes)
  wm.setConfigPortalTimeout(300);
  
  // Set custom AP name and password
  String apName = "BreadmakerSetup";
  String apPassword = "breadmaker123";
  
  if (debugSerial) Serial.printf("[WiFi] Starting AP: %s\n", apName.c_str());
  
  // Set callback for when connecting to previous WiFi fails, and enters Access Point mode
  wm.setAPCallback([](WiFiManager *myWiFiManager) {
    if (debugSerial) {
      Serial.println("[WiFi] Entered config mode");
      Serial.printf("[WiFi] AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
      Serial.printf("[WiFi] AP SSID: %s\n", myWiFiManager->getConfigPortalSSID().c_str());
      Serial.println("[WiFi] Connect to the AP and navigate to 192.168.4.1 to configure WiFi");
    }
  });

  // Set callback for when WiFi credentials are saved
  wm.setSaveConfigCallback([]() {
    if (debugSerial) {
      Serial.println("[WiFi] Configuration saved via WiFiManager");
      Serial.println("[WiFi] Device will restart shortly...");
    }
  });

  // Try to connect with saved credentials or start config portal
  bool connected = wm.autoConnect(apName.c_str(), apPassword.c_str());
  
  if (connected) {
    if (debugSerial) {
      Serial.println("[WiFi] WiFi connected successfully!");
      Serial.printf("[WiFi] IP address: %s\n", WiFi.localIP().toString().c_str());
    }
    
    // Save credentials to our JSON file for compatibility
    String ssid = WiFi.SSID();
    String pass = WiFi.psk();
    
    // OPTIMIZATION: Use StaticJsonDocument instead of DynamicJsonDocument for small JSON
    StaticJsonDocument<256> doc;
    doc["ssid"] = ssid;
    doc["pass"] = pass;
    
    File f = FFat.open(WIFI_FILE, "w");
    if (f) {
      serializeJson(doc, f);
      f.close();
      if (debugSerial) Serial.println("[WiFi] Credentials saved to FATFS");
    }
  } else {
    if (debugSerial) {
      Serial.println("[WiFi] Failed to connect to WiFi or configure via portal");
      Serial.println("[WiFi] Device will restart...");
    }
    delay(3000);
    ESP.restart();
  }
}

// Legacy functions for compatibility (not used with WiFiManager)
void startCaptivePortal() {
  if (debugSerial) Serial.println("[WiFi] Using WiFiManager instead of custom captive portal");
  startWiFiManagerPortal();
}

void processCaptivePortalDNS() {
  // Not needed with WiFiManager - it handles DNS internally
}

void cleanupCaptivePortalEndpoints() {
  // Not needed with WiFiManager
  if (debugSerial) Serial.println("[WiFi] WiFiManager handles cleanup automatically");
}
