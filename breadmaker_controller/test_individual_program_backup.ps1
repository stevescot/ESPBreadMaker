#!/usr/bin/env pwsh
# Test script to demonstrate individual program backup functionality

Write-Host "=== Individual Program Backup Test ===" -ForegroundColor Cyan
Write-Host ""

Write-Host "📋 WHAT GETS BACKED UP NOW:" -ForegroundColor Yellow
Write-Host ""

Write-Host "🔧 Core Configuration Files:" -ForegroundColor Green
Write-Host "   ✓ calibration.json       - Temperature calibration" -ForegroundColor White
Write-Host "   ✓ programs.json          - Master program definitions" -ForegroundColor White  
Write-Host "   ✓ pid-profiles.json      - PID controller profiles" -ForegroundColor White
Write-Host "   ✓ programs_index.json    - Program index metadata" -ForegroundColor White
Write-Host ""

Write-Host "🍞 Individual Program Files:" -ForegroundColor Green
Write-Host "   ✓ programs/program_0.json  - Program 0 details" -ForegroundColor White
Write-Host "   ✓ programs/program_1.json  - Program 1 details" -ForegroundColor White
Write-Host "   ✓ programs/program_2.json  - Program 2 details" -ForegroundColor White
Write-Host "   ✓ ... (program_3.json through program_19.json)" -ForegroundColor White
Write-Host ""

Write-Host "📊 Runtime State:" -ForegroundColor Green
Write-Host "   ✓ current_status.json    - Active program state" -ForegroundColor White
Write-Host ""

Write-Host "📁 BACKUP DIRECTORY STRUCTURE:" -ForegroundColor Yellow
Write-Host ""
Write-Host "data/device_backup_YYYYMMDD_HHMMSS/" -ForegroundColor Gray
Write-Host "├── calibration.json" -ForegroundColor White
Write-Host "├── programs.json" -ForegroundColor White
Write-Host "├── programs/" -ForegroundColor White
Write-Host "│   ├── program_0.json" -ForegroundColor White
Write-Host "│   ├── program_1.json" -ForegroundColor White
Write-Host "│   ├── program_2.json" -ForegroundColor White
Write-Host "│   └── ... (program_3.json through program_19.json)" -ForegroundColor White
Write-Host "├── pid-profiles.json" -ForegroundColor White
Write-Host "├── programs_index.json" -ForegroundColor White
Write-Host "└── current_status.json" -ForegroundColor White
Write-Host ""

Write-Host "🎯 WHEN BACKUPS HAPPEN:" -ForegroundColor Yellow
Write-Host ""
Write-Host "Automatically (Default Behavior):" -ForegroundColor Green
Write-Host "   .\unified_build.ps1 -UploadData           # Full data upload" -ForegroundColor Gray
Write-Host "   .\unified_build.ps1 -Build -WebOTA        # Firmware upload" -ForegroundColor Gray
Write-Host "   .\unified_build.ps1 -Build -WebOTA -UploadData  # Full deployment" -ForegroundColor Gray
Write-Host ""

Write-Host "Manual Control:" -ForegroundColor Green
Write-Host "   .\unified_build.ps1 -UploadData -Files index.html -PreserveState  # Force backup for partial" -ForegroundColor Gray
Write-Host "   .\unified_build.ps1 -UploadData -NoPreserveState                  # Skip backup for full" -ForegroundColor Gray
Write-Host ""

Write-Host "💡 BENEFITS OF INDIVIDUAL PROGRAM BACKUP:" -ForegroundColor Cyan
Write-Host "   ✅ Complete program definitions preserved (not just index)" -ForegroundColor White
Write-Host "   ✅ Custom recipe modifications saved" -ForegroundColor White
Write-Host "   ✅ Stage timing and temperature profiles backed up" -ForegroundColor White
Write-Host "   ✅ Fermentation parameters and adjustments preserved" -ForegroundColor White
Write-Host "   ✅ Ready for source control review and commits" -ForegroundColor White
Write-Host ""

if (Test-Path "c:\Users\steve\OneDrive\Documents\GitHub\ESPBreadMaker\breadmaker_controller\data\programs\program_0.json") {
    Write-Host "🔍 SAMPLE PROGRAM FILE FOUND:" -ForegroundColor Green
    Write-Host "   data/programs/program_0.json exists locally" -ForegroundColor White
    
    $sampleSize = (Get-Item "c:\Users\steve\OneDrive\Documents\GitHub\ESPBreadMaker\breadmaker_controller\data\programs\program_0.json").Length
    Write-Host "   File size: $sampleSize bytes" -ForegroundColor White
    Write-Host ""
}

Write-Host "⚡ NEXT STEPS:" -ForegroundColor Yellow
Write-Host "   1. Run: .\unified_build.ps1 -UploadData" -ForegroundColor Gray
Write-Host "   2. Check: data/device_backup_*/ directory created" -ForegroundColor Gray
Write-Host "   3. Review: Individual program files in programs/ subdirectory" -ForegroundColor Gray
Write-Host "   4. Commit: Important program definitions to source control" -ForegroundColor Gray
