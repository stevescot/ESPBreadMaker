#include <LittleFS.h>
#include <ArduinoJson.h>
#include "programs_manager.h"

// --- Programs storage ---
std::vector<ProgramMetadata> programMetadata;
Program activeProgram;

// --- Legacy compatibility ---
std::vector<Program> programs;

// --- Memory-efficient program loading ---

// Load only program metadata (names, IDs, basic info) - uses minimal memory
void loadProgramMetadata() {
  programMetadata.clear();
  
  File f = LittleFS.open("/programs.json", "r");
  if (!f || f.size() == 0) {
    if (f) f.close();
    Serial.println("[ERROR] programs.json not found or empty");
    return;
  }
  
  size_t fileSize = f.size();
  Serial.printf("[INFO] Loading program metadata, file size: %zu bytes\n", fileSize);
  
  // Use smaller buffer for streaming parse - we only need basic info
  DynamicJsonDocument doc(4096);  // 4KB buffer for metadata parsing
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  
  if (err) {
    Serial.printf("[ERROR] Failed to parse programs.json metadata: %s\n", err.c_str());
    return;
  }
  
  Serial.printf("[INFO] Parsing program metadata (memory usage: %zu bytes)\n", doc.memoryUsage());
  
  // Extract only metadata - don't load full stage data
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
  
  // Update legacy programs vector with empty programs for compatibility
  updateLegacyProgramsVector();
}

// Load full program data for a specific program ID
bool loadSpecificProgram(int programId) {
  // Check if already loaded
  if (programState.activeProgramId == programId && activeProgram.id == programId) {
    Serial.printf("[INFO] Program %d already loaded\n", programId);
    return true;
  }
  
  File f = LittleFS.open("/programs.json", "r");
  if (!f || f.size() == 0) {
    if (f) f.close();
    Serial.println("[ERROR] programs.json not found or empty");
    return false;
  }
  
  Serial.printf("[INFO] Loading full program data for ID %d\n", programId);
  
  // Use streaming JSON parser to find specific program
  DynamicJsonDocument doc(8192);  // 8KB buffer for single program
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  
  if (err) {
    Serial.printf("[ERROR] Failed to parse programs.json: %s\n", err.c_str());
    return false;
  }
  
  // Find the specific program
  for (JsonObject pobj : doc.as<JsonArray>()) {
    if (pobj["id"] == programId) {
      // Clear previous active program
      activeProgram = Program();
      
      // Load full program data
      activeProgram.id = programId;
      activeProgram.name = pobj["name"].as<String>();
      activeProgram.notes = pobj["notes"] | String("");
      activeProgram.icon = pobj["icon"] | String("");
      activeProgram.fermentBaselineTemp = pobj["fermentBaselineTemp"] | 20.0f;
      activeProgram.fermentQ10 = pobj["fermentQ10"] | 2.0f;
      
      activeProgram.customStages.clear();
      for (JsonObject st : pobj["customStages"].as<JsonArray>()) {
        CustomStage cs;
        cs.label = st["label"].as<String>();
        cs.min = st["min"] | 0;
        cs.temp = st["temp"] | 0.0;
        cs.noMix = st["noMix"] | false;
        cs.isFermentation = st["isFermentation"] | false;
        cs.instructions = st["instructions"] | String("");
        cs.light = st["light"] | String("");
        cs.buzzer = st["buzzer"] | String("");
        
        cs.mixPattern.clear();
        for (JsonObject m : st["mixPattern"].as<JsonArray>()) {
          MixStep ms;
          ms.mixSec = m["mixSec"] | 0;
          ms.waitSec = m["waitSec"] | 0;
          ms.durationSec = m["durationSec"] | 0;
          cs.mixPattern.push_back(ms);
        }
        activeProgram.customStages.push_back(cs);
      }
      
      // Update the active program (this is the critical part that was missing)
      programState.activeProgramId = programId;
      Serial.printf("[INFO] Loaded program '%s' with %zu stages\n", 
                    activeProgram.name.c_str(), activeProgram.customStages.size());
      
      // Update legacy programs vector
      updateLegacyProgramsVector();
      return true;
    }
  }
  
  Serial.printf("[ERROR] Program ID %d not found\n", programId);
  return false;
}

