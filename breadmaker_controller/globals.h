#pragma once
#include <Arduino.h>
// Global constants for use across modules
constexpr int MAX_PROGRAM_STAGES = 20;
constexpr int MAX_TEMP_SAMPLES = 50;
constexpr unsigned long STARTUP_DELAY_MS = 15000UL;
// ESP32 TTGO T-Display analog pin for RTD temperature sensor
constexpr int PIN_RTD = 36;  // ADC1_CH0 - analog input only, available on TTGO T-Display

// --- Fermentation tracking state struct ---
typedef struct {
  float initialFermentTemp;
  float fermentationFactor;
  unsigned long lastFermentAdjust;
  unsigned long predictedCompleteTime;
  float fermentLastTemp;
  float fermentLastFactor;
  unsigned long fermentLastUpdateMs;
  double fermentWeightedSec;
} FermentationState;

extern FermentationState fermentState;

// Settings save deferral
extern unsigned long pendingSettingsSaveTime;

// --- Dynamic restart tracking state struct ---
typedef struct {
  unsigned long lastDynamicRestart;
  String lastDynamicRestartReason;
  unsigned int dynamicRestartCount;
} DynamicRestartState;

extern DynamicRestartState dynamicRestart;

// Memory-efficient temperature averaging using Exponential Moving Average (EMA)
// Reduces memory usage from 200+ bytes to just 16 bytes while providing better filtering
#ifndef TEMP_EMA_STATE_STRUCT_DEFINED
#define TEMP_EMA_STATE_STRUCT_DEFINED
struct TemperatureEMAState {
    float smoothedTemperature = 0.0;    // Current smoothed temperature value
    float alpha = 0.1;                  // Smoothing factor (0.01 = very smooth, 0.5 = very responsive)
    float spikeThreshold = 5.0;         // °C - ignore readings that change by more than this
    bool initialized = false;           // Has the EMA been seeded with a value?
    unsigned long lastUpdate = 0;       // Last update timestamp
    unsigned long updateInterval = 500; // Update interval in milliseconds
    float lastRawTemp = 0.0;           // For spike detection
    uint32_t sampleCount = 0;          // Total samples processed (for statistics)
};
#endif

// Legacy compatibility aliases (for existing code that expects old structure)
#define TemperatureAveragingState TemperatureEMAState
#define tempSampleCount alpha  // Map old tempSampleCount to alpha (will need conversion)
#define tempSampleInterval updateInterval
#define averagedTemperature smoothedTemperature
#define tempSamplesReady initialized

#ifndef OUTPUT_STATES_STRUCT_DEFINED
#define OUTPUT_STATES_STRUCT_DEFINED
struct OutputStates {
    bool heater = false;
    bool motor = false;
    bool light = false;
    bool buzzer = false;
};
#endif

extern OutputStates outputStates;
extern TemperatureAveragingState tempAvg;

// WiFi status caching for performance optimization
#ifndef WIFI_CACHE_STRUCT_DEFINED
#define WIFI_CACHE_STRUCT_DEFINED
struct WiFiCache {
    String cachedIPString = "0.0.0.0";
    String cachedSSID = "";
    bool cachedConnected = false;
    int cachedRSSI = 0;
    unsigned long lastCacheUpdate = 0;
    static constexpr unsigned long CACHE_UPDATE_INTERVAL = 5000; // Update every 5 seconds
    
    // Update cache if needed (implemented in missing_stubs.cpp)
    void updateIfNeeded();
    
    // Get cached IP string (always fast)
    const String& getIPString();
    
    // Get cached connection status
    bool isConnected();
    
    // Get cached SSID
    const String& getSSID();
    
    // Get cached RSSI
    int getRSSI();
};
extern WiFiCache wifiCache;
#endif

#ifndef PROGRAM_STATE_STRUCT_DEFINED
#define PROGRAM_STATE_STRUCT_DEFINED
#include <vector>
#include <ctime>
struct Program;
struct ProgramState {
    unsigned int activeProgramId = 0;
    Program* customProgram = nullptr;
    size_t customStageIdx = 0, customMixIdx = 0, maxCustomStages = 0;
    unsigned long customStageStart = 0, customMixStepStart = 0;
    time_t programStartTime = 0;
    time_t actualStageStartTimes[MAX_PROGRAM_STAGES] = {0};
    bool isRunning = false, manualMode = false;
};
#endif

extern ProgramState programState;
// PIDControl struct definition for use across multiple files
#ifndef PID_CONTROL_STRUCT_DEFINED
#define PID_CONTROL_STRUCT_DEFINED
#include <PID_v1.h>

