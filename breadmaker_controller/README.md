# Breadmaker Controller (ESP8266)

A versatile kitchen appliance controller using an ESP8266 microcontroller. Features a modern web UI, program editor, OTA firmware update, temperature calibration, robust manual mode, and Home Assistant integration. Supports bread making, fermentation, sous vide, and various utility functions.

## Features
- Controls motor, heater, light, and buzzer (PWM, ~1V logic)
- Web UI: live status, SVG icons, program editor, stage progress, manual mode
- Multi-stage temperature control with PID
- Precise mixing patterns with customizable durations
- Manual mode: direct output toggles and manual temperature setpoint (PID)
- Persistent storage (LittleFS)
- OTA firmware update via web page
- Multi-point RTD temperature calibration
- WiFi setup via built-in captive portal (no WiFiManager)
- Temperature sensor noise suppression with startup delay
- Accurate timing with actual vs. estimated stage start times
- Google Calendar integration for stage reminders
- Home Assistant integration via REST sensors and automations

## Setup Stages
1. **Hardware Wiring & Connection**
   - **ESP8266 Board:** NodeMCU, Wemos D1 Mini, or similar.
   - **Breadmaker Board Connections:**
     - **Motor:** Connect to a digital output pin (e.g., D1/GPIO5) **with a 1k resistor in series**
     - **Heater:** Connect to a digital output pin (e.g., D2/GPIO4) **with a 1k resistor in series**
     - **Light:** Connect to a digital output pin (e.g., D6/GPIO12) **with a 1k resistor in series**
     - **Buzzer:** Connect to a digital output pin (e.g., D7/GPIO13) **with a 1k resistor in series**
     - **Temperature Sensor (RTD/Analog):** Connect to A0 (ADC input). **Add a 10k pull-down resistor from the sensor signal to ground** (typical for voltage divider circuits; adjust as needed for your sensor type).
   - **Power:** Use 5V USB or regulated supply. Ensure breadmaker control lines are opto-isolated or logic-level compatible.
   - **Pin assignments** can be changed in `outputs_manager.cpp` if needed.

2. **Firmware & Web UI Upload**
   - Install Arduino IDE and ESP8266 board support
   - Install required libraries:
     - ESPAsyncWebServer
     - ESPAsyncTCP
     - ESPAsyncHTTPUpdateServer
     - ArduinoJson
     - PID_v1
     - LittleFS
   - Upload the `data/` folder to LittleFS using the Arduino LittleFS Data Upload tool or provided scripts
   - Build and upload the firmware using provided scripts (`build_and_upload.ps1`, `.bat`) or Arduino IDE

3. **First Boot & WiFi Setup**
   - Power on the device. If no WiFi is configured, a dark-themed captive portal will appear for setup
   - Connect to the device's WiFi AP, open the captive portal, and enter your WiFi credentials
   - After connecting to your network, access the web UI at the device's IP address

4. **Web UI & Program Selection**
   - Use the web UI to select or edit programs, monitor status, and control outputs
   - Manual mode allows direct toggling of outputs and manual temperature setpoint
   - All program selection is by index (position in list), not by name

5. **Home Assistant Integration**
   - Use the provided YAML examples in `Home Assistant/` for:
     - REST sensors to monitor status, temperature, stage, and outputs
     - Automations for notifications, reminders, and control
     - Dashboard cards for live status and control
   - Example REST sensor:
     ```yaml
     sensor:
       - platform: rest
         name: Breadmaker Status
         resource: http://<breadmaker_ip>/api/status
         value_template: '{{ value_json.stage }}'
         json_attributes:
           - temp
           - setTemp
           - program
           - stage
           - running
           - manualMode
     ```
   - See `Home Assistant/breadmaker_automations.yaml` and `dashboard.yaml` for more.

## Capabilities
### Bread Making
- Sourdough programs with autolyse and stretch-and-fold reminders
- Classic bread programs with precise timing
- Specialized dough programs (pizza, enriched, whole wheat, rye)
- In-machine and external baking options

### Fermentation & Dairy
- Yogurt and kefir making
- Soft cheese production
- Temperature-controlled fermentation

### Desserts & Confectionery
- Chocolate tempering
- Caramel making with precise temperature control
- Consistent results for candy making

### Utility Functions
- Sous vide cooking
- Herb drying
- Container sanitization
- Proof box functionality
- Beeswax melting
- Hot towel warming
- DIY cosmetics preparation
- Butter melting and holding

## Hardware
- ESP8266 (NodeMCU, Wemos D1 Mini, etc.)
- Breadmaker with accessible control lines for motor, heater, light, buzzer
- RTD or analog temperature sensor (wired to A0)

## Recent Changes (2025)
- **Manual Mode:** Added direct control of outputs (heater, motor, light, buzzer) and manual temperature setpoint via new API endpoints and web UI. Manual mode disables program controls and allows real-time output toggling.
- **API Refactor:** All control and status endpoints now use `/api/` prefix. Status JSONs only include a program list (not full definitions) for efficiency.
- **UI/UX Overhaul:**
  - Main UI: Manual mode toggle, output controls, program dropdown, robust status polling, device icon hooks.
  - Update page: Shows firmware build time, upload progress, and rebooting status after OTA update.
  - All web UIs use a modern dark theme and improved event handling.
