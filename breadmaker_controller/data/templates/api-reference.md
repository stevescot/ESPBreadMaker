# Breadmaker Controller API Reference

This document provides a comprehensive reference for the Breadmaker Controller REST API.

## Base URL

All API endpoints are relative to the base URL:
```
http://YOUR_BREADMAKER_IP/
```

## Authentication

Currently, no authentication is required. All endpoints are accessible via HTTP GET requests.

## Content Types

- Responses are typically `application/json` or `text/plain`
- Requests with bodies should use `application/json`

## Status Endpoints

### GET /status

Returns comprehensive status information.

**Response:**
```json
{
  "scheduledStart": 0,
  "running": true,
  "program": "default",
  "stage": "Knead",
  "elapsed": 120,
  "setpoint": 35.0,
  "stageTemperature": 35.0,
  "timeLeft": 1800,
  "stageReadyAt": 1640995200,
  "programReadyAt": 1640995200,
  "temp": 24.5,
  "motor": true,
  "light": false,
  "buzzer": false,
  "heater": false,
  "stageStartTime": 1640993400,
  "manualMode": false,
  "target": 35.0,
  "raw_temp": 512,
  "pid_output": 0.0,
  "kp": 2.0,
  "ki": 5.0,
  "kd": 1.0,
  "programList": ["default", "basic", "white"]
}
```

### GET /ha

Home Assistant optimized status endpoint.

**Response:**
```json
{
  "state": "on",
  "stage": "Knead",
  "program": "default",
  "temperature": 24.5,
  "motor": true,
  "light": false,
  "buzzer": false,
  "heater": false,
  "stage_time_left": 1800,
  "stage_ready_at": 1640995200,
  "program_ready_at": 1640995200,
  "heap_free": 25000,
  "uptime_sec": 3600,
  "wifi_rssi": -45,
  "ip": "192.168.1.100",
  "firmware_build": "Dec 31 2023 12:00:00",
  "manual_mode": false,
  "setpoint": 35.0,
  "pid_output": 0.0,
  "raw_temp": 512,
  "calib_points": 5
}
```

## Control Endpoints

### GET /start

Starts the breadmaker with the currently selected program.

**Response:**
```json
{
  "running": true,
  "program": "default",
  "stage": "Knead",
  "message": "Started"
}
```

### GET /stop

Stops the breadmaker and resets to idle state.

**Response:**
```json
{
  "running": false,
  "program": "default",
  "stage": "Idle",
  "message": "Stopped"
}
```

### GET /pause

Pauses the current program without losing progress.

**Response:**
```json
{
  "running": false,
  "program": "default",
  "stage": "Knead",
  "message": "Paused"
}
```

### GET /resume

Resumes a paused program.

**Response:**
```json
{
  "running": true,
  "program": "default",
  "stage": "Knead",
  "message": "Resumed"
}
```

### GET /advance

Advances to the next stage in the current program.

**Response:**
```json
{
  "running": true,
  "program": "default",
  "stage": "Rise",
  "message": "Advanced to next stage"
}
```

### GET /back

Goes back to the previous stage in the current program.

**Response:**
```json
{
  "running": true,
  "program": "default",
  "stage": "Mix",
  "message": "Returned to previous stage"
}
```

### GET /select

Selects a program for execution.

**Parameters:**
- `program` (required): Name of the program to select

**Example:**
```
GET /select?program=white
```

**Response:**
```json
{
  "running": false,
  "program": "white",
  "stage": "Idle",
  "message": "Program selected"
}
```

## Manual Control Endpoints

### GET /api/manual_mode

Controls manual mode state.

**Parameters:**
- `state` (optional): `true` to enable, `false` to disable

**Example:**
```
GET /api/manual_mode?state=true
```

**Response:**
```json
{
  "manualMode": true,
  "message": "Manual mode enabled"
}
```

### GET /api/motor

Controls motor output (manual mode only).

**Parameters:**
- `state` (optional): `true` to turn on, `false` to turn off

**Example:**
```
GET /api/motor?state=true
```

**Response:**
```json
{
  "motor": true,
  "manualMode": true,
  "message": "Motor turned on"
}
```

### GET /api/heater

Controls heater output (manual mode only).

**Parameters:**
- `state` (optional): `true` to turn on, `false` to turn off

**Example:**
```
GET /api/heater?state=true
```

**Response:**
```json
{
  "heater": true,
  "manualMode": true,
  "message": "Heater turned on"
}
```

### GET /api/light

Controls light output (manual mode only).

**Parameters:**
- `state` (optional): `true` to turn on, `false` to turn off

**Example:**
```
GET /api/light?state=true
```

