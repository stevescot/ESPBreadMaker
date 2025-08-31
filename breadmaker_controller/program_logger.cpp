#include "program_logger.h"
#include <FFat.h>
#include <WiFi.h>
#include <time.h>

// Activity log configuration
const char* ACTIVITY_LOG_FILE = "/activity.log";
const size_t MAX_LOG_SIZE = 32768; // 32KB max log size
static bool activityLogEnabled = true;
extern bool debugSerial;

// Initialize activity logging
void initActivityLog() {
  if (!activityLogEnabled) return;
  
  // Check if log file is too large and truncate if needed
  File logFile = FFat.open(ACTIVITY_LOG_FILE, "r");
  if (logFile && logFile.size() > MAX_LOG_SIZE) {
    logFile.close();
    
    // Read last 16KB of log to preserve recent entries
    logFile = FFat.open(ACTIVITY_LOG_FILE, "r");
    if (logFile) {
      String recentEntries = "";
      size_t keepSize = MAX_LOG_SIZE / 2; // Keep last 16KB
      if (logFile.size() > keepSize) {
        logFile.seek(logFile.size() - keepSize);
        recentEntries = logFile.readString();
      }
      logFile.close();
      
      // Rewrite file with recent entries
      logFile = FFat.open(ACTIVITY_LOG_FILE, "w");
      if (logFile) {
        logFile.print("=== LOG TRUNCATED ===\n");
        logFile.print(recentEntries);
        logFile.close();
      }
    }
  } else if (logFile) {
    logFile.close();
  }
  
  // Log system startup
  logSystemEvent("System startup - Activity logging initialized");
}

// Format timestamp for log entries
String formatTimestamp() {
  time_t now;
  time(&now);
  struct tm *timeinfo = localtime(&now);
  
  char buffer[32];
  if (timeinfo->tm_year > 100) { // Valid time from NTP
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  } else {
    // Use uptime if no NTP time available
    unsigned long uptime = millis() / 1000;
    snprintf(buffer, sizeof(buffer), "T+%02lu:%02lu:%02lu", 
             uptime/3600, (uptime%3600)/60, uptime%60);
  }
  return String(buffer);
}

// Format duration in human readable format
String formatDuration(unsigned long seconds) {
  if (seconds < 60) {
    return String(seconds) + "s";
  } else if (seconds < 3600) {
    return String(seconds/60) + "m " + String(seconds%60) + "s";
  } else {
    unsigned long hours = seconds / 3600;
    unsigned long mins = (seconds % 3600) / 60;
    unsigned long secs = seconds % 60;
    return String(hours) + "h " + String(mins) + "m " + String(secs) + "s";
  }
}

// Write a log entry to the activity log file
void writeLogEntry(const String& level, const String& category, const String& message) {
  if (!activityLogEnabled) return;
  
  String timestamp = formatTimestamp();
  String logLine = timestamp + " [" + level + "] " + category + ": " + message + "\n";
  
  // Write to serial if debug enabled
  if (debugSerial) {
    Serial.print("[ACTIVITY] " + logLine);
  }
  
  // Write to log file
  File logFile = FFat.open(ACTIVITY_LOG_FILE, "a");
  if (logFile) {
    logFile.print(logLine);
    logFile.close();
  }
}

// Program lifecycle logging
void logProgramStart(const String& programName, int programId) {
  String message = "Started program '" + programName + "' (ID: " + String(programId) + ")";
  writeLogEntry("INFO", "PROGRAM", message);
}

void logProgramStop(const String& reason) {
  String message = "Program stopped - " + reason;
  writeLogEntry("INFO", "PROGRAM", message);
}

void logProgramComplete() {
  writeLogEntry("INFO", "PROGRAM", "Program completed successfully");
}

// Stage lifecycle logging
void logStageStart(const String& stageName, int stageIdx, float temperature) {
  String message = "Stage " + String(stageIdx) + " started: '" + stageName + "' (Temp: " + String(temperature, 1) + "°C)";
  writeLogEntry("INFO", "STAGE", message);
}

void logStageEnd(const String& stageName, int stageIdx, unsigned long durationSec, float endTemperature) {
  String message = "Stage " + String(stageIdx) + " completed: '" + stageName + "' (Duration: " + 
                   formatDuration(durationSec) + ", End temp: " + String(endTemperature, 1) + "°C)";
  writeLogEntry("INFO", "STAGE", message);
}

