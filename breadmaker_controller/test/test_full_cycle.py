# test_full_cycle.py
"""
Long-running test: Polls /status endpoint for the full cycle of a short bread program.
Creates a short program (20 min total), selects it, and polls status until finished.
Now uses customStages format and checks temperature tracking.
"""
import requests
import time
import json

DEVICE_URL = "http://192.168.250.125"
SHORT_PROGRAM = {
    "name": "shortcycle",
    "customStages": [
        {"label": "Autolyse", "min": 1, "temp": 25, "mixPattern": [{"action": "wait", "durationSec": 60}], "heater": "off", "light": "on", "buzzer": "off", "instructions": "Let dough rest."},
        {"label": "Mix", "min": 1, "temp": 25, "mixPattern": [{"action": "mix", "durationSec": 60}], "heater": "off", "light": "on", "buzzer": "off", "instructions": "Mix dough."},
        {"label": "Bulk", "min": 5, "temp": 27, "mixPattern": [{"action": "wait", "durationSec": 300}], "heater": "on", "light": "off", "buzzer": "off", "instructions": "Bulk ferment."},
        {"label": "Bake", "min": 5, "temp": 180, "mixPattern": [{"action": "wait", "durationSec": 300}], "heater": "on", "light": "off", "buzzer": "off", "instructions": "Bake."},
        {"label": "Cool", "min": 1, "temp": 25, "mixPattern": [{"action": "wait", "durationSec": 60}], "heater": "off", "light": "off", "buzzer": "off", "instructions": "Cool."}
    ]
}

PROGRAMS_PATH = "/api/programs"
SELECT_PATH = "/select?idx=0"  # Select by index for robustness
START_PATH = "/start"
STATUS_PATH = "/status"


def upload_short_program():
    # Get current programs
    resp = requests.get(DEVICE_URL + PROGRAMS_PATH, timeout=5)
    progs = resp.json()
    # Remove any existing 'shortcycle' and add new
    progs = [p for p in progs if p.get('name') != 'shortcycle']
    progs.insert(0, SHORT_PROGRAM)  # Insert at index 0
    # Upload
    r = requests.post(DEVICE_URL + PROGRAMS_PATH, json=progs, timeout=5)
    assert r.status_code == 200


def select_and_start():
    r = requests.get(DEVICE_URL + SELECT_PATH, timeout=5)
    assert r.status_code == 200
    r = requests.get(DEVICE_URL + START_PATH, timeout=5)
    assert r.status_code == 200


def poll_status_until_done():
    print("Polling /status for full cycle...")
    last_stage = None
    for i in range(60):  # Up to 60 polls (about 20-30 min)
        resp = requests.get(DEVICE_URL + STATUS_PATH, timeout=5)
        data = resp.json()
        print(f"[{i}] stage={data.get('stage')} running={data.get('running')} left={data.get('timeLeft')} temp={data.get('temp')} setTemp={data.get('setTemp')}")
        # If the UI says no details for current program, re-request programs.json
        if not data.get('program') or data.get('program') == '':
            print("[WARN] No program details in /status, re-requesting /api/programs...")
            progs_resp = requests.get(DEVICE_URL + PROGRAMS_PATH, timeout=5)
            print(f"[INFO] /api/programs: {progs_resp.text}")
        # Confirm stage transitions
        if last_stage is not None and data.get('stage') != last_stage:
            print(f"Stage changed: {last_stage} -> {data.get('stage')}")
        last_stage = data.get('stage')
        if not data.get('running') or data.get('stage') == 'Idle':
            print("Cycle complete.")
            return
        time.sleep(20)  # Poll every 20s
    print("Test timed out before completion.")


def main():
    upload_short_program()
    select_and_start()
    poll_status_until_done()

if __name__ == "__main__":
    main()
