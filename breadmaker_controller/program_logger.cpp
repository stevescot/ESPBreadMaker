#include "program_logger.h"
#include "missing_stubs.h"

// Global logger state
ProgramRunLog currentLog;
bool loggingEnabled = true;

// Initialize the logging system
bool initializeLogger() {
  if (!createLogDirectory()) {
    Serial.println("[LOGGER] Failed to create log directory");
    return false;
  }
  
  // Clean up old logs on startup
  cleanupOldLogs();
  
  resetCurrentLog();
  
  Serial.println("[LOGGER] Program logger initialized");
  return true;
}

// Generate unique run ID based on timestamp
String generateRunId() {
  time_t now = time(nullptr);
  if (now < 1640995200) { // Before Jan 1, 2022
    // Use millis-based ID if NTP not synced
    return "run_" + String(millis());
  }
  
  struct tm* timeinfo = localtime(&now);
  char buffer[32];
  strftime(buffer, sizeof(buffer), "run_%Y%m%d_%H%M%S", timeinfo);
  return String(buffer);
}

// Start logging a new program run
bool startProgramLog(const String& programName, uint8_t programId) {
  if (!loggingEnabled) return false;
  
  // Complete any existing log first
  if (currentLog.isActive) {
    Serial.println("[LOGGER] Completing previous log before starting new one");
    completeProgramLog(false);
  }
  
  resetCurrentLog();
  
  currentLog.runId = generateRunId();
  currentLog.programName = programName;
  currentLog.programId = programId;
  currentLog.startTime = time(nullptr);
  currentLog.totalPlannedTime = 0;
  currentLog.isActive = true;
  currentLog.logFilePath = getLogFilePath(currentLog.runId);
  currentLog.lastTempLog = 0;
  
  // Log program start event
  logEvent(LOG_PROGRAM_START, "Program started: " + programName);
  
  Serial.printf("[LOGGER] Started logging program: %s (ID: %s)\n", 
                programName.c_str(), currentLog.runId.c_str());
  
  return saveProgramLog();
}

// Log the start of a stage
bool logStageStart(uint8_t stageIndex, const String& stageName, bool isFermentation) {
  if (!loggingEnabled || !currentLog.isActive || stageIndex >= MAX_PROGRAM_STAGES) {
    return false;
  }
  
  // End previous stage if it exists
  if (currentLog.stageCount > 0) {
    logStageEnd(currentLog.stageCount - 1);
  }
  
  StageLogData& stage = currentLog.stages[currentLog.stageCount];
  stage.stageIndex = stageIndex;
  stage.stageName = stageName;
  stage.startTime = time(nullptr);
  stage.endTime = 0;
  stage.isFermentation = isFermentation;
  stage.avgTemperature = getAveragedTemperature();
  stage.setpoint = 0; // Will be updated during logging
  stage.tempHistoryCount = 0;
  stage.overrideDuration = 0;
  
  if (isFermentation) {
    stage.startFactor = fermentState.fermentationFactor;
    stage.biologicalTime = 0;
  }
  
  currentLog.stageCount++;
  
  logEvent(LOG_STAGE_START, "Stage started: " + stageName);
  
  Serial.printf("[LOGGER] Stage %d started: %s (fermentation: %s)\n", 
                stageIndex, stageName.c_str(), isFermentation ? "yes" : "no");
  
  return saveProgramLog();
}

// Log the end of a stage
bool logStageEnd(uint8_t stageIndex) {
  if (!loggingEnabled || !currentLog.isActive || stageIndex >= currentLog.stageCount) {
    return false;
  }
  
  StageLogData& stage = currentLog.stages[stageIndex];
  if (stage.endTime != 0) return true; // Already ended
  
  stage.endTime = time(nullptr);
  stage.actualDuration = stage.endTime - stage.startTime;
  
  if (stage.isFermentation) {
    stage.endFactor = fermentState.fermentationFactor;
    stage.biologicalTime = fermentState.accumulatedFermentMinutes;
  }
  
  calculateStageSummary(stage);
  
  logEvent(LOG_STAGE_END, "Stage completed: " + stage.stageName + 
           " (duration: " + formatDuration(stage.actualDuration) + ")");
  
  Serial.printf("[LOGGER] Stage %d completed: %s (duration: %ds)\n", 
                stageIndex, stage.stageName.c_str(), stage.actualDuration);
  
  return saveProgramLog();
}

