# ESP32 Over-The-Air (OTA) Upload Guide

## Overview
The ESP32 breadmaker controller supports Over-The-Air (OTA) firmware updates via WiFi. This allows you to upload new firmware without connecting a USB cable.

## Prerequisites
1. **ESP32 Connected to WiFi**: Device must be connected to your WiFi network
2. **OTA Enabled Firmware**: Current firmware must have OTA support (included by default)
3. **Network Access**: Your computer and ESP32 must be on the same network
4. **Python Installed**: Required for espota.py tool (usually included with Arduino)

## Device Information
- **IP Address**: 192.168.250.125
- **Hostname**: esp32-73AB98
- **OTA Port**: 3232 (default)
- **Web Interface**: http://192.168.250.125

## Upload Methods

### Method 1: Quick OTA Upload (Recommended)
```powershell
# Upload firmware only
.\build_and_upload_ota.ps1

# Upload firmware + web files  
.\build_and_upload_ota.ps1 -UploadData

# Upload with OTA password (if set)
.\build_and_upload_ota.ps1 -Password "yourpassword"
```

### Method 2: Manual OTA Upload
```powershell
# Build and upload via OTA
.\unified_build.ps1 -Build -WebOTA

# Build, upload firmware, and upload data files
.\unified_build.ps1 -Build -WebOTA -UploadData
.\build_esp32.ps1 -OTA 192.168.250.125 -UploadData
```

### Method 3: Separate Steps
```powershell
# 1. Compile only
.\build_esp32.ps1

# 2. Upload pre-compiled firmware
.\upload_ota.ps1 -IP 192.168.250.125

# 3. Upload web files (optional)
.\upload_files_sequential.ps1
```

## Arduino IDE OTA Setup

### For Arduino IDE 1.8.x:
1. Make sure ESP32 is running OTA-enabled firmware
2. Go to **Tools → Port**
3. Look for network port: `esp32-73AB98 at 192.168.250.125`
4. Select the network port
5. Upload sketch normally - it will upload over WiFi

### For Arduino IDE 2.x:
1. Same as above, but network ports appear in the port dropdown
2. Look for `esp32-73AB98 (192.168.250.125)` in the port list

## PlatformIO OTA Setup
Add this to your `platformio.ini`:
```ini
upload_protocol = espota
upload_port = 192.168.250.125
; upload_flags = --auth=yourpassword  ; if password is set
```

## Web-Based OTA
1. Open web browser to http://192.168.250.125/ota.html
2. Click "Choose File" and select `.bin` file from `build/esp32.esp32.esp32/`
3. Click "Upload" to start OTA update

## Troubleshooting

### Cannot Find Device
- Check ESP32 is connected to WiFi (same network as computer)
- Verify IP address: `ping 192.168.250.125`
- Check if OTA port is open: `telnet 192.168.250.125 3232`

### Upload Fails
- Ensure ESP32 is not running other operations
- Check available memory (OTA needs ~50% free space)
- Try uploading smaller firmware or clearing data partition
- Reset ESP32 and try again

### espota.py Not Found
- Install/reinstall ESP32 Arduino Core via Arduino IDE
- Check: `%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\`
- Make sure Python is installed and in PATH

### Password Issues
- If password is set on device, you must provide it for OTA
- To disable password: upload firmware with OTA password disabled
- Password is case-sensitive

## File Locations
- **Compiled Firmware**: `build/esp32.esp32.esp32/breadmaker_controller.ino.bin`
- **espota.py**: `%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\*/tools/espota.py`
- **Build Scripts**: `build_esp32.ps1`, `upload_ota.ps1`, `build_and_upload_ota.ps1`

## Security Notes
- OTA is only available when ESP32 is in normal operation mode
- Consider setting an OTA password for security
- OTA is disabled during critical operations (cooking, calibration)
- Always verify uploads completed successfully before using device

## Performance
- **OTA Upload Speed**: ~50-100 KB/s (depends on WiFi quality)
- **Upload Time**: ~30-60 seconds for typical firmware (~1-2MB)
- **Restart Time**: ~10-30 seconds after upload completes

## Next Steps After Upload
1. ESP32 will restart automatically
2. Wait for WiFi reconnection (10-30 seconds)
3. Verify new firmware: check version at http://192.168.250.125/api/ota/info
4. Test functionality before relying on device
