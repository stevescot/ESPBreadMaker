# Copilot Instructions for Breadmaker Controller

## Project Overview
This project is an ESP8266-based breadmaker controller with a web UI, stage-based breadmaking logic, and persistent storage using LittleFS. It supports program editing, calibration, OTA firmware updates, and live status monitoring.

## Key Files
- `breadmaker_controller.ino`: Main firmware, web server, state machine, and endpoints.
- `programs_manager.cpp/h`: Program loading and management.
- `data/`: Web UI files (HTML, JS, CSS, JSON)
  - `index.html`, `script.js`: Main UI and logic
  - `programs.html`, `programs.js`: Program editor (expects an array of program objects)
  - `calibration.html`: Temperature calibration
  - `update.html`: OTA firmware update
  - `programs.json`: Array of program objects (not an object)

## Data Format
- `programs.json` is an array of objects, each with a `name` and a `customStages` array. **All programs (classic and custom) must use the `customStages` format.**
  ```json
  [
    {
      "name": "Classic White Bread",
      "customStages": [
        {
          "label": "Autolyse",
          "min": 30,
          "temp": 25,
          "mixPattern": [
            { "action": "wait", "durationSec": 1800 }
          ],
          "heater": "off",
          "light": "on",
          "buzzer": "off",
          "instructions": "Let dough rest."
        },
        {
          "label": "Mix",
          "min": 15,
          "temp": 25,
          "mixPattern": [
            { "action": "mix", "durationSec": 60 },
            { "action": "wait", "durationSec": 60 },
            { "action": "mix", "durationSec": 120 },
            { "action": "wait", "durationSec": 30 }
          ],
          "heater": "off",
          "light": "on",
          "buzzer": "off",
          "instructions": "Mix dough as programmed."
        },
        // ...more stages for bulk, knockdown, rise, bake, cool...
      ]
    },
    {
      "name": "Chocolate Tempering",
      "customStages": [
        {
          "label": "Melt",
          "min": 10,
          "temp": 50,
          "mixPattern": [
            { "action": "mix", "durationSec": 12 },
            { "action": "wait", "durationSec": 48 }
          ],
          "heater": "on",
          "light": "off",
          "buzzer": "off",
          "instructions": "Add chocolate. Melt at 50°C with intermittent mixing."
        },
        {
          "label": "Cool",
          "min": 15,
          "temp": 28,
          "mixPattern": [
            { "action": "mix", "durationSec": 60 },
            { "action": "wait", "durationSec": 120 }
          ],
          "heater": "off",
          "light": "off",
          "buzzer": "off",
          "instructions": "Cool to 28°C, stirring occasionally."
        }
      ]
    }
  ]
  ```
- Each program must have:
  - `name`: Unique program name
  - `customStages`: Array of stage objects
- Each `customStage` contains:
  - `label`: Name of the stage
  - `min`: Duration in minutes
  - `temp`: Target temperature (°C)
  - `mixPattern`: Array of `{mixSec, waitSec}` objects (for stages with mixing cycles)
  - `noMix`: `true` if the stage has no mixing at all (preferred over a mixPattern with only waiting)
  - `heater`, `light`, `buzzer`: "on" or "off"
  - `instructions`: Free text for UI display (only for user actions)
- The backend and frontend must use only the `customStages` array to build and execute all programs. Classic fields like `autolyseTime`, `mixTime`, etc. are no longer supported.

## Web Endpoints
- `/status`: Returns JSON with current state, timing, and `stageStartTimes` (epoch seconds for each stage)
- `/api/programs`: GET/POST for program list (array format)
- `/api/calibration`: GET/POST for temperature calibration
- `/start`, `/stop`, `/pause`, `/resume`, `/advance`, `/back`: Control endpoints
- `/update.html`: OTA firmware update page
- `/programs.html`: Program editor page
- `/calibration.html`: Temperature calibration page
- `/index.html`: Main UI

## UI/UX
- All navigation to static pages should use `.html` extensions (e.g., `/programs.html`, `/calibration.html`, `/update.html`)
- The plan summary in the UI must use the backend-provided `stageStartTimes` for all stage start times
- The program editor (`programs.html`/`programs.js`) must treat programs as an array, not an object

## Timing Logic
- All stage start times are calculated and provided by the backend in `stageStartTimes`
- The frontend must not recalculate stage times, but use the provided array
- After advancing or starting, the next stage's start time and "stage ready at" must always match

## Best Practices
- When adding new programs, ensure unique names
- When editing or saving, always POST the full array to `/api/programs`
- For new features, follow the array-based data model and use the provided endpoints

## Troubleshooting
- If times show as 1970, check that the backend is providing correct epoch times and the frontend is using `stageStartTimes`
- If program editing is broken, ensure the JS treats programs as an array
- For navigation issues, check that all links/buttons use the correct `.html` paths

