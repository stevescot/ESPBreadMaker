# Quick OTA Build and Upload Script
# Configured for ESP32 at 192.168.250.125

param(
    [switch]$UploadData,        # Also upload data files
    [string]$Password = "",     # OTA password (if set on device)
    [switch]$Help
)

if ($Help) {
    Write-Host "Quick OTA Upload Script" -ForegroundColor Cyan
    Write-Host "Pre-configured for ESP32 at 192.168.250.125" -ForegroundColor White
    Write-Host ""
    Write-Host "Usage:" -ForegroundColor Yellow
    Write-Host "  .\build_and_upload_ota.ps1 [options]" -ForegroundColor White
    Write-Host ""
    Write-Host "Options:" -ForegroundColor Yellow
    Write-Host "  -UploadData         Also upload web files after firmware" -ForegroundColor White
    Write-Host "  -Password <pwd>     OTA password (if required by device)" -ForegroundColor White
    Write-Host "  -Help               Show this help" -ForegroundColor White
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Green
    Write-Host "  .\build_and_upload_ota.ps1                    # Upload firmware only" -ForegroundColor Gray
    Write-Host "  .\build_and_upload_ota.ps1 -UploadData        # Upload firmware + web files" -ForegroundColor Gray
    Write-Host "  .\build_and_upload_ota.ps1 -Password mypass   # Upload with OTA password" -ForegroundColor Gray
    exit 0
}

$ESP32_IP = "192.168.250.125"

Write-Host "ESP32 OTA Build and Upload" -ForegroundColor Green
Write-Host "Target Device: $ESP32_IP" -ForegroundColor Cyan
Write-Host ""

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
