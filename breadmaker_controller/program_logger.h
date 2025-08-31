#pragma once
#include <Arduino.h>

// Activity logging for ESP32 breadmaker controller
// Logs program events, stage changes, and system events to /activity.log

void initActivityLog();
void logProgramStart(const String& programName, int programId);
void logProgramStop(const String& reason);
void logProgramComplete();
void logProgramComplete(const String& programName, unsigned long totalSeconds);
void logStageStart(const String& stageName, int stageIdx, float temperature);
void logStageEnd(const String& stageName, int stageIdx, unsigned long durationSec, float endTemperature);
void logMixingStart(const String& stepName, int mixIdx);
void logMixingEnd(const String& stepName, int mixIdx, unsigned long durationSec);
void logMixStart(int patternIndex, unsigned long elapsedMs);
void logMixStop(int patternIndex, unsigned long elapsedMs);
void logMixCycleComplete(int totalPatterns);
void logMixPatternAdvance(int newPatternIndex);
void logTemperatureEvent(const String& event, float temperature);
void logTemperatureTargetChange(double newTarget, double currentTemp);
void logEmergencyShutdown(const String& reason, float temperature);
void logSystemEvent(const String& event);
void logFermentationUpdate(float factor, float scheduledElapsed, float realElapsed);
void logFermentationProgress(double progressPercent, double factor, double temperature);

// Activity log management
void clearActivityLog();
String getActivityLogSize();
bool isActivityLogEnabled();
void setActivityLogEnabled(bool enabled);

// Internal helper functions
void writeLogEntry(const String& level, const String& category, const String& message);
String formatTimestamp();
String formatDuration(unsigned long seconds);