## Serial Debugging

Serial debugging is essential for diagnosing firmware and web server issues on the ESP8266 breadmaker controller. Here are best practices and tips:

- **Web Server Type**: This project uses the `ESPAsyncWebServer` (AsyncWebServer). Do not use synchronous server methods like `handleClient()`—they are not compatible and not needed.
- **Initialization**: Add `Serial.begin(115200);` at the start of `setup()`. Print a message like `[DEBUG] Setup begin`.
- **Milestone Prints**: Print debug messages after each major step in `setup()` (e.g., after WiFi connects, after HTTP server starts):
  - `Serial.println("[DEBUG] WiFi connected");`
  - `Serial.println("[DEBUG] HTTP server started");`
- **Loop Tracing**: In `loop()`, print at the top for tracing. Do not add web server polling or `handleClient()` calls.
- **Stage Transitions**: Print when changing breadmaking stages, starting/stopping programs, or handling user actions:
  - `Serial.printf("[STAGE] Entering stage: %s\n", stageNames[currentStage]);`
  - `Serial.printf("[ACTION] User started program: %s\n", activeProgramName);`
- **User Actions**: Print when handling `/start`, `/stop`, `/pause`, `/resume`, `/advance`, `/back` endpoints.
- **/status Endpoint**: Print when `/status` is served, e.g. `Serial.println("[DEBUG] /status requested");`
- **Error Handling**: Print errors or unexpected conditions with `[ERROR]` prefix.
- **Web Server Issues**: If the device stops responding to HTTP but serial is still active, check if prints after HTTP server startup appear. If not, the web server may be blocking or crashing.
- **Serial Monitoring**: Use a serial terminal (115200 baud) to capture all output. If the last message is before HTTP server startup, the issue is likely in the web server code.
- **Firmware Freeze Checklist**: See the troubleshooting section for steps if the device becomes unresponsive.

**Example Serial Debug Prints:**
```cpp
void setup() {
  Serial.begin(115200);
  Serial.println("[DEBUG] Setup begin");
  // ... WiFi connect ...
  Serial.println("[DEBUG] WiFi connected");
  // ... HTTP server start ...
  Serial.println("[DEBUG] HTTP server started");
}

void loop() {
  Serial.println("[DEBUG] loop()");
  // ...existing code...
}

void startProgram(const char* name) {
  Serial.printf("[ACTION] User started program: %s\n", name);
  // ...existing code...
}

void changeStage(Stage s) {
  Serial.printf("[STAGE] Entering stage: %s\n", stageNames[s]);
  // ...existing code...
}

// In /status handler:
Serial.println("[DEBUG] /status requested");
```

## Hardware Pinout
- **Heater**: D1 (PWM, analogWrite 77 for ON)
- **Motor**: D2 (PWM, analogWrite 77 for ON)
- **RTD (Temperature Sensor)**: A0 (analog input)
- **Light**: D5 (PWM, analogWrite 77 for ON)
- **Buzzer**: D6 (PWM, analogWrite 77 for ON)

## Stage Descriptions
- **Autolyse**: Rest period, no mixing or heating, light ON.
- **Mix**: 
  - First 1 minute: Burst mode (motor ON for 200ms every 1.2s).
  - Then: 2 minutes mixing (motor ON), 30 seconds pause (motor OFF), repeated for the stage duration.
  - Heater OFF, light state unchanged.
- **Bulk**: Bulk fermentation. Heater ON (PID controls to `bulkTemp`), motor OFF.
- **KnockDown**:
  - Same logic as Mix: 1 min burst, then 2 min ON/30s OFF cycles for the stage duration.
  - Heater OFF, light state unchanged.
- **Rise**: Final rise. Heater ON (PID controls to `riseTemp`), motor OFF.
- **Bake1/Bake2**: Baking stages. Heater ON (PID controls to `bake1Temp`/`bake2Temp`), motor OFF.
- **Cool**: Cooling. All outputs OFF.

## Output Mode (Digital/Analog Switch)
- The controller supports two output modes, settable via the web UI or `/api/output_mode`:
  - **Digital (ON/OFF)**: Outputs are set HIGH/LOW (full on/off) for heater, motor, light, buzzer.
  - **Analog (PWM)**: Outputs use PWM (analogWrite 77 for ON, 0 for OFF) for finer control (default mode).
- The current mode is stored in `/settings.json` and can be changed live from the configuration page.
- Changing the mode affects all outputs immediately.

## Consistent Styling
- All web UI pages should use the shared `style.css` for a unified look and feel.
- Use the `.container` class for main content blocks to ensure consistent layout and spacing.
- Buttons should use the `.config-link-btn`, `.back-btn`, or similar classes for appearance and hover effects.
- When adding new pages or UI elements, always:
  - Link to `style.css` in the `<head>`.
  - Use existing CSS classes for layout, buttons, and forms.
  - Test on both desktop and mobile for responsive design.
