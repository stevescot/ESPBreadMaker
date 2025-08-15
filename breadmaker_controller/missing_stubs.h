#pragma once
#include <Arduino.h>
#include <WebServer.h>

// Function declarations for missing implementations

// Temperature and performance functions
float getAveragedTemperature();
void updateTemperatureSampling();
void updatePerformanceMetrics();
void checkAndSwitchPIDProfile();

// Performance metrics getter functions
unsigned long getMaxLoopTime();
unsigned long getAverageLoopTime();
unsigned long getLoopCount();
uint32_t getMinFreeHeap();
unsigned long getWifiReconnectCount();
float getHeapFragmentation();

// Program and state management functions
void updateActiveProgramVars();
void stopBreadmaker();
bool isStartupDelayComplete();

// PID and control functions
void updateTimeProportionalHeater();
void updatePIDTerms();

// Settings functions
void loadSettings();
void saveSettings();
void switchToProfile(const String& profileName);

// Endpoint functions
void otaEndpoints(WebServer& server);

// OTA display function
void displayMessage(const String& message);

// Fermentation calculation functions
float calculateFermentationFactor(float actualTemp);
// REMOVED: updateFermentationFactor() - redundant function, fermentation handled in updateFermentationTiming()
void updateFermentationCache();
unsigned long getAdjustedStageTimeMs(unsigned long baseTimeMs, bool hasFermentation);

// JSON streaming functions  
void streamStatusJson(Print& out);
