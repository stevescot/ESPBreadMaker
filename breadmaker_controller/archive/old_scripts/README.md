# Archived Build Scripts

This folder contains legacy build and upload scripts that have been replaced by the unified build system.

## Replaced Scripts

### Build Scripts
- `build_esp32.ps1` - Basic ESP32 compilation script (empty/deprecated)
- `setup_esp32_core.ps1` - ESP32 core setup script

### Upload Scripts
- `upload.ps1` - Basic upload script (empty/deprecated)
- `upload_all.ps1` - Upload all files script
- `upload_ota_direct.ps1` - Direct OTA upload script (empty/deprecated)
- `upload_simple.ps1` - Simple file upload script (empty/deprecated)
- `quick_upload.ps1` - Quick upload script (empty/deprecated)
- `upload_files_robust_deprecated.ps1` - Robust file upload script (deprecated)

### Program-Specific Scripts
- `upload_programs.bat` - Batch file for uploading programs
- `upload_programs_index.ps1` - PowerShell script for uploading program index

### Utility Scripts
- `reset_and_monitor.ps1` - Device reset and monitoring script
- `test_endpoints.ps1` - API endpoint testing script

## Migration to Unified Build System

All functionality from these scripts has been consolidated into:
- `unified_build.ps1` - Main build and deployment script
- VS Code tasks in `.vscode/tasks.json` - IDE integration

### Key Improvements
1. **Single Entry Point**: One script handles all build/upload operations
2. **VS Code Integration**: Proper task definitions with problem matchers
3. **Better Error Handling**: Comprehensive error reporting and validation
4. **Consistent Interface**: Standardized parameters and output formatting
5. **Web OTA Support**: Reliable firmware updates via HTTP endpoint

### Usage Examples
```powershell
# Build only
.\unified_build.ps1 -Build

# Build and upload firmware
.\unified_build.ps1 -Build -WebOTA

# Upload web files
.\unified_build.ps1 -UploadData

# Full deployment
.\unified_build.ps1 -Build -WebOTA -UploadData
```

## VS Code Tasks
Use Ctrl+Shift+P > Tasks: Run Task to access:
- Build Firmware
- Build and Upload (Web OTA)
- Full Deploy (Build + Upload All)
- Upload Web Files Only
- Upload Specific Files
- Clean Build
- Test Device Connectivity

These archived scripts are kept for reference but should not be used in new development.
