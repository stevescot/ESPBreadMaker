# `/status` Endpoint: Streaming JSON Fields Documentation

This document describes all fields now included in the `/status` endpoint response, as implemented with the streaming method in the firmware.

## Overview
- The `/status` endpoint returns a JSON object with detailed real-time state and progress information for the breadmaker controller.
- The response is generated using a streaming method for maximum RAM efficiency and robustness.
- All fields are always present, with safe defaults if not running or if a value is unavailable.

## Fields
| Property                  | Type    | Description |
|---------------------------|---------|-------------|
| `scheduledStart`          | int     | Scheduled program start time (epoch seconds, 0 if not scheduled) |
| `running`                 | bool    | Whether a program is currently running |
| `program`                 | string  | Name of the currently selected program |
| `programId`               | int     | Index of the currently selected program (0-based) |
| `stage`                   | string  | Label of the current stage, or "Idle" if not running |
| `stageStartTimes`         | array   | Array of planned start times (epoch seconds) for each stage, based on program definition and start time |
| `programStart`            | int     | Actual program start time (epoch seconds) |
| `elapsed`                 | int     | Elapsed time in the current stage (seconds) |
| `setTemp`                 | float   | Current temperature setpoint (°C) |
| `timeLeft`                | int     | Time left in the current stage (seconds) |
| `stageReadyAt`            | int     | Epoch time (seconds) when the current stage will be ready |
| `programReadyAt`          | int     | Epoch time (seconds) when the program will complete |
| `temp`                    | float   | Current measured temperature (°C) |
| `motor`                   | bool    | Motor state |
| `light`                   | bool    | Light state |
| `buzzer`                  | bool    | Buzzer state |
| `stageStartTime`          | int     | Milliseconds since boot when the current stage started |
| `manualMode`              | bool    | Manual mode active |
| `startupDelayComplete`    | bool    | Whether the startup delay is complete |
| `startupDelayRemainingMs` | int     | Remaining startup delay in milliseconds |
| `actualStageStartTimes`   | array   | Array of actual start times (epoch seconds) for each stage (length 20, 0 if not started) |
| `stageIdx`                | int     | Index of the current stage (0-based) |
| `mixIdx`                  | int     | Index of the current mix step (0-based) |
| `heater`                  | bool    | Heater state |
| `buzzer`                  | bool    | Buzzer state |
| `fermentationFactor`      | float   | Current fermentation factor |
| `predictedCompleteTime`   | int     | Predicted program completion time (epoch ms) |
| `initialFermentTemp`      | float   | Initial fermentation temperature (°C) |

## Notes
- All time fields are in seconds since the Unix epoch unless otherwise noted.
- Arrays such as `stageStartTimes` and `actualStageStartTimes` are always present; unused entries are 0.
- The endpoint is robust to all state transitions and never omits required fields.
- The UI uses these fields for progress bars, countdowns, plan summaries, and robust state display.
- The streaming method ensures low RAM usage and fast response, even with large programs.
