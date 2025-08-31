#include <FFat.h>
#include <ArduinoJson.h>
#include "programs_manager.h"
#include "globals.h"

// External variable declarations
extern bool debugSerial;

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
  
  // MEMORY OPTIMIZATION: Use much smaller buffer and streaming parser
  // Reduced from 4096 to 1024 bytes (75% reduction)
  DynamicJsonDocument doc(1024);  
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  
  if (err) {
    Serial.printf("[ERROR] Failed to parse programs_index.json: %s (try reducing file size)\n", err.c_str());
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
  
  // MEMORY OPTIMIZATION: Reduced from 3072 to 1536 bytes (50% reduction)
  // Individual program files should be smaller than the full index
  DynamicJsonDocument doc(1536);  
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  
  if (err) {
    Serial.printf("[ERROR] Failed to parse %s: %s (Free heap: %u bytes)\n", programFileName.c_str(), err.c_str(), ESP.getFreeHeap());
    Serial.println("[INFO] Consider simplifying program file or reducing stages/data");
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
    cs.disableAutoAdjust = st["disableAutoAdjust"] | false;
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
      ms.mixMs = m["mixMs"] | 0;        // NEW: millisecond fields
      ms.waitMs = m["waitMs"] | 0;      // NEW: millisecond fields
      ms.knockdown = m["knockdown"] | false;  // NEW: knockdown flag
      ms.label = m["label"] | String("");     // NEW: label field
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

bool splitProgramsJson() {
  // Split main programs.json into individual program files and metadata index
  // MEMORY OPTIMIZED: Process programs one at a time instead of loading entire array
  Serial.println("[INFO] Starting programs.json split operation (memory optimized)");
  
  // Read the main programs.json file
  File mainFile = FFat.open("/programs.json", "r");
  if (!mainFile || mainFile.size() == 0) {
    if (mainFile) mainFile.close();
    Serial.println("[ERROR] programs.json not found or empty");
    return false;
  }
  
  Serial.printf("[INFO] programs.json found, size: %zu bytes (Free heap: %u bytes)\n", 
                mainFile.size(), ESP.getFreeHeap());
  
  // Create programs directory if it doesn't exist
  if (!FFat.exists("/programs")) {
    FFat.mkdir("/programs");
    Serial.println("[INFO] Created /programs directory");
  }
  
  // Use smaller buffer and parse the array manually
  // Read just enough to get the array structure
  String content;
  content.reserve(mainFile.size() + 100); // Reserve space to avoid reallocations
  
  while (mainFile.available()) {
    content += mainFile.readString();
  }
  mainFile.close();
  
  Serial.printf("[INFO] File content loaded (Free heap: %u bytes)\n", ESP.getFreeHeap());
  
  // Quick validation
  if (!content.startsWith("[") || !content.endsWith("]")) {
    Serial.println("[ERROR] programs.json is not a valid array");
    return false;
  }
  
  // Remove outer brackets and whitespace
  content = content.substring(1, content.length() - 1);
  content.trim();
  
  // Prepare metadata collection (smaller buffer)
  DynamicJsonDocument metaDoc(1024);
  JsonArray metaArray = metaDoc.to<JsonArray>();
  
  int successCount = 0;
  int startPos = 0;
  
  // Split on top-level commas (between program objects)
  while (startPos < content.length()) {
    int nextComma = -1;
    int braceDepth = 0;
    bool inString = false;
    bool escaped = false;
    
    // Find the next top-level comma or end of content
    for (int i = startPos; i < content.length(); i++) {
      char c = content[i];
      
      if (escaped) {
        escaped = false;
        continue;
      }
      
      if (c == '\\') {
        escaped = true;
        continue;
      }
      
      if (c == '"') {
        inString = !inString;
        continue;
      }
      
      if (!inString) {
        if (c == '{') {
          braceDepth++;
        } else if (c == '}') {
          braceDepth--;
        } else if (c == ',' && braceDepth == 0) {
          nextComma = i;
          break;
        }
      }
    }
    
    // Extract program JSON
    String programJson;
    if (nextComma != -1) {
      programJson = content.substring(startPos, nextComma);
      startPos = nextComma + 1;
    } else {
      programJson = content.substring(startPos);
      startPos = content.length(); // End the loop
    }
    
    programJson.trim();
    if (programJson.length() == 0) continue;
    
    // Parse individual program with minimal memory
    DynamicJsonDocument programDoc(1024); // Smaller buffer per program
    DeserializationError err = deserializeJson(programDoc, programJson);
    
    if (err) {
      Serial.printf("[WARNING] Failed to parse program: %s\n", err.c_str());
      continue;
    }
    
    JsonObject program = programDoc.as<JsonObject>();
    
    if (!program.containsKey("id")) {
      Serial.println("[WARNING] Skipping program without id");
      continue;
    }
    
    int programId = program["id"];
    String filename = "/programs/program_" + String(programId) + ".json";
    
    // Fix data types in the program object before saving
    if (program.containsKey("fermentBaselineTemp") && program["fermentBaselineTemp"].is<String>()) {
      program["fermentBaselineTemp"] = program["fermentBaselineTemp"].as<String>().toFloat();
    }
    if (program.containsKey("fermentQ10") && program["fermentQ10"].is<String>()) {
      program["fermentQ10"] = program["fermentQ10"].as<String>().toFloat();
    }
    
    // Write individual program file
    File progFile = FFat.open(filename, "w");
    if (!progFile) {
      Serial.printf("[ERROR] Failed to create %s\n", filename.c_str());
      continue;
    }
    
    serializeJson(program, progFile);
    progFile.close();
    
    Serial.printf("[INFO] Created %s (Free heap: %u bytes)\n", filename.c_str(), ESP.getFreeHeap());
    successCount++;
    
    // Create metadata object for index
    JsonObject meta = metaArray.createNestedObject();
    meta["id"] = program["id"];
    meta["name"] = program["name"];
    if (program.containsKey("notes")) meta["notes"] = program["notes"];
    if (program.containsKey("icon")) meta["icon"] = program["icon"];
    // Ensure fermentation parameters are stored as numbers, not strings
    if (program.containsKey("fermentBaselineTemp")) {
      if (program["fermentBaselineTemp"].is<String>()) {
        meta["fermentBaselineTemp"] = program["fermentBaselineTemp"].as<String>().toFloat();
      } else {
        meta["fermentBaselineTemp"] = program["fermentBaselineTemp"];
      }
    }
    if (program.containsKey("fermentQ10")) {
      if (program["fermentQ10"].is<String>()) {
        meta["fermentQ10"] = program["fermentQ10"].as<String>().toFloat();
      } else {
        meta["fermentQ10"] = program["fermentQ10"];
      }
    }
    
    // Force garbage collection between programs
    programDoc.clear();
  }
  
  // Create programs_index.json with metadata
  File indexFile = FFat.open("/programs_index.json", "w");
  if (!indexFile) {
    Serial.println("[ERROR] Failed to create programs_index.json");
    return false;
  }
  
  serializeJson(metaDoc, indexFile);
  indexFile.close();
  
  Serial.printf("[INFO] Created programs_index.json with %d programs\n", metaArray.size());
  Serial.printf("[INFO] Split operation complete: %d programs processed successfully (Free heap: %u bytes)\n", 
                successCount, ESP.getFreeHeap());
  
  // Reload metadata to reflect changes
  loadProgramMetadata();
  
  return successCount > 0;
}

// --- Cache invalidation functions ---
void invalidateProgramCache(int programId) {
  if (debugSerial) Serial.printf("[CACHE] Invalidating cache for program %d\n", programId);
  
  // If this is the currently active program, unload it
  if (activeProgram.id == programId) {
    unloadActiveProgram();
    if (debugSerial) Serial.printf("[CACHE] Unloaded active program %d from cache\n", programId);
  }
  
  // Note: Individual program files don't affect the metadata cache
  // Only reload metadata if this was a structural change
}

void invalidateProgramMetadataCache() {
  if (debugSerial) Serial.println("[CACHE] Invalidating program metadata cache");
  
  // Clear and reload all program metadata
  loadProgramMetadata();
  
  // Also unload active program since metadata might have changed
  if (activeProgram.id != -1) {
    int oldId = activeProgram.id;
    unloadActiveProgram();
    if (debugSerial) Serial.printf("[CACHE] Unloaded active program %d due to metadata cache invalidation\n", oldId);
  }
  
  if (debugSerial) Serial.printf("[CACHE] Program metadata cache reloaded, %zu programs available\n", programMetadata.size());
}


