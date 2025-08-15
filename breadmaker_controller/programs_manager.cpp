#include <FFat.h>
#include <ArduinoJson.h>
#include "programs_manager.h"
#include "globals.h"

// --- Programs storage ---
std::vector<ProgramMetadata> programMetadata;
Program activeProgram;

// Load only program metadata (names, IDs, basic info) - uses minimal memory
void loadProgramMetadata() {
  programMetadata.clear();
  
  // Use lightweight index file instead of full programs.json
  File f = FFat.open("/programs_index.json", "r");
  if (!f || f.size() == 0) {
    if (f) f.close();
    Serial.println("[ERROR] programs_index.json not found or empty");
    return;
  }
  
  size_t fileSize = f.size();
  Serial.printf("[INFO] Loading program metadata from index, file size: %zu bytes\n", fileSize);
  
  // Much smaller buffer needed for index file
  DynamicJsonDocument doc(4096);  // 4KB buffer should be plenty for index
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  
  if (err) {
    Serial.printf("[ERROR] Failed to parse programs_index.json: %s\n", err.c_str());
    return;
  }
  
  Serial.printf("[INFO] Parsing program metadata (memory usage: %zu bytes)\n", doc.memoryUsage());
  
  // Extract metadata from index
  for (JsonObject pobj : doc.as<JsonArray>()) {
    ProgramMetadata meta;
    
    if (pobj.containsKey("id")) {
      meta.id = pobj["id"];
    } else {
      continue; // skip programs without id
    }
    
    meta.name = pobj["name"].as<String>();
    meta.notes = pobj["notes"] | String("");
    meta.icon = pobj["icon"] | String("");
    meta.fermentBaselineTemp = pobj["fermentBaselineTemp"] | 20.0f;
    meta.fermentQ10 = pobj["fermentQ10"] | 2.0f;
    
    // Count stages without loading them
    if (pobj.containsKey("customStages")) {
      meta.stageCount = pobj["customStages"].size();
    }
    
    programMetadata.push_back(meta);
  }
  
  Serial.printf("[INFO] Loaded metadata for %zu programs\n", programMetadata.size());
}

// Load full program data for a specific program ID
bool loadSpecificProgram(int programId) {
  // Check if already loaded
  if (programState.activeProgramId == programId && activeProgram.id == programId) {
    Serial.printf("[INFO] Program %d already loaded\n", programId);
    return true;
  }
  
  // Load individual program file instead of entire programs.json
  String programFileName = "/program_" + String(programId) + ".json";
  File f = FFat.open(programFileName, "r");
  if (!f || f.size() == 0) {
    if (f) f.close();
    Serial.printf("[ERROR] Program file %s not found or empty\n", programFileName.c_str());
    return false;
  }
  
  Serial.printf("[INFO] Loading program from %s (Free heap: %u bytes)\n", programFileName.c_str(), ESP.getFreeHeap());
  
  // Much smaller buffer needed for individual program file (max ~2.5KB)
  DynamicJsonDocument doc(3072);  // 3KB buffer - plenty for individual program
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  
  if (err) {
    Serial.printf("[ERROR] Failed to parse %s: %s (Free heap: %u bytes)\n", programFileName.c_str(), err.c_str(), ESP.getFreeHeap());
    return false;
  }
  
  // Clear previous active program
  activeProgram = Program();
  
  // Load program data from single program object
  JsonObject pobj = doc.as<JsonObject>();
  
  activeProgram.id = programId;
  activeProgram.name = pobj["name"].as<String>();
  activeProgram.notes = pobj["notes"] | String("");
  activeProgram.icon = pobj["icon"] | String("");
  activeProgram.fermentBaselineTemp = pobj["fermentBaselineTemp"] | 20.0f;
  activeProgram.fermentQ10 = pobj["fermentQ10"] | 2.0f;
  
  // Load stages with memory-efficient approach
  activeProgram.customStages.clear();
  JsonArray stages = pobj["customStages"];
  activeProgram.customStages.reserve(stages.size());  // Reserve exact space needed
  
  for (JsonObject st : stages) {
    CustomStage cs;
    cs.label = st["label"].as<String>();
    cs.min = st["min"] | 0;
    cs.temp = st["temp"] | 0.0;
    cs.noMix = st["noMix"] | false;
    cs.isFermentation = st["isFermentation"] | false;
    cs.instructions = st["instructions"] | String("");
    cs.light = st["light"] | String("");
    cs.buzzer = st["buzzer"] | String("");
    
    // Load mix pattern efficiently
    cs.mixPattern.clear();
    JsonArray mixArray = st["mixPattern"];
    cs.mixPattern.reserve(mixArray.size());  // Reserve exact space needed
    
    for (JsonObject m : mixArray) {
      MixStep ms;
      ms.mixSec = m["mixSec"] | 0;
      ms.waitSec = m["waitSec"] | 0;
      ms.durationSec = m["durationSec"] | 0;
      cs.mixPattern.push_back(ms);
    }
    activeProgram.customStages.push_back(cs);
  }
  
  // Update the active program
  programState.activeProgramId = programId;
  Serial.printf("[INFO] Loaded program '%s' with %zu stages (Free heap: %u bytes)\n", 
                activeProgram.name.c_str(), activeProgram.customStages.size(), ESP.getFreeHeap());
  
  return true;
}

