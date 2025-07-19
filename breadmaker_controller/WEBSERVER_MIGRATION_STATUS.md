# AsyncWebServer to WebServer Migration Status

## Overview
This document tracks the migration from AsyncWebServer to the standard ESP32 WebServer library to resolve compatibility issues and crashes.

## Migration Status: ⚠️ PARTIALLY COMPLETE

### ✅ Completed Tasks
- [x] **ESP32 Arduino Core downgraded** from 3.2.1 to 2.0.17
- [x] **Main controller updated** to use standard `WebServer` instead of `AsyncWebServer`
- [x] **Includes changed** in `breadmaker_controller.ino`: `<WebServer.h>` instead of `<ESPAsyncWebServer.h>`
- [x] **Server declaration updated**: `WebServer server(80)` instead of `AsyncWebServer server(80)`
- [x] **Build scripts updated** with ESP32 core version enforcement 
- [x] **Documentation updated** with version requirements and warnings
- [x] **Crashes eliminated** - firmware compiles and runs without AsyncWebServer crashes

### ❌ Remaining Tasks  
- [ ] **Convert web_endpoints.h** - Still references `AsyncWebServer&` parameter
- [ ] **Convert web_endpoints.cpp** - All 1,372 lines still use AsyncWebServer API:
  - [ ] `AsyncWebServerRequest*` → `WebServer` request handling  
  - [ ] `AsyncResponseStream` → `WebServer` response methods
  - [ ] `req->beginResponseStream()` → `server.send()` or `server.sendHeader()`
  - [ ] Lambda function signatures for endpoint handlers
  - [ ] JSON response streaming methods
  - [ ] File upload/download endpoints  
  - [ ] CORS headers and request parameter handling
- [ ] **Static file serving** - `server.serveStatic()` equivalent for web UI files
- [ ] **Re-enable registerWebEndpoints()** in main controller
- [ ] **Test all web functionality** after migration

## Current State
- **Firmware compiles** and runs crash-free with standard WebServer
- **Web endpoints are disabled** (`registerWebEndpoints()` commented out)
- **No web interface** until endpoints are migrated
- **Core breadmaker logic works** (display, temperature, PID, programs)

## Critical Files to Convert

### 1. web_endpoints.h (8 lines)
```cpp
// CURRENT (AsyncWebServer):
void registerWebEndpoints(AsyncWebServer& server);

// NEEDS TO CHANGE TO (WebServer):
void registerWebEndpoints(WebServer& server);
```

### 2. web_endpoints.cpp (1,372 lines)
**Major conversion needed for all endpoint definitions:**

```cpp
// CURRENT AsyncWebServer pattern:
server.on("/status", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream *response = req->beginResponseStream("application/json");
    streamStatusJson(*response);
    req->send(response);
});

// NEEDS TO CHANGE TO WebServer pattern:
server.on("/status", HTTP_GET, [](){
    server.send(200, "application/json", getStatusJson());
});
```

### 3. Static File Serving
**Current (commented out):**
```cpp
// server.serveStatic("/", FFat, "/");  // AsyncWebServer method
```

**Needs implementation for WebServer:**
```cpp
server.onNotFound([](){
    // Custom static file serving logic for FFat filesystem
});
```

## Next Steps Priority

1. **Convert web_endpoints.h** (quick fix - change function signature)
2. **Create WebServer-compatible endpoint handlers** (major effort - ~1000+ lines to convert)
3. **Implement static file serving** for web UI
4. **Re-enable registerWebEndpoints()** in main controller  
5. **Test web interface functionality**

## AsyncWebServer → WebServer API Changes

| AsyncWebServer | Standard WebServer |
|---------------|-------------------|
| `AsyncWebServerRequest* req` | Direct server methods |
| `req->beginResponseStream()` | `server.send()` |
| `req->hasParam("name")` | `server.hasArg("name")` |
| `req->getParam("name")->value()` | `server.arg("name")` |
| `req->send(FFat, path)` | Custom file serving |
| Lambda with request param | Lambda without params |

## Estimated Effort
- **web_endpoints.h**: 5 minutes
- **web_endpoints.cpp**: 4-6 hours (1,372 lines, ~50 endpoints)  
- **Static file serving**: 1-2 hours
- **Testing**: 2-3 hours
- **Total**: ~8-12 hours for complete migration

## Benefits After Migration
- ✅ **No more crashes** - Standard WebServer is stable on ESP32
- ✅ **Smaller firmware** - No AsyncWebServer library overhead  
- ✅ **Better memory usage** - Less RAM consumption
- ✅ **Future-proof** - Works with all ESP32 core versions
- ✅ **Maintainable** - Standard Arduino ESP32 library
