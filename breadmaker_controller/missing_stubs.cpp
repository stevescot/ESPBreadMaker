#include "missing_stubs.h"
#include "globals.h"
#include "programs_manager.h"
#include "calibration.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <cmath>

#ifdef ESP32
#include <esp_system.h>
#include <WiFi.h>
#include <FFat.h>
#endif

// External function declarations
extern float readTemperature(); // Raw temperature reading from calibration.cpp

// Define build date if not already defined - force rebuild each time
#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__
#endif

// Force a rebuild by including current timestamp as comment
// BUILD_TIMESTAMP: Aug 23 2025 13:30:00 - FORCE PID PROFILE CREATION DEBUG

// Stub implementations for missing functions

// Add these includes if needed
extern bool debugSerial;
extern unsigned long startupTime;
extern ProgramState programState;
extern OutputStates outputStates;
extern PIDControl pid;
extern TemperatureAveragingState tempAvg;
extern FermentationState fermentState;
extern DynamicRestartState dynamicRestart;
extern unsigned long windowSize;

// External function declarations
extern void setHeater(bool on);
extern void setMotor(bool on);
extern void setLight(bool on);
extern void setBuzzer(bool on);
extern void invalidateStatusCache();
extern void clearResumeState();
extern size_t getProgramCount();
extern String getProgramName(int programId);
extern bool isProgramValid(int programId);
extern bool ensureProgramLoaded(int programId);
extern Program* getActiveProgramMutable();
extern void switchToProfile(const String& profileName);
extern void saveSettings();

// Performance tracking variables
static unsigned long lastLoopTime = 0;
static unsigned long totalLoopTime = 0;
static unsigned long maxLoopTime = 0;
static unsigned long loopCount = 0;
static uint32_t minFreeHeap = UINT32_MAX;
static unsigned long lastWifiStatus = 0;
static unsigned long wifiReconnectCount = 0;

// Fermentation calculation cache
struct FermentationCache {
  int cachedProgramId = -1;
  unsigned int cachedStageIdx = UINT_MAX;
  time_t cachedProgramEndTime = 0;
  unsigned long cachedStageEndTimes[20] = {0};
  unsigned long cachedTotalDuration = 0;
  unsigned long cachedElapsedTime = 0;
  unsigned long cachedRemainingTime = 0;
  unsigned long lastCacheUpdate = 0;
  bool isValid = false;
};
static FermentationCache fermentCache;

// Temperature and performance functions
float getAveragedTemperature() {
    return tempAvg.smoothedTemperature;
}

// Memory-efficient EMA temperature sampling - replaces complex array-based averaging
// Uses only 16 bytes vs 200+ bytes, provides superior filtering with infinite sample history
void updateTemperatureSampling() {
    unsigned long nowMs = millis();
    
    // Sample at the configured interval
    if (nowMs - tempAvg.lastUpdate >= tempAvg.updateInterval) {
        tempAvg.lastUpdate = nowMs;
        
        // Take a new temperature sample using the calibrated readTemperature function
        float calibratedTemp = readTemperature();
        
        // CRITICAL SAFETY FIX: Reject invalid temperature readings to prevent PID malfunction
        if (calibratedTemp <= -999.0f || calibratedTemp >= 999.0f) {
            if (debugSerial) {
                Serial.printf("[TEMP-EMA] SAFETY: Rejecting invalid temperature %.2f°C from sensor failure\n", calibratedTemp);
            }
            // Do not update EMA with invalid readings - keep last valid temperature
            return;
        }
        
        // Additional validation: Reject physically impossible temperatures
        if (calibratedTemp < -50.0f || calibratedTemp > 300.0f) {
            if (debugSerial) {
                Serial.printf("[TEMP-EMA] SAFETY: Rejecting out-of-range temperature %.2f°C\n", calibratedTemp);
            }
            return;
        }
        
        // Spike detection disabled - accepting all temperature readings
        // (Removed due to false positives after reboots)
        /*
        if (tempAvg.initialized) {
            float tempChange = abs(calibratedTemp - tempAvg.lastCalibratedTemp);
            if (tempChange > tempAvg.spikeThreshold) {
                // Count consecutive spikes
                tempAvg.consecutiveSpikes++;
                
                // If we've had many consecutive "spikes", the EWMA might be stuck at wrong value
                if (tempAvg.consecutiveSpikes >= 10) {
                    // Reset EWMA to current reading - the "spikes" are probably correct
                    tempAvg.smoothedTemperature = calibratedTemp;
                    tempAvg.lastCalibratedTemp = calibratedTemp;
                    tempAvg.consecutiveSpikes = 0;
                    
                    if (debugSerial) {
                        Serial.printf("[TEMP-EMA] RESET: %d consecutive spikes detected, resetting EWMA to %.2f°C\n", 
                                    10, calibratedTemp);
                    }
                } else {
                    // Normal spike detection - ignore this reading
                    if (debugSerial) {
                        Serial.printf("[TEMP-EMA] Spike %d/10: %.2f°C change (%.2f -> %.2f), ignoring\n", 
                                    tempAvg.consecutiveSpikes, tempChange, tempAvg.lastCalibratedTemp, calibratedTemp);
                    }
                    return;
                }
            } else {
                // Reset spike counter on normal reading
                tempAvg.consecutiveSpikes = 0;
            }
        }
        */
        
        // Initialize EMA with first valid reading
        if (!tempAvg.initialized) {
            tempAvg.smoothedTemperature = calibratedTemp;
            tempAvg.initialized = true;
            if (debugSerial) {
                Serial.printf("[TEMP-EMA] Initialized with %.2f°C\n", calibratedTemp);
            }
        } else {
            // Apply Exponential Moving Average: new = α × current + (1-α) × previous
            tempAvg.smoothedTemperature = tempAvg.alpha * calibratedTemp + 
                                        (1.0 - tempAvg.alpha) * tempAvg.smoothedTemperature;
        }
        
        tempAvg.lastCalibratedTemp = calibratedTemp;
        tempAvg.sampleCount++;
        
        // Optional: Log every 10th sample for debugging
        if (debugSerial && (tempAvg.sampleCount % 10 == 0)) {
            Serial.printf("[TEMP-EMA] Sample #%u: Raw=%.2f°C, Smoothed=%.2f°C, α=%.3f\n", 
                         tempAvg.sampleCount, calibratedTemp, tempAvg.smoothedTemperature, tempAvg.alpha);
        }
    }
}

