#ifndef PROGRAMS_MANAGER_H
#define PROGRAMS_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <map>
#include <string>

// --- MixStep struct ---
struct MixStep {
    uint16_t mixSec = 0;
    uint16_t waitSec = 0;
    uint16_t durationSec = 0; // Optional: total duration for this step
};

// --- CustomStage struct ---
struct CustomStage {
    String label;
    uint16_t min = 0; // Duration in minutes
    float temp = 0.0f;
    bool noMix = false;
    bool isFermentation = false; // Flag to enable fermentation time adjustment
    std::vector<MixStep> mixPattern;
    String instructions; // Stage instructions
    String light; // legacy, can be ignored
    String buzzer; // legacy, can be ignored
};

// --- Program struct ---
struct Program {
    int id = -1; // Unique program ID
    String name;
    String notes;
    String icon; // optional, can be empty
    float fermentBaselineTemp = 20.0f; // Baseline temperature for fermentation calculations
    float fermentQ10 = 2.0f; // Q10 factor for fermentation time adjustment
    std::vector<CustomStage> customStages;
};



// --- Programs map ---
extern std::vector<Program> programs;
// --- Ordered program names (matches load order from JSON) ---
// programNamesOrdered is no longer needed; use programs[programId].name for lookups

// --- Program management functions ---
void loadPrograms();
void savePrograms();

#endif // PROGRAMS_MANAGER_H
