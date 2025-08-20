#!/usr/bin/env pwsh
# Demo script to show state preservation functionality

Write-Host "=== State Preservation Demo ===" -ForegroundColor Cyan
Write-Host ""

Write-Host "1. DEFAULT BEHAVIOR - Full Upload with State Preservation:" -ForegroundColor Yellow
Write-Host "   .\unified_build.ps1 -UploadData" -ForegroundColor Gray
Write-Host "   → Automatically backs up device state to data/device_backup_*/" -ForegroundColor Green
Write-Host "   → Uploads all data files" -ForegroundColor Green  
Write-Host "   → Restores calibration.json, programs.json, etc." -ForegroundColor Green
Write-Host "   → Keeps backup in data directory for source control review" -ForegroundColor Green
Write-Host ""

Write-Host "2. FIRMWARE UPLOAD with State Preservation:" -ForegroundColor Yellow
Write-Host "   .\unified_build.ps1 -Build -WebOTA" -ForegroundColor Gray
Write-Host "   → Backs up device state before firmware upload" -ForegroundColor Green
Write-Host "   → Uploads new firmware" -ForegroundColor Green
Write-Host "   → Waits for device to restart" -ForegroundColor Green
Write-Host "   → Restores preserved settings automatically" -ForegroundColor Green
Write-Host ""

Write-Host "3. PARTIAL UPLOAD (no state preservation by default):" -ForegroundColor Yellow
Write-Host "   .\unified_build.ps1 -UploadData -Files index.html" -ForegroundColor Gray
Write-Host "   → Uploads only specified files" -ForegroundColor Green
Write-Host "   → No state preservation (since it's partial)" -ForegroundColor Yellow
Write-Host ""

Write-Host "4. FORCE STATE PRESERVATION for partial upload:" -ForegroundColor Yellow
Write-Host "   .\unified_build.ps1 -UploadData -Files index.html -PreserveState" -ForegroundColor Gray
Write-Host "   → Uploads only specified files" -ForegroundColor Green
Write-Host "   → Forces state backup/restore anyway" -ForegroundColor Green
Write-Host ""

Write-Host "5. DISABLE STATE PRESERVATION for full upload:" -ForegroundColor Yellow
Write-Host "   .\unified_build.ps1 -UploadData -NoPreserveState" -ForegroundColor Gray
Write-Host "   → Uploads all files without state preservation" -ForegroundColor Green
Write-Host "   → Useful for development when you want speed" -ForegroundColor Yellow
Write-Host ""

Write-Host "BACKUP LOCATION:" -ForegroundColor Cyan
Write-Host "   data/device_backup_YYYYMMDD_HHMMSS/" -ForegroundColor Gray
Write-Host "   ├── calibration.json     ← Your actual device calibration" -ForegroundColor Green
Write-Host "   ├── programs.json        ← Master program definitions" -ForegroundColor Green
Write-Host "   ├── programs/" -ForegroundColor Green
Write-Host "   │   ├── program_0.json   ← Individual program details" -ForegroundColor Green
Write-Host "   │   ├── program_1.json   ← Individual program details" -ForegroundColor Green
Write-Host "   │   └── ... (program_2.json through program_19.json)" -ForegroundColor Green
Write-Host "   ├── pid-profiles.json    ← PID tuning parameters" -ForegroundColor Green
Write-Host "   └── current_status.json  ← Active program state" -ForegroundColor Green
Write-Host ""

Write-Host "GIT WORKFLOW:" -ForegroundColor Cyan
Write-Host "   1. Run upload (state backed up automatically)" -ForegroundColor Gray
Write-Host "   2. Review: git status" -ForegroundColor Gray
Write-Host "   3. Add important configs: git add data/device_backup_*/calibration.json" -ForegroundColor Gray
Write-Host "   4. Commit: git commit -m 'Update calibration from device'" -ForegroundColor Gray
Write-Host ""

Write-Host "💡 KEY BENEFITS:" -ForegroundColor Green
Write-Host "   ✅ No more lost calibration data during uploads" -ForegroundColor White
Write-Host "   ✅ Custom programs survive firmware updates" -ForegroundColor White
Write-Host "   ✅ Individual program definitions fully preserved" -ForegroundColor White
Write-Host "   ✅ Stage timing and temperature profiles backed up" -ForegroundColor White
Write-Host "   ✅ Configuration changes tracked in source control" -ForegroundColor White
Write-Host "   ✅ Zero configuration - works automatically" -ForegroundColor White
Write-Host "   ✅ Manual override options available" -ForegroundColor White