void updatePerformanceMetrics() {
  unsigned long currentTime = micros();
  if (lastLoopTime > 0) {
    unsigned long loopDuration = currentTime - lastLoopTime;
    totalLoopTime += loopDuration;
    if (loopDuration > maxLoopTime) {
      maxLoopTime = loopDuration;
    }
  }
  lastLoopTime = currentTime;
  loopCount++;
  
  // Track minimum free heap
  uint32_t currentHeap = ESP.getFreeHeap();
  if (currentHeap < minFreeHeap) {
    minFreeHeap = currentHeap;
  }
  
  // Track WiFi reconnections
  unsigned long currentWifiStatus = WiFi.status();
  if (lastWifiStatus != WL_CONNECTED && currentWifiStatus == WL_CONNECTED) {
    wifiReconnectCount++;
  }
  lastWifiStatus = currentWifiStatus;
}

// Performance metrics getter functions
unsigned long getMaxLoopTime() {
  return maxLoopTime;
}

unsigned long getAverageLoopTime() {
  return loopCount > 0 ? totalLoopTime / loopCount : 0;
}

unsigned long getLoopCount() {
  return loopCount;
}

uint32_t getMinFreeHeap() {
  return minFreeHeap == UINT32_MAX ? ESP.getFreeHeap() : minFreeHeap;
}

unsigned long getWifiReconnectCount() {
  return wifiReconnectCount;
}

float getHeapFragmentation() {
  // Calculate heap fragmentation percentage
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t maxAlloc = ESP.getMaxAllocHeap();
  if (freeHeap > 0) {
    return ((float)(freeHeap - maxAlloc) / freeHeap) * 100.0;
  }
  return 0.0;
}

void checkAndSwitchPIDProfile() {
  // Skip if auto-switching is disabled
  if (!pid.autoSwitching) return;
  
  // Skip if no valid setpoint
  if (pid.Setpoint <= 0) return;
  
  // If no profiles are loaded, load them first (with caching to prevent repeated loading)
  if (pid.profiles.empty()) {
    static bool pidProfileLoadAttempted = false;
    if (!pidProfileLoadAttempted) {
      if (debugSerial) Serial.println("[PID-PROFILE] No profiles loaded, loading default profiles");
      loadPIDProfiles();
      pidProfileLoadAttempted = true; // Prevent repeated file system access if loading fails
    } else {
      // Profiles failed to load previously, don't keep trying
      return;
    }
  }
  
  // Find appropriate profile for current setpoint and switch immediately if needed
  for (auto& profile : pid.profiles) {
    if (pid.Setpoint >= profile.minTemp && pid.Setpoint < profile.maxTemp) {
      // Only switch if we're not already using the correct profile
      if (abs(pid.Kp - profile.kp) > 0.001 || abs(pid.Ki - profile.ki) > 0.0001 || abs(pid.Kd - profile.kd) > 0.01) {
        if (debugSerial) {
          Serial.printf("[PID-PROFILE] Setpoint %.1f°C requires '%s' profile (Kp=%.3f, Ki=%.6f, Kd=%.1f)\n",
                        pid.Setpoint, profile.name.c_str(), profile.kp, profile.ki, profile.kd);
        }
        switchToProfile(profile.name);
        saveSettings();
      }
      return; // Found appropriate profile, exit
    }
  }
}

String getCurrentActiveProfileName() {
  if (pid.Setpoint <= 0) {
    return "None"; // No setpoint, no active profile
  }
  
  // Find appropriate profile for current setpoint
  for (auto& profile : pid.profiles) {
    if (pid.Setpoint >= profile.minTemp && pid.Setpoint < profile.maxTemp) {
      return profile.name;
    }
  }
  
  return "Unknown"; // Setpoint outside all profile ranges
}

// Save PID profiles to file - OPTIMIZED with streaming to reduce memory usage
void savePIDProfiles() {
  File file = FFat.open("/pid-profiles.json", "w");
  if (!file) {
    if (debugSerial) Serial.println("[ERROR] Failed to open pid-profiles.json for writing");
    return;
  }
  
  // OPTIMIZATION: Use streaming JSON generation to avoid memory allocation
  file.print("{\"pidProfiles\":[");
  
  for (size_t i = 0; i < pid.profiles.size(); i++) {
    if (i > 0) file.print(",");
    const auto& profile = pid.profiles[i];
    
    file.print("{\"name\":\"");
    file.print(profile.name);
    file.print("\",\"minTemp\":");
    file.print(profile.minTemp, 1);
    file.print(",\"maxTemp\":");
    file.print(profile.maxTemp, 1);
    file.print(",\"kp\":");
    file.print(profile.kp, 6);
    file.print(",\"ki\":");
    file.print(profile.ki, 6);
    file.print(",\"kd\":");
    file.print(profile.kd, 2);
    file.print(",\"windowMs\":");
    file.print(profile.windowMs);
    file.print(",\"description\":\"");
    file.print(profile.description);
    file.print("\"}");
  }
  
  file.print("],\"autoSwitching\":");
  file.print(pid.autoSwitching ? "true" : "false");
  file.print("}");
  
  file.close();
  
  if (debugSerial) Serial.println("[INFO] PID profiles saved successfully using streaming");
}

// Load PID profiles from file
void loadPIDProfiles() {
  if (debugSerial) Serial.println("[DEBUG] loadPIDProfiles() called");
  
  File file = FFat.open("/pid-profiles.json", "r");
  if (!file) {
    if (debugSerial) Serial.println("[WARN] pid-profiles.json not found, creating default profiles");
    createDefaultPIDProfiles();
    savePIDProfiles();
    if (debugSerial) Serial.printf("[DEBUG] After creation: pid.profiles.size() = %d\n", pid.profiles.size());
    return;
  }
  
  // Create JSON document
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    if (debugSerial) Serial.printf("[ERROR] Failed to parse pid-profiles.json: %s\n", error.c_str());
    createDefaultPIDProfiles();
    return;
  }
  
  // Clear existing profiles
  pid.profiles.clear();
  
  // Load profiles from JSON
  JsonArray profiles = doc["pidProfiles"];
  for (JsonObject profile : profiles) {
    PIDProfile pidProfile;
    pidProfile.name = profile["name"].as<String>();
    pidProfile.minTemp = profile["minTemp"];
    pidProfile.maxTemp = profile["maxTemp"];
    pidProfile.kp = profile["kp"];
    pidProfile.ki = profile["ki"];
    pidProfile.kd = profile["kd"];
    pidProfile.windowMs = profile["windowMs"] | 15000; // Default 15s if missing
    pidProfile.description = profile["description"].as<String>();
    
    pid.profiles.push_back(pidProfile);
  }
  
  // Load settings - activeProfile is NOT loaded, it's determined automatically
  pid.autoSwitching = doc["autoSwitching"] | true; // Default to auto-switching enabled
  
  if (debugSerial) Serial.printf("[INFO] Loaded %d PID profiles, auto-switching: %s\n", 
                                 pid.profiles.size(), pid.autoSwitching ? "enabled" : "disabled");
}

