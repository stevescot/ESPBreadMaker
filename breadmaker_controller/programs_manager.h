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
    std::vector<MixStep> mixPattern;
    String light; // legacy, can be ignored
    String buzzer; // legacy, can be ignored
};

// --- Program struct ---
struct Program {
    String name;
    String notes;
    String icon; // optional, can be empty
    std::vector<CustomStage> customStages;
};

// --- Programs map ---
extern std::map<String, Program> programs;

// --- Program management functions ---
void loadPrograms();
void savePrograms();

#endif // PROGRAMS_MANAGER_H