- **Build & Upload Workflow:**
  - Scripts for building and uploading both firmware and web files (LittleFS image) are provided (`build_and_upload.ps1`, `.bat`, `upload_files.ps1`).
  - Web UI files are uploaded to ESP8266 LittleFS for serving.
- **Firmware Improvements:**
  - Modularized code for maintainability and robustness.
  - Pin initialization for all outputs in `outputs_manager.cpp`.
  - OTA update endpoint and troubleshooting guidance.
  - Firmware build time available via `/api/firmware_info`.
- **Program Handling:**
  - Program selection by index (recommended) or name (legacy support).
  - Custom stages only; classic stage logic removed.
  - Resume state robustly saved and restored.
- **Testing & Debugging:**
  - Python and JS test coverage for backend and UI.
  - Hardware troubleshooting and best practices documented.

## Latest Updates (July 2025)
- **Startup Temperature Sensor Stabilization:** Added 15-second delay after power-on and resume to prevent temperature sensor noise from affecting program startup. UI displays startup delay status and disables Start button until stabilization is complete.
- **Enhanced Calendar Link Accuracy:** Calendar links now use actual stage start times for completed stages and dynamically updated estimates for future stages. When stages are manually advanced, future calendar events automatically reflect realistic updated timing.
- **Improved Timing Synchronization:** Fixed stage and program ready time calculations to use actual elapsed time rather than planned durations, ensuring UI displays remain accurate after manual stage advancement.
- **Robust Stage Progression:** Enhanced debug output and timing logic to ensure stage advancement works correctly with proper timing calculations and state management.
- **Mix Pattern Logic Improvements:** Fixed firmware mix pattern repetition to cycle correctly through all patterns for the full stage duration, not just execute once.
- **Auto-Tune UI Modularization:** Split PID auto-tuning JavaScript into separate modular files for better maintainability and error handling.
- **Program Management Enhancements:** Improved "Save All" functionality with timeout protection, payload size validation, and enhanced user feedback.
- **Status JSON Expansion:** Added `actualStageStartTimes` array to status JSON to track when each stage actually began, enabling more accurate calendar events and timing displays.

## Major Changes (2025-06)
- **Manual Mode:** Direct output control and manual temperature setpoint (PID) via web UI and API.
- **API & Status:** `/api/status` and related endpoints provide concise, robust status and program lists.
- **UI Improvements:**
  - Manual mode toggle and output controls
  - Program dropdown and robust status polling
  - Firmware build time and upload/rebooting status on update page
- **Build/Upload:** Scripts for firmware and web files (LittleFS) included; see build_and_upload.ps1 and upload_files.ps1.
- **Persistent Program Selection:** Last selected program saved and restored on boot.
- **Custom Stages Only:** All programs use custom stages for maximum flexibility.
- **Error Handling:** Robust to missing/default program names and invalid states.

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
3. **Upload `data/` folder** to LittleFS using the Arduino LittleFS Data Upload tool or provided scripts
4. **Build and upload** the firmware using provided scripts or Arduino IDE
5. **Power on the device.** If no WiFi is configured, a dark-themed captive portal will appear for setup
6. **Access the web UI** at the device's IP address
7. **Select programs by index**: The web UI now selects programs by their position in the list, not by name. This avoids issues with long or special-character names.
8. **Manual mode:** Use the toggle on the main UI to directly control outputs or set a manual temperature setpoint (PID controlled).

## Program Categories
### Bread Programs
- Multiple sourdough variants (basic, in-machine, pizza)
- Classic white bread
- Whole wheat bread
- Enriched sweet dough
- Rye bread

### Utility Programs
- Sous vide cooking
- Herb drying
- Container sanitization
- Proof box
- Beeswax melting
- Hot towel warming
- DIY cosmetics
- Butter melting

### Fermentation Programs
- Yogurt/kefir making
- Soft cheese making

### Dessert Programs
- Chocolate tempering
- Soft caramel making

## Web Pages
- `/` — Main status and manual/program control
- `/config` — Program editor, calibration, and firmware update (combined)
- `/update` — OTA firmware update (shows build time, upload progress, and reboot status)
- `/calibrate` — RTD calibration (live raw value, multi-point)
- `/programs` — Program editor (advanced, supports JSON import/export)
- `/wifi.html` — WiFi captive portal (auto-appears if not configured, dark theme)

## API Endpoints
- `/api/status` — Current status, mode, outputs, temperature, and program list
- `/api/manual` — Set manual mode and direct output states
- `/api/manual_temp` — Set manual temperature setpoint (PID)
- `/api/programs` — List available programs (names only)
- `/api/select` — Select program by index or name
- `/api/start`, `/api/stop` — Start/stop selected program
- `/api/firmware_info` — Firmware build time and version
- `/api/update` — OTA firmware upload

## Security
- OTA and config pages are open by default. For real-world use, add authentication or restrict access to your local network.

## Notes
- Temperature ranges from 5°C (cold proof) to 230°C (bread baking)
- Mix patterns can be customized with durations from 5 seconds to several hours
- All programs use the custom stages system for maximum flexibility
- Each stage can have specific heating, mixing, lighting, and buzzer requirements
- Programs include detailed ingredient measurements and instructions
- The system supports both time-based and temperature-based stage transitions
- Utility functions make full use of precise temperature control and mixing capabilities
- 15-second startup delay prevents temperature sensor noise from affecting program timing
- Calendar links automatically update when stages are manually advanced for accurate scheduling
- Timing displays use actual elapsed time rather than estimates for precision

## License
MIT