void createDefaultPIDProfiles() {
  if (debugSerial) Serial.println("[DEBUG] createDefaultPIDProfiles() called - creating 2 clean profiles");
  pid.profiles.clear();
  
  // Low Temperature Range (0-40°C) - Using tuned values from user testing
  PIDProfile lowRange;
  lowRange.name = "0-40°C (Fermentation)";
  lowRange.minTemp = 0;
  lowRange.maxTemp = 40;
  lowRange.kp = 0.13;      // User's optimized values
  lowRange.ki = 0.0004;    // User's optimized values  
  lowRange.kd = 11.0;      // User's optimized values
  lowRange.windowMs = 5000;
  lowRange.description = "Optimized for ambient to fermentation temperatures. Lower derivative for thermal mass.";
  pid.profiles.push_back(lowRange);
  
  // High Temperature Range (40-250°C) - Using tuned values from user testing
  PIDProfile highRange;
  highRange.name = "40°C+ (Baking)";
  highRange.minTemp = 40;
  highRange.maxTemp = 250;
  highRange.kp = 0.09;     // User's optimized values
  highRange.ki = 0.0003;   // User's optimized values
  highRange.kd = 1.6;      // User's optimized values
  highRange.windowMs = 12000;
  highRange.description = "Optimized for baking temperatures. Higher derivative for responsiveness.";
  pid.profiles.push_back(highRange);
  
  // Set defaults - no activeProfile persistence (determined automatically by setpoint)
  pid.autoSwitching = true;
  
  if (debugSerial) {
    Serial.printf("[INFO] Created %d clean PID profiles: '%s' and '%s'\n", 
                  pid.profiles.size(), pid.profiles[0].name.c_str(), pid.profiles[1].name.c_str());
  }
}

// Program and state management functions
void updateActiveProgramVars() {
  if (getProgramCount() > 0 && programState.activeProgramId < getProgramCount() && isProgramValid(programState.activeProgramId)) {
    // Ensure the program is loaded
    if (!ensureProgramLoaded(programState.activeProgramId)) {
      if (debugSerial) Serial.printf("[ERROR] Failed to load program %d\n", programState.activeProgramId);
      programState.customProgram = nullptr;
      programState.maxCustomStages = 0;
      return;
    }
    
    programState.customProgram = getActiveProgramMutable();
    programState.maxCustomStages = programState.customProgram ? programState.customProgram->customStages.size() : 0;
    
    if (debugSerial) Serial.printf("[INFO] updateActiveProgramVars: Program %d loaded, customProgram=%p, stages=%zu\n", 
                                   programState.activeProgramId, (void*)programState.customProgram, programState.maxCustomStages);
  } else {
    programState.customProgram = nullptr;
    programState.maxCustomStages = 0;
    if (debugSerial) Serial.printf("[INFO] updateActiveProgramVars: No valid program (count=%zu, id=%d)\n", 
                                   getProgramCount(), programState.activeProgramId);
  }
}

void stopBreadmaker() {
    if (debugSerial) Serial.println("[ACTION] stopBreadmaker() called");
    programState.isRunning = false;
    programState.customStageIdx = 0;
    programState.customStageStart = 0;
    
    // Turn off all outputs
    setHeater(false);
    setMotor(false);
    setLight(false);
    setBuzzer(true); // Brief beep to indicate stop (handled non-blocking)
    
    // Update output states structure
    outputStates.heater = false;
    outputStates.motor = false;
    outputStates.light = false;
    outputStates.buzzer = false;
    
    invalidateStatusCache();
    clearResumeState();
}

bool isStartupDelayComplete() {
    unsigned long elapsed = millis() - startupTime;
    return elapsed >= STARTUP_DELAY_MS;
}

// PID and control functions  
void updateTimeProportionalHeater() {
  unsigned long nowMs = millis();
  
  // Constants for ESP32 implementation
  const unsigned long MIN_WINDOW_TIME = 2000;   // Minimum window time before dynamic restart
  const float PID_CHANGE_THRESHOLD = 0.05;      // Threshold for significant PID output change
  const unsigned long minOnTime = 2000;         // Minimum heater ON time (2 seconds)
  const unsigned long minOffTime = 5000;        // Minimum heater OFF time (5 seconds)
  
  static unsigned long windowStartTime = 0;
  static float lastPIDOutput = 0.0;
  
  // Initialize window if not set
  if (windowStartTime == 0) {
    windowStartTime = nowMs;
  }
  
  // Disable heater if no setpoint, or if program not running AND not in manual mode
  if (pid.Setpoint <= 0 || (!programState.isRunning && !programState.manualMode)) {
    setHeater(false);
    return;
  }
  
  // Calculate initial ON time based on PID output
  unsigned long theoreticalOnTime = (unsigned long)(pid.Output * windowSize);
  unsigned long onTime = theoreticalOnTime;
  
  // Apply minimum ON/OFF time constraints
  if (pid.Output <= 0.001f) {
    onTime = 0;  // Complete OFF
  } else if (pid.Output >= 0.999f) {
    onTime = windowSize;  // Complete ON
  } else {
    // Apply minimum ON time
    if (onTime > 0 && onTime < minOnTime) {
      onTime = minOnTime;
    }
    // Ensure minimum OFF time
    if (onTime > 0 && onTime < windowSize && (windowSize - onTime) < minOffTime) {
      onTime = windowSize - minOffTime;
    }
  }
  
  // Dynamic window restart logic
  bool shouldRestartWindow = false;
  unsigned long elapsed = nowMs - windowStartTime;
  
  if (elapsed >= MIN_WINDOW_TIME) {
    float outputChange = abs(pid.Output - lastPIDOutput);
    if (outputChange >= PID_CHANGE_THRESHOLD) {
      bool needMoreHeat = (pid.Output > lastPIDOutput && elapsed >= onTime && pid.Output > 0.7);
      bool needLessHeat = (pid.Output < lastPIDOutput && elapsed < onTime && pid.Output < 0.3);
      
      if (needMoreHeat || needLessHeat) {
        shouldRestartWindow = true;
        dynamicRestart.lastDynamicRestart = nowMs;
        dynamicRestart.dynamicRestartCount++;
        
        if (needMoreHeat) {
          // OPTIMIZATION: Use snprintf instead of String concatenation
          char buffer[64];
          snprintf(buffer, sizeof(buffer), "Need more heat (output increased to %.1f%%)", pid.Output * 100);
          dynamicRestart.lastDynamicRestartReason = buffer;
        } else {
          // OPTIMIZATION: Use snprintf instead of String concatenation
          char buffer[64];
          snprintf(buffer, sizeof(buffer), "Reduce heat (output decreased to %.1f%%)", pid.Output * 100);
          dynamicRestart.lastDynamicRestartReason = buffer;
        }
        
        if (debugSerial) {
          Serial.printf("[PID-DYNAMIC] Restart #%u: %s (elapsed: %lums)\n", 
                       dynamicRestart.dynamicRestartCount, dynamicRestart.lastDynamicRestartReason.c_str(), elapsed);
        }
      }
    }
  }
  
  // Start new window if needed
  if (nowMs - windowStartTime >= windowSize || shouldRestartWindow) {
    windowStartTime = nowMs;
    lastPIDOutput = pid.Output;
    
    // Recalculate onTime for new window
    theoreticalOnTime = (unsigned long)(pid.Output * windowSize);
    onTime = theoreticalOnTime;
    
    // Reapply constraints
    if (pid.Output <= 0.001f) {
      onTime = 0;
    } else if (pid.Output >= 0.999f) {
      onTime = windowSize;
    } else {
      if (onTime > 0 && onTime < minOnTime) {
        onTime = minOnTime;
      }
      if (onTime > 0 && onTime < windowSize && (windowSize - onTime) < minOffTime) {
        onTime = windowSize - minOffTime;
      }
    }
    
    if (debugSerial && millis() % 15000 < 50) {  // Debug every 15 seconds
      Serial.printf("[PID-RELAY] Setpoint: %.1f°C, Input: %.1f°C, Output: %.2f, OnTime: %lums/%lums (%.1f%%) %s\n", 
                   pid.Setpoint, pid.Input, pid.Output, onTime, windowSize, (onTime * 100.0 / windowSize),
                   shouldRestartWindow ? "[DYNAMIC]" : "[NORMAL]");
    }
  }
  
  // Determine heater state based on position within current window
  elapsed = nowMs - windowStartTime;
  bool heaterShouldBeOn = (elapsed < onTime);
  setHeater(heaterShouldBeOn);
}

