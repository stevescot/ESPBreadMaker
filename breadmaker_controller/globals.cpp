// Ensure struct definitions are available
#include "globals.h"
// --- Fermentation tracking state instance ---
FermentationState fermentState = {
  0.0, // initialFermentTemp
  1.0, // fermentationFactor
  0,   // lastFermentAdjust
  0,   // predictedCompleteTime
  0.0, // fermentLastTemp
  1.0, // fermentLastFactor
  0,   // fermentLastUpdateMs
  0.0  // fermentWeightedSec
};

// Settings save deferral timer
unsigned long pendingSettingsSaveTime = 0;

// --- Dynamic restart tracking state instance ---
DynamicRestartState dynamicRestart = {
  0,    // lastDynamicRestart
  "",   // lastDynamicRestartReason
  0     // dynamicRestartCount
};

// --- Output states instance ---
OutputStates outputStates;

// --- Temperature averaging state instance ---
TemperatureAveragingState tempAvg;

// --- WiFi cache instance ---
WiFiCache wifiCache;

// --- Program state instance ---
ProgramState programState;

// --- PID control instance ---
PIDControl pid;

// --- Safety system instance ---
SafetySystem safetySystem;

// --- Finish-by configuration instances ---
FinishByConfig finishByConfig;
FinishByState finishByState;
