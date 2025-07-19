# Quick LittleFS Upload Script
# This script only creates and uploads the LittleFS image (web files)
# Use this when you only want to update the web interface files

param(
    [string]$Port = "COM4",
    [int]$BaudRate = 921600
)

$ErrorActionPreference = "Stop"

Write-Host "Quick LittleFS Upload Script" -ForegroundColor Cyan
Write-Host "============================" -ForegroundColor Cyan

# Configuration
$DataDir = "data"
$LittleFSImage = "littlefs.bin"

try {
    # Verify data directory exists
    if (-not (Test-Path $DataDir)) {
        throw "Data directory '$DataDir' not found"
    }
    
    # Find mklittlefs tool
    $MkLittleFS = Get-ChildItem -Path "$env:USERPROFILE\AppData\Local\Arduino15\packages\esp8266\tools\mklittlefs" -Recurse -Name "mklittlefs.exe" | Select-Object -First 1
    if (-not $MkLittleFS) {
        throw "mklittlefs tool not found"
    }
    $MkLittleFSPath = "$env:USERPROFILE\AppData\Local\Arduino15\packages\esp8266\tools\mklittlefs\$($MkLittleFS.Replace('\mklittlefs.exe', ''))\mklittlefs.exe"
    
    # Check esptool
    try {
        $null = python -m esptool version 2>$null
        $ESPTool = "python -m esptool"
    } catch {
        throw "esptool not found. Please install: pip install esptool"
    }
    
    Write-Host "Creating LittleFS image..." -ForegroundColor Yellow
    
    # Remove old image if exists
    if (Test-Path $LittleFSImage) {
        Remove-Item $LittleFSImage -Force
    }
    
    # Create LittleFS image
    & $MkLittleFSPath -c $DataDir -s 2097152 $LittleFSImage
    
    if ($LASTEXITCODE -ne 0) {
        throw "LittleFS image creation failed"
    }
    
    $imageSize = (Get-Item $LittleFSImage).Length
    Write-Host "✓ LittleFS image created ($([math]::Round($imageSize / 1024))KB)" -ForegroundColor Green
    
    Write-Host "Uploading to ESP8266..." -ForegroundColor Yellow
    Write-Host "Port: $Port" -ForegroundColor Gray
    Write-Host "Flash address: 0x200000" -ForegroundColor Gray
    
    # Upload LittleFS
    & python -m esptool --chip esp8266 --port $Port --baud $BaudRate write_flash 0x200000 $LittleFSImage
    
    if ($LASTEXITCODE -ne 0) {
        throw "LittleFS upload failed"
    }
    
    Write-Host "✓ Web files uploaded successfully!" -ForegroundColor Green
    Write-Host "`nThe ESP8266 will restart and serve the updated web interface." -ForegroundColor Green
    
} catch {
    Write-Host "❌ Upload failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