// Settings functions - MOVED TO breadmaker_controller.ino

void switchToProfile(const String& profileName) {
  for (const auto& profile : pid.profiles) {
    if (profile.name == profileName) {
      // Update active PID parameters
      pid.Kp = profile.kp;
      pid.Ki = profile.ki;
      pid.Kd = profile.kd;
      // NOTE: activeProfile is NOT persisted - it's determined dynamically by setpoint
      
      // Apply to PID controller
      if (pid.controller) {
        pid.controller->SetTunings(pid.Kp, pid.Ki, pid.Kd);
      }
      
      if (debugSerial) {
        Serial.printf("[switchToProfile] Switched to '%s': Kp=%.6f, Ki=%.6f, Kd=%.6f\n",
                      profileName.c_str(), pid.Kp, pid.Ki, pid.Kd);
      }
      return;
    }
  }
  if (debugSerial) Serial.printf("[switchToProfile] Profile '%s' not found!\n", profileName.c_str());
}

// Calculate individual PID terms for monitoring
void updatePIDTerms() {
  static unsigned long lastSampleTime = 0;
  unsigned long now = millis();
  
  // For PID tuning interface, update more frequently (every 200ms instead of sampleTime)
  // This provides better responsiveness for the tuning graphs
  unsigned long updateInterval = min(pid.sampleTime, 200UL);
  
  if (now - lastSampleTime >= updateInterval) {
    double error = pid.Setpoint - pid.Input;
    double dInput = pid.Input - pid.lastInput;
    
    // Calculate P term
    pid.pidP = pid.Kp * error;
    
    // Calculate I term (accumulated integral) - use same calculation as main PID
    double sampleTimeSec = updateInterval / 1000.0;
    pid.lastITerm += (pid.Ki * error * sampleTimeSec);
    
    // Apply integral windup protection (same as main PID)
    if (pid.lastITerm > 1.0) pid.lastITerm = 1.0;
    else if (pid.lastITerm < 0.0) pid.lastITerm = 0.0;
    pid.pidI = pid.lastITerm;
    
    // Calculate D term (derivative on input, not error)
    if (lastSampleTime > 0) { // Ensure we have a previous sample
      pid.pidD = -pid.Kd * (dInput / sampleTimeSec);
    } else {
      pid.pidD = 0; // First sample, no derivative
    }
    
    // Store for next calculation - only update if we actually calculated
    pid.lastInput = pid.Input;
    lastSampleTime = now;
    
    // Optional debug output for tuning
    if (debugSerial && (now % 5000 < updateInterval)) { // Log every 5 seconds
      Serial.printf("[PID-TERMS] P=%.3f, I=%.3f, D=%.3f, Output=%.3f, Error=%.2f, dInput=%.2f\n",
                    pid.pidP, pid.pidI, pid.pidD, pid.Output, error, dInput);
    }
  }
}

// Display message function used by OTA manager
void displayMessage(const String& message) {
  if (debugSerial) Serial.println("Display: " + message);
}

