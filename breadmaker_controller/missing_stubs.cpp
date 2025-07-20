#include "missing_stubs.h"
#include "globals.h"
#include "programs_manager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>

#ifdef ESP32
#include <esp_system.h>
#include <WiFi.h>
#endif

// Define build date if not already defined
#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__
#endif

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

// Temperature and performance functions
float getAveragedTemperature() {
    return tempAvg.averagedTemperature;
}

void updateTemperatureSampling() {
  unsigned long nowMs = millis();
  
  // Sample at the configured interval
  if (nowMs - tempAvg.lastTempSample >= tempAvg.tempSampleInterval) {
    tempAvg.lastTempSample = nowMs;
    // Take a new temperature sample
    float rawTemp = analogRead(PIN_RTD) * 3.3 / 4095.0 * 100.0; // Simple ADC conversion for stub
    tempAvg.tempSamples[tempAvg.tempSampleIndex] = rawTemp;
    tempAvg.tempSampleIndex = (tempAvg.tempSampleIndex + 1) % tempAvg.tempSampleCount;
    // Mark samples as ready once we've filled the array at least once
    if (tempAvg.tempSampleIndex == 0 && !tempAvg.tempSamplesReady) {
      tempAvg.tempSamplesReady = true;
    }
    // Calculate averaged temperature if we have enough samples
    if (tempAvg.tempSamplesReady && tempAvg.tempSampleCount > 0) {
      // Step 1: Copy and sort samples for outlier rejection
      float sortedSamples[MAX_TEMP_SAMPLES];
      for (int i = 0; i < tempAvg.tempSampleCount; i++) {
        sortedSamples[i] = tempAvg.tempSamples[i];
      }
      // Simple bubble sort
      for (int i = 0; i < tempAvg.tempSampleCount - 1; i++) {
        for (int j = 0; j < tempAvg.tempSampleCount - i - 1; j++) {
          if (sortedSamples[j] > sortedSamples[j + 1]) {
            float temp = sortedSamples[j];
            sortedSamples[j] = sortedSamples[j + 1];
            sortedSamples[j + 1] = temp;
          }
        }
      }
      // Step 2: Get clean samples (reject outliers if we have enough samples)
      float cleanSamples[MAX_TEMP_SAMPLES];
      int cleanCount = 0;
      int effectiveSamples = tempAvg.tempSampleCount - (2 * tempAvg.tempRejectCount);
      if (effectiveSamples >= 3) {
        for (int i = tempAvg.tempRejectCount; i < tempAvg.tempSampleCount - tempAvg.tempRejectCount; i++) {
          cleanSamples[cleanCount++] = sortedSamples[i];
        }
      } else {
        for (int i = 0; i < tempAvg.tempSampleCount; i++) {
          cleanSamples[cleanCount++] = sortedSamples[i];
        }
      }
      // Step 3: Apply weighted average
      float weightedSum = 0.0;
      float totalWeight = 0.0;
      for (int i = 0; i < cleanCount; i++) {
        float weight = 0.5 + (1.5 * i) / (cleanCount - 1);
        weightedSum += cleanSamples[i] * weight;
        totalWeight += weight;
      }
      tempAvg.averagedTemperature = weightedSum / totalWeight;
    } else {
      tempAvg.averagedTemperature = rawTemp;
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

void checkAndSwitchPIDProfile() {
  // Only check every 5 seconds to avoid excessive switching
  if (millis() - pid.lastProfileCheck < 5000) return;
  pid.lastProfileCheck = millis();
  
  // Skip if auto-switching is disabled
  if (!pid.autoSwitching) return;
  
  // Skip if no valid temperature reading
  if (tempAvg.averagedTemperature <= 0) return;
  
  // Find appropriate profile for current temperature
  for (auto& profile : pid.profiles) {
    if (tempAvg.averagedTemperature >= profile.minTemp && 
        tempAvg.averagedTemperature < profile.maxTemp && 
        profile.name != pid.activeProfile) {
      Serial.printf("[checkAndSwitchPIDProfile] Temperature %.1f°C - switching from '%s' to '%s'\n",
                    tempAvg.averagedTemperature, pid.activeProfile.c_str(), profile.name.c_str());
      switchToProfile(profile.name);
      saveSettings();
      break;
    }
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
  
  // Disable heater if program not running or no setpoint
  if (!programState.isRunning || pid.Setpoint <= 0) {
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
          dynamicRestart.lastDynamicRestartReason = "Need more heat (output increased to " + String((pid.Output*100), 1) + "%)";
        } else {
          dynamicRestart.lastDynamicRestartReason = "Reduce heat (output decreased to " + String((pid.Output*100), 1) + "%)";
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
      pid.activeProfile = profileName;
      
      // Apply to PID controller
      if (pid.controller) {
        pid.controller->SetTunings(pid.Kp, pid.Ki, pid.Kd);
      }
      
      Serial.printf("[switchToProfile] Switched to '%s': Kp=%.6f, Ki=%.6f, Kd=%.6f\n",
                    profileName.c_str(), pid.Kp, pid.Ki, pid.Kd);
      return;
    }
  }
  Serial.printf("[switchToProfile] Profile '%s' not found!\n", profileName.c_str());
}

// Display message function used by OTA manager
void displayMessage(const String& message) {
  Serial.println("Display: " + message);
}

// Stream status JSON function - comprehensive implementation
// This function streams JSON directly without creating large strings in memory
void streamStatusJson(Print& out) {
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
    Program* p = getActiveProgramMutable();
    if (p) {
      out.print("\"program\":\"");
      out.print(p->name);
      out.print("\",");
      
      out.printf("\"programId\":%d,", programState.activeProgramId);
      
      // === Stage Information ===
      if (programState.customStageIdx < p->customStages.size()) {
        CustomStage& stage = p->customStages[programState.customStageIdx];
        
        out.print("\"stage\":\"");
        out.print(stage.label);
        out.print("\",");
        
        out.printf("\"stageIdx\":%u,", (unsigned)programState.customStageIdx);
        out.printf("\"setpoint\":%.1f,", stage.temp);
        
        // Calculate time left in current stage
        unsigned long timeLeft = 0;
        if (programState.isRunning && programState.customStageStart > 0) {
          unsigned long elapsed = (millis() - programState.customStageStart) / 1000;
          unsigned long stageDuration = stage.min * 60;
          timeLeft = (elapsed < stageDuration) ? (stageDuration - elapsed) : 0;
        }
        out.printf("\"stage_time_left\":%lu,", timeLeft);
        out.printf("\"timeLeft\":%lu,", timeLeft);
        
        // Stage ready time
        if (programState.isRunning && timeLeft > 0) {
          unsigned long readyAt = (time(nullptr) + timeLeft) * 1000; // Convert to ms
          out.printf("\"stage_ready_at\":%lu,", readyAt);
          out.printf("\"stageReadyAt\":%lu,", readyAt / 1000);
        } else {
          out.print("\"stage_ready_at\":0,");
          out.print("\"stageReadyAt\":0");
        }
        out.print(",");
      } else {
        out.print("\"stage\":\"Idle\",");
        out.print("\"stageIdx\":0,");
        out.print("\"setpoint\":0,");
        out.print("\"stage_time_left\":0,");
        out.print("\"timeLeft\":0,");
        out.print("\"stage_ready_at\":0,");
        out.print("\"stageReadyAt\":0,");
      }
    } else {
      out.print("\"program\":\"Unknown\",");
      out.printf("\"programId\":%d,", programState.activeProgramId);
      out.print("\"stage\":\"Idle\",");
      out.print("\"stageIdx\":0,");
      out.print("\"setpoint\":0,");
      out.print("\"stage_time_left\":0,");
      out.print("\"timeLeft\":0,");
      out.print("\"stage_ready_at\":0,");
      out.print("\"stageReadyAt\":0,");
    }
  } else {
    out.print("\"program\":\"None\",");
    out.print("\"programId\":-1,");
    out.print("\"stage\":\"Idle\",");
    out.print("\"stageIdx\":0,");
    out.print("\"setpoint\":0,");
    out.print("\"stage_time_left\":0,");
    out.print("\"timeLeft\":0,");
    out.print("\"stage_ready_at\":0,");
    out.print("\"stageReadyAt\":0,");
  }
  
  // === Temperature and Outputs ===
  float temp = getAveragedTemperature();
  out.printf("\"temperature\":%.1f,", temp);
  out.printf("\"temp\":%.1f,", temp);
  out.printf("\"setTemp\":%.1f,", pid.Setpoint);
  
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
  
  out.print("\"manual_mode\":");
  out.print(programState.manualMode ? "true" : "false");
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
  
  // === Stage Start Times Array ===
  out.print("\"actualStageStartTimes\":[");
  for (int i = 0; i < 20; i++) {
    if (i > 0) out.print(",");
    out.printf("%lu", (unsigned long)programState.actualStageStartTimes[i]);
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
  out.print(WiFi.status() == WL_CONNECTED ? "true" : "false");
  out.print(",");
  
  out.print("\"ip\":\"");
  out.print(WiFi.localIP().toString());
  out.print("\",");
  
  // === WiFi Details ===
  out.print("\"wifi\":{");
  if (WiFi.status() == WL_CONNECTED) {
    out.print("\"connected\":true,");
    out.print("\"ssid\":\"");
    out.print(WiFi.SSID());
    out.print("\",");
    out.printf("\"rssi\":%d,", WiFi.RSSI());
    out.print("\"ip\":\"");
    out.print(WiFi.localIP().toString());
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
  
  // === Status ===
  out.print("\"status\":\"ok\"");
  
  // Close main JSON object
  out.print("}");
}
