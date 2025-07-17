#include "ota_manager.h"
#include <ArduinoOTA.h>
#include <WiFi.h>
#include "globals.h"

// OTA status
OTAStatus otaStatus;

// OTA password (default)
String otaPassword = "breadmaker2024";

extern bool debugSerial;
extern void displayMessage(const String& message);

void otaManagerInit() {
  if (!otaStatus.enabled) {
    if (debugSerial) Serial.println("[OTA] OTA is disabled");
    return;
  }

  // Set OTA hostname
  ArduinoOTA.setHostname(otaStatus.hostname.c_str());
  
  // Set OTA password for security
  ArduinoOTA.setPassword(otaPassword.c_str());
  
  // Configure OTA callbacks
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    
    otaStatus.inProgress = true;
    otaStatus.progress = 0;
    otaStatus.error = "";
    
    if (debugSerial) {
      Serial.println("[OTA] Start updating " + type);
    }
    
    // Show OTA message on display
    displayMessage("OTA Update\nStarting...");
  });
  
  ArduinoOTA.onEnd([]() {
    otaStatus.inProgress = false;
    otaStatus.progress = 100;
    
    if (debugSerial) {
      Serial.println("[OTA] Update complete");
    }
    
    // Show completion message
    displayMessage("OTA Update\nComplete!\nRestarting...");
    delay(2000);
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    otaStatus.progress = (progress / (total / 100));
    
    if (debugSerial) {
      Serial.printf("[OTA] Progress: %u%%\r", otaStatus.progress);
    }
    
    // Update display with progress
    displayMessage("OTA Update\n" + String(otaStatus.progress) + "%");
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    otaStatus.inProgress = false;
    otaStatus.progress = 0;
    
    String errorMsg;
    switch (error) {
      case OTA_AUTH_ERROR:
        errorMsg = "Auth Failed";
        break;
      case OTA_BEGIN_ERROR:
        errorMsg = "Begin Failed";
        break;
      case OTA_CONNECT_ERROR:
        errorMsg = "Connect Failed";
        break;
      case OTA_RECEIVE_ERROR:
        errorMsg = "Receive Failed";
        break;
      case OTA_END_ERROR:
        errorMsg = "End Failed";
        break;
      default:
        errorMsg = "Unknown Error";
        break;
    }
    
    otaStatus.error = errorMsg;
    
    if (debugSerial) {
      Serial.printf("[OTA] Error[%u]: %s\n", error, errorMsg.c_str());
    }
    
    // Show error on display
    displayMessage("OTA Error\n" + errorMsg);
  });
  
  // Start OTA service
  ArduinoOTA.begin();
  
  if (debugSerial) {
    Serial.println("[OTA] OTA initialized");
    Serial.printf("[OTA] Hostname: %s\n", otaStatus.hostname.c_str());
    Serial.printf("[OTA] Password: %s\n", otaPassword.c_str());
  }
}

void otaManagerLoop() {
  if (otaStatus.enabled && WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
}

void setOTAPassword(const String& password) {
  otaPassword = password;
  if (debugSerial) {
    Serial.printf("[OTA] Password updated to: %s\n", password.c_str());
  }
}

bool isOTAEnabled() {
  return otaStatus.enabled;
}

void enableOTA(bool enabled) {
  otaStatus.enabled = enabled;
  if (debugSerial) {
    Serial.printf("[OTA] OTA %s\n", enabled ? "enabled" : "disabled");
  }
}

String getOTAHostname() {
  return otaStatus.hostname;
}

void setOTAHostname(const String& hostname) {
  otaStatus.hostname = hostname;
  if (debugSerial) {
    Serial.printf("[OTA] Hostname updated to: %s\n", hostname.c_str());
  }
}