// Stream status JSON function - comprehensive implementation
// This function streams JSON directly without creating large strings in memory
void streamStatusJson(Print& out) {
  // Re-enable fermentation cache after optimizing the main performance bottlenecks
  updateFermentationCache();
  
  // Variable to store total program remaining time (will be calculated below)
  unsigned long totalProgramRemainingTime = 0;
  
  // Start main JSON object
  out.print("{");
  
  // === Core State ===
  out.print("\"state\":\"");
  out.print(programState.isRunning ? "on" : "off");
  out.print("\",");
  
  out.print("\"running\":");
  out.print(programState.isRunning ? "true" : "false");
  out.print(",");
  
  // === Program Information ===
  if (programState.activeProgramId >= 0 && programState.activeProgramId < getProgramCount()) {
    // OPTIMIZATION: Cache program data to avoid frequent file system access
    static String cachedProgramName = "";
    static int lastCachedProgramId = -1;
    static Program* cachedProgram = nullptr;
    
    if (lastCachedProgramId != programState.activeProgramId) {
      // Only load program from file system when program ID changes
      cachedProgram = getActiveProgramMutable();
      if (cachedProgram) {
        cachedProgramName = cachedProgram->name;
        lastCachedProgramId = programState.activeProgramId;
      } else {
        cachedProgramName = "Unknown";
        lastCachedProgramId = -1;
        cachedProgram = nullptr;
      }
    }
    
    if (cachedProgramName.length() > 0 && cachedProgram) {
      out.print("\"program\":\"");
      out.print(cachedProgramName);
      out.print("\",");
      
      out.printf("\"programId\":%d,", programState.activeProgramId);
      
      // === Stage Information ===
      if (programState.customStageIdx < cachedProgram->customStages.size()) {
        CustomStage& stage = cachedProgram->customStages[programState.customStageIdx];
        
        out.print("\"stage\":\"");
        out.print(stage.label);
        out.print("\",");
        
        out.printf("\"stageIdx\":%u,", (unsigned)programState.customStageIdx);
        out.printf("\"stageTemperature\":%.1f,", stage.temp);
        
        // Calculate time left in current stage (fermentation-adjusted with periodic updates)
        unsigned long timeLeft = 0;
        unsigned long adjustedTimeLeft = 0;
        if (programState.isRunning && programState.customStageStart > 0) {
          unsigned long elapsed = (millis() - programState.customStageStart) / 1000;
          unsigned long baseStageDuration = stage.min * 60;
          
          // Get or calculate adjusted stage duration with periodic updates (every 30 minutes for better performance)
          unsigned long adjustedStageDuration = baseStageDuration;
          if (stage.isFermentation) {
            unsigned long timeSinceLastUpdate = millis() - programState.lastFermentationUpdate;
            bool needsUpdate = (programState.adjustedStageDurations[programState.customStageIdx] == 0) || 
                              (timeSinceLastUpdate > 1800000); // 30 minutes = 1,800,000 ms
            
            if (needsUpdate) {
              // Recalculate adjusted duration based on current fermentation conditions
              adjustedStageDuration = getAdjustedStageTimeMs(baseStageDuration * 1000, true) / 1000;
              programState.adjustedStageDurations[programState.customStageIdx] = adjustedStageDuration;
              programState.lastFermentationUpdate = millis();
            } else {
              // Use cached adjusted duration
              adjustedStageDuration = programState.adjustedStageDurations[programState.customStageIdx];
            }
          }
          
          // Original time left (non-adjusted) - ADD SAFETY CHECKS
          if (elapsed < baseStageDuration) {
            timeLeft = baseStageDuration - elapsed;
          } else {
            timeLeft = 0; // Stage should have ended
            if (debugSerial) Serial.printf("[TIMING-SAFETY] Stage %d elapsed (%lu) > duration (%lu), setting timeLeft=0\n", 
                                          programState.customStageIdx, elapsed, baseStageDuration);
          }
          
          // Fermentation-adjusted time left (using periodically updated duration) - ADD SAFETY CHECKS
          if (elapsed < adjustedStageDuration) {
            adjustedTimeLeft = adjustedStageDuration - elapsed;
          } else {
            adjustedTimeLeft = 0; // Stage should have ended
            if (debugSerial) Serial.printf("[TIMING-SAFETY] Stage %d elapsed (%lu) > adjusted duration (%lu), setting adjustedTimeLeft=0\n", 
                                          programState.customStageIdx, elapsed, adjustedStageDuration);
          }
        }
        out.printf("\"timeLeft\":%lu,", timeLeft);
        out.printf("\"adjustedTimeLeft\":%lu,", adjustedTimeLeft);
        
        // Calculate total program remaining time (current stage + all future stages with cached fermentation adjustment)
        totalProgramRemainingTime = adjustedTimeLeft; // Start with current stage time left
        const Program* p = getActiveProgram();
        if (p && programState.isRunning) {
          // Use cached remaining time if available and valid (refreshed with fermentation cache)
          if (fermentCache.isValid && fermentCache.cachedRemainingTime > 0) {
            totalProgramRemainingTime = fermentCache.cachedRemainingTime;
          } else {
            // Add time for all remaining stages after current one (fallback calculation)
            for (size_t i = programState.customStageIdx + 1; i < p->customStages.size(); i++) {
              const CustomStage& futureStage = p->customStages[i];
              unsigned long adjustedDurationMs;
              
              if (futureStage.isFermentation) {
                // For future fermentation stages, use current fermentation conditions
                // This will be updated every 30 minutes when those stages are reached
                adjustedDurationMs = getAdjustedStageTimeMs(futureStage.min * 60 * 1000, true);
              } else {
                // Non-fermentation stages use original duration
                adjustedDurationMs = futureStage.min * 60 * 1000;
              }
              totalProgramRemainingTime += adjustedDurationMs / 1000;
            }
          }
        }
        
        // Stage ready time (using adjusted duration)
        if (programState.isRunning && adjustedTimeLeft > 0) {
          unsigned long readyAt = (time(nullptr) + adjustedTimeLeft) * 1000; // Convert to ms
          out.printf("\"stage_ready_at\":%lu,", readyAt);
          out.printf("\"stageReadyAt\":%lu", readyAt / 1000);
        } else {
          out.print("\"stage_ready_at\":0,");
          out.print("\"stageReadyAt\":0");
        }
        out.print(",");
      } else {
        out.print("\"stage\":\"Idle\",");
        out.print("\"stageIdx\":0,");
        out.print("\"stageTemperature\":0,");
        out.print("\"timeLeft\":0,");
        out.print("\"adjustedTimeLeft\":0,");
        out.print("\"stage_ready_at\":0,");
        out.print("\"stageReadyAt\":0,");
      }
    } else {
      out.print("\"program\":\"Unknown\",");
      out.printf("\"programId\":%d,", programState.activeProgramId);
      out.print("\"stage\":\"Idle\",");
      out.print("\"stageIdx\":0,");
      out.print("\"stageTemperature\":0,");
      out.print("\"timeLeft\":0,");
      out.print("\"adjustedTimeLeft\":0,");
      out.print("\"stage_ready_at\":0,");
      out.print("\"stageReadyAt\":0,");
    }
  } else {
    out.print("\"program\":\"None\",");
    out.print("\"programId\":-1,");
    out.print("\"stage\":\"Idle\",");
    out.print("\"stageIdx\":0,");
    out.print("\"stageTemperature\":0,");
    out.print("\"timeLeft\":0,");
    out.print("\"adjustedTimeLeft\":0,");
    out.print("\"stage_ready_at\":0,");
    out.print("\"stageReadyAt\":0,");
  }
  
  // === Temperature and Outputs ===
  float temp = getAveragedTemperature();
  out.printf("\"temperature\":%.1f,", temp);
  out.printf("\"temp\":%.1f,", temp);
  
  // Add raw (unfiltered) temperature reading
  float rawTemp = readTemperature();
  out.printf("\"rawTemperature\":%.1f,", rawTemp);
  out.printf("\"tempRaw\":%.1f,", rawTemp);
  
  out.printf("\"setpoint\":%.1f,", pid.Setpoint);
  
  out.print("\"heater\":");
  out.print(outputStates.heater ? "true" : "false");
  out.print(",");
  
  out.print("\"motor\":");
  out.print(outputStates.motor ? "true" : "false");
  out.print(",");
  
  out.print("\"light\":");
  out.print(outputStates.light ? "true" : "false");
  out.print(",");
  
  out.print("\"buzzer\":");
  out.print(outputStates.buzzer ? "true" : "false");
  out.print(",");
  
  out.print("\"manualMode\":");
  out.print(programState.manualMode ? "true" : "false");
  out.print(",");
  
  // === Mix and Timing ===
  out.printf("\"mixIdx\":%u,", (unsigned)programState.customMixIdx);
  
  // === Program Completion Prediction ===
  if (fermentState.predictedCompleteTime > 0) {
    out.printf("\"program_ready_at\":%lu,", fermentState.predictedCompleteTime * 1000);
    out.printf("\"programReadyAt\":%lu,", fermentState.predictedCompleteTime);
    out.printf("\"predictedCompleteTime\":%lu,", fermentState.predictedCompleteTime * 1000);
  } else {
    out.print("\"program_ready_at\":0,");
    out.print("\"programReadyAt\":0,");
    out.print("\"predictedCompleteTime\":0,");
  }
  
  // === Startup Delay ===
  bool startupComplete = isStartupDelayComplete();
  out.print("\"startupDelayComplete\":");
  out.print(startupComplete ? "true" : "false");
  out.print(",");
  
  if (!startupComplete) {
    unsigned long remaining = STARTUP_DELAY_MS - (millis() - startupTime);
    out.printf("\"startupDelayRemainingMs\":%lu,", remaining);
  } else {
    out.print("\"startupDelayRemainingMs\":0,");
  }
  
  // === Fermentation Data ===
  out.printf("\"fermentationFactor\":%.3f,", fermentState.fermentationFactor);
  out.printf("\"initialFermentTemp\":%.1f,", fermentState.initialFermentTemp);
  
  // === New Clear Fermentation Timing Data ===
  out.printf("\"fermentScheduledElapsed\":%.1f,", fermentState.scheduledElapsedSeconds);
  out.printf("\"fermentRealElapsed\":%.1f,", fermentState.realElapsedSeconds);
  out.printf("\"fermentAccumulatedMinutes\":%.2f,", fermentState.accumulatedFermentMinutes);
  
  // === Predicted Stage End Times (Fermentation-Adjusted) ===
  out.print("\"predictedStageEndTimes\":[");
  if (fermentCache.isValid && programState.activeProgramId >= 0) {
    Program* p = getActiveProgramMutable();
    if (p && p->customStages.size() > 0) {
      for (size_t i = 0; i < p->customStages.size() && i < 20; i++) {
        if (i > 0) out.print(",");
        out.printf("%lu", fermentCache.cachedStageEndTimes[i]);
      }
    }
  }
  out.print("],");
  
  // === Predicted Program End Time ===
  out.printf("\"predictedProgramEnd\":%lu,", (unsigned long)fermentCache.cachedProgramEndTime);
  
  // === Additional timing data for UI ===
  out.printf("\"totalProgramDuration\":%lu,", fermentCache.cachedTotalDuration);
  out.printf("\"elapsedTime\":%lu,", fermentCache.cachedElapsedTime);
  out.printf("\"remainingTime\":%lu,", fermentCache.cachedRemainingTime); // Use cached value for consistency
  
  // === Stage Start Times Array ===
  out.print("\"actualStageStartTimes\":[");
  for (int i = 0; i < 20; i++) {
    if (i > 0) out.print(",");
    out.printf("%lu", (unsigned long)programState.actualStageStartTimes[i]);
  }
  out.print("],");
  
  // === Stage End Times Array ===
  out.print("\"actualStageEndTimes\":[");
  for (int i = 0; i < 20; i++) {
    if (i > 0) out.print(",");
    out.printf("%lu", (unsigned long)programState.actualStageEndTimes[i]);
  }
  out.print("],");
  
  // === Health and System Data ===
  out.printf("\"uptime_sec\":%lu,", millis() / 1000);
  
  out.print("\"firmware_version\":\"ESP32-WebServer\",");
  
  out.print("\"build_date\":\"");
  out.print(FIRMWARE_BUILD_DATE);
  out.print("\",");
  
  out.printf("\"free_heap\":%u,", ESP.getFreeHeap());
  
  out.print("\"connected\":");
  out.print(wifiCache.isConnected() ? "true" : "false");
  out.print(",");
  
  out.print("\"ip\":\"");
  out.print(wifiCache.getIPString());
  out.print("\",");
  
  // === WiFi Details ===
  out.print("\"wifi\":{");
  if (wifiCache.isConnected()) {
    out.print("\"connected\":true,");
    out.print("\"ssid\":\"");
    out.print(wifiCache.getSSID());
    out.print("\",");
    out.printf("\"rssi\":%d,", wifiCache.getRSSI());
    out.print("\"ip\":\"");
    out.print(wifiCache.getIPString());
    out.print("\"");
  } else {
    out.print("\"connected\":false");
  }
  out.print("},");
  
  // === PID Information for debugging ===
  out.printf("\"pid_kp\":%.6f,", pid.Kp);
  out.printf("\"pid_ki\":%.6f,", pid.Ki);
  out.printf("\"pid_kd\":%.6f,", pid.Kd);
  out.printf("\"pid_output\":%.3f,", pid.Output);
  out.printf("\"pid_input\":%.1f,", pid.Input);
  out.printf("\"pid_p\":%.3f,", pid.pidP);
  out.printf("\"pid_i\":%.3f,", pid.pidI);
  out.printf("\"pid_d\":%.3f,", pid.pidD);
  
  // === Safety System Status ===
  out.print("\"safety\":{");
  out.print("\"emergencyShutdown\":");
  out.print(safetySystem.emergencyShutdown ? "true" : "false");
  out.print(",");
  
  if (safetySystem.emergencyShutdown) {
    out.print("\"shutdownReason\":\"");
    out.print(safetySystem.shutdownReason);
    out.print("\",");
    out.printf("\"shutdownTime\":%lu,", safetySystem.shutdownTime);
  }
  
  out.print("\"temperatureValid\":");
  out.print(safetySystem.temperatureValid ? "true" : "false");
  out.print(",");
  
  out.print("\"heatingEffective\":");
  out.print(safetySystem.heatingEffective ? "true" : "false");
  out.print(",");
  
  out.print("\"pidSaturated\":");
  out.print(safetySystem.pidSaturated ? "true" : "false");
  out.print(",");
  
  out.printf("\"invalidTempCount\":%u,", safetySystem.invalidTempCount);
  out.printf("\"zeroTempCount\":%u,", safetySystem.zeroTempCount);
  
  // Performance metrics
  if (safetySystem.loopCount > 0) {
    unsigned long avgLoopTime = safetySystem.totalLoopTime / safetySystem.loopCount;
    out.printf("\"avgLoopTimeUs\":%lu,", avgLoopTime);
    out.printf("\"maxLoopTimeUs\":%lu,", safetySystem.maxLoopTime);
  } else {
    out.print("\"avgLoopTimeUs\":0,");
    out.print("\"maxLoopTimeUs\":0,");
  }
  
  out.printf("\"maxSafeTemp\":%.1f,", SafetySystem::MAX_SAFE_TEMPERATURE);
  out.printf("\"emergencyTemp\":%.1f", SafetySystem::EMERGENCY_TEMPERATURE);
  out.print("},");
  
  // === Status ===
  if (safetySystem.emergencyShutdown) {
    out.print("\"status\":\"emergency_shutdown\",");
  } else {
    out.print("\"status\":\"ok\",");
  }
  
  // === Scheduled Start Information ===
  extern time_t scheduledStart;
  extern int scheduledStartStage;
  
  out.printf("\"scheduledStart\":%lu,", (unsigned long)scheduledStart);
  out.printf("\"scheduledStartStage\":%d", scheduledStartStage);
  
  // Close main JSON object
  out.print("}");
}

