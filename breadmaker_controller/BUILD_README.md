# Build Scripts for ESP32 Breadmaker Controller

This directory contains scripts to build and upload the breadmaker controller firmware and web interface to your ESP32 TTGO T-Display.

## ⚠️ CRITICAL VERSION REQUIREMENTS

**ESP32 Arduino Core 2.0.17 ONLY** - Do not use newer versions (3.x) as they have breaking changes and will cause crashes!

## Files

- `setup_esp32_core.ps1` - **Run this first!** Sets up ESP32 Arduino Core 2.0.17 and removes incompatible versions
- `build_esp32.ps1` - Main build script that compiles and uploads firmware (includes version check)
- `upload_files_esp32.ps1` - Quick script to only upload web files (FFat) without recompiling firmware
- `reset_and_monitor.ps1` - Utility to reset ESP32 and monitor serial output

## Requirements

1. **Arduino CLI** - Install from https://arduino.cc/en/software
2. **ESP32 Arduino Core 2.0.17** - Installed automatically by `setup_esp32_core.ps1`
3. **Python** - For monitoring and utilities

## Usage

### First Time Setup
```powershell
# 1. Setup ESP32 core (run once)
.\setup_esp32_core.ps1

# 2. Build and upload firmware
.\build_esp32.ps1 COM3
```

### Full Build and Upload
```powershell
.\build_esp32.ps1 COM3
```

This will:
1. Check ESP32 Arduino Core version (must be 2.0.17)
2. Compile the firmware 
3. Upload the firmware to ESP32 TTGO T-Display
4. Display memory usage and OTA information

### Upload Only Web Files
If you only changed web files and don't need to recompile firmware:
```powershell
.\upload_files_esp32.ps1 COM3
```

### Monitor Serial Output
```powershell
.\reset_and_monitor.ps1 COM3
```

## Configuration

The scripts are configured for:
- **ESP32 Board:** TTGO T-Display (16MB flash)
- **Board FQBN:** esp32:esp32:esp32
- **Partition Scheme:** app3M_fat9M_16MB (3MB app + 9.9MB FFat)
- **Flash Size:** 16MB 
- **Upload Speed:** 921600 baud
- **File System:** FFat (not SPIFFS or LittleFS)
- **Display:** ST7789 TFT (135x240 pixels)

## Partition Layout

```
Name      Type    SubType   Offset    Size      Purpose
nvs       data    nvs       0x9000    24KB      WiFi credentials, settings
otadata   data    ota       0xf000    8KB       OTA update metadata
app0      app     ota_0     0x10000   1.5MB     Main firmware (slot 0)
app1      app     ota_1     0x190000  1.5MB     OTA firmware (slot 1)
ffat      data    fat       0x310000  9.9MB     Web files, programs, data
```

## Troubleshooting

### "Arduino CLI not found"
- Install Arduino CLI from the official website
- Or add it to your PATH environment variable

### "esptool not found"
- Install Python if not already installed
- Run: `pip install esptool`

### "ESP32 core not found"
- Run `.\setup_esp32_core.ps1` to install the correct ESP32 Arduino Core
- Ensure version 2.0.17 is installed (newer versions will cause crashes)

### "mkfatfs tool not found"
- Install ESP32 Arduino Core via Arduino IDE Board Manager
- Go to Tools > Board > Boards Manager, search for "ESP32" and install version 2.0.17

### "Compilation failed"
- Check that all required libraries are installed:
  - AsyncTCP (ESP32 version)
  - ESPAsyncWebServer
  - ArduinoJson
  - PID
  - LovyanGFX (for display)

### "Upload failed"
- Check COM port number in Device Manager
- Ensure ESP32 is connected and drivers are installed
- Try pressing the BOOT button on ESP32 during upload
- Verify USB cable supports data transfer (not just power)

### "Display issues"
- Ensure LovyanGFX library is configured for ST7789
- Check display rotation settings in display_manager.cpp
- Verify pin assignments match TTGO T-Display

## After Upload

1. Open Serial Monitor (115200 baud) to see boot messages
2. The ESP32 will try to connect to WiFi using saved credentials
3. If no WiFi is configured, it may start an access point
4. Navigate to the ESP32's IP address in a web browser
5. Test the manual mode and device controls

## Web Interface Features

- Manual Mode toggle for direct device control
- Temperature setpoint control in manual mode
- Program selection and execution
- Real-time status display
- Device control icons (heater, motor, light, buzzer)
- Stage progress and timing information