// Temperature-dependent PID profile structure
struct PIDProfile {
    String name;
    float minTemp;
    float maxTemp;
    double kp;
    double ki;
    double kd;
    unsigned long windowMs;
    String description;
};

struct PIDControl {
    double Setpoint = 0, Input = 0, Output = 0;
    double Kp = 2.0, Ki = 5.0, Kd = 1.0;
    unsigned long sampleTime = 1000;
    double pidP = 0, pidI = 0, pidD = 0;
    double lastInput = 0, lastITerm = 0;
    PID* controller = nullptr;
    
    // Temperature-dependent profiles
    std::vector<PIDProfile> profiles;
    String activeProfile = "Baking Heat";
    bool autoSwitching = true;
    unsigned long lastProfileCheck = 0;
};
extern PIDControl pid;
#endif

// Safety monitoring system for critical operation parameters
#ifndef SAFETY_SYSTEM_STRUCT_DEFINED
#define SAFETY_SYSTEM_STRUCT_DEFINED
struct SafetySystem {
    // Temperature sensor validation
    bool temperatureValid = true;
    unsigned long lastValidTempTime = 0;
    float lastValidTemperature = 0.0;
    unsigned int invalidTempCount = 0;
    unsigned int zeroTempCount = 0;
    static constexpr unsigned int MAX_INVALID_TEMP = 5;      // Max consecutive invalid readings
    static constexpr unsigned int MAX_ZERO_TEMP = 3;        // Max consecutive zero readings
    static constexpr unsigned long TEMP_TIMEOUT_MS = 10000; // 10 seconds without valid temp
    
    // Heating effectiveness monitoring
    bool heatingEffective = true;
    unsigned long heatingStartTime = 0;
    float heatingStartTemp = 0.0;
    unsigned long lastTempRiseTime = 0;
    static constexpr unsigned long HEATING_CHECK_INTERVAL = 30000; // 30 seconds
    static constexpr float MIN_TEMP_RISE = 2.0;                    // Minimum rise in 30s
    static constexpr unsigned long MAX_HEATING_TIME = 300000;      // 5 minutes max heat time
    
    // Temperature limits for oven operation
    static constexpr float MAX_SAFE_TEMPERATURE = 235.0;     // Maximum oven temperature
    static constexpr float EMERGENCY_TEMPERATURE = 240.0;    // Emergency shutdown threshold
    static constexpr float MIN_VALID_TEMPERATURE = -10.0;    // Minimum valid sensor reading
    static constexpr float MAX_VALID_TEMPERATURE = 250.0;    // Maximum valid sensor reading
    
    // PID saturation monitoring (normal during heat-up)
    bool pidSaturated = false;
    unsigned long pidSaturationStart = 0;
    static constexpr unsigned long MAX_PID_SATURATION = 600000; // 10 minutes max saturation
    
    // Loop performance monitoring
    unsigned long loopStartTime = 0;
    unsigned long maxLoopTime = 0;
    unsigned long totalLoopTime = 0;
    unsigned int loopCount = 0;
    static constexpr unsigned long CRITICAL_LOOP_TIME = 1000; // 1 second critical threshold
    
    // Emergency shutdown state
    bool emergencyShutdown = false;
    String shutdownReason = "";
    unsigned long shutdownTime = 0;
    
    // Safety system enable/disable (for testing without heater)
    bool safetyEnabled = true;
    
    // Safety check intervals (low-impact timing)
    unsigned long lastSafetyCheck = 0;
    static constexpr unsigned long SAFETY_CHECK_INTERVAL = 1000; // Check every 1 second
    
    // Initialize safety system
    void init() {
        temperatureValid = true;
        lastValidTempTime = millis();
        invalidTempCount = 0;
        zeroTempCount = 0;
        heatingEffective = true;
        pidSaturated = false;
        emergencyShutdown = false;
        shutdownReason = "";
        maxLoopTime = 0;
        totalLoopTime = 0;
        loopCount = 0;
        lastSafetyCheck = millis();
    }
    
    // Check if temperature reading is valid
    bool isTemperatureValid(float temp) const {
        return (temp >= MIN_VALID_TEMPERATURE && temp <= MAX_VALID_TEMPERATURE && temp != 0.0);
    }
    
    // Check if temperature is in safe operating range
    bool isTemperatureSafe(float temp) const {
        return (temp <= MAX_SAFE_TEMPERATURE);
    }
    
    // Check if emergency shutdown is needed
    bool isEmergencyShutdownNeeded(float temp) const {
        return (temp >= EMERGENCY_TEMPERATURE);
    }
};
extern SafetySystem safetySystem;
#endif
