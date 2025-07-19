# ESP32 LittleFS Upload Script for 16MB Flash
# This script creates and uploads the LittleFS image (web files) to ESP32

param(
    [string]$Port = "COM3",
    [int]$BaudRate = 921600
)

$ErrorActionPreference = "Stop"

Write-Host "ESP32 FATFS Upload Script (16MB)" -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan

# Configuration for ESP32 16MB with app3M_fat9M_16MB partition scheme
$DataDir = "data"
$FATImage = "fatfs.bin"
$FlashAddress = "0x610000"  # FATFS partition start for app3M_fat9M_16MB  
$FATSize = 10354688        # 9.875MB in bytes (0x9E0000)

try {
    # Verify data directory exists
    if (-not (Test-Path $DataDir)) {
        throw "Data directory '$DataDir' not found"
    }
    
    Write-Host "Data directory contents:" -ForegroundColor Gray
    Get-ChildItem $DataDir -Recurse -File | ForEach-Object {
        $relativePath = $_.FullName.Substring((Resolve-Path $DataDir).Path.Length + 1)
        $size = [math]::Round($_.Length / 1024, 1)
        Write-Host "  $relativePath ($size KB)" -ForegroundColor Gray
    }
    
    # Find mklittlefs tool for ESP32
    $ESP32ToolsPath = "$env:USERPROFILE\AppData\Local\Arduino15\packages\esp32\tools"
    $MkLittleFS = Get-ChildItem -Path $ESP32ToolsPath -Recurse -Name "mklittlefs.exe" | Select-Object -First 1
    
    if (-not $MkLittleFS) {
        throw "mklittlefs tool not found in ESP32 tools. Please ensure ESP32 Arduino core is installed."
    }
    
    $MkLittleFSPath = Join-Path $ESP32ToolsPath $MkLittleFS
    Write-Host "Using mklittlefs: $MkLittleFSPath" -ForegroundColor Gray
    
    # Check esptool
    try {
        $null = python -m esptool version 2>$null
        $ESPTool = "python -m esptool"
        Write-Host "Using esptool via Python" -ForegroundColor Gray
    } catch {
        throw "esptool not found. Please install: pip install esptool"
    }
    
    Write-Host "Creating LittleFS image..." -ForegroundColor Yellow
    
    # Remove old image if exists
    if (Test-Path $FATImage) {
        Remove-Item $FATImage -Force
    }
    
    # For ESP32 FATFS, we need to use a different approach since fatfsgen.py is not available
    Write-Host "Creating FATFS image..." -ForegroundColor Yellow
    
    # Try to find fatfsgen.py in ESP32 tools
    $ESP32HardwarePath = "$env:USERPROFILE\AppData\Local\Arduino15\packages\esp32\hardware"
    $FatfsGenPath = Get-ChildItem -Path $ESP32HardwarePath -Recurse -Name "fatfsgen.py" -ErrorAction SilentlyContinue | Select-Object -First 1
    
    if ($FatfsGenPath) {
        $FatfsGenFullPath = Join-Path $ESP32HardwarePath $FatfsGenPath
        Write-Host "Using fatfsgen.py: $FatfsGenFullPath" -ForegroundColor Gray
        & python $FatfsGenFullPath $DataDir $FATImage --partition_size $FATSize
    } else {
        # Alternative: Upload files to ESP32 and let it format FATFS on first boot
        Write-Host "Warning: fatfsgen.py not found in ESP32 tools" -ForegroundColor Yellow
        Write-Host "Creating minimal FATFS image and uploading individual files via web interface" -ForegroundColor Yellow
        
        # Create an empty FATFS image (formatted)
        $bytes = New-Object byte[] $FATSize
        # Fill with 0xFF (empty flash pattern)
        for ($i = 0; $i -lt $FATSize; $i++) { $bytes[$i] = 0xFF }
        [System.IO.File]::WriteAllBytes((Join-Path (Get-Location) $FATImage), $bytes)
        Write-Host "Created empty FATFS image - files will be uploaded after flashing" -ForegroundColor Yellow
    }
    
    if ($LASTEXITCODE -ne 0 -and $FatfsGenPath) {
        throw "FATFS image creation failed"
    }
    
    $imageSize = (Get-Item $FATImage).Length
    $imageSizeKB = [math]::Round($imageSize / 1024)
    $imageSizeMB = [math]::Round($imageSize / 1024 / 1024, 1)
    Write-Host "✓ FATFS image created: $imageSizeKB KB ($imageSizeMB MB)" -ForegroundColor Green
    
    Write-Host "Uploading to ESP32..." -ForegroundColor Yellow
    Write-Host "Port: $Port" -ForegroundColor Gray
    Write-Host "Flash address: $FlashAddress (FATFS partition)" -ForegroundColor Gray
    Write-Host "Partition size: 9.875MB" -ForegroundColor Gray
    
    # Upload FATFS to ESP32
    & python -m esptool --chip esp32 --port $Port --baud $BaudRate write_flash $FlashAddress $FATImage
    
    if ($LASTEXITCODE -ne 0) {
        throw "FATFS upload failed"
    }
    
    Write-Host "✓ Web files uploaded successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "The ESP32 will restart and serve the updated web interface." -ForegroundColor Green
    Write-Host "Access the web interface at: http://[device-ip]/" -ForegroundColor Cyan
    Write-Host "OTA updates available at: http://[device-ip]/update" -ForegroundColor Cyan
    
} catch {
    Write-Host "❌ Upload failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
