# ESP32 AsyncWebServer Crash Analysis

## Issue Summary
- **Problem**: Queue semaphore assertion failure at `server.begin()` in ESP32
- **Location**: `breadmaker_controller.ino` line 352: `server.begin()`
- **Error**: `assert failed: xQueueSemaphoreTake queue.c:1709 (( pxQueue ))`
- **Confirmed**: NOT related to display library (LovyanGFX) - crash persists with display disabled

## Root Cause
ESPAsyncWebServer library has known compatibility issues with ESP32, particularly with newer Arduino Core versions. The queue semaphore failure occurs during internal AsyncWebServer initialization.

## Solutions to Try

### Option 1: Switch to Standard WebServer (Recommended)
Replace ESPAsyncWebServer with the standard ESP32 WebServer library for better stability.

### Option 2: ESPAsyncWebServer Fixes
- Try different version of ESPAsyncWebServer library
- Adjust ESP32 Arduino Core version
- Configure stack size parameters

### Option 3: Alternative Libraries
- ESP32WebServer (Arduino core built-in)
- ElegantOTA for updates
- Custom web handling

## Implementation Plan
1. Test with standard WebServer library first
2. If successful, gradually port AsyncWebServer features
3. Maintain API compatibility for existing endpoints

## Files to Modify
- `breadmaker_controller.ino`: Change includes and server declaration
- `web_endpoints.h`: Update function signatures
- `web_endpoints.cpp`: Convert async handlers to standard handlers
