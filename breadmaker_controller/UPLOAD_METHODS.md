# ESP32 Firmware Upload Methods

This document describes the reliable upload methods for the ESP32 Breadmaker Controller.

## Recent Fixes (August 2025)

### Timezone Fix
- **Issue**: Scheduled start times were off by 1 hour (22:00 input showed as 23:00)
- **Cause**: ESP32 was using UTC time instead of local timezone
- **Fix**: Added proper timezone configuration in `breadmaker_controller.ino`:
  ```cpp
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  ```
- **Result**: Scheduled times now work correctly in local timezone

### Common Timezone Settings
If you're in a different timezone, update the `setenv("TZ", ...)` line:
- **UK (GMT/BST)**: `"GMT0BST,M3.5.0/1,M10.5.0"`
- **US Eastern**: `"EST5EDT,M3.2.0,M11.1.0"`
- **US Pacific**: `"PST8PDT,M3.2.0,M11.1.0"`
- **Australia (AEST)**: `"AEST-10AEDT,M10.1.0,M4.1.0/3"`

## Updated Scripts (August 2025)

The following scripts have been updated to use only proven, reliable upload methods:

### 1. `upload_ota.ps1` - Primary Firmware Upload
- **Method**: Direct curl HTTP POST with multipart form data
- **Proven**: ✅ Works reliably with large firmware files
- **Usage**: `.\upload_ota.ps1 -IP 192.168.250.125`
- **Requirements**: curl (available in Windows 10+ by default)

```powershell
# Upload firmware only
.\upload_ota.ps1 -IP 192.168.250.125

# Show help
.\upload_ota.ps1 -Help
```

### 2. `build_esp32.ps1` - Build Firmware
- **Updated**: Now uses `--build-path "build"` to ensure binary is created in predictable location
- **Output**: `build\breadmaker_controller.ino.bin`
- **Usage**: `.\build_esp32.ps1`

### 3. `build_and_upload_ota.ps1` - Combined Build and Upload
- **Updated**: Uses `build_esp32.ps1` + `upload_ota.ps1` sequence
- **Reliable**: Separates build and upload for better error handling
- **Usage**: `.\build_and_upload_ota.ps1`

## Successful Upload Command

The working curl command that was tested and proven:

```bash
curl -v -X POST -F "file=@build\breadmaker_controller.ino.bin;filename=firmware.bin" --max-time 300 --connect-timeout 30 "http://192.168.250.125/api/update"
```

**Response on success:**
```
Update successful! Rebooting...
```

## Removed/Deprecated Methods

The following methods were removed due to reliability issues:

- ❌ **PowerShell Invoke-WebRequest**: Failed with large firmware files
- ❌ **Arduino CLI OTA upload**: Inconsistent with network uploads  
- ❌ **--retry flag**: Caused issues with some curl versions

### Deprecated Scripts (Still Present)

The following scripts contain old upload methods and should **NOT** be used:

- `unified_build.ps1` - Contains PowerShell Invoke-WebRequest method
- `unified_build_new.ps1` - Contains PowerShell Invoke-WebRequest method  
- `test_powershell_upload.ps1` - Test script with failing method

**Use these instead:**
- ✅ `build_esp32.ps1` + `upload_ota.ps1`
- ✅ `build_and_upload_ota.ps1`

## Troubleshooting

### If upload fails:

1. **Check device connectivity:**
   ```powershell
   curl -X GET "http://192.168.250.125/api/firmware_info"
   ```

2. **Verify build output exists:**
   ```powershell
   Test-Path "build\breadmaker_controller.ino.bin"
   ```

3. **Clean build if needed:**
   ```powershell
   Remove-Item -Path "build" -Recurse -Force -ErrorAction SilentlyContinue
   .\build_esp32.ps1
   ```

4. **Check curl is available:**
   ```powershell
   Get-Command curl
   ```

### Upload Process

1. **Build firmware:** `.\build_esp32.ps1`
2. **Verify binary:** Check `build\breadmaker_controller.ino.bin` exists
3. **Upload firmware:** `.\upload_ota.ps1 -IP 192.168.250.125`
4. **Wait for reboot:** ~15 seconds
5. **Verify update:** Check `/api/firmware_info` for new build timestamp

### File Size Limits

- **Maximum firmware size**: ~3MB (OTA partition limit)
- **Typical size**: ~1.2MB (37% of available space)
- **Upload timeout**: 300 seconds (5 minutes)

## Network Requirements

- ESP32 must be connected to WiFi
- Web server must be running on port 80
- `/api/update` endpoint must be accessible
- Firewall must allow HTTP traffic

## Success Indicators

- Curl exits with code 0
- Response contains "Update successful! Rebooting..."
- Device restarts within 30 seconds
- New firmware timestamp in `/api/firmware_info`

---

*Last updated: August 17, 2025*
*Working method validated with TTGO T-Display ESP32*