// === Fermentation Calculations ===

float calculateFermentationFactor(float actualTemp) {
  // Get program parameters from active program instead of hardcoded values
  float baselineTemp = 20.0;  // Default fallback
  float q10 = 2.0;            // Default fallback
  
  // Get actual program parameters if available
  if (getProgramCount() > 0 && programState.activeProgramId < getProgramCount()) {
    Program *p = getActiveProgramMutable();
    if (p) {
      baselineTemp = p->fermentBaselineTemp > 0 ? p->fermentBaselineTemp : 20.0;
      q10 = p->fermentQ10 > 0 ? p->fermentQ10 : 2.0;
    }
  }
  
  if (debugSerial) Serial.printf("[FERMENT-CALC] Using program values: baseline=%.1f, Q10=%.1f\n", baselineTemp, q10);
  
  // Biologically realistic fermentation factor based on yeast activity
  float factor = 0.0;
  
  if (actualTemp < 0.0) {
    // Below freezing: dough is frozen, no fermentation
    factor = 0.0;
    if (debugSerial) Serial.printf("[FERMENT] FROZEN: temp=%.1f°C < 0°C, factor=0 (no fermentation)\n", actualTemp);
  } else if (actualTemp > 59.0) {
    // Above 59°C: yeast cells are killed, no fermentation
    factor = 0.0;
    if (debugSerial) Serial.printf("[FERMENT] TOO HOT: temp=%.1f°C > 59°C, factor=0 (yeast death)\n", actualTemp);
    // TODO: Add warning flag for overheating
  } else if (actualTemp <= 36.0) {
    // 0°C to 36°C: Normal Q10 calculation, peak activity at 36°C
    float tempDiff = baselineTemp - actualTemp;
    float exponent = tempDiff / 10.0;
    factor = pow(q10, exponent);
    
    // Special case: if baseline is above 36°C, calculate from 36°C as the peak
    if (baselineTemp > 36.0) {
      float tempDiffFrom36 = 36.0 - actualTemp;
      float exponentFrom36 = tempDiffFrom36 / 10.0;
      factor = pow(q10, exponentFrom36);
    }
    
    if (debugSerial) {
      Serial.printf("[FERMENT] NORMAL: temp=%.1f°C, baseline=%.1f°C, Q10=%.1f\n", actualTemp, baselineTemp, q10);
      Serial.printf("[FERMENT] TempDiff=%.1f, Exponent=%.2f, Factor=%.3f\n", tempDiff, exponent, factor);
    }
  } else {
    // 36°C to 59°C: Linear interpolation from peak factor (at 36°C) down to 0 (at 59°C)
    // First calculate the peak factor at 36°C
    float tempDiffAt36 = baselineTemp - 36.0;
    float exponentAt36 = tempDiffAt36 / 10.0;
    float peakFactor = pow(q10, exponentAt36);
    
    // Special case: if baseline is above 36°C, use factor=1.0 as peak
    if (baselineTemp > 36.0) {
      peakFactor = 1.0;
    }
    
    // Linear interpolation: factor decreases from peakFactor at 36°C to 0 at 59°C
    float tempRange = 59.0 - 36.0; // 23°C range
    float tempAbove36 = actualTemp - 36.0;
    float interpolationRatio = 1.0 - (tempAbove36 / tempRange); // 1.0 at 36°C, 0.0 at 59°C
    factor = peakFactor * interpolationRatio;
    
    if (debugSerial) {
      Serial.printf("[FERMENT] HIGH TEMP: temp=%.1f°C (36-59°C range)\n", actualTemp);
      Serial.printf("[FERMENT] Peak factor at 36°C: %.3f, interpolation ratio: %.2f, final factor: %.3f\n", 
                    peakFactor, interpolationRatio, factor);
    }
  }
  
  // Ensure factor is never negative
  if (factor < 0.0) factor = 0.0;
  
  // Clamp to reasonable upper bound (20x max)
  if (factor > 20.0) factor = 20.0;
  
  return factor;
}

