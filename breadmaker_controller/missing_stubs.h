#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Function declarations for missing implementations

// Temperature and performance functions
float getAveragedTemperature();
void updateTemperatureSampling();
void updatePerformanceMetrics();
void checkAndSwitchPIDProfile();

// Program and state management functions
void updateActiveProgramVars();
void stopBreadmaker();
bool isStartupDelayComplete();

// PID and control functions
void updateTimeProportionalHeater();

// Settings functions
void loadSettings();
void saveSettings();
void switchToProfile(const String& profileName);

// Endpoint functions
void otaEndpoints(AsyncWebServer& server);

// OTA display function
void displayMessage(const String& message);