// Log a temperature sample
bool logTemperatureSample(float temperature, float setpoint, float fermentationFactor) {
  if (!loggingEnabled || !currentLog.isActive || currentLog.stageCount == 0) {
    return false;
  }
  
  // Add to current stage's temperature history
  StageLogData& currentStage = currentLog.stages[currentLog.stageCount - 1];
  if (currentStage.tempHistoryCount < 50) {
    TempHistoryPoint& point = currentStage.tempHistory[currentStage.tempHistoryCount];
    point.timestamp = time(nullptr);
    point.temperature = temperature;
    point.setpoint = setpoint;
    point.fermentationFactor = fermentationFactor;
    point.isValid = true;
    currentStage.tempHistoryCount++;
    
    // Update stage setpoint
    currentStage.setpoint = setpoint;
  }
  
  currentLog.lastTempLog = millis();
  return true;
}

// Log an event
bool logEvent(LogEventType type, const String& description) {
  if (!loggingEnabled || !currentLog.isActive || currentLog.eventCount >= 50) {
    return false;
  }
  
  auto& event = currentLog.events[currentLog.eventCount];
  event.timestamp = time(nullptr);
  event.type = type;
  event.description = description;
  currentLog.eventCount++;
  
  return true;
}

// Log user override
bool logUserOverride(const String& overrideType, const String& details) {
  currentLog.overrideCount++;
  return logEvent(LOG_USER_OVERRIDE, overrideType + ": " + details);
}

// Complete program logging
bool completeProgramLog(bool wasCompleted) {
  if (!loggingEnabled || !currentLog.isActive) {
    return false;
  }
  
  currentLog.endTime = time(nullptr);
  currentLog.completed = wasCompleted;
  currentLog.totalDuration = currentLog.endTime - currentLog.startTime;
  
  // End current stage if active
  if (currentLog.stageCount > 0) {
    logStageEnd(currentLog.stageCount - 1);
  }
  
  calculateProgramSummary();
  
  logEvent(wasCompleted ? LOG_PROGRAM_COMPLETE : LOG_PROGRAM_STOPPED, 
           wasCompleted ? "Program completed successfully" : "Program stopped by user");
  
  currentLog.isActive = false;
  
  bool result = saveProgramLog();
  
  Serial.printf("[LOGGER] Program log completed: %s (duration: %s, completed: %s)\n",
                currentLog.runId.c_str(), 
                formatDuration(currentLog.totalDuration).c_str(),
                wasCompleted ? "yes" : "no");
  
  return result;
}

// Save current log to file
bool saveProgramLog() {
  if (!loggingEnabled || currentLog.runId.isEmpty()) {
    return false;
  }
  
  return writeLogToFile(currentLog.logFilePath);
}

// Update periodic temperature logging
void updatePeriodicTemperatureLogging() {
  if (!loggingEnabled || !currentLog.isActive) {
    return;
  }
  
  unsigned long now = millis();
  if (currentLog.lastTempLog == 0 || (now - currentLog.lastTempLog) >= TEMP_LOG_INTERVAL_MS) {
    float temp = getAveragedTemperature();
    float setpoint = 0; // Get from current program state
    float factor = fermentState.fermentationFactor;
    
    logTemperatureSample(temp, setpoint, factor);
  }
}

// Create log directory
bool createLogDirectory() {
  if (!FFat.exists(LOG_DIR)) {
    return FFat.mkdir(LOG_DIR);
  }
  return true;
}

// Clean up old log files
bool cleanupOldLogs() {
  // Implementation would scan directory and remove old files
  // For now, just return true
  return true;
}

// Get log file path
String getLogFilePath(const String& runId) {
  return String(LOG_DIR) + "/" + runId + ".json";
}

// Write log to JSON file
bool writeLogToFile(const String& filePath) {
  File file = FFat.open(filePath, "w");
  if (!file) {
    Serial.printf("[LOGGER] Failed to open log file: %s\n", filePath.c_str());
    return false;
  }
  
  String json = serializeLogToJson();
  size_t written = file.print(json);
  file.close();
  
  bool success = (written > 0);
  if (!success) {
    Serial.printf("[LOGGER] Failed to write log file: %s\n", filePath.c_str());
  }
  
  return success;
}