// Helper function to maintain backward compatibility
void updateLegacyProgramsVector() {
  programs.clear();
  
  // Find the maximum program ID
  int maxId = -1;
  for (const auto& meta : programMetadata) {
    if (meta.id > maxId) maxId = meta.id;
  }
  
  if (maxId >= 0) {
    // Resize vector to accommodate all program IDs
    programs.resize(maxId + 1);
    
    // Initialize all slots with dummy programs
    for (auto& p : programs) {
      p.id = -1;
    }
    
    // Fill in metadata for all programs
    for (const auto& meta : programMetadata) {
      if (meta.id >= 0 && meta.id < (int)programs.size()) {
        programs[meta.id].id = meta.id;
        programs[meta.id].name = meta.name;
        programs[meta.id].notes = meta.notes;
        programs[meta.id].icon = meta.icon;
        programs[meta.id].fermentBaselineTemp = meta.fermentBaselineTemp;
        programs[meta.id].fermentQ10 = meta.fermentQ10;
        
        // Only fill stages if this is the active program
        if (meta.id == programState.activeProgramId && activeProgram.id == meta.id) {
          programs[meta.id].customStages = activeProgram.customStages;
        } else {
          programs[meta.id].customStages.clear(); // Empty stages for non-active programs
        }
      }
    }
  }
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
  
  // Check if it's loaded in the programs vector
  if (programId < programs.size() && programs[programId].id == programId && !programs[programId].customStages.empty()) {
    return true;
  }
  
  return false;
}

void unloadActiveProgram() {
  activeProgram = Program();
  programState.activeProgramId = -1;
  Serial.println("[INFO] Active program unloaded to free memory");
  updateLegacyProgramsVector();
}

size_t getAvailableMemory() {
  return ESP.getFreeHeap();
}

// Save programs to LittleFS (reads from file and updates, maintaining all data)
void savePrograms() {
  // For saving, we need to maintain the full JSON structure
  // Read the entire file, update the active program if needed, then save
  
  File f = LittleFS.open("/programs.json", "r");
  if (!f || f.size() == 0) {
    if (f) f.close();
    Serial.println("[ERROR] Cannot save: programs.json not found or empty");
    return;
  }
  
  // Use larger buffer for full file operations
  DynamicJsonDocument doc(24576);  // 24KB buffer for full file
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  
  if (err) {
    Serial.printf("[ERROR] Failed to parse programs.json for saving: %s\n", err.c_str());
    return;
  }
  
  // Update the active program in the JSON if it's loaded
  if (programState.activeProgramId >= 0 && activeProgram.id == programState.activeProgramId) {
    for (JsonObject pobj : doc.as<JsonArray>()) {
      if (pobj["id"] == programState.activeProgramId) {
        // Update the active program's data
        pobj["name"] = activeProgram.name;
        pobj["notes"] = activeProgram.notes;
        pobj["icon"] = activeProgram.icon;
        pobj["fermentBaselineTemp"] = activeProgram.fermentBaselineTemp;
        pobj["fermentQ10"] = activeProgram.fermentQ10;
        
        // Update stages
        pobj.remove("customStages");
        JsonArray stages = pobj.createNestedArray("customStages");
        for (const CustomStage& cs : activeProgram.customStages) {
          JsonObject st = stages.createNestedObject();
          st["label"] = cs.label;
          st["min"] = cs.min;
          st["temp"] = cs.temp;
          st["noMix"] = cs.noMix;
          st["isFermentation"] = cs.isFermentation;
          st["instructions"] = cs.instructions;
          st["light"] = cs.light;
          st["buzzer"] = cs.buzzer;
          JsonArray mixArr = st.createNestedArray("mixPattern");
          for (const MixStep& ms : cs.mixPattern) {
            JsonObject m = mixArr.createNestedObject();
            m["mixSec"] = ms.mixSec;
            m["waitSec"] = ms.waitSec;
            m["durationSec"] = ms.durationSec;
          }
        }
        break;
      }
    }
  }
  
  // Save back to file
  f = LittleFS.open("/programs.json", "w");
  if (f) {
    serializeJsonPretty(doc, f);
    f.close();
    Serial.println("[INFO] Programs saved successfully");
  } else {
    Serial.println("[ERROR] Failed to open programs.json for writing");
  }
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
  if (isProgramLoaded(programId)) {
    return true;
  }
  return loadSpecificProgram(programId);
}


