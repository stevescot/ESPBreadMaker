# `/status` Endpoint: UI Requirements Documentation

This document describes the requirements and expectations for the `/status` endpoint as inferred from the web UI (`script.js` and `index.htm`).

## General
- The `/status` endpoint must return a JSON object representing the current state of the breadmaker controller.
- The response must always be valid JSON and never crash or omit required fields, even in error or idle states.
- All fields must be present and have sensible default values if not running.

## Required Properties (as used by the UI)

| Property                  | Type    | Description |
|---------------------------|---------|-------------|
| `running`                 | bool    | Whether a program is currently running. |
| `program`                 | string  | Name of the currently selected program. |
| `programId`               | int     | Index of the currently selected program (0-based). |
| `stage`                   | string  | Label of the current stage, or "Idle" if not running. |
| `stageIdx`                | int     | Index of the current stage (0-based). |
| `temp`                    | float   | Current measured temperature (°C). |
| `setTemp`                 | float   | Current temperature setpoint (°C). |
| `heater`                  | bool    | Heater state (true/false). |
| `motor`                   | bool    | Motor state (true/false). |
| `light`                   | bool    | Light state (true/false). |
| `buzzer`                  | bool    | Buzzer state (true/false). |
| `manualMode`              | bool    | Manual mode active (true/false). |
| `mixIdx`                  | int     | Index of the current mix step (0-based). |
| `timeLeft`                | int     | Time left in the current stage (seconds). |
| `stageReadyAt`            | int     | Epoch time (seconds) when the current stage will be ready. |
| `programReadyAt`          | int     | Epoch time (seconds) when the program will complete. |
| `startupDelayComplete`    | bool    | Whether the startup delay is complete. |
| `startupDelayRemainingMs` | int     | Remaining startup delay in milliseconds. |
| `fermentationFactor`      | float   | Current fermentation factor. |
| `predictedCompleteTime`   | int     | Predicted program completion time (epoch ms). |
| `initialFermentTemp`      | float   | Initial fermentation temperature (°C). |
| `actualStageStartTimes`   | array   | (Optional) Array of epoch times (seconds) for when each stage actually started. |

## UI Usage Details
- The UI expects all fields above to be present and valid at all times.
- The `programId` is used to select the correct program in dropdowns.
- The `stage` and `stageIdx` are used for progress bars, plan summaries, and direct stage skipping.
- `temp` and `setTemp` are used for display and manual temperature control.
- Device states (`heater`, `motor`, `light`, `buzzer`) are used for icon and status updates.
- `manualMode` disables/enables program control buttons and toggles manual controls.
- `timeLeft`, `stageReadyAt`, and `programReadyAt` are used for countdowns and scheduling displays.
- `startupDelayComplete` and `startupDelayRemainingMs` control the enabling/disabling of the Start button and display of stabilization messages.
- `fermentationFactor`, `predictedCompleteTime`, and `initialFermentTemp` are used for fermentation info panels.
- `actualStageStartTimes` is used for Google Calendar integration and plan summaries.

## Error Handling
- The endpoint must never omit required fields, even if the controller is idle, in error, or in an invalid state.
- If a value is not available, provide a safe default (e.g., 0, false, "Idle", or empty string).
- The UI expects robust handling of all state transitions and will not crash if fields are present and valid.

## Example Minimal Response (Idle)
```json
{
  "running": false,
  "program": "",
  "programId": 0,
  "stage": "Idle",
  "stageIdx": 0,
  "temp": 0.0,
  "setTemp": 0.0,
  "heater": false,
  "motor": false,
  "light": false,
  "buzzer": false,
  "manualMode": false,
  "mixIdx": 0,
  "timeLeft": 0,
  "stageReadyAt": 0,
  "programReadyAt": 0,
  "startupDelayComplete": true,
  "startupDelayRemainingMs": 0,
  "fermentationFactor": 0.0,
  "predictedCompleteTime": 0,
  "initialFermentTemp": 0.0,
  "actualStageStartTimes": []
}
```

## Notes
- The UI does not use a `programList` property in `/status` (it uses `/api/programs` for the list).
- All program and stage selection is index-based and order-preserving.
- The endpoint must be robust to invalid indices and state transitions.
