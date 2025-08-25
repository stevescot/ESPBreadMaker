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

// --- Lightweight program metadata for selection lists ---
struct ProgramMetadata {
    int id = -1;
    String name;
    String notes;
    String icon;
    float fermentBaselineTemp = 20.0f;
    float fermentQ10 = 2.0f;
    size_t stageCount = 0; // Number of stages without loading them
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

// --- Programs storage ---
extern std::vector<ProgramMetadata> programMetadata; // Lightweight metadata for all programs
extern Program activeProgram; // Only the currently active program is fully loaded

// --- Program management functions ---
void loadProgramMetadata(); // Load only names and basic info
bool loadSpecificProgram(int programId); // Load full program data for specific program
bool splitProgramsJson(); // Split main programs.json into individual files and index

// --- Helper functions ---
bool isProgramLoaded(int programId); // Check if a program is currently loaded
void unloadActiveProgram(); // Free memory by unloading active program
size_t getAvailableMemory(); // Check available heap memory

// --- API functions ---
const std::vector<ProgramMetadata>& getProgramMetadata(); // Get metadata for all programs
const Program* getActiveProgram(); // Get currently loaded program (null if none)
bool ensureProgramLoaded(int programId); // Ensure a program is loaded, load if necessary

// --- Helper functions for legacy migration ---
size_t getProgramCount(); // Get program count (replaces programs.size())
String getProgramName(int programId); // Get program name by ID 
bool isProgramValid(int programId); // Check if program exists and is valid
Program* getActiveProgramMutable(); // Get active program reference for modification
int findProgramIdByName(const String& name); // Find program ID by name

// --- Cache invalidation functions ---
void invalidateProgramCache(int programId); // Invalidate cache for specific program
void invalidateProgramMetadataCache(); // Invalidate metadata cache and reload

#endif // PROGRAMS_MANAGER_H