// Serialize log to JSON
String serializeLogToJson() {
  String json = "{\n";
  json += "  \"runId\": \"" + currentLog.runId + "\",\n";
  json += "  \"programName\": \"" + currentLog.programName + "\",\n";
  json += "  \"programId\": " + String(currentLog.programId) + ",\n";
  json += "  \"startTime\": \"" + formatTimestamp(currentLog.startTime) + "\",\n";
  
  if (currentLog.endTime > 0) {
    json += "  \"endTime\": \"" + formatTimestamp(currentLog.endTime) + "\",\n";
  }
  
  json += "  \"completed\": " + String(currentLog.completed ? "true" : "false") + ",\n";
  json += "  \"totalDuration\": " + String(currentLog.totalDuration) + ",\n";
  
  // Stages array
  json += "  \"stages\": [\n";
  for (uint8_t i = 0; i < currentLog.stageCount; i++) {
    if (i > 0) json += ",\n";
    json += serializeStageToJson(currentLog.stages[i]);
  }
  json += "\n  ],\n";
  
  // Events array
  json += "  \"events\": [\n";
  for (uint8_t i = 0; i < currentLog.eventCount; i++) {
    if (i > 0) json += ",\n";
    json += serializeEventToJson(i);
  }
  json += "\n  ],\n";
  
  // Summary
  json += "  \"summary\": {\n";
  json += "    \"totalPlannedTime\": " + String(currentLog.totalPlannedTime) + ",\n";
  json += "    \"totalActualTime\": " + String(currentLog.totalActualTime) + ",\n";
  json += "    \"fermentationStages\": " + String(currentLog.fermentationStages) + ",\n";
  json += "    \"avgTemperature\": " + String(currentLog.avgTemperature, 1) + ",\n";
  json += "    \"overrides\": " + String(currentLog.overrideCount) + ",\n";
  json += "    \"interventions\": " + String(currentLog.interventionCount) + "\n";
  json += "  }\n";
  
  json += "}";
  return json;
}

// Serialize stage to JSON
String serializeStageToJson(const StageLogData& stage) {
  String json = "    {\n";
  json += "      \"stageIndex\": " + String(stage.stageIndex) + ",\n";
  json += "      \"stageName\": \"" + stage.stageName + "\",\n";
  json += "      \"startTime\": \"" + formatTimestamp(stage.startTime) + "\",\n";
  
  if (stage.endTime > 0) {
    json += "      \"endTime\": \"" + formatTimestamp(stage.endTime) + "\",\n";
  }
  
  json += "      \"plannedDuration\": " + String(stage.plannedDuration) + ",\n";
  json += "      \"actualDuration\": " + String(stage.actualDuration) + ",\n";
  
  if (stage.overrideDuration > 0) {
    json += "      \"overrideDuration\": " + String(stage.overrideDuration) + ",\n";
  }
  
  json += "      \"isFermentation\": " + String(stage.isFermentation ? "true" : "false") + ",\n";
  json += "      \"avgTemperature\": " + String(stage.avgTemperature, 1) + ",\n";
  json += "      \"setpoint\": " + String(stage.setpoint, 1);
  
  if (stage.isFermentation) {
    json += ",\n      \"fermentationData\": {\n";
    json += "        \"startFactor\": " + String(stage.startFactor, 3) + ",\n";
    json += "        \"endFactor\": " + String(stage.endFactor, 3) + ",\n";
    json += "        \"avgFactor\": " + String(stage.avgFactor, 3) + ",\n";
    json += "        \"biologicalTime\": " + String(stage.biologicalTime, 1) + ",\n";
    json += "        \"temperatureHistory\": [\n";
    
    for (int i = 0; i < stage.tempHistoryCount; i++) {
      if (i > 0) json += ",\n";
      const TempHistoryPoint& point = stage.tempHistory[i];
      json += "          {\"time\": \"" + formatTimestamp(point.timestamp) + "\", ";
      json += "\"temp\": " + String(point.temperature, 1) + ", ";
      json += "\"factor\": " + String(point.fermentationFactor, 3) + "}";
    }
    
    json += "\n        ]\n      }";
  }
  
  json += "\n    }";
  return json;
}

