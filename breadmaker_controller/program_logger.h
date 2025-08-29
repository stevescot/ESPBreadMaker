#pragma once
#include <Arduino.h>
#include <FFat.h>
#include <time.h>
#include "globals.h"

// Program logging system for comprehensive run tracking and history

// Configuration constants
#define MAX_LOG_FILES 20
#define MAX_LOG_AGE_DAYS 30
#define LOG_DIR "/logs"
#define TEMP_LOG_INTERVAL_MS 300000  // 5 minutes
#define MAX_TEMP_HISTORY_POINTS 200

// Log event types
enum LogEventType {
  LOG_PROGRAM_START,
  LOG_STAGE_START,
  LOG_STAGE_END,
  LOG_TEMPERATURE_SAMPLE,
  LOG_USER_OVERRIDE,
  LOG_MANUAL_INTERVENTION,
  LOG_PAUSE,
  LOG_RESUME,
  LOG_PROGRAM_COMPLETE,
  LOG_PROGRAM_STOPPED
};

// Temperature history point
struct TempHistoryPoint {
  time_t timestamp;
  float temperature;
  float setpoint;
  float fermentationFactor;
  bool isValid;
};

// Stage log data
struct StageLogData {
  uint8_t stageIndex;
  String stageName;
  time_t startTime;
  time_t endTime;
  uint32_t plannedDuration;
  uint32_t actualDuration;
  uint32_t overrideDuration;
  bool isFermentation;
  float avgTemperature;
  float setpoint;
  
  // Fermentation-specific data
  float startFactor;
  float endFactor;
  float avgFactor;
  float biologicalTime;
  int tempHistoryCount;
  TempHistoryPoint tempHistory[50]; // Limited history per stage
};

// Program run log
struct ProgramRunLog {
  String runId;
  String programName;
  uint8_t programId;
  time_t startTime;
  time_t endTime;
  bool completed;
  uint32_t totalDuration;
  
  // Stage data
  uint8_t stageCount;
  StageLogData stages[MAX_PROGRAM_STAGES];
  
  // Events and interventions
  uint8_t eventCount;
  struct {
    time_t timestamp;
    LogEventType type;
    String description;
  } events[50];
  
  // Summary statistics
  uint32_t totalPlannedTime;
  uint32_t totalActualTime;
  uint8_t fermentationStages;
  float avgTemperature;
  uint8_t overrideCount;
  uint8_t interventionCount;
  
  // Current state
  bool isActive;
  String logFilePath;
  time_t lastTempLog;
};

// Global logger state
extern ProgramRunLog currentLog;
extern bool loggingEnabled;

// Core logging functions
bool initializeLogger();
String generateRunId();
bool startProgramLog(const String& programName, uint8_t programId);
bool logStageStart(uint8_t stageIndex, const String& stageName, bool isFermentation);
bool logStageEnd(uint8_t stageIndex);
bool logTemperatureSample(float temperature, float setpoint, float fermentationFactor);
bool logEvent(LogEventType type, const String& description);
bool logUserOverride(const String& overrideType, const String& details);
bool completeProgramLog(bool wasCompleted);
bool saveProgramLog();

// Temperature history management
bool addTemperaturePoint(float temperature, float setpoint, float fermentationFactor);
void updatePeriodicTemperatureLogging();

// File management
bool createLogDirectory();
bool cleanupOldLogs();
String getLogFilePath(const String& runId);
bool logFileExists(const String& runId);

// JSON serialization
bool writeLogToFile(const String& filePath);
String serializeLogToJson();
String serializeStageToJson(const StageLogData& stage);
String serializeEventToJson(int eventIndex);

// History and retrieval
bool getLogFileList(String logFiles[], int maxFiles, int& count);
bool loadLogFromFile(const String& runId, ProgramRunLog& log);
bool deleteLog(const String& runId);

// Statistics and analysis
void calculateStageSummary(StageLogData& stage);
void calculateProgramSummary();
float calculateAverageTemperature(const TempHistoryPoint history[], int count);
float calculateAverageFermentationFactor(const TempHistoryPoint history[], int count);

// Utility functions
String formatTimestamp(time_t timestamp);
String formatDuration(uint32_t seconds);
bool isValidTimestamp(time_t timestamp);
void resetCurrentLog();

// Integration helpers
void checkAndStartLogging();
void checkPeriodicLogging();
bool shouldLogTemperature();
