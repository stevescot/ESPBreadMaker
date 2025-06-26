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
- **Program selection by index**: The web UI and backend now use a program index (not name) for selection (`/select?idx=N`). This is robust for long names and special characters. Name-based selection is still supported for compatibility, but index is recommended.
- **Google Calendar links for interactions**: The plan summary table now generates Google Calendar links only for stages that require user interaction (e.g., add mix-ins, remove paddle, shape dough). The event time matches when the interaction is needed, and the event links back to the breadmaker's web UI so you can check the current status directly.

## Major Changes (2025-06)
- **Custom Stages Only:** All classic stage logic and variables have been removed from both firmware and UI. Only `customStages` are supported for all programs, navigation, and reporting.
- **Robust Resume State:** Resume state (`/resume.json`) is now saved every minute while running, immediately when a program is started, and when a program is selected. This allows the system to restore the last selected or started program after a power cycle, even if it was not running yet.
- **Endpoints Updated:** `/status` and `/ha` endpoints now report the current custom stage label or "Idle" as appropriate. All navigation and state logic uses only `customStages`.
- **UI Improvements:**
  - Main UI and plan summary are robust to initial load, stopped state, and missing/invalid program names.
  - Plan summary table updates row status for custom stages and hides calendar icons until running.
  - Program editor UI now uses a dropdown for program selection, supports LLM prompt/copy, JSON paste/validate/add, and only shows the selected program.
  - Custom stage cards in the editor use the same color palette as the main UI. Added "Move Up" and "Move Down" for stage reordering.
- **Persistent Program Selection:** The last selected program is saved to `settings.json` and restored on boot.
- **Test Coverage:** Python backend tests (`test_status_endpoint.py`, `test_programs_format.py`) pass. Long-running test (`test_full_cycle.py`) runs and verifies backend response. JavaScript UI tests are present but require Node.js/Jest to run.
- **Error Handling:** System is robust to missing/default program names and invalid states. Resume state is cleared on stop or program end.

See the test/README.md for test plan and details.

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
7. **Select programs by index**: The web UI now selects programs by their position in the list, not by name. This avoids issues with long or special-character names.

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
- **Program selection:** If you have issues selecting a program with a long or special-character name, use the index-based selection (now default in the UI).
- **Google Calendar links:** Calendar links in the plan summary are only shown for stages that require user interaction. The event time matches the required action, and the event links back to the breadmaker's web UI (e.g., `http://breadmaker.local/`).

## License
MIT
