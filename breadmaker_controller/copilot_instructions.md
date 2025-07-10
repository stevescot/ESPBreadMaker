# Copilot Instructions for Breadmaker Controller

## attitude
if you have made changes, recompile to confirm they are succesful
if there are compile errors attempt a fix first, do this a maximum 3 times before asking for confirmation
always add stubs of fuctions at the top of ino files and variables at the top

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

## Web Endpoints
- `/status`: Returns JSON with current state, timing, and `stageStartTimes` (epoch seconds for each stage)
- `/api/programs`: GET for program list (array format)
- `/api/calibration`: GET for temperature calibration
- `/start`, `/stop`, `/pause`, `/resume`, `/advance`, `/back`: Get for Control endpoints
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

**Example Serial Debug Prints:**
```cpp
void setup() {
  Serial.begin(115200);
  Serial.println("[DEBUG] Setup begin");
  // ... WiFi connect ...
  Serial.println("[DEBUG] WiFi connected");
  // ... HTTP server start ...
  Serial.println("[DEBUG] HTTP server started");

## Hardware Pinout
- **Heater**: D1 (PWM, analogWrite 77 for ON)
- **Motor**: D2 (PWM, analogWrite 77 for ON)
- **RTD (Temperature Sensor)**: A0 (analog input)
- **Light**: D5 (PWM, analogWrite 77 for ON)
- **Buzzer**: D6 (PWM, analogWrite 77 for ON)


## Consistent Styling
- All web UI pages should use the shared `style.css` or equivalent for a unified look and feel.
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

## Coding Order and Declarations
- When editing `.ino` and `.h` files, always ensure that function and variable definitions (or forward declarations) appear before any use.
- For example, if you call `stopBreadmaker()` or `broadcastStatusWS()` in a lambda or function, make sure it is either defined or forward-declared above that point in the file.

**Summary:**
- Add forward declarations for all new functions at the top of `.ino` files, before any use (including in lambdas or inline functions), to prevent build errors and ensure maintainability.

## User Instructions vs. Automated Actions
- **Instructions** (the `instructions` field in a custom stage, or any user-facing prompt) should only describe actions that require human intervention (e.g., "Add chocolate now", "Remove paddle", "Check dough consistency").
- Any actions that can be performed automatically by the firmware or UI logic (such as turning on/off heater, motor, light, buzzer, or progressing to the next stage) must be handled by the system and should **not** be included in the instructions for the user.
- When designing or editing programs, only include steps in `instructions` that the user must actually perform. All device operations that can be automated should be implemented in code, not as user instructions.
- This ensures the user is only prompted when necessary, and the system remains as automated and user-friendly as possible.


## Status JSON vs. Program Definitions
- The `/status` endpoint (and all status JSON returned by control endpoints or WebSocket pushes)
- The full program definitions (all fields, stages, etc.) are only provided by the `/api/programs` endpoint.
- This keeps status updates small and efficient, and prevents memory or JSON truncation issues on the ESP8266.
- If you need to display or edit program details in the UI, fetch them from `/api/programs` and cache as needed.

---
This file is for Copilot and future maintainers. Update as the project evolves.
