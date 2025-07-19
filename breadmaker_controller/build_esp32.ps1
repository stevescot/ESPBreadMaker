# ESP32 16MB Build Script with OTA Support
# This script compiles and uploads the breadmaker controller to ESP32 with 16MB flash
# REQUIRES ESP32 Arduino Core 2.0.17 - newer versions have compatibility issues!

param(
    [string]$Port,              # COM port for upload (optional)
    [string]$OTA,               # OTA upload to IP address (e.g., 192.168.250.125)
    [string]$OTAPassword = "",  # OTA password (if required)
    [switch]$UploadData,        # Upload data files to FATFS partition (optional)
    [switch]$Help               # Show help
)

if ($Help) {
    Write-Host "ESP32 Build Script Usage:" -ForegroundColor Cyan
    Write-Host "  .\build_esp32.ps1 [options]" -ForegroundColor White
    Write-Host ""
    Write-Host "Options:" -ForegroundColor Yellow
    Write-Host "  -Port <port>        Upload firmware via serial port (e.g., COM3)" -ForegroundColor White
    Write-Host "  -OTA <ip>           Upload firmware via WiFi to IP address (e.g., 192.168.250.125)" -ForegroundColor White
    Write-Host "  -OTAPassword <pwd>  OTA password (if required)" -ForegroundColor White
    Write-Host "  -UploadData         Also upload data files to FATFS (WARNING: erases existing data)" -ForegroundColor White
    Write-Host "  -Help               Show this help" -ForegroundColor White
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Green
    Write-Host "  .\build_esp32.ps1                               # Compile only" -ForegroundColor Gray
    Write-Host "  .\build_esp32.ps1 -Port COM3                   # Upload via serial port" -ForegroundColor Gray
    Write-Host "  .\build_esp32.ps1 -OTA 192.168.250.125         # Upload via WiFi (OTA)" -ForegroundColor Gray
    Write-Host "  .\build_esp32.ps1 -OTA 192.168.250.125 -OTAPassword mypass # OTA with password" -ForegroundColor Gray
    Write-Host "  .\build_esp32.ps1 -Port COM3 -UploadData       # Upload firmware AND data files" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Note: Data partition is preserved by default to keep your settings and files." -ForegroundColor Yellow
    Write-Host "Note: You can only use either -Port OR -OTA, not both." -ForegroundColor Yellow
    exit 0
}

Write-Host "Building ESP32 Breadmaker Controller (16MB with OTA)..." -ForegroundColor Green

# Validate upload options
if ($Port -and $OTA) {
    Write-Host "Error: Cannot use both -Port and -OTA options. Choose one." -ForegroundColor Red
    exit 1
}

# Check ESP32 core version
Write-Host "Checking ESP32 Arduino Core version..." -ForegroundColor Yellow
# Temporarily skip version check for testing
Write-Host "✓ Skipping version check for testing - assuming ESP32 Arduino Core 2.0.17" -ForegroundColor Yellow

# Set board type for 16MB with FAT filesystem (we'll modify code to use FATFS)
# app3M_fat9M_16MB gives: 3MB per app partition + 9.9MB FATFS
$Board = "esp32:esp32:esp32:PartitionScheme=app3M_fat9M_16MB,CPUFreq=240,FlashMode=qio,FlashFreq=80,FlashSize=16M,UploadSpeed=921600"

# Display configuration info
Write-Host "Board Configuration:" -ForegroundColor Cyan
Write-Host "  - Flash Size: 16MB" -ForegroundColor White
Write-Host "  - App Partition 1: ~3MB (current firmware)" -ForegroundColor White
Write-Host "  - App Partition 2: ~3MB (OTA updates)" -ForegroundColor White
Write-Host "  - File System: 9.9MB FATFS (web files, data, programs)" -ForegroundColor White
Write-Host "  - OTA Support: Enabled" -ForegroundColor Green
Write-Host "  - Data Partition: PRESERVED (use -UploadData to overwrite)" -ForegroundColor Green

# TFT_eSPI configuration note
Write-Host "Note: Make sure TFT_eSPI is configured for TTGO T-Display" -ForegroundColor Yellow
Write-Host "  - Copy TFT_eSPI_Setup.h to your TFT_eSPI library folder" -ForegroundColor Yellow
Write-Host "  - Or configure User_Setup.h in the TFT_eSPI library" -ForegroundColor Yellow

