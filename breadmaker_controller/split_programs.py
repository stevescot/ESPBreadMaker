#!/usr/bin/env python3
"""
Split programs.json into individual program files to reduce memory usage
"""

import json
import os
import sys

def split_programs_json():
    """Split programs.json into individual program files"""
    
    # Read the main programs.json file
    programs_file = "c:/Users/Steve.Aitken/Documents/GitHub/ESPBreadMaker/breadmaker_controller/data/programs.json"
    
    if not os.path.exists(programs_file):
        print(f"Error: {programs_file} not found")
        return False
    
    with open(programs_file, 'r') as f:
        programs = json.load(f)
    
    # Create programs directory
    programs_dir = "c:/Users/Steve.Aitken/Documents/GitHub/ESPBreadMaker/breadmaker_controller/data/programs"
    os.makedirs(programs_dir, exist_ok=True)
    
    # Split each program into its own file
    for program in programs:
        program_id = program['id']
        program_file = os.path.join(programs_dir, f"program_{program_id}.json")
        
        with open(program_file, 'w') as f:
            json.dump(program, f, indent=2)
        
        print(f"Created: {program_file}")
    
    # Create a lightweight index file with just metadata
    metadata = []
    for program in programs:
        meta = {
            'id': program['id'],
            'name': program['name'],
            'notes': program.get('notes', ''),
            'icon': program.get('icon', ''),
            'fermentBaselineTemp': program.get('fermentBaselineTemp', 20.0),
            'fermentQ10': program.get('fermentQ10', 2.0)
        }
        metadata.append(meta)
    
    index_file = "c:/Users/Steve.Aitken/Documents/GitHub/ESPBreadMaker/breadmaker_controller/data/programs_index.json"
    with open(index_file, 'w') as f:
        json.dump(metadata, f, indent=2)
    
    print(f"Created metadata index: {index_file}")
    print(f"Split {len(programs)} programs into individual files")
    
    return True

if __name__ == "__main__":
    success = split_programs_json()
    sys.exit(0 if success else 1)