// REMOVED: updateFermentationFactor() - redundant function that conflicted with main fermentation logic
// Main fermentation calculations are now handled in updateFermentationTiming() in breadmaker_controller.ino

unsigned long getAdjustedStageTimeMs(unsigned long baseTimeMs, bool hasFermentation) {
  if (!hasFermentation) {
    return baseTimeMs; // No fermentation adjustment
  }
  
  // SAFETY CHECK: Prevent extremely large fermentation factors from causing overflow
  if (fermentState.fermentationFactor < 0.01f || fermentState.fermentationFactor > 100.0f) {
    if (debugSerial) Serial.printf("[FERMENT-SAFETY] Invalid fermentation factor %.3f, using 1.0\n", fermentState.fermentationFactor);
    return baseTimeMs; // Use original time if factor is invalid
  }
  
  // Apply current fermentation factor with overflow protection
  double result = (double)baseTimeMs * fermentState.fermentationFactor;
  
  // SAFETY CHECK: Prevent integer overflow (limit to ~24 hours = 86,400,000 ms)
  if (result > 86400000.0) {
    if (debugSerial) Serial.printf("[FERMENT-SAFETY] Calculated time %.0f ms too large, capping at 24 hours\n", result);
    return 86400000; // Cap at 24 hours
  }
  
  return (unsigned long)result;
}

