
#pragma once
#include <Arduino.h>
// Global constants for use across modules
#define MAX_PROGRAM_STAGES 20
#define MAX_TEMP_SAMPLES 50
#define STARTUP_DELAY_MS 15000UL
#define PIN_RTD A0

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

// --- Dynamic restart tracking state struct ---
typedef struct {
  unsigned long lastDynamicRestart;
  String lastDynamicRestartReason;
  unsigned int dynamicRestartCount;
} DynamicRestartState;

extern DynamicRestartState dynamicRestart;

// Struct for temperature averaging state
#ifndef TEMP_AVG_STATE_STRUCT_DEFINED
#define TEMP_AVG_STATE_STRUCT_DEFINED
struct TemperatureAveragingState {
    int tempSampleCount = 10;
    int tempRejectCount = 2;
    float tempSamples[MAX_TEMP_SAMPLES] = {0};
    int tempSampleIndex = 0;
    bool tempSamplesReady = false;
    unsigned long lastTempSample = 0;
    unsigned long tempSampleInterval = 500;
    float averagedTemperature = 0.0;
    float lastTemp = 0.0;
};
#endif

#ifndef OUTPUT_STATES_STRUCT_DEFINED
#define OUTPUT_STATES_STRUCT_DEFINED
struct OutputStates {
    bool heater = false;
    bool motor = false;
    bool light = false;
    bool buzzer = false;
};
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
// PIDControl struct definition for use across multiple files
#ifndef PID_CONTROL_STRUCT_DEFINED
#define PID_CONTROL_STRUCT_DEFINED
#include <PID_v1.h>
struct PIDControl {
    double Setpoint = 0, Input = 0, Output = 0;
    double Kp = 2.0, Ki = 5.0, Kd = 1.0;
    unsigned long sampleTime = 1000;
    double pidP = 0, pidI = 0, pidD = 0;
    double lastInput = 0, lastITerm = 0;
    PID* controller = nullptr;
};
extern PIDControl pid;
#endif
