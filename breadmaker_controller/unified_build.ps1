# Unified Build and Upload Script for ESP32 Breadmaker Controller
# Consolidates all build, upload, and maintenance operations

param(
    [switch]$Build,           # Compile firmware
    [switch]$WebOTA,          # Upload firmware via web OTA (preferred)
    [string]$Serial = "",     # Upload firmware via serial port (e.g., "COM3")
    [switch]$UploadData,      # Upload data files (web UI, programs, etc.)
    [string[]]$Files = @(),   # Specific files to upload (optional, used with -UploadData)
    [switch]$Clean,           # Clean build artifacts
    [switch]$Test,            # Run basic API tests
    [switch]$Help,            # Show help
    [string]$IP = "192.168.250.125"  # Device IP address
)

# Configuration
$DEVICE_IP = $IP
$WEB_OTA_ENDPOINT = "http://$DEVICE_IP/api/ota/upload"
$FIRMWARE_FILE = "build\breadmaker_controller.ino.bin"
$ESP32_FQBN = "esp32:esp32:esp32"
$BUILD_PATH = "build"

# Colors for output
function Write-Success($message) { Write-Host "✅ $message" -ForegroundColor Green }
function Write-Error($message) { Write-Host "❌ $message" -ForegroundColor Red }
function Write-Warning($message) { Write-Host "⚠️ $message" -ForegroundColor Yellow }
function Write-Info($message) { Write-Host "ℹ️ $message" -ForegroundColor Cyan }

function Show-Help {
    Write-Host "=== ESP32 Breadmaker Controller - Unified Build Tool ===" -ForegroundColor White
    Write-Host ""
    Write-Host "BASIC OPERATIONS:" -ForegroundColor Yellow
    Write-Host "  -Build                    Compile firmware only"
    Write-Host "  -WebOTA                   Upload firmware via web endpoint recommended"
    Write-Host "  -Serial COM3              Upload firmware via serial port if connected"
    Write-Host "  -UploadData               Upload data files HTML, JS, programs"
    Write-Host "  -Files file1,file2        Upload specific files only with -UploadData"
    Write-Host "  -Clean                    Remove build artifacts"
    Write-Host "  -Test                     Run API connectivity tests"
    Write-Host ""
    Write-Host "COMMON COMBINATIONS:" -ForegroundColor Yellow
    Write-Host "  .\unified_build.ps1 -Build"
    Write-Host "  # Compile firmware and check for errors"
    Write-Host ""
    Write-Host "  .\unified_build.ps1 -Build -WebOTA"
    Write-Host "  # Build and upload firmware via web (most reliable)"
    Write-Host ""
    Write-Host "  .\unified_build.ps1 -UploadData"
    Write-Host "  # Upload only web files and data"
    Write-Host ""
    Write-Host "  .\unified_build.ps1 -Build -WebOTA -UploadData"
    Write-Host "  # Full deployment: build firmware and upload everything"
    Write-Host ""
    Write-Host "DEVICE INFO:" -ForegroundColor Yellow
    Write-Host "  IP Address: $DEVICE_IP"
    Write-Host "  Web Interface: http://$DEVICE_IP/"
    Write-Host "  OTA Endpoint: $WEB_OTA_ENDPOINT"
    Write-Host ""
    Write-Host "NOTES:" -ForegroundColor Yellow
    Write-Host "  • Web OTA is preferred over serial (device is wireless-only)"
    Write-Host "  • ArduinoOTA port 3232 is unreliable, use web endpoint"
    Write-Host "  • Always build before uploading to catch compilation errors"
    Write-Host "  • Device uses FFat filesystem (not LittleFS)"
}

function Test-DeviceConnectivity {
    Write-Info "Testing device connectivity..."
    
    try {
        $response = Invoke-WebRequest -Uri "http://$DEVICE_IP/api/status" -TimeoutSec 5 -UseBasicParsing
        if ($response.StatusCode -eq 200) {
            Write-Success "Device is online at $DEVICE_IP"
            
            # Parse JSON response to get device info
            $status = $response.Content | ConvertFrom-Json
            Write-Info "Firmware: $($status.firmware_version)"
            Write-Info "Build Date: $($status.build_date)"
            Write-Info "Free Heap: $($status.free_heap) bytes"
            Write-Info "Uptime: $($status.uptime_sec) seconds"
            
            return $true
        }
    }
    catch {
        Write-Error "Device is not reachable at $DEVICE_IP"
        Write-Error "Error: $($_.Exception.Message)"
        return $false
    }
}

