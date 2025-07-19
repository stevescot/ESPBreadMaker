# Quick Web Files Upload Script
# 
# Simple wrapper around the robust upload script for everyday use.
# This script uploads all web files from ./data to the ESP32.
#
# Usage:
#   .\upload_files.ps1              # Upload to default IP (192.168.250.125)
#   .\upload_files.ps1 "192.168.1.100"  # Upload to specific IP

param(
    [string]$ServerIP = "192.168.250.125"
)

Write-Host "ESP32 Breadmaker Web Files Upload" -ForegroundColor Cyan
Write-Host "==================================" -ForegroundColor Cyan
Write-Host "Target: http://$ServerIP" -ForegroundColor Yellow
Write-Host ""

# Call the robust upload script with default settings
& ".\upload_files_robust.ps1" -ServerIP $ServerIP

Write-Host ""
Write-Host "Upload completed! You can now access the web interface at:" -ForegroundColor Green
Write-Host "http://$ServerIP" -ForegroundColor Cyan
