# ESP32 Unified Build and Upload Script

## Overview
The `build_esp32.ps1` script is now a comprehensive, all-in-one solution for building and uploading the ESP32 breadmaker controller firmware. It has replaced all the separate upload scripts for a cleaner, more maintainable solution.

## What's Integrated
This single script now handles:
- ✅ **Firmware Compilation** - ESP32 Arduino CLI compilation
- ✅ **Serial Upload** - USB cable firmware upload
- ✅ **OTA Upload** - WiFi Over-The-Air firmware upload  
- ✅ **Web File Upload** - Data files upload via HTTP
- ✅ **Validation** - Comprehensive error checking and guidance
- ✅ **Progress Feedback** - Detailed status and progress reporting

## Replaced Scripts
The following scripts have been **moved to archive/** and are no longer needed:
- `upload_ota.ps1` - ✅ Integrated into build_esp32.ps1
- `build_and_upload_ota.ps1` - ✅ Integrated into build_esp32.ps1  
- `upload_files_robust.ps1` - ✅ Integrated into build_esp32.ps1
- `upload_files_sequential.ps1` - ✅ Integrated into build_esp32.ps1
- Various other upload scripts in archive/

## Usage Examples

### Basic Operations
```powershell
# Compile only (no upload)
.\build_esp32.ps1 -CompileOnly

# Compile and upload via USB serial
.\build_esp32.ps1 -Port COM3

# Compile and upload via WiFi (OTA)
.\build_esp32.ps1 -OTA 192.168.250.125
```

### Advanced Operations
```powershell
# Upload firmware + web files via serial
.\build_esp32.ps1 -Port COM3 -UploadData

# Upload firmware + web files via OTA
.\build_esp32.ps1 -OTA 192.168.250.125 -UploadData

# OTA upload with password
.\build_esp32.ps1 -OTA 192.168.250.125 -OTAPassword "mypassword"

# Show help and all options
.\build_esp32.ps1 -Help
```

## Parameters Reference

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `-Port` | String | COM port for serial upload | `COM3` |
| `-OTA` | String | IP address for OTA upload | `192.168.250.125` |
| `-OTAPassword` | String | OTA password (if required) | `"mypass"` |
| `-UploadData` | Switch | Upload web files after firmware | N/A |
| `-CompileOnly` | Switch | Compile without uploading | N/A |
| `-Help` | Switch | Show comprehensive help | N/A |

## Upload Methods

### Serial Upload (USB Cable)
- **When to use**: First-time setup, recovery, when WiFi unavailable
- **Requirements**: ESP32 connected via USB cable
- **Speed**: Fast (~500KB/s)
- **Reliability**: Very high
- **Data upload**: Not currently implemented (use OTA method)

### OTA Upload (WiFi)
- **When to use**: Regular updates, remote deployment
- **Requirements**: ESP32 connected to WiFi, OTA-enabled firmware running
- **Speed**: Moderate (~50-100KB/s, depends on WiFi)
- **Reliability**: High (with retry logic)
- **Data upload**: Full HTTP upload support

## Data File Upload

The script includes integrated web file upload functionality:

- **Method**: HTTP multipart upload to `/api/upload` endpoint
- **Retry Logic**: 3 attempts per file with delays
- **Progress Tracking**: Real-time upload status
- **Error Handling**: Detailed error reporting
- **File Support**: All files in `./data/` directory recursively

### Data Upload Process
1. Confirms data overwrite (preserves settings by default)
2. Scans `./data/` directory for all files
3. Uploads each file via HTTP with retry logic
4. Reports success/failure for each file
5. Provides summary statistics

## Error Handling

The script includes comprehensive error handling:

- **Validation**: Prevents conflicting options
- **Connectivity**: Tests device reachability before upload
- **Dependencies**: Checks for required tools (arduino-cli, espota.py)
- **File Existence**: Verifies binary and data files exist
- **Detailed Messages**: Clear error descriptions and suggestions

## Integration Benefits

### For Developers
- **Single Command**: One script for all operations
- **Consistent Interface**: Same parameters regardless of upload method
- **Better Error Messages**: Integrated validation and helpful suggestions
- **No External Dependencies**: Self-contained file upload logic

### For Users
- **Simplified Workflow**: No need to remember multiple scripts
- **Intelligent Defaults**: Sensible behavior for common scenarios
- **Better Feedback**: Progress tracking and clear status messages
- **Reduced Complexity**: Fewer files to manage

## Device Configuration

For your current setup:
- **IP Address**: 192.168.250.125
- **Hostname**: esp32-73AB98
- **OTA Port**: 3232 (automatically configured)
- **Web Interface**: http://192.168.250.125

## Migration Guide

If you were using the old scripts:

### Old Method
```powershell
.\build_esp32.ps1                          # Compile
.\upload_ota.ps1 -IP 192.168.250.125       # Upload firmware
.\upload_files_robust.ps1                  # Upload web files
```

### New Method  
```powershell
.\build_esp32.ps1 -OTA 192.168.250.125 -UploadData
```

## Performance Comparison

| Operation | Old Method | New Method | Improvement |
|-----------|------------|------------|-------------|
| Commands needed | 3 separate | 1 combined | 67% fewer commands |
| Script files | 5+ files | 1 file | 80% fewer files |
| Error handling | Basic | Comprehensive | Detailed validation |
| Progress feedback | Limited | Full tracking | Real-time status |
| Retry logic | Manual | Automatic | Built-in resilience |

## Troubleshooting

### Common Issues

**Compilation fails**
```
.\build_esp32.ps1 -CompileOnly  # Test compilation only
```

**OTA unreachable**
```powershell
# Test connectivity
Test-NetConnection -ComputerName 192.168.250.125 -Port 3232
```

**File upload fails**
```powershell
# Check device status
Invoke-RestMethod http://192.168.250.125/api/status
```

## Next Steps

1. **Test the new script**: Start with `-CompileOnly` to verify compilation
2. **Archive cleanup**: Old scripts are safely stored in `archive/`
3. **Update workflows**: Use the new unified script in your development process
4. **Report issues**: Any problems with the integrated functionality

The new unified script provides a much cleaner, more reliable, and easier-to-use development experience!
