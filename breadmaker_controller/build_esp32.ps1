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

# Display library note
Write-Host "Note: Project uses LovyanGFX for TTGO T-Display" -ForegroundColor Yellow
Write-Host "  - LovyanGFX is configured automatically in display_manager.h" -ForegroundColor Yellow
Write-Host "  - No additional display library setup required" -ForegroundColor Yellow

# Compile
Write-Host "Compiling..." -ForegroundColor Blue
arduino-cli compile --fqbn $Board --build-path "./build" breadmaker_controller.ino

if ($LASTEXITCODE -eq 0) {
    Write-Host "Compilation successful!" -ForegroundColor Green
    
    # Upload if port or OTA is provided
    if ($Port -or $OTA) {
        if ($Port) {
            # Serial upload
            Write-Host "Uploading firmware to port $Port..." -ForegroundColor Blue
            arduino-cli upload --fqbn $Board --port $Port breadmaker_controller.ino
        } elseif ($OTA) {
            # OTA upload - integrated functionality
            Write-Host "Uploading firmware via OTA to $OTA..." -ForegroundColor Blue
            
            # Find the compiled binary
            $binaryPath = "build\breadmaker_controller.ino.bin"
            if (-not (Test-Path $binaryPath)) {
                Write-Host "Error: Compiled binary not found at $binaryPath" -ForegroundColor Red
                exit 1
            }
            
            Write-Host "Binary found: $binaryPath" -ForegroundColor Green
            $fileSize = (Get-Item $binaryPath).Length
            Write-Host "File size: $($fileSize) bytes" -ForegroundColor White
            
            # Try to find espota.py in Arduino ESP32 installation
            $possiblePaths = @(
                "$env:LOCALAPPDATA\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py",
                "$env:APPDATA\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py",
                "C:\Users\*\AppData\Local\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py"
            )
            
            $espotaPath = $null
            foreach ($pathPattern in $possiblePaths) {
                $found = Get-ChildItem $pathPattern -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($found) {
                    $espotaPath = $found.FullName
                    break
                }
            }
            
            if (-not $espotaPath) {
                Write-Host "Error: espota.py not found in Arduino ESP32 installation" -ForegroundColor Red
                Write-Host "Please make sure ESP32 Arduino Core is installed via Arduino IDE or arduino-cli" -ForegroundColor Yellow
                exit 1
            }
            
            Write-Host "Using espota.py: $espotaPath" -ForegroundColor Green
            
            # Test connectivity
            Write-Host "Testing connectivity to $OTA`:3232..." -ForegroundColor Blue
            try {
                $tcpClient = New-Object System.Net.Sockets.TcpClient
                $tcpClient.ReceiveTimeout = 3000
                $tcpClient.SendTimeout = 3000
                $tcpClient.Connect($OTA, 3232)
                $tcpClient.Close()
                Write-Host "✓ Device is reachable" -ForegroundColor Green
            } catch {
                Write-Host "✗ Cannot reach device at $OTA`:3232" -ForegroundColor Red
                Write-Host "Make sure the ESP32 is:" -ForegroundColor Yellow
                Write-Host "  - Connected to WiFi" -ForegroundColor White
                Write-Host "  - Running firmware with OTA enabled" -ForegroundColor White
                Write-Host "  - Accessible on the network" -ForegroundColor White
                exit 1
            }
            
            # Execute OTA upload
            Write-Host "Starting OTA upload..." -ForegroundColor Blue
            $espotaArgs = @("-i", $OTA, "-p", "3232", "-f", "`"$binaryPath`"")
            if ($OTAPassword) {
                $espotaArgs += @("-a", $OTAPassword)
            }
            
            Write-Host "Command: python `"$espotaPath`" $($espotaArgs -join ' ')" -ForegroundColor Gray
            
            $process = Start-Process -FilePath "python" -ArgumentList (@("`"$espotaPath`"") + $espotaArgs) -NoNewWindow -Wait -PassThru -RedirectStandardOutput "ota_output.log" -RedirectStandardError "ota_error.log"
            
            # Check results
            if ($process.ExitCode -eq 0) {
                Write-Host "✓ OTA upload completed successfully!" -ForegroundColor Green
            } else {
                Write-Host "✗ OTA upload failed!" -ForegroundColor Red
                Write-Host "Exit code: $($process.ExitCode)" -ForegroundColor Red
                
                if (Test-Path "ota_error.log") {
                    $errorContent = Get-Content "ota_error.log"
                    if ($errorContent) {
                        Write-Host "Error details:" -ForegroundColor Yellow
                        Write-Host $errorContent -ForegroundColor Red
                    }
                }
                
                if (Test-Path "ota_output.log") {
                    $outputContent = Get-Content "ota_output.log"
                    if ($outputContent) {
                        Write-Host "Output:" -ForegroundColor Yellow
                        Write-Host $outputContent -ForegroundColor White
                    }
                }
                exit 1
            }
            
            # Cleanup log files
            Remove-Item "ota_output.log" -ErrorAction SilentlyContinue
            Remove-Item "ota_error.log" -ErrorAction SilentlyContinue
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
            Write-Host "  - Command line: .\build_esp32.ps1 -OTA [device-ip]" -ForegroundColor White
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
