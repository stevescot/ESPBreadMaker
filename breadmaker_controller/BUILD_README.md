# Build Scripts for ESP8266 Breadmaker Controller

This directory contains scripts to build and upload the breadmaker controller firmware and web interface to your ESP8266.

## Files

- `build_and_upload.ps1` - Main PowerShell script that compiles firmware and uploads both ROM and LittleFS
- `build_and_upload.bat` - Batch file version for Windows
- `upload_files.ps1` - Quick script to only upload web files (LittleFS) without recompiling firmware

## Requirements

1. **Arduino CLI** - Install from https://arduino.cc/en/software
2. **ESP8266 Arduino Core** - Install via Arduino IDE Board Manager
3. **esptool** - Install with: `pip install esptool`
4. **Python** - Required for esptool

## Usage

### Full Build and Upload (PowerShell - Recommended)
```powershell
.\build_and_upload.ps1
```

This will:
1. Compile the firmware
2. Create a LittleFS image from the `data` directory
3. Upload the firmware to ESP8266 on COM4
4. Upload the LittleFS image (web files) to ESP8266

### Full Build and Upload (Batch)
```cmd
build_and_upload.bat
```

### Upload Only Web Files
If you only changed web files and don't need to recompile firmware:
```powershell
.\upload_files.ps1
```

### Custom COM Port
```powershell
.\build_and_upload.ps1 -Port "COM3"
```

## Configuration

The scripts are configured for:
- **COM Port:** COM4 (change with `-Port` parameter)
- **Board:** esp8266:esp8266:nodemcuv2 (NodeMCU v2)
- **Baud Rate:** 921600
- **LittleFS Size:** 2MB

## Troubleshooting

### "Arduino CLI not found"
- Install Arduino CLI from the official website
- Or add it to your PATH environment variable

### "esptool not found"
- Install Python if not already installed
- Run: `pip install esptool`

### "mklittlefs tool not found"
- Install ESP8266 Arduino Core via Arduino IDE Board Manager
- Go to Tools > Board > Boards Manager, search for "ESP8266" and install

### "Compilation failed"
- Check that all required libraries are installed:
  - ESPAsyncTCP
  - ESPAsyncWebServer
  - ArduinoJson
  - PID

### "Upload failed"
- Check COM port number in Device Manager
- Ensure ESP8266 is connected and drivers are installed
- Try pressing the RESET button on ESP8266 during upload

## After Upload

1. Open Serial Monitor (115200 baud) to see boot messages
2. The ESP8266 will try to connect to WiFi using saved credentials
3. If no WiFi is configured, it may start an access point
4. Navigate to the ESP8266's IP address in a web browser
5. Test the manual mode and device controls

## Web Interface Features

- Manual Mode toggle for direct device control
- Temperature setpoint control in manual mode
- Program selection and execution
- Real-time status display
- Device control icons (heater, motor, light, buzzer)
- Stage progress and timing information