// Update fermentation cache - only recalculate when program or stage changes
// NOTE: Cache now uses fermentState.predictedCompleteTime for consistency with programReadyAt
void updateFermentationCache() {
  // Check if cache is still valid - increased to 15 seconds for better performance
  bool cacheValid = fermentCache.isValid && 
                   fermentCache.cachedProgramId == programState.activeProgramId &&
                   fermentCache.cachedStageIdx == programState.customStageIdx &&
                   (millis() - fermentCache.lastCacheUpdate) < 15000; // Refresh every 15 seconds max
  
  if (cacheValid) {
    return; // Use existing cache
  }
  
  // Reset cache
  fermentCache.cachedProgramId = programState.activeProgramId;
  fermentCache.cachedStageIdx = programState.customStageIdx;
  fermentCache.lastCacheUpdate = millis();
  fermentCache.isValid = false;
  
  // Clear arrays
  for (int i = 0; i < 20; i++) {
    fermentCache.cachedStageEndTimes[i] = 0;
  }
  
  if (programState.activeProgramId < 0 || programState.activeProgramId >= getProgramCount()) {
    fermentCache.cachedProgramEndTime = 0;
    fermentCache.cachedTotalDuration = 0;
    fermentCache.cachedElapsedTime = 0;
    fermentCache.cachedRemainingTime = 0;
    fermentCache.isValid = true;
    return;
  }
  
  Program* p = getActiveProgramMutable();
  if (!p || p->customStages.size() == 0) {
    fermentCache.cachedProgramEndTime = 0;
    fermentCache.cachedTotalDuration = 0;
    fermentCache.cachedElapsedTime = 0;
    fermentCache.cachedRemainingTime = 0;
    fermentCache.isValid = true;
    return;
  }
  
  // Calculate stage end times
  unsigned long runningTime = 0; // Always start from 0 for cumulative durations
  
  if (programState.isRunning && programState.customStageStart > 0) {
    // Running: calculate from current stage position
    unsigned long elapsed = (millis() - programState.customStageStart) / 1000;
    CustomStage& currentStage = p->customStages[programState.customStageIdx];
    unsigned long adjustedStageDuration = getAdjustedStageTimeMs(currentStage.min * 60 * 1000, currentStage.isFermentation) / 1000;
    unsigned long remaining = (elapsed < adjustedStageDuration) ? (adjustedStageDuration - elapsed) : 0;
    
    // Calculate cumulative durations for completed stages
    for (size_t i = 0; i < programState.customStageIdx && i < 20; i++) {
      CustomStage& completedStage = p->customStages[i];
      unsigned long completedDuration = getAdjustedStageTimeMs(completedStage.min * 60 * 1000, completedStage.isFermentation) / 1000;
      runningTime += completedDuration;
      fermentCache.cachedStageEndTimes[i] = runningTime; // Store cumulative duration
    }
    
    // Add current stage total duration (not just remaining)
    runningTime += adjustedStageDuration;
    
    // Cache current stage end time (cumulative duration from program start)
    time_t currentTime = time(nullptr);
    time_t programStart = programState.actualStageStartTimes[0];
    unsigned long elapsedProgramTime = (currentTime > programStart && programStart > 0) ? (currentTime - programStart) : runningTime;
    
    if (programState.customStageIdx < 20) {
      fermentCache.cachedStageEndTimes[programState.customStageIdx] = runningTime;
    }
    
    // Calculate remaining stages - accumulate durations
    for (size_t i = programState.customStageIdx + 1; i < p->customStages.size() && i < 20; i++) {
      CustomStage& stage = p->customStages[i];
      unsigned long adjustedDuration = getAdjustedStageTimeMs(stage.min * 60 * 1000, stage.isFermentation) / 1000;
      runningTime += adjustedDuration;
      // Stage end time = cumulative duration up to this point
      fermentCache.cachedStageEndTimes[i] = runningTime;
    }
    
    // Calculate timing data for UI using the correctly calculated program end time
    if (programState.actualStageStartTimes[0] > 0) {
      // Calculate absolute program end time for display
      time_t programEndTime = programStart + runningTime;
      fermentCache.cachedProgramEndTime = programEndTime; // Absolute end timestamp
      fermentCache.cachedTotalDuration = runningTime; // Total duration in seconds
      fermentCache.cachedElapsedTime = elapsedProgramTime;
      fermentCache.cachedRemainingTime = (programEndTime > currentTime) ? 
                                        (programEndTime - currentTime) : 0;
    } else {
      fermentCache.cachedProgramEndTime = 0;
      fermentCache.cachedTotalDuration = runningTime;
      fermentCache.cachedElapsedTime = 0;
      fermentCache.cachedRemainingTime = runningTime;
    }
  } else {
    // Not running - calculate full program preview from now
    for (size_t i = 0; i < p->customStages.size() && i < 20; i++) {
      CustomStage& stage = p->customStages[i];
      unsigned long adjustedDuration = getAdjustedStageTimeMs(stage.min * 60 * 1000, stage.isFermentation) / 1000;
      runningTime += adjustedDuration;
      fermentCache.cachedStageEndTimes[i] = runningTime;
    }
    
    fermentCache.cachedProgramEndTime = runningTime;
    fermentCache.cachedTotalDuration = 0;
    fermentCache.cachedElapsedTime = 0;
    fermentCache.cachedRemainingTime = 0;
  }
  
  fermentCache.isValid = true;
}

// === WiFi Cache Implementation ===

// Update WiFi cache if needed (low-impact check)
void WiFiCache::updateIfNeeded() {
    unsigned long now = millis();
    if (now - lastCacheUpdate >= CACHE_UPDATE_INTERVAL) {
        lastCacheUpdate = now;
        
        // Cache WiFi status
        cachedConnected = (WiFi.status() == WL_CONNECTED);
        
        if (cachedConnected) {
            // Only call expensive toString() when actually connected and cache is stale
            cachedIPString = WiFi.localIP().toString();
            cachedSSID = WiFi.SSID();
            cachedRSSI = WiFi.RSSI();
        } else {
            cachedIPString = "0.0.0.0";
            cachedSSID = "";
            cachedRSSI = 0;
        }
    }
}

// Get cached IP string (always fast)
const String& WiFiCache::getIPString() {
    updateIfNeeded();
    return cachedIPString;
}

// Get cached connection status
bool WiFiCache::isConnected() {
    updateIfNeeded();
    return cachedConnected;
}

// Get cached SSID
const String& WiFiCache::getSSID() {
    updateIfNeeded();
    return cachedSSID;
}

// Get cached RSSI
int WiFiCache::getRSSI() {
    updateIfNeeded();
    return cachedRSSI;
}
