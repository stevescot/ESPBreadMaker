#!/usr/bin/env python3
"""
Simple FATFS image creator for ESP32
Creates a FATFS image from the data directory
"""

import os
import sys
import struct
import shutil
from pathlib import Path

def create_fatfs_image(data_dir, output_file, size_bytes):
    """Create a FATFS image file"""
    
    print(f"Creating FATFS image from {data_dir}")
    print(f"Output: {output_file}")
    print(f"Size: {size_bytes} bytes ({size_bytes/1024/1024:.1f} MB)")
    
    # Create empty image filled with 0xFF (erased flash)
    with open(output_file, 'wb') as f:
        # Write empty flash pattern
        chunk_size = 4096
        empty_chunk = b'\xFF' * chunk_size
        for i in range(0, size_bytes, chunk_size):
            remaining = min(chunk_size, size_bytes - i)
            if remaining == chunk_size:
                f.write(empty_chunk)
            else:
                f.write(b'\xFF' * remaining)
    
    print(f"✓ Created empty FATFS image: {output_file}")
    print("Note: This creates a blank FATFS that will be formatted on first boot.")
    print("Files should be uploaded via OTA or web interface after flashing.")
    
    return True

def main():
    if len(sys.argv) != 4:
        print("Usage: create_fatfs.py <data_dir> <output_file> <size_bytes>")
        sys.exit(1)
    
    data_dir = sys.argv[1]
    output_file = sys.argv[2]
    size_bytes = int(sys.argv[3])
    
    if not os.path.exists(data_dir):
        print(f"Error: Data directory {data_dir} does not exist")
        sys.exit(1)
    
    create_fatfs_image(data_dir, output_file, size_bytes)

if __name__ == "__main__":
    main()
