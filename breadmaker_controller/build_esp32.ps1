# ESP32 TTGO T-Display Build Script for Windows
# This script compiles and uploads the breadmaker controller to ESP32

Write-Host "Building ESP32 TTGO T-Display Breadmaker Controller..." -ForegroundColor Green

# Set board type
$Board = "esp32:esp32:esp32:PartitionScheme=default,CPUFreq=240,FlashMode=qio,FlashFreq=80,FlashSize=4M,UploadSpeed=921600"

# TFT_eSPI configuration note
Write-Host "Note: Make sure TFT_eSPI is configured for TTGO T-Display" -ForegroundColor Yellow
Write-Host "  - Copy TFT_eSPI_Setup.h to your TFT_eSPI library folder" -ForegroundColor Yellow
Write-Host "  - Or configure User_Setup.h in the TFT_eSPI library" -ForegroundColor Yellow

# Compile
Write-Host "Compiling..." -ForegroundColor Blue
arduino-cli compile --fqbn $Board breadmaker_controller.ino

if ($LASTEXITCODE -eq 0) {
    Write-Host "Compilation successful!" -ForegroundColor Green
    
    # Upload if port is provided
    if ($args.Count -gt 0) {
        $Port = $args[0]
        Write-Host "Uploading to port $Port..." -ForegroundColor Blue
        arduino-cli upload --fqbn $Board --port $Port breadmaker_controller.ino
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Upload successful!" -ForegroundColor Green
        } else {
            Write-Host "Upload failed!" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "No port specified. Skipping upload." -ForegroundColor Yellow
        Write-Host "Usage: .\build_esp32.ps1 [port]" -ForegroundColor Yellow
        Write-Host "Example: .\build_esp32.ps1 COM3" -ForegroundColor Yellow
    }
} else {
    Write-Host "Compilation failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Build process completed!" -ForegroundColor Green