**Response:**
```json
{
  "light": true,
  "manualMode": true,
  "message": "Light turned on"
}
```

### GET /api/buzzer

Controls buzzer output (manual mode only).

**Parameters:**
- `state` (optional): `true` to turn on, `false` to turn off

**Example:**
```
GET /api/buzzer?state=true
```

**Response:**
```json
{
  "buzzer": true,
  "manualMode": true,
  "message": "Buzzer turned on"
}
```

## Audio Endpoints

### GET /api/buzzer_tone

Plays a tone on the buzzer.

**Parameters:**
- `freq` (optional): Frequency in Hz (default: 1000)
- `duration` (optional): Duration in milliseconds (default: 500)

**Example:**
```
GET /api/buzzer_tone?freq=800&duration=1000
```

**Response:**
```json
{
  "buzzer": true,
  "frequency": 800,
  "duration": 1000,
  "message": "Tone playing"
}
```

### GET /api/beep

Plays a standard beep sound.

**Response:**
```json
{
  "buzzer": true,
  "message": "Beep played"
}
```

### GET /api/short_beep

Plays a short beep sound.

**Response:**
```json
{
  "buzzer": true,
  "message": "Short beep played"
}
```

## Diagnostic Endpoints

### GET /api/temperature

Returns current temperature reading.

**Response:**
```json
{
  "temperature": 24.5,
  "raw": 512,
  "calibrated": true,
  "sampleCount": 10
}
```

### GET /api/pid_debug

Returns PID controller debug information.

**Response:**
```json
{
  "setpoint": 35.0,
  "input": 24.5,
  "output": 0.0,
  "kp": 2.0,
  "ki": 5.0,
  "kd": 1.0,
  "windowSize": 30000,
  "onTime": 0,
  "heaterState": false
}
```

### GET /api/pid_params

Returns or updates PID parameters.

**Parameters (optional):**
- `kp`: Proportional gain
- `ki`: Integral gain
- `kd`: Derivative gain
- `window`: Window size in milliseconds

**Example:**
```
GET /api/pid_params?kp=2.5&ki=6.0&kd=1.2
```

**Response:**
```json
{
  "kp": 2.5,
  "ki": 6.0,
  "kd": 1.2,
  "windowSize": 30000,
  "message": "PID parameters updated"
}
```

### GET /api/firmware_info

Returns firmware information.

**Response:**
```json
{
  "build": "Dec 31 2023 12:00:00",
  "version": "1.0.0",
  "features": ["PID", "WiFi", "WebSocket"]
}
```

## File Management Endpoints

### GET /

Returns the main web interface (index.html).

### GET /config.html

Returns the configuration page.

### GET /programs.html

Returns the program editor page.

### GET /calibration.html

Returns the temperature calibration page.

### GET /update.html

Returns the firmware update page.

### GET /controllertuning.html

Returns the PID controller tuning page.

## WebSocket API

The breadmaker also supports WebSocket connections for real-time updates.

**WebSocket URL:**
```
ws://YOUR_BREADMAKER_IP:81/
```

**Message Format:**
The WebSocket sends JSON messages in the same format as the `/status` endpoint whenever the status changes.

## Error Responses

All endpoints may return error responses in the following format:

```json
{
  "error": "Error message",
  "code": 400
}
```

Common error codes:
- `400`: Bad Request - Invalid parameters
- `404`: Not Found - Endpoint does not exist
- `500`: Internal Server Error - Server error

## Rate Limiting

No rate limiting is currently implemented, but excessive requests may cause performance issues.

## Examples

### Start a program with curl:
```bash
curl -X GET "http://192.168.1.100/select?program=white"
curl -X GET "http://192.168.1.100/start"
```

### Monitor temperature:
```bash
curl -X GET "http://192.168.1.100/api/temperature"
```

### Enable manual mode and turn on motor:
```bash
curl -X GET "http://192.168.1.100/api/manual_mode?state=true"
curl -X GET "http://192.168.1.100/api/motor?state=true"
```

### Play a tone:
```bash
curl -X GET "http://192.168.1.100/api/buzzer_tone?freq=1000&duration=2000"
```

## Integration Notes

### Home Assistant
- Use the `/ha` endpoint for optimal performance
- Implement proper error handling for network timeouts
- Consider using WebSocket for real-time updates

### Node-RED
- Use HTTP request nodes for control
- Implement flow control for status monitoring
- Add error handling for network issues

### Custom Applications
- Implement proper JSON parsing
- Handle network timeouts gracefully
- Use appropriate polling intervals (10-30 seconds recommended)

---

*API Version: 1.0*
*Last Updated: [Date]*
