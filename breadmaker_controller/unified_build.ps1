# Unified Build and Upload Script for ESP32 Breadmaker Controller
# Consolidates all build, upload, and maintenance operations
#
# Usage Examples:
#   .\unified_build.ps1 -Build                           # Compile only
#   .\unified_build.ps1 -Build -WebOTA                   # Build and upload via web OTA (recommended)
#   .\unified_build.ps1 -Build -Serial COM3              # Build and upload via serial
#   .\unified_build.ps1 -UploadData                      # Upload data files only
#   .\unified_build.ps1 -Build -WebOTA -UploadData       # Build firmware and upload everything
#   .\unified_build.ps1 -Clean                           # Clean build artifacts
#   .\unified_build.ps1 -Test                            # Run API tests without building
#   .\unified_build.ps1 -Help                            # Show detailed help

param(
    [switch]$Build,           # Compile firmware
    [switch]$WebOTA,          # Upload firmware via web OTA (preferred)
    [string]$Serial = "",     # Upload firmware via serial port (e.g., "COM3")
    [switch]$UploadData,      # Upload data files (web UI, programs, etc.)
    [switch]$Clean,           # Clean build artifacts
    [switch]$Test,            # Run basic API tests
    [switch]$Help,            # Show help
    [string]$IP = "192.168.250.125"  # Device IP address
)

# Load required assemblies
Add-Type -AssemblyName System.Web

# Configuration
$DEVICE_IP = $IP
$BUILD_SCRIPT = ".\build_esp32.ps1"
$WEB_OTA_ENDPOINT = "http://$DEVICE_IP/api/update"
$FIRMWARE_FILE = "build\breadmaker_controller.ino.bin"

# Colors for output
function Write-Success($message) { Write-Host "✅ $message" -ForegroundColor Green }
function Write-Error($message) { Write-Host "❌ $message" -ForegroundColor Red }
function Write-Warning($message) { Write-Host "⚠️ $message" -ForegroundColor Yellow }
function Write-Info($message) { Write-Host "ℹ️ $message" -ForegroundColor Cyan }

function Show-Help {
    Write-Host @"
=== ESP32 Breadmaker Controller - Unified Build Tool ===

BASIC OPERATIONS:
  -Build                    Compile firmware only
  -WebOTA                   Upload firmware via web endpoint (recommended)
  -Serial COM3              Upload firmware via serial port
  -UploadData               Upload data files (HTML, JS, programs)
  -Clean                    Remove build artifacts
  -Test                     Run API connectivity tests

COMMON COMBINATIONS:
  .\unified_build.ps1 -Build
  # Compile firmware and check for errors

  .\unified_build.ps1 -Build -WebOTA
  # Build and upload firmware via web (most reliable)

  .\unified_build.ps1 -UploadData
  # Upload only web files and data

  .\unified_build.ps1 -Build -WebOTA -UploadData
  # Full deployment: build firmware and upload everything

  .\unified_build.ps1 -Build -Serial COM3
  # Build and upload via serial (if connected)

  .\unified_build.ps1 -Test
  # Test device connectivity and API endpoints

DEVICE INFO:
  IP Address: $DEVICE_IP
  Web Interface: http://$DEVICE_IP/
  OTA Endpoint: $WEB_OTA_ENDPOINT
  
NOTES:
  - Web OTA is preferred over serial (device is wireless-only)
  - ArduinoOTA port 3232 is unreliable, use web endpoint
  - Always build before uploading to catch compilation errors
  - Device uses FFat filesystem (not LittleFS)

"@ -ForegroundColor White
}

function Test-DeviceConnectivity {
    Write-Info "Testing device connectivity..."
    
    try {
        $response = Invoke-WebRequest -Uri "http://$DEVICE_IP/api/status" -TimeoutSec 5 -UseBasicParsing
        if ($response.StatusCode -eq 200) {
            Write-Success "Device is online at $DEVICE_IP"
            
            # Test key endpoints
            Write-Info "Testing API endpoints..."
            
            $settings = curl -s "http://$DEVICE_IP/api/settings" 2>$null
            if ($settings) { Write-Success "✓ /api/settings: $settings" }
            else { Write-Warning "✗ /api/settings failed" }
            
            $programs = curl -s "http://$DEVICE_IP/api/programs" 2>$null
            if ($programs) { Write-Success "✓ /api/programs: $programs" }
            else { Write-Warning "✗ /api/programs failed" }
            
            return $true
        }
    }
    catch {
        Write-Error "Device not reachable at $DEVICE_IP"
        Write-Info "Check device power and network connection"
        return $false
    }
}

function Build-Firmware {
    Write-Info "Building ESP32 firmware..."
    
    if (-not (Test-Path $BUILD_SCRIPT)) {
        Write-Error "Build script not found: $BUILD_SCRIPT"
        return $false
    }
    
    try {
        & $BUILD_SCRIPT
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Firmware compiled successfully"
            if (Test-Path $FIRMWARE_FILE) {
                $size = (Get-Item $FIRMWARE_FILE).Length
                Write-Info "Firmware size: $([math]::Round($size/1KB, 1)) KB"
                return $true
            }
        }
        else {
            Write-Error "Compilation failed with exit code $LASTEXITCODE"
            return $false
        }
    }
    catch {
        Write-Error "Build process failed: $($_.Exception.Message)"
        return $false
    }
}