// Mixing step logging
void logMixingStart(const String& stepName, int mixIdx) {
  String message = "Mix step " + String(mixIdx) + " started: " + stepName;
  writeLogEntry("DEBUG", "MIXING", message);
}

void logMixingEnd(const String& stepName, int mixIdx, unsigned long durationSec) {
  String message = "Mix step " + String(mixIdx) + " completed: " + stepName + 
                   " (Duration: " + formatDuration(durationSec) + ")";
  writeLogEntry("DEBUG", "MIXING", message);
}

// Temperature and system events
void logTemperatureEvent(const String& event, float temperature) {
  String message = event + " (Temp: " + String(temperature, 1) + "°C)";
  writeLogEntry("INFO", "TEMPERATURE", message);
}

void logEmergencyShutdown(const String& reason, float temperature) {
  String message = "EMERGENCY SHUTDOWN: " + reason + " (Temp: " + String(temperature, 1) + "°C)";
  writeLogEntry("ERROR", "SAFETY", message);
}

void logSystemEvent(const String& event) {
  writeLogEntry("INFO", "SYSTEM", event);
}

void logFermentationUpdate(float factor, float scheduledElapsed, float realElapsed) {
  String message = "Fermentation update - Factor: " + String(factor, 3) + 
                   ", Scheduled: " + String(scheduledElapsed, 1) + "s" +
                   ", Real: " + String(realElapsed, 1) + "s";
  writeLogEntry("DEBUG", "FERMENT", message);
}

// Activity log management functions
void clearActivityLog() {
  if (FFat.exists(ACTIVITY_LOG_FILE)) {
    FFat.remove(ACTIVITY_LOG_FILE);
  }
  logSystemEvent("Activity log cleared by user");
}

String getActivityLogSize() {
  File logFile = FFat.open(ACTIVITY_LOG_FILE, "r");
  if (logFile) {
    size_t size = logFile.size();
    logFile.close();
    
    if (size < 1024) {
      return String(size) + " bytes";
    } else if (size < 1024 * 1024) {
      return String(size / 1024.0, 1) + " KB";
    } else {
      return String(size / (1024.0 * 1024.0), 1) + " MB";
    }
  }
  return "0 bytes";
}

bool isActivityLogEnabled() {
  return activityLogEnabled;
}

void setActivityLogEnabled(bool enabled) {
  if (enabled != activityLogEnabled) {
    activityLogEnabled = enabled;
    if (enabled) {
      logSystemEvent("Activity logging enabled");
    } else {
      logSystemEvent("Activity logging disabled");
    }
  }
}

// Additional overloaded functions for enhanced logging
void logProgramComplete(const String& programName, unsigned long totalSeconds) {
  String message = "Program '" + programName + "' completed in " + formatDuration(totalSeconds);
  writeLogEntry("INFO", "PROGRAM", message);
}

void logMixStart(int patternIndex, unsigned long elapsedMs) {
  String message = "Mix pattern " + String(patternIndex) + " started at " + String(elapsedMs) + "ms";
  writeLogEntry("DEBUG", "MIXING", message);
}

void logMixStop(int patternIndex, unsigned long elapsedMs) {
  String message = "Mix pattern " + String(patternIndex) + " stopped at " + String(elapsedMs) + "ms";
  writeLogEntry("DEBUG", "MIXING", message);
}

void logMixCycleComplete(int totalPatterns) {
  String message = "All " + String(totalPatterns) + " mix patterns completed, restarting cycle";
  writeLogEntry("INFO", "MIXING", message);
}

void logMixPatternAdvance(int newPatternIndex) {
  String message = "Advanced to mix pattern " + String(newPatternIndex);
  writeLogEntry("DEBUG", "MIXING", message);
}

void logTemperatureTargetChange(double newTarget, double currentTemp) {
  String message = "Target changed to " + String(newTarget, 1) + "°C (current: " + String(currentTemp, 1) + "°C)";
  writeLogEntry("INFO", "TEMPERATURE", message);
}

void logFermentationProgress(double progressPercent, double factor, double temperature) {
  String message = String(progressPercent, 1) + "% complete, factor=" + String(factor, 3) + ", temp=" + String(temperature, 1) + "°C";
  writeLogEntry("INFO", "FERMENT", message);
}