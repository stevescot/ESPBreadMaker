#pragma once
#include <Arduino.h>
#include <WebServer.h>

// Function declarations for missing implementations

// Temperature and performance functions
double getAveragedTemperature();
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
void initializeStageArrays();
bool isStartupDelayComplete();

// PID and control functions
void updateTimeProportionalHeater();
void updatePIDTerms();
String getCurrentActiveProfileName();

// Settings functions
void loadSettings();
void saveSettings();
void switchToProfile(const String& profileName);
void savePIDProfiles();
void loadPIDProfiles();
void createDefaultPIDProfiles();

// Endpoint functions
void otaEndpoints(WebServer& server);

// OTA display function
void displayMessage(const String& message);

// Fermentation calculation functions
float calculateFermentationFactor(float actualTemp);
// REMOVED: updateFermentationFactor() - redundant function, fermentation handled in updateFermentationTiming()
void updateFermentationCache();
unsigned long getAdjustedStageTimeMs(unsigned long baseTimeMs, bool hasFermentation);

// Resume state management functions (from main .ino)
extern void saveResumeState();
extern void clearResumeState();

// JSON streaming functions  
void streamStatusJson(Print& out);

// Helper functions for fast endpoint timing arrays
void appendStageStatus(WebServer& server);
void appendPredictedStageEndTimes(WebServer& server);
void appendCachedStageTemperatures(WebServer& server);
void appendCachedStageOriginalDurations(WebServer& server);
void appendActualStageStartTimes(WebServer& server);
void appendActualStageEndTimes(WebServer& server);
void appendAdjustedStageDurations(WebServer& server);
