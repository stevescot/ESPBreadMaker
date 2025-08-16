#!/usr/bin/env python3
"""
Merge individual program files back into programs.json
This reverses the split_programs.py operation to maintain synchronization
"""

import json
import os
import sys
import glob

def merge_programs_json():
    """Merge individual program files back into a single programs.json"""
    
    # Directory containing individual program files
    programs_dir = "c:/Users/Steve.Aitken/Documents/GitHub/ESPBreadMaker/breadmaker_controller/data/programs"
    
    if not os.path.exists(programs_dir):
        print(f"Error: {programs_dir} not found")
        return False
    
    # Find all program_X.json files
    program_files = glob.glob(os.path.join(programs_dir, "program_*.json"))
    
    if not program_files:
        print("Error: No program_*.json files found")
        return False
    
    programs = []
    
    # Load each individual program file
    for program_file in sorted(program_files):
        try:
            with open(program_file, 'r') as f:
                program = json.load(f)
                programs.append(program)
            print(f"Loaded: {os.path.basename(program_file)}")
        except Exception as e:
            print(f"Error loading {program_file}: {e}")
            return False
    
    # Sort by program ID to ensure correct order
    programs.sort(key=lambda x: x.get('id', 999))
    
    # Write the merged programs.json
    output_file = "c:/Users/Steve.Aitken/Documents/GitHub/ESPBreadMaker/breadmaker_controller/data/programs.json"
    
    try:
        with open(output_file, 'w') as f:
            json.dump(programs, f, indent=2)
        print(f"Created merged file: {output_file}")
        print(f"Merged {len(programs)} programs from individual files")
        return True
    except Exception as e:
        print(f"Error writing {output_file}: {e}")
        return False

if __name__ == "__main__":
    success = merge_programs_json()
    sys.exit(0 if success else 1)
