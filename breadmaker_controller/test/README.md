# Breadmaker Controller Test Plan

## Overview
This folder contains test scaffolding and Copilot instructions for testing the breadmaker controller firmware and web UI. It is designed to help maintainers and contributors verify timing logic, UI synchronization, and robustness against recent and historical issues.

## Test Structure
- `test_status_endpoint.py`: Tests backend `/status` endpoint logic, including stage timing, plan summary, and edge cases. **Now also checks for `setTemp` and property length limits.**
- `test_programs_format.py`: Verifies correct handling of array-based `programs.json` (only `customStages` allowed) in both backend and frontend.
- `test_ui_sync.js`: Simulates frontend polling and checks for correct plan summary, navigation, and temperature chart modal logic.
- `test_navigation.js`: Programmatically tests Next/Back button logic, including repeated forward/backward transitions and boundary conditions. **Also tests program selection by index.**
- `test_full_cycle.py`: Long-running test that uploads a short (20 min) program, starts it, and polls `/status` for the full breadmaking cycle, including temperature tracking.
- `README.md`: This file. Describes test goals, recent issues, and Copilot instructions.

## Recent Issues and Test Focus
- **Timing/Stage Sync**: Ensure `/status` always returns correct `stageStartTimes`, `stageReadyAt`, and `programReadyAt` for both running and idle states. Test for NTP loss, millis() overflow, and program changes.
- **Temperature Reporting**: Confirm `/status` includes both `temp` (current) and `setTemp` (setpoint), and that the UI displays both and the temperature chart modal works.
- **UI Flicker/Navigation**: Confirm frontend never shows the calendar icon for the active stage and that all navigation uses `.html` links.
- **Program Editor**: Validate add/edit/delete/save for array-based `programs.json` (no classic fields allowed).
- **Freeze/Blocking**: Simulate backend stalls (e.g., file/network) and verify UI/HTTP timeout handling.
- **Edge Cases**: Test with missing/invalid programs, zero/negative durations, and rapid stage changes. **Test property length limits for program name, stage label, and instructions.**
- **Navigation Consistency**: Repeatedly advancing and reversing stages should always yield consistent, correct stage names and never go out of bounds.
- **Debug Serial Config**: Test that the config UI can toggle debug serial output and that the backend honors the setting.
- **Mixing Safety**: For sous vide and similar programs, verify that mixing is off or safe for fragile items (e.g., eggs).

## Copilot Instructions
- When writing or updating tests, focus on:
  - Verifying JSON structure and timing values from `/status`, including `setTemp`.
  - Simulating both running and idle states.
  - Checking for UI/UX regressions (navigation, icons, plan summary, temperature chart modal).
  - Reproducing and documenting any freeze or blocking scenarios.
  - Testing property length limits and program selection by index.
  - Verifying debug serial config can be toggled from the UI and is respected by the backend.
  - Ensuring mixing logic is safe for all program types.
- If a new issue is found, add a minimal test case and update this README with a description and troubleshooting notes.

## Integration
- Tests should be runnable on a host PC (mocking HTTP endpoints and file reads) or on-device (if possible).
- Use Python (for backend logic) and JavaScript (for UI logic) as appropriate.

## Running Tests
To run all tests and capture output, open a terminal in the `breadmaker_controller` root directory and run:
```shell
python test/run_all_tests.py
```
Or, to run just the long-running full cycle test:
```shell
python test/test_full_cycle.py
```
This will execute all Python and JavaScript tests and print results to the terminal for Copilot agent review.

The Python tests will query the device at `192.168.250.125` for live `/status` endpoint checks.

Ensure Node.js is installed to run the JavaScript tests.

If you add new tests, update `run_all_tests.py` and this README.

---

_Last updated: 2025-06-26_
