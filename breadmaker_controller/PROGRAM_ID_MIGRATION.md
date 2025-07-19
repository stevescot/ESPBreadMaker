# Program ID Migration Analysis

## Overview

The ESP Breadmaker Controller underwent a significant migration from an **array index-based** system to an **explicit ID-based** system for program management. This change was necessary due to the transition from ESP8266 (40KB RAM) to ESP32 (320KB RAM) and the implementation of on-demand program loading.

## The Migration

### **Before: Array Index System (ESP8266 Era)**
```cpp
// Old system - direct array access
std::vector<Program> programs;  // All programs loaded in memory
Program& p = programs[arrayIndex];  // Direct access by position
```

**Frontend (old):**
```javascript
// Used array position directly
opt.value = idx;  // Array index (0, 1, 2, ...)
fetch('/select?idx=' + arrayIndex);  // Array position
```

**Backend (old):**
```cpp
// Direct array access
programState.activeProgramId = arrayIndex;
Program& p = programs[arrayIndex];
```

### **After: ID-Based System (ESP32 Era)**
```cpp
// New system - ID-based lookup
std::vector<ProgramMetadata> programMetadata;  // Lightweight metadata only
Program activeProgram;  // Only one program loaded at a time
// Find by ID, not by array position
```

**Frontend (new):**
```javascript
// Migration-compatible code
opt.value = (typeof p.id === 'number') ? p.id : idx;  // Use ID if available, fallback to index
fetch('/select?idx=' + programId);  // Send program ID (not array position)
const idx = programs.findIndex(p => String(p.id) === String(selectedId));  // Find array position by ID
```

**Backend (new):**
```cpp
// ID-based lookup required
int programId = server.arg("idx").toInt();  // This is now an ID, not array index
// Must find program by ID, then load it
bool found = ensureProgramLoaded(programId);  // Load specific program by ID
programState.activeProgramId = programId;  // Store the ID, not array position
```

## Data Structure Evolution

### **programs.json Structure**
```json
[
  {
    "id": 0,  // ← Explicit ID field (NEW)
    "name": "Bread - Sourdough Basic",
    "customStages": [...]
  },
  {
    "id": 1,  // ← Explicit ID field (NEW)  
    "name": "Bread - Sourdough In-Machine",
    "customStages": [...]
  }
]
```

**Key Changes:**
- Added explicit `"id"` field to each program
- ID values: 0, 1, 2, ..., 19 (currently 20 programs)
- IDs happen to match array positions, but this is not guaranteed
- System must work by ID lookup, not array indexing

## Migration Strategy in Frontend

The frontend implements backward compatibility:

```javascript
// 1. Program dropdown value assignment (supports both systems)
programs.forEach((p, idx) => {
  opt.value = (typeof p.id === 'number') ? p.id : idx;  // Use ID if available
  opt.textContent = p.name || `Program ${idx+1}`;
});

// 2. Program selection (sends ID, not array position)
fetch('/select?idx=' + encodeURIComponent(selectedId))

// 3. Array position lookup (find by ID)
const idx = programs.findIndex(p => String(p.id) === String(selectedId));
```

## Backend Migration Requirements

### **Parameter Interpretation**
- **Old**: `/select?idx=2` meant "select programs[2]" (array position 2)
- **New**: `/select?idx=2` means "select program with id=2" (could be any array position)

### **Required Backend Changes**
1. **Program Selection**: Find program by ID, not array index
2. **Program Loading**: Use `ensureProgramLoaded(programId)` for on-demand loading
3. **State Management**: Store program ID, not array position
4. **Validation**: Check if program ID exists, not if array index is valid

## Current Implementation Status

### ✅ **Working (Already Migrated)**
- **programs.json**: Has explicit ID fields (0-19)
- **Frontend**: Migration-compatible code in script.js
- **Memory Management**: On-demand loading system in place

### 🔄 **Needs Migration (Current TODOs)**
- **`/select` endpoint**: Currently treats `idx` as direct value, needs ID-based lookup
- **Program state management**: Integration with existing program loading system
- **Validation logic**: Check ID validity, not array bounds

## Example Implementation Pattern

**Correct endpoint implementation:**
```cpp
server.on("/select", HTTP_GET, [&](){
  if (server.hasArg("idx")) {
    int programId = server.arg("idx").toInt();  // This is an ID, not array index
    
    // 1. Validate ID exists
    if (!isProgramValid(programId)) {
      server.send(400, "application/json", "{\"error\":\"Invalid program ID\"}");
      return;
    }
    
    // 2. Load program by ID (on-demand loading)
    if (!ensureProgramLoaded(programId)) {
      server.send(500, "application/json", "{\"error\":\"Failed to load program\"}");
      return;
    }
    
    // 3. Set active program ID (not array position)
    programState.activeProgramId = programId;
    updateActiveProgramVars();
    
    server.send(200, "application/json", "{\"status\":\"ok\",\"selected\":" + String(programId) + "}");
  }
});
```

## Migration Benefits

1. **Memory Efficiency**: Only load programs when needed (on-demand)
2. **Scalability**: Can handle large program collections 
3. **Flexibility**: Program IDs can be non-sequential or reordered
4. **Backwards Compatibility**: Frontend supports both old and new systems
5. **Future-Proof**: Easy to add/remove programs without breaking array positions

## Testing Migration

To verify the migration works correctly:

1. **Frontend Test**: Check that program selection sends correct ID values
2. **Backend Test**: Verify `/select?idx=X` finds program by ID, not array position
3. **Edge Cases**: Test with non-sequential IDs, missing programs
4. **Memory Test**: Verify only selected program is loaded into memory

The migration maintains full compatibility while enabling the memory and performance benefits of the ESP32 platform.
