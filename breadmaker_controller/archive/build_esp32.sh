#!/bin/bash
# ESP32 TTGO T-Display Build Script
# This script compiles and uploads the breadmaker controller to ESP32

echo "Building ESP32 TTGO T-Display Breadmaker Controller..."

# Set board type
BOARD="esp32:esp32:esp32:PartitionScheme=default,CPUFreq=240,FlashMode=qio,FlashFreq=80,FlashSize=4M,UploadSpeed=921600"

# Set libraries path (adjust as needed)
LIBRARIES_PATH="--libraries ~/Arduino/libraries"

# TFT_eSPI configuration
echo "Note: Make sure TFT_eSPI is configured for TTGO T-Display"
echo "  - Copy TFT_eSPI_Setup.h to your TFT_eSPI library folder"
echo "  - Or configure User_Setup.h in the TFT_eSPI library"

# Compile
echo "Compiling..."
arduino-cli compile --fqbn $BOARD $LIBRARIES_PATH breadmaker_controller.ino

if [ $? -eq 0 ]; then
    echo "Compilation successful!"
    
    # Upload if port is provided
    if [ ! -z "$1" ]; then
        echo "Uploading to port $1..."
        arduino-cli upload --fqbn $BOARD --port $1 breadmaker_controller.ino
        
        if [ $? -eq 0 ]; then
            echo "Upload successful!"
        else
            echo "Upload failed!"
            exit 1
        fi
    else
        echo "No port specified. Skipping upload."
        echo "Usage: ./build_esp32.sh [port]"
        echo "Example: ./build_esp32.sh /dev/ttyUSB0"
    fi
else
    echo "Compilation failed!"
    exit 1
fi

echo "Build process completed!"