function Build-Firmware {
    Write-Info "Building ESP32 firmware..."
    
    # Ensure build directory exists
    if (!(Test-Path $BUILD_PATH)) {
        New-Item -ItemType Directory -Path $BUILD_PATH -Force | Out-Null
    }
    
    # Build the firmware
    $buildArgs = @(
        "compile",
        "--fqbn", $ESP32_FQBN,
        "--build-path", $BUILD_PATH,
        "--verbose",
        "breadmaker_controller.ino"
    )
    
    Write-Info "Running: arduino-cli $($buildArgs -join ' ')"
    
    $process = Start-Process -FilePath "arduino-cli" -ArgumentList $buildArgs -Wait -PassThru -NoNewWindow
    
    if ($process.ExitCode -eq 0) {
        Write-Success "Firmware build completed successfully"
        
        # Check if binary file exists
        if (Test-Path $FIRMWARE_FILE) {
            $fileSize = (Get-Item $FIRMWARE_FILE).Length
            Write-Success "Binary created: $FIRMWARE_FILE ($fileSize bytes)"
            return $true
        } else {
            Write-Error "Binary file not found: $FIRMWARE_FILE"
            return $false
        }
    } else {
        Write-Error "Firmware build failed (exit code: $($process.ExitCode))"
        return $false
    }
}

function Upload-FirmwareOTA {
    param(
        [switch]$UsePowerShell  # Use PowerShell method instead of curl
    )
    
    Write-Info "Uploading firmware via Web OTA..."
    
    if (!(Test-Path $FIRMWARE_FILE)) {
        Write-Error "Firmware binary not found: $FIRMWARE_FILE"
        Write-Error "Run with -Build first to compile the firmware"
        return $false
    }
    
    if ($UsePowerShell) {
        # PowerShell multipart form upload method
        try {
            Write-Info "Uploading to: $WEB_OTA_ENDPOINT"
            Write-Info "Using PowerShell multipart form upload..."
            
            # Create form data for the file upload
            $boundary = [System.Guid]::NewGuid().ToString()
            $fileBinary = [System.IO.File]::ReadAllBytes($FIRMWARE_FILE)
            
            $body = @"
--$boundary
Content-Disposition: form-data; name="file"; filename="firmware.bin"
Content-Type: application/octet-stream

"@
            $bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($body)
            $bodyBytes += $fileBinary
            $bodyBytes += [System.Text.Encoding]::UTF8.GetBytes("`r`n--$boundary--`r`n")
            
            $response = Invoke-WebRequest -Uri $WEB_OTA_ENDPOINT -Method POST -Body $bodyBytes -ContentType "multipart/form-data; boundary=$boundary" -TimeoutSec 60
            
            if ($response.StatusCode -eq 200) {
                Write-Success "Firmware uploaded successfully via PowerShell"
                Write-Success "Device should be rebooting now..."
                
                # Wait for device to reboot
                Write-Info "Waiting for device to restart..."
                Start-Sleep -Seconds 15
                
                # Test if device is back online
                $retryCount = 0
                $maxRetries = 6
                do {
                    Start-Sleep -Seconds 5
                    $retryCount++
                    Write-Info "Checking if device is back online (attempt $retryCount/$maxRetries)..."
                    if (Test-DeviceConnectivity) {
                        Write-Success "Device is back online after firmware update"
                        return $true
                    }
                } while ($retryCount -lt $maxRetries)
                
                Write-Warning "Device may still be restarting or upload may have failed"
                return $false
            } else {
                Write-Error "Upload failed with status: $($response.StatusCode)"
                return $false
            }
        }
        catch {
            Write-Error "PowerShell upload failed: $($_.Exception.Message)"
            Write-Info "Falling back to curl method..."
            # Fall through to curl method
        }
    }
    
    # Curl binary upload method (default)
    try {
        Write-Info "Uploading to: $WEB_OTA_ENDPOINT"
        Write-Info "Using curl for multipart form upload..."
        
        # Use curl with proper multipart form upload (not raw binary)
        $curlArgs = @(
            "-v"
            "-X", "POST"
            "-F", "file=@$FIRMWARE_FILE"
            "--max-time", "120"
            "--connect-timeout", "10"
            "--retry", "2"
            "--retry-delay", "5"
            $WEB_OTA_ENDPOINT
        )
        
        Write-Info "Running: curl $($curlArgs -join ' ')"
        
        # Execute curl command
        $curlResult = & curl @curlArgs 2>&1
        $curlExitCode = $LASTEXITCODE
        
        if ($curlExitCode -eq 0) {
            Write-Success "Firmware uploaded successfully via curl"
            Write-Success "Device should be rebooting now..."
            
            # Wait for device to reboot
            Write-Info "Waiting for device to restart..."
            Start-Sleep -Seconds 15
            
            # Test if device is back online
            $retryCount = 0
            $maxRetries = 6
            do {
                Start-Sleep -Seconds 5
                $retryCount++
                Write-Info "Checking if device is back online (attempt $retryCount/$maxRetries)..."
                if (Test-DeviceConnectivity) {
                    Write-Success "Device is back online after firmware update"
                    return $true
                }
            } while ($retryCount -lt $maxRetries)
            
            Write-Warning "Device may still be restarting or upload may have failed"
            return $false
        } else {
            Write-Error "Upload failed with curl exit code: $curlExitCode"
            Write-Error "Curl output: $curlResult"
            return $false
        }
    }
    catch {
        Write-Error "Failed to upload firmware: $($_.Exception.Message)"
        return $false
    }
}

