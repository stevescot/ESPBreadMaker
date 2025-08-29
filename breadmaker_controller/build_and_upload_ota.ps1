# Quick OTA Build and Upload Script - LEGACY - Use esp32-manager.ps1 instead
# Redirects to the new unified manager

param(
    [switch]$UploadData,        # Also upload data files
    [string]$Password = "",     # OTA password (if set on device)
    [switch]$Help
)

if ($Help) {
    Write-Host "Legacy script! Use esp32-manager.ps1 instead:" -ForegroundColor Yellow
    Write-Host "  .\esp32-manager.ps1 -Build -Upload -UploadWeb" -ForegroundColor Cyan
    Write-Host "  .\esp32-manager.ps1 -Help" -ForegroundColor Cyan
    exit 0
}

Write-Host "⚠️ Legacy script detected!" -ForegroundColor Yellow
Write-Host "Redirecting to esp32-manager.ps1 for better safety and features..." -ForegroundColor Cyan
Write-Host ""

if ($UploadData) {
    & ".\esp32-manager.ps1" -Build -Upload -UploadWeb
} else {
    & ".\esp32-manager.ps1" -Build -Upload
}

# Build and upload firmware
Write-Host "Building and uploading firmware..." -ForegroundColor Blue

# First build the firmware
& ".\build_esp32.ps1"

if ($LASTEXITCODE -ne 0) {
    Write-Host "Firmware build failed!" -ForegroundColor Red
    exit 1
}

# Then upload using the reliable HTTP method
& ".\upload_ota.ps1" -IP $ESP32_IP

if ($LASTEXITCODE -ne 0) {
    Write-Host "Firmware upload failed!" -ForegroundColor Red
    exit 1
}

# Upload data files if requested
if ($UploadData) {
    Write-Host ""
    Write-Host "Uploading web files..." -ForegroundColor Blue
    & ".\upload_files_sequential.ps1"
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Web files uploaded successfully!" -ForegroundColor Green
    } else {
        Write-Host "Web files upload failed!" -ForegroundColor Red
        exit 1
    }
}

Write-Host ""
Write-Host "✓ OTA upload completed successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "Device Access:" -ForegroundColor Cyan
Write-Host "  Web Interface: http://$ESP32_IP" -ForegroundColor White
Write-Host "  OTA Updates:   http://$ESP32_IP/ota.html" -ForegroundColor White
Write-Host "  File Manager:  http://$ESP32_IP/config.html" -ForegroundColor White
Write-Host ""
Write-Host "The device will restart and should be ready in ~10-30 seconds." -ForegroundColor Yellow