- This ensures a professional, cohesive user experience across all configuration, status, and editor pages.

## Integration Note
- Always check and update both the backend (`.ino` firmware) and the frontend (`.html`/`.js` files) together:
  - If you change the web UI (HTML/JS), verify the backend endpoints and data formats in `.ino` match and are compatible.
  - If you change the backend endpoints, data format, or timing logic in `.ino`, update the frontend to consume the new format and test the UI.
- This ensures the UI and firmware remain in sync and prevents subtle bugs or broken features.

## Security: Avoid eval and dynamic code execution

- **Do NOT use `eval()` or `new Function()` in any frontend JavaScript.**
- These are blocked by the Content Security Policy (CSP) for security reasons and will cause errors in the browser.
- Refactor any code that tries to use dynamic code execution. All logic should be implemented without `eval` or similar.
- If you use third-party libraries, ensure they do not require `eval()` or relax the CSP.

## Device Icon Coloring

- Device icons (motor, heater, light, buzzer) should visually indicate ON/OFF state by changing color or brightness.
- The UI uses a shared `renderIcons` function in `common.js` to update icon appearance based on device status.
- If icons do not color as expected, ensure `renderIcons` from `common.js` is called from your main UI script, and that the icon HTML/CSS supports color changes.
- See `common.js` and `script.js` for implementation details.

## Coding Order and Declarations
- When editing `.ino` and `.h` files, always ensure that function and variable definitions (or forward declarations) appear before any use.
- For example, if you call `stopBreadmaker()` or `broadcastStatusWS()` in a lambda or function, make sure it is either defined or forward-declared above that point in the file.
- **Always add a forward declaration for any new function (e.g., `void broadcastStatusWS();`) at the top of your `.ino` file, before any code that uses it.**
- This prevents linker and scope errors, especially with C++ lambdas and Arduino sketches.
- The same applies to struct and class definitions: define them before any variable or function that uses them.
- If you add new types or functions, update the header (`.h`) first, then include and use them in the `.ino` or `.cpp` files.

**Summary:**
- Add forward declarations for all new functions at the top of `.ino` files, before any use (including in lambdas or inline functions), to prevent build errors and ensure maintainability.

## User Instructions vs. Automated Actions
- **Instructions** (the `instructions` field in a custom stage, or any user-facing prompt) should only describe actions that require human intervention (e.g., "Add chocolate now", "Remove paddle", "Check dough consistency").
- Any actions that can be performed automatically by the firmware or UI logic (such as turning on/off heater, motor, light, buzzer, or progressing to the next stage) must be handled by the system and should **not** be included in the instructions for the user.
- When designing or editing programs, only include steps in `instructions` that the user must actually perform. All device operations that can be automated should be implemented in code, not as user instructions.
- This ensures the user is only prompted when necessary, and the system remains as automated and user-friendly as possible.

## Temperature Limits and Safety
- **Typical bread machine temperature range:** Most bread machines can safely heat between ~20°C (room temp) and 230°C (bake). However, many models are limited to about 180–200°C for baking, and 40–90°C for fermentation, proofing, or dairy.
- **Recommended limits for custom programs:**
  - **Proofing/Fermentation:** 20–45°C (yeast and lactic cultures)
  - **Yogurt/Cultured Dairy:** 35–45°C
  - **Soft Cheese:** 70–90°C
  - **Baking:** 180–200°C is a safe and typical range for most bread machines. Avoid exceeding 200°C unless you have confirmed your machine supports higher bake temperatures.
  - **Sous Vide/Infusions:** 40–90°C (never boil)
- **Do not exceed 230°C** in any program. Most bread machine heaters and sensors are not designed for higher temperatures and may be damaged or unsafe.
- **If unsure, consult your breadmaker’s manual** for safe temperature ranges for each function.
- **Firmware and UI should validate user input** to prevent setting unsafe temperatures.
- **If a program requests a temperature outside the safe range,** the system should warn the user or refuse to run.

## Status JSON vs. Program Definitions
- The `/status` endpoint (and all status JSON returned by control endpoints or WebSocket pushes) **must only include a `programList` field** (an array of available program names), **not the full program definitions**.
- The full program definitions (all fields, stages, etc.) are only provided by the `/api/programs` endpoint.
- This keeps status updates small and efficient, and prevents memory or JSON truncation issues on the ESP8266.
- If you need to display or edit program details in the UI, fetch them from `/api/programs` and cache as needed.

---
This file is for Copilot and future maintainers. Update as the project evolves.

- When working with Arduino `.ino` files, always declare all global state variables (such as custom stage indices, timers, and flags) at the top of the file, grouped with other state variables, and include a comment describing their purpose. This ensures clarity, maintainability, and prevents accidental shadowing or type mismatches.