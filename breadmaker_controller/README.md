# Breadmaker Controller (ESP32 TTGO T-Display)

A versatile kitchen appliance controller using an ESP32 TTGO T-Display microcontroller with built-in 1.14" color display. Features a modern web UI, program editor, OTA firmware update, temperature calibration, robust manual mode, and Home Assistant integration. Supports bread making, fermentation, sous vide, and various utility functions.

## Features
- Controls motor, heater, light, and buzzer (digital outputs with 1k series resistors)
- Built-in 1.14" color display (ST7789 controller) with LovyanGFX graphics
- Web UI: live status, SVG icons, program editor, stage progress, manual mode
- Multi-stage temperature control with PID
- Precise mixing patterns with customizable durations
- Manual mode: direct output toggles and manual temperature setpoint (PID)
- Persistent storage (FFat filesystem)
- OTA firmware update via web page
- Multi-point RTD temperature calibration
- WiFi setup via WiFiManager captive portal
- Temperature sensor noise suppression with startup delay
- Accurate timing with actual vs. estimated stage start times
- Google Calendar integration for stage reminders
- Home Assistant integration via REST sensors and automations

> **Note:** This project has been migrated from ESP8266 to ESP32 TTGO T-Display for improved performance, memory capacity, and built-in display functionality. The ESP32 version uses the standard WebServer library instead of AsyncWebServer for better stability.

## Setup Stages
1. **Hardware Wiring & Connection**
   - **ESP32 TTGO T-Display:** Built-in 1.14" color display with USB-C programming.
   - **Breadmaker Board Connections:**
     - **Motor:** Connect to a digital output pin **with a 1k resistor in series**
     - **Heater:** Connect to a digital output pin **with a 1k resistor in series**
     - **Light:** Connect to a digital output pin **with a 1k resistor in series**
     - **Buzzer:** Connect to a digital output pin **with a 1k resistor in series**
     - **Temperature Sensor (RTD/Analog):** Connect to GPIO 34 (ADC1_CH6). **Add a 10k pull-down resistor from the sensor signal to ground** (typical for voltage divider circuits; adjust as needed for your sensor type).
   - **Power:** Use 5V USB-C or regulated supply. Ensure breadmaker control lines are opto-isolated or logic-level compatible.
   - **Pin assignments** can be changed in `outputs_manager.cpp` if needed.

2. **Firmware & Web UI Upload**
   - Install Arduino IDE and ESP32 board support
   - **CRITICAL: Use ESP32 Arduino Core version 2.0.17 ONLY** - newer versions (3.x) have compatibility issues
   - Install required libraries:
     - ArduinoJson (JSON parsing and generation)
     - PID (Brett Beauregard's PID controller library)
     - LovyanGFX (advanced graphics library for TTGO T-Display)
     - WiFiManager (WiFi configuration portal)
   - Upload the `data/` folder to FFat using the Arduino ESP32 Data Upload tool or provided scripts
   - Build and upload the firmware using the unified build script (`unified_build.ps1`) or Arduino IDE

3. **First Boot & WiFi Setup**
   - Power on the device. If no WiFi is configured, WiFiManager will start a captive portal for setup
   - Connect to the device's WiFi AP (usually "ESP32-XXXX"), open a web browser, and enter your WiFi credentials
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
- ESP32 TTGO T-Display (with built-in 1.14" color display)
- Breadmaker with accessible control lines for motor, heater, light, buzzer
- RTD or analog temperature sensor (wired to analog input pin)

## Recent Changes (2025)
- **Manual Mode:** Added direct control of outputs (heater, motor, light, buzzer) and manual temperature setpoint via new API endpoints and web UI. Manual mode disables program controls and allows real-time output toggling.
- **API Refactor:** All control and status endpoints now use `/api/` prefix. Status JSONs only include a program list (not full definitions) for efficiency.
- **UI/UX Overhaul:**
  - Main UI: Manual mode toggle, output controls, program dropdown, robust status polling, device icon hooks.
  - Update page: Shows firmware build time, upload progress, and rebooting status after OTA update.
  - All web UIs use a modern dark theme and improved event handling.
- **Build & Upload Workflow:**
  - Unified build script for compilation and upload (`unified_build.ps1`) supports both serial and web OTA methods.
  - Web UI files are uploaded to ESP32 FFat for serving.
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
- **Build/Upload:** Unified build script included; see unified_build.ps1 for all build and upload operations.
- **Persistent Program Selection:** Last selected program saved and restored on boot.
- **Custom Stages Only:** All programs use custom stages for maximum flexibility.
- **Error Handling:** Robust to missing/default program names and invalid states.

See the test/README.md for test plan and details.

## Getting Started
1. **Install Arduino IDE** and ESP32 board support
2. **Setup ESP32 Arduino Core version 2.0.17** using the provided setup script:
   ```bash
   .\setup_esp32_core.ps1
   ```
3. **Install Libraries** (or use the build script which will check dependencies):
   - ArduinoJson
   - PID (Brett Beauregard's library)
   - LovyanGFX
   - WiFiManager
4. **Upload `data/` folder** to FFat using the Arduino ESP32 Data Upload tool or provided scripts
5. **Build and upload** the firmware using provided scripts:
   ```bash
   .\unified_build.ps1 -Build -Serial COM3    # Upload via serial
   .\unified_build.ps1 -Build -WebOTA         # Upload via WiFi (recommended)
   ```
6. **Power on the device.** If no WiFi is configured, WiFiManager will start a captive portal for setup
7. **Access the web UI** at the device's IP address
8. **Select programs by index**: The web UI now selects programs by their position in the list, not by name. This avoids issues with long or special-character names.
9. **Manual mode:** Use the toggle on the main UI to directly control outputs or set a manual temperature setpoint (PID controlled).

## Remote Firmware Updates

The ESP32 Breadmaker Controller supports **three methods** for remote firmware updates:

### 1. Web-Based Update (Easiest)
- **Access**: Navigate to `http://[device-ip]/update` in your web browser
- **Method**: Upload `.bin` file directly through web interface
- **Advantages**: No additional tools required, progress indication, works from any device
- **Requirements**: Device must be connected to WiFi and accessible on network

### 2. Command-Line OTA (Developer Friendly)
- **Usage**: `.\unified_build.ps1 -Build -WebOTA`
- **Method**: Uses `espota.py` tool from ESP32 Arduino Core
- **Advantages**: Integrated with build process, automated compilation and upload
- **Requirements**: Python, ESP32 Arduino Core, device on same network

### 3. Arduino IDE Network Port (IDE Integration)
- **Access**: Arduino IDE → Tools → Port → Network port
- **Method**: ArduinoOTA service on port 3232
- **Advantages**: Works directly in Arduino IDE after initial setup
- **Requirements**: Device visible on network, OTA password (default: "breadmaker2024")

**Note**: All OTA methods require the device to be running and connected to WiFi. For first-time setup or recovery, use serial upload via USB.

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
- `/update` — Web-based firmware update (upload .bin files, progress tracking)
- `/ota` — OTA configuration and Arduino IDE setup instructions
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
- `/api/update` — Web-based firmware upload (POST with .bin file)
- `/api/ota/status` — OTA update status and configuration
- `/api/ota/info` — Device information for OTA updates

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