// Legacy function for backward compatibility
void loadPrograms() {
  loadProgramMetadata();
  // Don't load any specific program yet - wait for selection
}

// Helper functions

bool isProgramLoaded(int programId) {
  // Check if it's the active program
  if (programState.activeProgramId == programId && activeProgram.id == programId) {
    return true;
  }
  
  return false;
}

void unloadActiveProgram() {
  activeProgram = Program();
  programState.activeProgramId = -1;
  Serial.println("[INFO] Active program unloaded to free memory");
}

size_t getAvailableMemory() {
  return ESP.getFreeHeap();
}

// API function to get program metadata (for selection lists)
const std::vector<ProgramMetadata>& getProgramMetadata() {
  return programMetadata;
}

// API function to get active program
const Program* getActiveProgram() {
  if (programState.activeProgramId >= 0 && activeProgram.id == programState.activeProgramId) {
    return &activeProgram;
  }
  return nullptr;
}

// API function to ensure a program is loaded
bool ensureProgramLoaded(int programId) {
  Serial.printf("[DEBUG] ensureProgramLoaded called with ID %d\n", programId);
  if (isProgramLoaded(programId)) {
    Serial.printf("[DEBUG] Program ID %d already loaded\n", programId);
    return true;
  }
  Serial.printf("[DEBUG] Program ID %d not loaded, attempting to load...\n", programId);
  bool result = loadSpecificProgram(programId);
  Serial.printf("[DEBUG] loadSpecificProgram returned: %s\n", result ? "true" : "false");
  return result;
}

// --- Helper functions for legacy migration ---

// Get program count (replaces programs.size())
size_t getProgramCount() {
  return programMetadata.size();
}

// Get program name by ID (replaces programs[id].name)
String getProgramName(int programId) {
  for (const auto& meta : programMetadata) {
    if (meta.id == programId) {
      return meta.name;
    }
  }
  return "";
}

// Check if program exists and is valid (replaces programs[id].id != -1)
bool isProgramValid(int programId) {
  for (const auto& meta : programMetadata) {
    if (meta.id == programId) {
      return true;
    }
  }
  return false;
}

// Get active program reference (replaces programs[activeProgramId])
Program* getActiveProgramMutable() {
  if (programState.activeProgramId >= 0 && activeProgram.id == programState.activeProgramId) {
    return &activeProgram;
  }
  return nullptr;
}

// Find program ID by name (replaces programs[i].name == name searches)
int findProgramIdByName(const String& name) {
  for (const auto& meta : programMetadata) {
    if (meta.name == name) {
      return meta.id;
    }
  }
  return -1;
}


