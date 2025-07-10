# Breadmaker Controller ESP Endpoints and Status Object Documentation

## Endpoints

### `/status` (GET)
- **Description:** Returns the current status of the breadmaker controller.
- **Response:** JSON object with the following properties:
  - See **Status Object Properties** below.

### `/api/programs` (GET)
- **Description:** Returns the list of available programs in original order.
- **Response:** JSON array of program objects (from `programs.json`).

### `/select?program={index}` (GET)
- **Description:** Selects the program at the given integer index.
- **Parameters:**
  - `program` (int): Index of the program to select (0-based).
- **Response:** 200 OK, status updated.

### `/start` (GET)
- **Description:** Starts the currently selected program.
- **Response:** 200 OK, status updated.

### `/stop` (GET)
- **Description:** Stops the current program.
- **Response:** 200 OK, status updated.

### `/advance` (GET)
- **Description:** Advances to the next stage in the current program.
- **Response:** 200 OK, status updated.

### `/back` (GET)
- **Description:** Moves back to the previous stage in the current program. Robust to invalid state.
- **Response:** 200 OK, status updated.

### `/resume` (GET)
- **Description:** Resumes the last saved program state.
- **Response:** 200 OK, status updated.

### `/start_at_stage?stage={index}` (GET)
- **Description:** Starts the current program at the specified stage index.
- **Parameters:**
  - `stage` (int): Index of the stage to start at (0-based).
- **Response:** 200 OK, status updated.

### `/ha` (GET)
- **Description:** Home Assistant-friendly status endpoint. Returns detailed health, filesystem, and temperature control info using real values.
- **Response:** JSON object (see below).

---

## Status Object Properties

Returned by `/status` and `/ha` endpoints. All units and calculation methods are described.

| Property                  | Type    | Units         | Description |
|---------------------------|---------|---------------|-------------|
| `state`                   | string  | n/a           | "on" if running, "off" if idle |
| `stage`                   | string  | n/a           | Current stage label, or "Idle" |
| `program`                 | string  | n/a           | Name of the active program |
| `programId`               | int     | n/a           | Index of the active program (0-based) |
| `temperature`             | float   | °C            | Averaged measured temperature |
| `setpoint`                | float   | °C            | Current temperature setpoint |
| `heater`                  | bool    | n/a           | Heater state (true/false) |
| `motor`                   | bool    | n/a           | Motor state (true/false) |
| `light`                   | bool    | n/a           | Light state (true/false) |
| `buzzer`                  | bool    | n/a           | Buzzer state (true/false) |
| `manual_mode`             | bool    | n/a           | Manual mode active |
| `stageIdx`                | int     | n/a           | Current stage index |
| `mixIdx`                  | int     | n/a           | Current mix index |
| `stage_time_left`         | int     | seconds/ms    | Time left in current stage (0 if not running) |
| `stage_ready_at`          | int     | epoch ms      | Time when current stage will be ready (0 if not running) |
| `program_ready_at`        | int     | epoch ms      | Time when program will complete (0 if not running) |
| `startupDelayComplete`    | bool    | n/a           | Startup delay complete |
| `startupDelayRemainingMs` | int     | ms            | Remaining startup delay in ms |
| `fermentationFactor`      | float   | n/a           | Fermentation factor (calculated) |
| `predictedCompleteTime`   | int     | epoch ms      | Predicted program completion time |
| `initialFermentTemp`      | float   | °C            | Initial fermentation temperature |
| `health`                  | object  | n/a           | See below |

### `health` object
| Property            | Type   | Units      | Description |
|---------------------|--------|------------|-------------|
| `uptime_sec`        | int    | seconds    | Uptime since boot |
| `firmware_version`  | string | n/a        | Firmware build version |
| `build_date`        | string | n/a        | Firmware build date |
| `reset_reason`      | string | n/a        | Last reset reason |
| `chip_id`           | int    | n/a        | ESP chip ID |
| `watchdog_enabled`  | bool   | n/a        | Watchdog enabled |
| `startup_complete`  | bool   | n/a        | Startup delay complete |
| `watchdog_last_feed`| int    | ms         | Time since last watchdog feed |
| `watchdog_timeout_ms`| int   | ms         | Watchdog timeout |
| `memory`            | object | n/a        | See below |
| `performance`       | object | n/a        | See below |
| `network`           | object | n/a        | See below |
| `filesystem`        | object | n/a        | See below |
| `error_counts`      | array  | n/a        | Error counters (reserved) |
| `temperature_control`| object| n/a        | See below |

#### `memory` object
| Property         | Type   | Units   | Description |
|------------------|--------|---------|-------------|
| `free_heap`      | int    | bytes   | Free heap memory |
| `max_free_block` | int    | bytes   | Largest free block |
| `min_free_heap`  | int    | bytes   | Minimum free heap (since boot) |
| `fragmentation`  | float  | %       | Heap fragmentation |

#### `performance` object
| Property           | Type   | Units   | Description |
|--------------------|--------|---------|-------------|
| `cpu_usage`        | int    | %       | CPU usage (placeholder) |
| `avg_loop_time_us` | int    | us      | Average main loop time |
| `max_loop_time_us` | int    | us      | Max main loop time |
| `loop_count`       | int    | n/a     | Main loop count since boot |

#### `network` object
| Property         | Type   | Units   | Description |
|------------------|--------|---------|-------------|
| `connected`      | bool   | n/a     | WiFi connected |
| `rssi`           | int    | dBm     | WiFi signal strength |
| `reconnect_count`| int    | n/a     | WiFi reconnects (reserved) |
| `ip`             | string | n/a     | Local IP address |

#### `filesystem` object
| Property      | Type   | Units   | Description |
|---------------|--------|---------|-------------|
| `usedBytes`   | int    | bytes   | Used filesystem bytes |
| `totalBytes`  | int    | bytes   | Total filesystem bytes |
| `freeBytes`   | int    | bytes   | Free filesystem bytes |
| `utilization` | float  | %       | Filesystem utilization |

#### `temperature_control` object
| Property           | Type   | Units   | Description |
|--------------------|--------|---------|-------------|
| `input`            | float  | °C      | Filtered temperature input |
| `output`           | float  | %       | PID output (duty cycle) |
| `raw_temp`         | float  | °C      | Raw sensor temperature |
| `thermal_runaway`  | bool   | n/a     | Thermal runaway detected |
| `sensor_fault`     | bool   | n/a     | Sensor fault detected |
| `window_elapsed_ms`| int    | ms      | Elapsed time in PID window |
| `window_size_ms`   | int    | ms      | PID window size |
| `on_time_ms`       | int    | ms      | Time heater has been on in window |
| `duty_cycle_percent`| float | %       | Heater duty cycle percent |

---

## Notes
- All program selection and state logic is index-based and robust to invalid state.
- All endpoints return 200 OK on success, with robust error handling for invalid requests.
- The UI must use `/api/programs` for the program list; `/status` does not include `programList`.
- All endpoints are robust to invalid indices and state transitions.
- All units are SI unless otherwise noted.