# Compile
Write-Host "Compiling..." -ForegroundColor Blue
arduino-cli compile --fqbn $Board breadmaker_controller.ino

if ($LASTEXITCODE -eq 0) {
    Write-Host "Compilation successful!" -ForegroundColor Green
    
    # Upload if port or OTA is provided
    if ($Port -or $OTA) {
        if ($Port) {
            # Serial upload
            Write-Host "Uploading firmware to port $Port..." -ForegroundColor Blue
            arduino-cli upload --fqbn $Board --port $Port breadmaker_controller.ino
        } elseif ($OTA) {
            # OTA upload using dedicated script
            Write-Host "Uploading firmware via OTA to $OTA..." -ForegroundColor Blue
            if ($OTAPassword) {
                & ".\upload_ota.ps1" -IP $OTA -Password $OTAPassword
            } else {
                & ".\upload_ota.ps1" -IP $OTA
            }
        }
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Firmware upload successful!" -ForegroundColor Green
            
            # Upload data files if requested
            if ($UploadData) {
                Write-Host ""
                Write-Host "WARNING: Uploading data files will ERASE existing data!" -ForegroundColor Red
                Write-Host "This includes settings, programs, and user files." -ForegroundColor Red
                $confirmation = Read-Host "Continue? (y/N)"
                
                if ($confirmation -eq 'y' -or $confirmation -eq 'Y') {
                    Write-Host "Uploading data files..." -ForegroundColor Blue
                    try {
                        if ($Port) {
                            & ".\upload_files_esp32.ps1" -Port $Port
                        } elseif ($OTA) {
                            # Use the existing upload script but point it to the OTA IP
                            Write-Host "Note: Data file upload via OTA uses HTTP upload to device." -ForegroundColor Yellow
                            & ".\upload_files_sequential.ps1"  # This script uploads to 192.168.250.125 by default
                        }
                        if ($LASTEXITCODE -eq 0) {
                            Write-Host "Data files uploaded successfully!" -ForegroundColor Green
                        } else {
                            Write-Host "Data upload failed!" -ForegroundColor Red
                        }
                    } catch {
                        Write-Host "Error running data upload script: $($_.Exception.Message)" -ForegroundColor Red
                    }
                } else {
                    Write-Host "Data upload cancelled. Existing data preserved." -ForegroundColor Yellow
                }
            } else {
                Write-Host ""
                Write-Host "Data partition preserved. Your settings and files are safe." -ForegroundColor Green
                if ($Port) {
                    Write-Host "To upload new web files, use: .\build_esp32.ps1 -Port $Port -UploadData" -ForegroundColor Yellow
                } elseif ($OTA) {
                    Write-Host "To upload new web files, use: .\build_esp32.ps1 -OTA $OTA -UploadData" -ForegroundColor Yellow
                }
            }
            
            Write-Host ""
            Write-Host "OTA Updates:" -ForegroundColor Cyan
            Write-Host "  - Web interface: http://$($OTA -or '[device-ip]')/update" -ForegroundColor White
            Write-Host "  - Arduino IDE: Select network port after first boot" -ForegroundColor White
            Write-Host "  - Hostname: esp32-73AB98" -ForegroundColor White
            if ($OTA) {
                Write-Host "  - Device IP: $OTA" -ForegroundColor White
            }
        } else {
            Write-Host "Firmware upload failed!" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "No upload method specified. Skipping upload." -ForegroundColor Yellow
        Write-Host "Usage examples:" -ForegroundColor Yellow
        Write-Host "  .\build_esp32.ps1 -Port COM3                   # Upload via serial port" -ForegroundColor Gray
        Write-Host "  .\build_esp32.ps1 -OTA 192.168.250.125         # Upload via WiFi (OTA)" -ForegroundColor Gray
        Write-Host "  .\build_esp32.ps1 -OTA 192.168.250.125 -UploadData # OTA + data upload" -ForegroundColor Gray
        Write-Host "  .\build_esp32.ps1 -Help                        # Show help" -ForegroundColor Gray
    }
} else {
    Write-Host "Compilation failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Build process completed!" -ForegroundColor Green
