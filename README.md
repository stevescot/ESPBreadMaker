# Breadmaker Controller (ESP8266)

A drop-in replacement controller for breadmakers, using an ESP8266 microcontroller. Features a modern web UI, program editor, OTA firmware update, and temperature calibration.

## Features
- Controls motor, heater, light, and buzzer (PWM, ~1V logic)
- Web UI: live status, SVG icons, program editor, stage progress
- Dual-stage bake with independent times/temps, PID control
- Manual toggles for outputs
- Persistent storage (LittleFS)
- OTA firmware update via web page
- Multi-point RTD temperature calibration
- WiFi setup via built-in captive portal (no WiFiManager)

## Hardware
- ESP8266 (NodeMCU, Wemos D1 Mini, etc.)
- Breadmaker with accessible control lines for motor, heater, light, buzzer
- RTD or analog temperature sensor (wired to A0)

## Recent Changes (2025)
- **WiFi captive portal**: Now uses `/wifi.html` in LittleFS, with modern dark styling and AJAX feedback. WiFi credentials are stored only in `/wifi.json` (LittleFS), not EEPROM. WiFiManager is no longer used.
- **Stage structure**: The main breadmaking flow now starts with an "Autolyse" stage (replaces old delay). The "starter" stage is removed from the main flow.
- **Program format**: Programs now use `autolyseTime` (min) as the first stage field instead of `delayStart`. All UI and backend logic updated for this.
- **UI consistency**: All web UIs (main, program editor, captive portal) use a dark theme for visual consistency.
- **Program editor**: `/programs.html` and `/programs.js` updated to allow editing/adding programs with the new stage structure. Fields are: autolyseTime, mixTime, bulkTemp, bulkTime, knockDownTime, riseTemp, riseTime, bake1Temp, bake1Time, bake2Temp, bake2Time, coolTime.
- **Backend**: `breadmaker_controller.ino` updated to use `autolyseTime` for the autolyse stage, and to load WiFi credentials from `/wifi.json` via `loadWiFiCreds()`.
- **Sample program**: The default program in `/programs.json` now uses `autolyseTime` instead of `delayStart`.

## Getting Started
1. **Install Arduino IDE** and ESP8266 board support
2. **Install Libraries:**
   - ESPAsyncWebServer
   - ESPAsyncTCP
   - ESPAsyncHTTPUpdateServer
   - ArduinoJson
   - PID_v1
   - LittleFS
3. **Upload `data/` folder** to LittleFS using the Arduino LittleFS Data Upload tool
4. **Build and upload** the firmware
5. **Power on the device.** If no WiFi is configured, a dark-themed captive portal will appear for setup
6. **Access the web UI** at the device's IP address

## Web Pages
- `/` — Main status and control
- `/config` — Program editor, calibration, firmware update
- `/update` — OTA firmware update
- `/calibrate` — RTD calibration
- `/programs` — Program editor
- `/wifi.html` — WiFi captive portal (auto-appears if not configured)

## Security
- OTA and config pages are open by default. For real-world use, add authentication.

## Notes
- For sourdough, use the "Autolyse" stage for the initial rest. Starter management is not part of the main breadmaking flow.
- All configuration and program data is stored in LittleFS.

## License
MIT