function Upload-FirmwareWeb {
    Write-Info "Uploading firmware via web OTA..."
    
    if (-not (Test-Path $FIRMWARE_FILE)) {
        Write-Error "Firmware file not found: $FIRMWARE_FILE"
        Write-Info "Run with -Build first to compile firmware"
        return $false
    }
    
    if (-not (Test-DeviceConnectivity)) {
        return $false
    }
    
    try {
        Write-Info "Uploading to $WEB_OTA_ENDPOINT..."
        $result = curl -X POST -F "update=@$FIRMWARE_FILE" $WEB_OTA_ENDPOINT 2>&1
        
        if ($result -match "Update successful") {
            Write-Success "Firmware uploaded successfully!"
            Write-Info "Device is rebooting..."
            
            # Wait for reboot
            Start-Sleep -Seconds 15
            
            if (Test-DeviceConnectivity) {
                Write-Success "Device restarted and is online"
                return $true
            }
            else {
                Write-Warning "Device uploaded but not yet responding"
                return $true
            }
        }
        else {
            Write-Error "Upload failed: $result"
            return $false
        }
    }
    catch {
        Write-Error "Upload failed: $($_.Exception.Message)"
        return $false
    }
}

function Upload-FirmwareSerial {
    param([string]$port)
    
    Write-Info "Uploading firmware via serial port $port..."
    
    if (-not (Test-Path $FIRMWARE_FILE)) {
        Write-Error "Firmware file not found: $FIRMWARE_FILE"
        return $false
    }
    
    try {
        & $BUILD_SCRIPT -Port $port
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Serial upload completed"
            return $true
        }
        else {
            Write-Error "Serial upload failed"
            return $false
        }
    }
    catch {
        Write-Error "Serial upload error: $($_.Exception.Message)"
        return $false
    }
}

function Upload-DataFiles {
    Write-Info "Uploading data files..."
    
    if (-not (Test-DeviceConnectivity)) {
        return $false
    }
    
    # Upload critical data files
    $dataFiles = @(
        "data\programs_index.json",
        "data\index.html",
        "data\script.js",
        "data\programs.html",
        "data\programs.js",
        "data\style.css"
    )
    
    $uploadCount = 0
    $totalFiles = $dataFiles.Count
    
    foreach ($file in $dataFiles) {
        if (Test-Path $file) {
            $filename = Split-Path $file -Leaf
            Write-Info "Uploading $filename..."
            
            try {
                $content = Get-Content $file -Raw -Encoding UTF8
                $encodedContent = [System.Web.HttpUtility]::UrlEncode($content)
                
                $result = curl -s -X POST -H "Content-Type: application/x-www-form-urlencoded" `
                    -d "filename=$filename&content=$encodedContent" `
                    "http://$DEVICE_IP/debug/upload" 2>$null
                
                if ($result -match "uploaded") {
                    Write-Success "✓ $filename uploaded"
                    $uploadCount++
                }
                else {
                    Write-Warning "✗ $filename upload may have failed"
                }
            }
            catch {
                Write-Warning "✗ Failed to upload $filename"
            }
        }
        else {
            Write-Warning "✗ File not found: $file"
        }
    }
    
    Write-Info "Uploaded $uploadCount of $totalFiles files"
    return ($uploadCount -gt 0)
}

function Clean-Build {
    Write-Info "Cleaning build artifacts..."
    
    $pathsToClean = @(
        "build",
        "*.bin",
        "*.elf",
        "*.map"
    )
    
    foreach ($path in $pathsToClean) {
        if (Test-Path $path) {
            Remove-Item $path -Recurse -Force
            Write-Success "Removed: $path"
        }
    }
    
    Write-Success "Build cleanup completed"
}

# Main execution logic
if ($Help) {
    Show-Help
    exit 0
}

if (-not ($Build -or $WebOTA -or $Serial -or $UploadData -or $Clean -or $Test)) {
    Write-Warning "No action specified. Use -Help for usage information."
    exit 1
}

Write-Host "=== ESP32 Breadmaker Controller Build Tool ===" -ForegroundColor Cyan
Write-Info "Target device: $DEVICE_IP"

$success = $true

# Clean operation
if ($Clean) {
    Clean-Build
}

# Build operation
if ($Build) {
    if (-not (Build-Firmware)) {
        $success = $false
    }
}

# Upload operations
if ($WebOTA -and $success) {
    if (-not (Upload-FirmwareWeb)) {
        $success = $false
    }
}

if ($Serial -and $success) {
    if (-not (Upload-FirmwareSerial -port $Serial)) {
        $success = $false
    }
}

if ($UploadData -and $success) {
    if (-not (Upload-DataFiles)) {
        $success = $false
    }
}

# Test operation
if ($Test) {
    Test-DeviceConnectivity | Out-Null
}

# Final status
Write-Host "`n=== Operation Complete ===" -ForegroundColor Cyan
if ($success) {
    Write-Success "All operations completed successfully"
    
    if ($WebOTA -or $Serial) {
        Write-Info "Device should be running new firmware"
        Write-Info "Web interface: http://$DEVICE_IP/"
    }
    
    exit 0
}
else {
    Write-Error "Some operations failed"
    exit 1
}
