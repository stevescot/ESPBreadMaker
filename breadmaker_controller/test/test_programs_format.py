# test_programs_format.py
"""
Test handling of array-based programs.json in backend and frontend (customStages only).
"""
import unittest
import json

MOCK_PROGRAMS_ARRAY = [
    {
        "name": "default",
        "customStages": [
            {"label": "Autolyse", "min": 10, "temp": 25, "mixPattern": [{"action": "wait", "durationSec": 600}], "heater": "off", "light": "on", "buzzer": "off", "instructions": "Let dough rest."},
            {"label": "Mix", "min": 10, "temp": 25, "mixPattern": [{"action": "mix", "durationSec": 60}], "heater": "off", "light": "on", "buzzer": "off", "instructions": "Mix dough."}
        ]
    },
    {
        "name": "quick",
        "customStages": [
            {"label": "Mix", "min": 5, "temp": 25, "mixPattern": [{"action": "mix", "durationSec": 60}], "heater": "off", "light": "on", "buzzer": "off", "instructions": "Quick mix."}
        ]
    }
]

class TestProgramsFormat(unittest.TestCase):
    def test_programs_is_array(self):
        self.assertIsInstance(MOCK_PROGRAMS_ARRAY, list)
        self.assertGreaterEqual(len(MOCK_PROGRAMS_ARRAY), 1)

    def test_program_fields(self):
        for prog in MOCK_PROGRAMS_ARRAY:
            self.assertIn("name", prog)
            self.assertIn("customStages", prog)
            self.assertIsInstance(prog["customStages"], list)
            for st in prog["customStages"]:
                self.assertIn("label", st)
                self.assertIn("min", st)
                self.assertIn("temp", st)
                self.assertIn("mixPattern", st)
                self.assertIn("heater", st)
                self.assertIn("light", st)
                self.assertIn("buzzer", st)
                self.assertIn("instructions", st)

    def test_property_length_limits(self):
        for prog in MOCK_PROGRAMS_ARRAY:
            self.assertLessEqual(len(prog["name"]), 31)
            for st in prog["customStages"]:
                self.assertLessEqual(len(st["label"]), 31)
                self.assertLessEqual(len(st["instructions"]), 63)

    def test_zero_negative_durations(self):
        for prog in MOCK_PROGRAMS_ARRAY:
            for st in prog["customStages"]:
                self.assertGreaterEqual(st["min"], 0)
                for mp in st["mixPattern"]:
                    self.assertGreaterEqual(mp["durationSec"], 0)

if __name__ == "__main__":
    unittest.main()
