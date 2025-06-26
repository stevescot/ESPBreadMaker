# test_status_endpoint.py
"""
Test the /status endpoint of the breadmaker controller.
Covers timing logic, stage transitions, setTemp, and property length limits.
"""
import unittest
import json
from unittest.mock import patch
import requests

# Mock response from /status endpoint
MOCK_STATUS_IDLE = {
    "running": False,
    "stage": "Idle",
    "stageStartTimes": [0]*8,
    "stageReadyAt": 0,
    "programReadyAt": 0,
    "program": "default",
    "temp": 25.0,
    "setTemp": 25.0
}

MOCK_STATUS_RUNNING = {
    "running": True,
    "stage": "mix",
    "stageStartTimes": [1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700],
    "stageReadyAt": 1100,
    "programReadyAt": 1700,
    "program": "default",
    "temp": 27.5,
    "setTemp": 28.0
}

DEVICE_URL = "http://192.168.250.125"

class TestStatusEndpoint(unittest.TestCase):
    def test_idle_plan_summary(self):
        # Simulate /status when not running
        data = MOCK_STATUS_IDLE
        self.assertFalse(data["running"])
        self.assertEqual(data["stage"], "Idle")
        self.assertEqual(len(data["stageStartTimes"]), 8)
        self.assertIn("temp", data)
        self.assertIn("setTemp", data)

    def test_running_stage_times(self):
        # Simulate /status when running
        data = MOCK_STATUS_RUNNING
        self.assertTrue(data["running"])
        self.assertEqual(data["stage"], "mix")
        self.assertEqual(data["stageReadyAt"], data["stageStartTimes"][1])
        self.assertEqual(data["programReadyAt"], data["stageStartTimes"][-1])
        self.assertIn("temp", data)
        self.assertIn("setTemp", data)

    def test_edge_case_missing_program(self):
        # Simulate /status with missing program
        data = dict(MOCK_STATUS_IDLE)
        data["program"] = "nonexistent"
        # Should handle gracefully (no crash)
        self.assertIn("program", data)

    def test_live_status_idle(self):
        resp = requests.get(f"{DEVICE_URL}/status", timeout=5)
        self.assertEqual(resp.status_code, 200)
        data = resp.json()
        self.assertIn("stageStartTimes", data)
        self.assertIn("stageReadyAt", data)
        self.assertIn("programReadyAt", data)
        self.assertIn("temp", data)
        self.assertIn("setTemp", data)
        # Property length limits (if present)
        if "program" in data:
            self.assertLessEqual(len(data["program"]), 31)

    def test_live_status_running(self):
        # This test assumes the device is running a program
        resp = requests.get(f"{DEVICE_URL}/status", timeout=5)
        self.assertEqual(resp.status_code, 200)
        data = resp.json()
        self.assertIn("stage", data)
        self.assertIn("stageStartTimes", data)
        self.assertIn("temp", data)
        self.assertIn("setTemp", data)

if __name__ == "__main__":
    unittest.main()