function Upload-FirmwareSerial {
    param(
        [string]$Port = $null
    )
    
    Write-Info "Uploading firmware via Serial..."
    
    if (!(Test-Path $FIRMWARE_FILE)) {
        Write-Error "Firmware binary not found: $FIRMWARE_FILE"
        Write-Error "Run with -Build first to compile the firmware"
        return $false
    }
    
    # Try to find esptool
    $esptoolPath = $null
    $possiblePaths = @(
        "esptool.py",
        "esptool",
        "$env:USERPROFILE\.platformio\penv\Scripts\esptool.py",
        "$env:USERPROFILE\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\*\esptool.py"
    )
    
    foreach ($path in $possiblePaths) {
        if ($path -like "*\*\*") {
            # Handle wildcard paths
            $resolvedPaths = Get-ChildItem -Path ($path -replace "\*", "*") -ErrorAction SilentlyContinue
            if ($resolvedPaths) {
                $esptoolPath = $resolvedPaths | Select-Object -First 1 -ExpandProperty FullName
                break
            }
        } elseif (Get-Command $path -ErrorAction SilentleContinue) {
            $esptoolPath = $path
            break
        }
    }
    
    if (-not $esptoolPath) {
        Write-Error "esptool not found. Please install esptool or use -WebOTA instead"
        Write-Info "Install with: pip install esptool"
        return $false
    }
    
    Write-Info "Found esptool at: $esptoolPath"
    
    # Auto-detect COM port if not specified
    if (-not $Port) {
        Write-Info "Auto-detecting ESP32 COM port..."
        try {
            $ports = Get-CimInstance -Class Win32_PnPEntity | Where-Object { 
                $_.Name -match "Silicon Labs CP210x|USB-SERIAL CH340|USB Serial Port|Arduino" -and 
                $_.Name -match "COM\d+" 
            }
            
            if ($ports) {
                $Port = ($ports[0].Name -replace ".*\((COM\d+)\).*", '$1')
                Write-Info "Detected ESP32 on port: $Port"
            } else {
                Write-Error "No ESP32 COM port detected. Please specify -Port parameter"
                return $false
            }
        }
        catch {
            Write-Error "Failed to detect COM port: $($_.Exception.Message)"
            return $false
        }
    }
    
    try {
        Write-Info "Uploading to ESP32 on $Port using esptool..."
        
        # ESP32 partition addresses (standard for most ESP32 boards)
        $bootloaderAddr = "0x1000"
        $partitionAddr = "0x8000"
        $firmwareAddr = "0x10000"
        
        # Build esptool command
        $esptoolArgs = @(
            "--chip", "esp32"
            "--port", $Port
            "--baud", "921600"
            "--before", "default_reset"
            "--after", "hard_reset"
            "write_flash"
            "-z"
            "--flash_mode", "dio"
            "--flash_freq", "80m"
            "--flash_size", "4MB"
            $firmwareAddr, $FIRMWARE_FILE
        )
        
        Write-Info "Running esptool command..."
        
        if ($esptoolPath -like "*.py") {
            $result = & python $esptoolPath @esptoolArgs 2>&1
        } else {
            $result = & $esptoolPath @esptoolArgs 2>&1
        }
        
        $exitCode = $LASTEXITCODE
        
        if ($exitCode -eq 0) {
            Write-Success "Firmware uploaded successfully via Serial"
            Write-Info "ESP32 should be restarting now..."
            return $true
        } else {
            Write-Error "Serial upload failed with exit code: $exitCode"
            Write-Error "Output: $result"
            return $false
        }
    }
    catch {
        Write-Error "Serial upload failed: $($_.Exception.Message)"
        return $false
    }
}