// Serialize event to JSON
String serializeEventToJson(int eventIndex) {
  const auto& event = currentLog.events[eventIndex];
  String json = "    {\n";
  json += "      \"time\": \"" + formatTimestamp(event.timestamp) + "\",\n";
  json += "      \"type\": \"" + String(static_cast<int>(event.type)) + "\",\n";
  json += "      \"description\": \"" + event.description + "\"\n";
  json += "    }";
  return json;
}

// Calculate stage summary statistics
void calculateStageSummary(StageLogData& stage) {
  if (stage.tempHistoryCount > 0) {
    stage.avgTemperature = calculateAverageTemperature(stage.tempHistory, stage.tempHistoryCount);
    
    if (stage.isFermentation) {
      stage.avgFactor = calculateAverageFermentationFactor(stage.tempHistory, stage.tempHistoryCount);
    }
  }
}

// Calculate program summary statistics
void calculateProgramSummary() {
  currentLog.totalActualTime = currentLog.totalDuration;
  currentLog.fermentationStages = 0;
  float tempSum = 0;
  int tempCount = 0;
  
  for (uint8_t i = 0; i < currentLog.stageCount; i++) {
    const StageLogData& stage = currentLog.stages[i];
    currentLog.totalPlannedTime += stage.plannedDuration;
    
    if (stage.isFermentation) {
      currentLog.fermentationStages++;
    }
    
    if (stage.tempHistoryCount > 0) {
      for (int j = 0; j < stage.tempHistoryCount; j++) {
        tempSum += stage.tempHistory[j].temperature;
        tempCount++;
      }
    }
  }
  
  if (tempCount > 0) {
    currentLog.avgTemperature = tempSum / tempCount;
  }
}

// Calculate average temperature from history
float calculateAverageTemperature(const TempHistoryPoint history[], int count) {
  if (count == 0) return 0.0;
  
  float sum = 0;
  for (int i = 0; i < count; i++) {
    sum += history[i].temperature;
  }
  return sum / count;
}

// Calculate average fermentation factor from history
float calculateAverageFermentationFactor(const TempHistoryPoint history[], int count) {
  if (count == 0) return 1.0;
  
  float sum = 0;
  for (int i = 0; i < count; i++) {
    sum += history[i].fermentationFactor;
  }
  return sum / count;
}

// Format timestamp as ISO string
String formatTimestamp(time_t timestamp) {
  if (timestamp == 0) return "";
  
  struct tm* timeinfo = localtime(&timestamp);
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", timeinfo);
  return String(buffer);
}

// Format duration in human readable form
String formatDuration(uint32_t seconds) {
  if (seconds < 60) {
    return String(seconds) + "s";
  } else if (seconds < 3600) {
    return String(seconds / 60) + "m " + String(seconds % 60) + "s";
  } else {
    uint32_t hours = seconds / 3600;
    uint32_t minutes = (seconds % 3600) / 60;
    return String(hours) + "h " + String(minutes) + "m";
  }
}

// Check if timestamp is valid
bool isValidTimestamp(time_t timestamp) {
  return timestamp > 1640995200; // After Jan 1, 2022
}

// Reset current log structure
void resetCurrentLog() {
  currentLog.runId = "";
  currentLog.programName = "";
  currentLog.programId = 0;
  currentLog.startTime = 0;
  currentLog.endTime = 0;
  currentLog.completed = false;
  currentLog.totalDuration = 0;
  currentLog.stageCount = 0;
  currentLog.eventCount = 0;
  currentLog.totalPlannedTime = 0;
  currentLog.totalActualTime = 0;
  currentLog.fermentationStages = 0;
  currentLog.avgTemperature = 0;
  currentLog.overrideCount = 0;
  currentLog.interventionCount = 0;
  currentLog.isActive = false;
  currentLog.logFilePath = "";
  currentLog.lastTempLog = 0;
}

// Integration helper functions
void checkAndStartLogging() {
  if (programState.isRunning && !currentLog.isActive) {
    // Try to get program name
    String programName = "Unknown Program";
    // This would need to be integrated with the program loading system
    startProgramLog(programName, programState.activeProgramId);
  }
}

void checkPeriodicLogging() {
  updatePeriodicTemperatureLogging();
}

bool shouldLogTemperature() {
  if (!currentLog.isActive) return false;
  
  unsigned long now = millis();
  return (currentLog.lastTempLog == 0 || (now - currentLog.lastTempLog) >= TEMP_LOG_INTERVAL_MS);
}
