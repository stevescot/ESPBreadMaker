# ESP32 Quick Upload Script
# Simple script for common upload tasks
#
# Usage:
#   .\quick_upload.ps1                    # Upload files only
#   .\quick_upload.ps1 -firmware          # Upload firmware only  
#   .\quick_upload.ps1 -both              # Upload both files and firmware
#   .\quick_upload.ps1 -build             # Build and upload firmware
#   .\quick_upload.ps1 -ip 192.168.1.100  # Use different IP
#
param(
    [string]$IP = "192.168.250.125",
    [switch]$firmware,
    [switch]$both,
    [switch]$build,
    [switch]$help
)

if ($help) {
    Write-Host "ESP32 Quick Upload Script" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  .\quick_upload.ps1                    # Upload files only"
    Write-Host "  .\quick_upload.ps1 -firmware          # Upload firmware only"
    Write-Host "  .\quick_upload.ps1 -both              # Upload both files and firmware"
    Write-Host "  .\quick_upload.ps1 -build             # Build and upload firmware"
    Write-Host "  .\quick_upload.ps1 -ip 192.168.1.100  # Use different IP"
    Write-Host ""
    Write-Host "This script uses the comprehensive upload_all.ps1 script internally."
    exit 0
}

# Display what will be done
if ($build) {
    Write-Host "Will build firmware first, then upload" -ForegroundColor Yellow
} elseif ($firmware) {
    Write-Host "Will upload firmware only" -ForegroundColor Yellow
} elseif ($both) {
    Write-Host "Will upload both files and firmware" -ForegroundColor Yellow
} else {
    Write-Host "Will upload files only (default)" -ForegroundColor Yellow
}

Write-Host "Target IP: $IP" -ForegroundColor Cyan

# Show what command will be run
if ($build) {
    Write-Host "Running: .\upload_all.ps1 -ServerIP $IP -BuildFirst -UploadFirmware" -ForegroundColor Gray
} elseif ($firmware) {
    Write-Host "Running: .\upload_all.ps1 -ServerIP $IP -UploadFirmware" -ForegroundColor Gray
} elseif ($both) {
    Write-Host "Running: .\upload_all.ps1 -ServerIP $IP -UploadFiles -UploadFirmware" -ForegroundColor Gray
} else {
    Write-Host "Running: .\upload_all.ps1 -ServerIP $IP -UploadFiles" -ForegroundColor Gray
}
Write-Host ""

# Run the main upload script with proper argument splatting
if ($build) {
    & .\upload_all.ps1 -ServerIP $IP -BuildFirst -UploadFirmware
} elseif ($firmware) {
    & .\upload_all.ps1 -ServerIP $IP -UploadFirmware
} elseif ($both) {
    & .\upload_all.ps1 -ServerIP $IP -UploadFiles -UploadFirmware
} else {
    & .\upload_all.ps1 -ServerIP $IP -UploadFiles
}