function Upload-DataFiles {
    param([string[]]$SpecificFiles = @())
    
    Write-Info "Uploading data files..."
    
    $dataPath = "data"
    if (!(Test-Path $dataPath)) {
        Write-Error "Data directory not found: $dataPath"
        return $false
    }
    
    # Get list of files to upload
    $filesToUpload = @()
    
    if ($SpecificFiles.Count -gt 0) {
        # Upload specific files
        foreach ($file in $SpecificFiles) {
            $fullPath = Join-Path $dataPath $file
            if (Test-Path $fullPath) {
                $filesToUpload += $fullPath
            } else {
                Write-Warning "File not found: $fullPath"
            }
        }
    } else {
        # Upload all files in data directory
        $filesToUpload = Get-ChildItem -Path $dataPath -File -Recurse | ForEach-Object { $_.FullName }
    }
    
    if ($filesToUpload.Count -eq 0) {
        Write-Warning "No files to upload"
        return $true
    }
    
    Write-Info "Uploading $($filesToUpload.Count) files..."
    
    $uploadEndpoint = "http://$DEVICE_IP/upload"
    $successCount = 0
    
    foreach ($filePath in $filesToUpload) {
        $fileName = Split-Path $filePath -Leaf
        
        try {
            # Create multipart form data
            $boundary = [System.Guid]::NewGuid().ToString()
            $fileBinary = [System.IO.File]::ReadAllBytes($filePath)
            
            $body = @"
--$boundary
Content-Disposition: form-data; name="file"; filename="$fileName"
Content-Type: application/octet-stream

"@
            $bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($body)
            $bodyBytes += $fileBinary
            $bodyBytes += [System.Text.Encoding]::UTF8.GetBytes("`r`n--$boundary--`r`n")
            
            $response = Invoke-WebRequest -Uri $uploadEndpoint -Method POST -Body $bodyBytes -ContentType "multipart/form-data; boundary=$boundary" -TimeoutSec 10
            
            if ($response.StatusCode -eq 200) {
                Write-Success "Uploaded: $fileName"
                $successCount++
            } else {
                Write-Error "Failed to upload $fileName (Status: $($response.StatusCode))"
            }
        }
        catch {
            Write-Error "Failed to upload ${fileName}: $($_.Exception.Message)"
        }
    }
    
    Write-Success "Upload completed: $successCount/$($filesToUpload.Count) files uploaded successfully"
    return $successCount -eq $filesToUpload.Count
}

function Clean-Build {
    Write-Info "Cleaning build artifacts..."
    
    if (Test-Path $BUILD_PATH) {
        Remove-Item -Path $BUILD_PATH -Recurse -Force
        Write-Success "Build directory cleaned"
    } else {
        Write-Info "Build directory already clean"
    }
}

# Main execution logic
try {
    if ($Help) {
        Show-Help
        exit 0
    }
    
    if ($Clean) {
        Clean-Build
        exit 0
    }
    
    if ($Test) {
        $deviceOnline = Test-DeviceConnectivity
        if ($deviceOnline) {
            exit 0
        } else {
            exit 1
        }
    }
    
    # Build firmware if requested
    if ($Build) {
        $buildSuccess = Build-Firmware
        if (!$buildSuccess) {
            Write-Error "Build failed - aborting"
            exit 1
        }
    }
    
    # Upload firmware if requested
    if ($WebOTA) {
        if (!$Build) {
            Write-Warning "WebOTA requested without -Build. Using existing binary if available."
        }
        
        $uploadSuccess = Upload-FirmwareOTA
        if (!$uploadSuccess) {
            Write-Error "Firmware upload failed"
            exit 1
        }
    }
    
    # Upload via serial if requested
    if ($Serial -ne "") {
        $serialSuccess = Upload-FirmwareSerial -Port $Serial
        if (-not $serialSuccess) {
            Write-Error "Serial upload failed"
            exit 1
        }
    }
    
    # Upload data files if requested
    if ($UploadData) {
        $dataUploadSuccess = Upload-DataFiles -SpecificFiles $Files
        if (!$dataUploadSuccess) {
            Write-Error "Data file upload failed"
            exit 1
        }
    }
    
    # If no actions specified, show help
    if (!$Build -and !$WebOTA -and !$UploadData -and $Serial -eq "") {
        Write-Warning "No actions specified. Use -Help for usage information."
        Show-Help
        exit 0
    }
    
    Write-Success "All operations completed successfully!"
    exit 0
}
catch {
    Write-Error "Unexpected error: $($_.Exception.Message)"
    exit 1
}
