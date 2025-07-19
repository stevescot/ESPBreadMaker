# ESP32 Breadmaker Web API - Original Endpoint Specifications

This document serves as the authoritative reference for recreating the web endpoints after migrating from AsyncWebServer to standard WebServer.

## Core Endpoints

### Status and Control
- `GET /` - Serve main web interface (index.html)
- `GET /status` - **CRITICAL**: Main status JSON with all breadmaker state
- `GET /start` - Start program (immediate or scheduled with ?time=HH:MM, ?stage=N)
- `GET /stop` - Stop current program
- `GET /advance` - Advance to next stage
- `GET /back` - Go back to previous stage  
- `GET /resume` - Resume saved program state
- `GET /select?program=N` - Select program by index

### Manual Controls
- `GET /toggle_heater` - Toggle heater on/off
- `GET /toggle_motor` - Toggle motor on/off
- `GET /toggle_light` - Toggle light on/off
- `GET /toggle_buzzer` - Toggle buzzer on/off
- `GET /beep` - Short beep sound

### Temperature & PID
- `GET /sensor` - Raw temperature reading
- `GET /set_sensor_interval?interval=MS` - Set temperature sampling interval
- `GET /api/pid` - Get PID parameters and current values
- `POST /api/pid` - Set PID parameters (JSON body: {kp, ki, kd, setpoint})
- `GET /api/pid_profiles` - Get available PID profiles
- `POST /api/pid_profiles` - Save/load PID profiles

### Programs & Configuration
- `GET /api/programs` - List all available programs
- `GET /api/program?id=N` - Get specific program details
- `POST /api/program` - Upload/save program
- `DELETE /api/program?id=N` - Delete program
- `GET /api/files` - List filesystem files
- `POST /api/files` - Upload files
- `DELETE /api/files?path=PATH` - Delete files/folders

### System & Diagnostics
- `GET /api/firmware_info` - Firmware version and build info
- `POST /api/restart` - Restart the ESP32
- `GET /api/output_mode` - Get current output mode (relay/PWM)
- `POST /api/output_mode` - Set output mode
- `GET /api/calibration` - Get temperature calibration data
- `POST /api/calibration` - Set temperature calibration
- `GET /api/ota` - OTA update status
- `POST /api/ota_upload` - OTA firmware upload
- `GET /api/home_assistant` - Home Assistant integration data

### Static Files
- All files from `/data/` directory (HTML, CSS, JS, JSON)
- CORS headers for cross-origin requests
- Proper MIME types (text/html, application/json, etc.)

## Status Object Structure (GET /status)

The status endpoint returns a comprehensive JSON object with all breadmaker state:

```json
{
  "state": "on|off",
  "stage": "Stage Name or Idle",
  "program": "Program Name",
  "programId": 0,
  "temperature": 25.5,
  "setpoint": 30.0,
  "heater": true,
  "motor": false,
  "light": false,
  "buzzer": false,
  "manual_mode": false,
  "stageIdx": 2,
  "mixIdx": 0,
  "stage_time_left": 1800,
  "stage_ready_at": 1672531200000,
  "program_ready_at": 1672541200000,
  "startupDelayComplete": true,
  "startupDelayRemainingMs": 0,
  "fermentationFactor": 1.0,
  "predictedCompleteTime": 1672541200000,
  "initialFermentTemp": 20.0,
  "actualStageStartTimes": [1672527600, 1672529400, ...],
  "health": {
    "uptime_sec": 86400,
    "firmware_version": "ESP32-WebServer",
    "build_date": "Jan 18 2025 10:30:00",
    "reset_reason": "power_on",
    "chip_id": 12345678,
    "memory": {
      "free_heap": 200000,
      "max_free_block": 100000,
      "min_free_heap": 180000,
      "fragmentation": 10.5
    },
    "performance": {
      "avg_loop_time_us": 50,
      "max_loop_time_us": 500,
      "loop_count": 1000000
    },
    "network": {
      "connected": true,
      "rssi": -45,
      "ip": "192.168.1.100"
    },
    "filesystem": {
      "usedBytes": 5242880,
      "totalBytes": 10485760,
      "freeBytes": 5242880,
      "utilization": 50.0
    },
    "temperature_control": {
      "input": 25.5,
      "output": 0.75,
      "raw_temp": 25.6,
      "thermal_runaway": false,
      "sensor_fault": false,
      "window_elapsed_ms": 15000,
      "window_size_ms": 30000,
      "on_time_ms": 22500,
      "duty_cycle_percent": 75.0
    }
  }
}
```

## Request/Response Patterns

### JSON Responses
All API endpoints return JSON with appropriate HTTP status codes:
- `200 OK` - Success with data
- `400 Bad Request` - Invalid parameters
- `404 Not Found` - Resource not found
- `500 Internal Server Error` - System error

### Error Response Format
```json
{
  "error": "error_code",
  "message": "Human readable error message"
}
```

### POST Request Format
POST endpoints expect JSON in request body:
```json
Content-Type: application/json
{
  "parameter": "value"
}
```

## WebServer Implementation Notes

### Lambda Function Signatures
- AsyncWebServer: `[](AsyncWebServerRequest* req){ ... }`
- WebServer: `[&](){ ... }` (capture server by reference)

### Response Methods
- AsyncWebServer: `req->send(code, type, content)`
- WebServer: `server.send(code, type, content)`

### Parameter Access
- AsyncWebServer: `req->hasParam("name")`, `req->getParam("name")->value()`
- WebServer: `server.hasArg("name")`, `server.arg("name")`

### File Serving
- AsyncWebServer: `req->send(FFat, path)`
- WebServer: Custom implementation using `server.streamFile()`

### JSON Streaming
- AsyncWebServer: `AsyncResponseStream* response = req->beginResponseStream("application/json")`
- WebServer: Build string and use `server.send(200, "application/json", jsonString)`

## Critical Functions to Implement

1. **streamStatusJson()** - Generate complete status JSON
2. **serveStaticFile()** - Serve files from FFat filesystem  
3. **sendJsonError()** - Standard error response format
4. **getStatusJsonString()** - Build status JSON as string
5. **registerWebEndpoints()** - Register all endpoints with server

## Migration Priority

1. **High Priority**: `/status`, `/start`, `/stop`, manual controls
2. **Medium Priority**: PID endpoints, programs API, static files
3. **Low Priority**: Advanced diagnostics, OTA, calibration

This specification ensures the migrated WebServer endpoints maintain full compatibility with the existing web UI and provide all the functionality that was available with AsyncWebServer.
