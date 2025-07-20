# File Cleanup Recommendations

## Files That Can Be Removed
The following files are now superseded by `unified_build.ps1`:

### Batch Files (.bat)
- `upload_programs.bat` → Use `.\unified_build.ps1 -UploadData`

### PowerShell Scripts (.ps1) in Root
- `build_and_upload_ota.ps1` → Use `.\unified_build.ps1 -Build -WebOTA`
- `upload_files_robust.ps1` → Use `.\unified_build.ps1 -UploadData`
- `upload_ota_direct.ps1` → Use `.\unified_build.ps1 -WebOTA`
- `upload_simple.ps1` → Use `.\unified_build.ps1 -UploadData`
- `upload_programs_index.ps1` → Use `.\unified_build.ps1 -UploadData`
- `upload_all.ps1` → Use `.\unified_build.ps1 -Build -WebOTA -UploadData`

### Archive Folder Scripts
The entire `archive/` folder contains old scripts that are no longer needed:
- `archive/build_and_upload.ps1`
- `archive/upload_data*.ps1`
- `archive/upload_files*.ps1`
- `archive/old_upload_scripts/` (entire folder)

### Legacy Display Library Files
- `TFT_eSPI_Setup.h` → No longer used (project uses LovyanGFX, not TFT_eSPI)

## Files to Keep

### Core Build Scripts
- `build_esp32.ps1` - Core compilation script (used by unified_build.ps1)
- `unified_build.ps1` - **NEW: Main build/upload tool**
- `reset_and_monitor.ps1` - Serial monitoring utility
- `setup_esp32_core.ps1` - Development environment setup

### Data Management
- `create_fatfs.py` - Filesystem creation utility
- `split_programs.py` - Program file management

### Documentation
- `copilot_instructions.md` - **UPDATED: Build procedures**
- `BUILD_README.md`
- `OTA_README.md`
- Various `*.md` files for documentation

## Migration Commands

To clean up the old files while preserving functionality:

```powershell
# Remove superseded batch files
Remove-Item upload_programs.bat

# Remove superseded PowerShell scripts
Remove-Item build_and_upload_ota.ps1, upload_files_robust.ps1, upload_ota_direct.ps1
Remove-Item upload_simple.ps1, upload_programs_index.ps1, upload_all.ps1

# Remove archive folder (contains only legacy scripts)
Remove-Item archive -Recurse -Force

# Verify the new unified script works
.\unified_build.ps1 -Test
```

## New Workflow Summary

### For Copilot/Future Development:
1. **Build only**: `.\unified_build.ps1 -Build`
2. **Build + Upload**: `.\unified_build.ps1 -Build -WebOTA`
3. **Upload data files**: `.\unified_build.ps1 -UploadData`
4. **Full deployment**: `.\unified_build.ps1 -Build -WebOTA -UploadData`
5. **Test connectivity**: `.\unified_build.ps1 -Test`

### Replaced Commands:
- Old: `upload_programs.bat` → New: `.\unified_build.ps1 -UploadData`
- Old: `build_and_upload_ota.ps1` → New: `.\unified_build.ps1 -Build -WebOTA`
- Old: Multiple upload scripts → New: Single `.\unified_build.ps1` with parameters

This consolidation provides:
- ✅ Single script for all operations
- ✅ Consistent error handling and output
- ✅ Built-in connectivity testing
- ✅ Clear parameter-based operation selection
- ✅ Comprehensive help documentation
- ✅ Future-proof extensibility
