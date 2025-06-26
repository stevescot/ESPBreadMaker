# run_all_tests.py
"""
Script to run all breadmaker controller tests against the device at 192.168.250.125.
Captures output for Copilot agent review.
Run this script from the root breadmaker_controller directory.
"""
import subprocess
import sys
import os

TEST_DIR = os.path.join(os.path.dirname(__file__))
TEST_FILES = [
    os.path.join(TEST_DIR, 'test_status_endpoint.py'),
    os.path.join(TEST_DIR, 'test_programs_format.py'),
    os.path.join(TEST_DIR, 'test_full_cycle.py')
]
JS_TEST_FILES = [
    os.path.join(TEST_DIR, 'test_ui_sync.js'),
    os.path.join(TEST_DIR, 'test_navigation.js')
]

def run_python_tests():
    for test in TEST_FILES:
        print(f"\n=== Running {os.path.basename(test)} ===")
        result = subprocess.run([sys.executable, test], capture_output=True, text=True)
        print(result.stdout)
        if result.stderr:
            print(result.stderr)

def run_js_tests():
    try:
        import shutil
        if not shutil.which('node'):
            print("Node.js is not installed. Skipping JS tests.")
            return
    except ImportError:
        print("shutil not available. Skipping JS tests.")
        return
    for test in JS_TEST_FILES:
        print(f"\n=== Running {os.path.basename(test)} ===")
        result = subprocess.run(['node', test], capture_output=True, text=True)
        print(result.stdout)
        if result.stderr:
            print(result.stderr)

def main():
    print("Running Python tests...")
    run_python_tests()
    print("\nRunning JavaScript tests...")
    run_js_tests()

if __name__ == "__main__":
    main()
