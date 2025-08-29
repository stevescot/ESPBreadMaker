# ESP32 Breadmaker Controller - Master Management Script
# Consolidates all build, upload, and maintenance operations

param(
    # Core Operations
    [switch]$Build,                    # Compile firmware
    [switch]$Upload,                   # Upload firmware (web OTA preferred)
    [string]$Serial = "",              # Upload firmware via serial port (e.g., "COM3")
    [switch]$UploadWeb,                # Upload web files only (safe - excludes programs)
    [switch]$UploadAll,                # Upload ALL files (DANGEROUS - includes programs)
    [string[]]$Files = @(),            # Specific files to upload
    
    # State Management
    [switch]$BackupState,              # Backup device state only
    [switch]$RestoreState,             # Restore device state from backup
    [switch]$PreserveState,            # Auto backup/restore during uploads
    [switch]$NoPreserveState,          # Disable state preservation
    
    # Maintenance
    [switch]$Clean,                    # Clean build artifacts
    [switch]$Test,                     # Run connectivity tests
    [switch]$Status,                   # Show device status
    [switch]$Monitor,                  # Monitor device in real-time
    
    # Configuration
    [string]$IP = "192.168.250.125",   # Device IP address
    [string]$Password = "",            # OTA password (if required)
    [switch]$Help                      # Show help
)

# Configuration
$DEVICE_IP = $IP
$BUILD_PATH = "build"
$DATA_PATH = "data"
$FIRMWARE_FILE = "$BUILD_PATH\breadmaker_controller.ino.bin"

# Colors and utilities
function Write-Success($msg) { Write-Host "✅ $msg" -ForegroundColor Green }
function Write-Error($msg) { Write-Host "❌ $msg" -ForegroundColor Red }
function Write-Warning($msg) { Write-Host "⚠️ $msg" -ForegroundColor Yellow }
function Write-Info($msg) { Write-Host "ℹ️ $msg" -ForegroundColor Cyan }
function Write-Step($msg) { Write-Host "🔄 $msg" -ForegroundColor Blue }

function Show-Help {
    Write-Host @"
=== ESP32 Breadmaker Controller - Master Manager ===

BASIC OPERATIONS:
  -Build                     Compile firmware only
  -Upload                    Upload firmware via web OTA (recommended)
  -Serial COM3               Upload firmware via serial port
  -UploadWeb                 Upload web files only (SAFE - preserves programs)
  -UploadAll                 Upload ALL files (DANGEROUS - overwrites programs)
  -Files file1,file2         Upload specific files only

STATE MANAGEMENT:
  -BackupState               Backup current device state to local files
  -RestoreState              Restore device state from backup
  -PreserveState             Auto backup/restore during operations
  -NoPreserveState           Disable automatic state preservation

MAINTENANCE:
  -Clean                     Remove build artifacts
  -Test                      Test device connectivity and API
  -Status                    Show current device status
  -Monitor                   Real-time monitoring of device

CONFIGURATION:
  -IP 192.168.x.x            Device IP address (default: 192.168.250.125)
  -Password <pass>           OTA password if required

COMMON WORKFLOWS:
  .\esp32-manager.ps1 -Build
  # Just compile and check for errors

  .\esp32-manager.ps1 -Build -Upload
  # Build and upload firmware (recommended)

  .\esp32-manager.ps1 -UploadWeb
  # Update web interface safely (preserves programs)

  .\esp32-manager.ps1 -Build -Upload -UploadWeb
  # Full update: firmware + web interface

  .\esp32-manager.ps1 -BackupState
  # Save current programs and settings

  .\esp32-manager.ps1 -Status -Monitor
  # Check status and monitor in real-time

SAFETY:
  - UploadWeb is SAFE and preserves your program settings
  - UploadAll is DANGEROUS and will overwrite device programs
  - State preservation is enabled by default for safety

"@ -ForegroundColor White
}

# Build firmware
function Invoke-Build {
    Write-Step "Building firmware..."
    
    if (!(Test-Path "breadmaker_controller.ino")) {
        Write-Error "Arduino sketch not found. Run from breadmaker_controller directory."
        return $false
    }
    
    # Ensure build directory exists
    if (!(Test-Path $BUILD_PATH)) {
        New-Item -ItemType Directory -Path $BUILD_PATH | Out-Null
    }
    
    # Run Arduino CLI build
    $buildCmd = "arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir `"$BUILD_PATH`" ."
    Write-Info "Running: $buildCmd"
    
    try {
        Invoke-Expression $buildCmd
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Firmware built successfully"
            return $true
        } else {
            Write-Error "Build failed with exit code $LASTEXITCODE"
            return $false
        }
    } catch {
        Write-Error "Build error: $($_.Exception.Message)"
        return $false
    }
}

# Upload firmware via web OTA
function Invoke-UploadFirmware {
    Write-Step "Uploading firmware via web OTA..."
    
    if (!(Test-Path $FIRMWARE_FILE)) {
        Write-Error "Firmware file not found: $FIRMWARE_FILE"
        Write-Info "Run with -Build first to compile firmware"
        return $false
    }
    
    try {
        $uploadUrl = "http://$DEVICE_IP/api/ota/upload"
        Write-Info "Uploading to: $uploadUrl"
        
        $result = curl -X POST -F "file=@$FIRMWARE_FILE" $uploadUrl
        
        if ($result -match "success|uploaded|ok") {
            Write-Success "Firmware uploaded successfully"
            Write-Info "Device will restart in ~10-30 seconds"
            return $true
        } else {
            Write-Error "Upload failed: $result"
            return $false
        }
    } catch {
        Write-Error "Upload error: $($_.Exception.Message)"
        return $false
    }
}

# Upload firmware via serial
function Invoke-UploadSerial {
    param([string]$Port)
    
    Write-Step "Uploading firmware via serial port $Port..."
    
    if (!(Test-Path $FIRMWARE_FILE)) {
        Write-Error "Firmware file not found: $FIRMWARE_FILE"
        return $false
    }
    
    try {
        $uploadCmd = "arduino-cli upload --fqbn esp32:esp32:esp32 --port $Port --input-dir `"$BUILD_PATH`""
        Write-Info "Running: $uploadCmd"
        
        Invoke-Expression $uploadCmd
        
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Serial upload completed"
            return $true
        } else {
            Write-Error "Serial upload failed"
            return $false
        }
    } catch {
        Write-Error "Serial upload error: $($_.Exception.Message)"
        return $false
    }
}

# Backup device state
function Invoke-BackupState {
    Write-Step "Backing up device state..."
    
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $backupDir = "device_backup_$timestamp"
    
    try {
        New-Item -ItemType Directory -Path $backupDir | Out-Null
        
        # Backup programs.json
        $programsUrl = "http://$DEVICE_IP/programs.json"
        $programsBackup = "$backupDir\programs.json"
        curl -s $programsUrl -o $programsBackup
        
        # Backup individual program files
        for ($i = 0; $i -lt 20; $i++) {
            $programUrl = "http://$DEVICE_IP/program_$i.json"
            $programBackup = "$backupDir\program_$i.json"
            
            try {
                $result = curl -s $programUrl
                if ($result -notmatch "404|Not Found") {
                    $result | Out-File -FilePath $programBackup -Encoding UTF8
                    Write-Info "Backed up program_$i.json"
                }
            } catch {
                # Program file doesn't exist, skip
            }
        }
        
        # Backup other state files
        $stateFiles = @("calibration.json", "settings.json", "state.json")
        foreach ($file in $stateFiles) {
            try {
                $stateUrl = "http://$DEVICE_IP/$file"
                $stateBackup = "$backupDir\$file"
                curl -s $stateUrl -o $stateBackup
                Write-Info "Backed up $file"
            } catch {
                Write-Warning "Could not backup $file (may not exist)"
            }
        }
        
        Write-Success "State backed up to: $backupDir"
        return $backupDir
    } catch {
        Write-Error "Backup failed: $($_.Exception.Message)"
        return $null
    }
}

# Restore device state
function Invoke-RestoreState {
    param([string]$BackupDir = "")
    
    if ($BackupDir -eq "") {
        # Find most recent backup
        $backups = Get-ChildItem -Directory | Where-Object { $_.Name -match "device_backup_" } | Sort-Object Name -Descending
        if ($backups.Count -eq 0) {
            Write-Error "No backup directories found"
            return $false
        }
        $BackupDir = $backups[0].Name
        Write-Info "Using most recent backup: $BackupDir"
    }
    
    if (!(Test-Path $BackupDir)) {
        Write-Error "Backup directory not found: $BackupDir"
        return $false
    }
    
    Write-Step "Restoring state from: $BackupDir"
    
    try {
        $uploadUrl = "http://$DEVICE_IP/api/upload"
        $restored = 0
        
        # Restore all files in backup directory
        $backupFiles = Get-ChildItem -Path $BackupDir -File
        
        foreach ($file in $backupFiles) {
            try {
                $result = curl -X POST -F "file=@$($file.FullName);filename=$($file.Name)" $uploadUrl
                if ($result -match "uploaded|success") {
                    Write-Info "Restored: $($file.Name)"
                    $restored++
                } else {
                    Write-Warning "Failed to restore: $($file.Name)"
                }
            } catch {
                Write-Warning "Error restoring $($file.Name): $($_.Exception.Message)"
            }
        }
        
        Write-Success "Restored $restored files from backup"
        return $true
    } catch {
        Write-Error "Restore failed: $($_.Exception.Message)"
        return $false
    }
}

# Upload web files (safe - excludes programs)
function Invoke-UploadWebSafe {
    param([string[]]$SpecificFiles = @())
    
    Write-Step "Uploading web files (safe mode - programs preserved)..."
    
    if (!(Test-Path $DATA_PATH)) {
        Write-Error "Data directory not found: $DATA_PATH"
        return $false
    }
    
    # Get files to upload
    if ($SpecificFiles.Count -gt 0) {
        $filesToUpload = @()
        foreach ($file in $SpecificFiles) {
            $fullPath = Join-Path $DATA_PATH $file
            if (Test-Path $fullPath) {
                $filesToUpload += Get-Item $fullPath
            } else {
                Write-Warning "File not found: $fullPath"
            }
        }
    } else {
        # All files except program files
        $filesToUpload = Get-ChildItem -Path $DATA_PATH -File -Recurse | Where-Object {
            $_.Name -notmatch "^(programs|program_\d+)\.json$"
        }
    }
    
    if ($filesToUpload.Count -eq 0) {
        Write-Warning "No files to upload"
        return $true
    }
    
    # Show what's being preserved
    $programFiles = Get-ChildItem -Path $DATA_PATH -File | Where-Object {
        $_.Name -match "^(programs|program_\d+)\.json$"
    }
    if ($programFiles.Count -gt 0) {
        Write-Info "🔒 Preserved files (not uploaded):"
        foreach ($pf in $programFiles) {
            Write-Host "    - $($pf.Name)" -ForegroundColor Gray
        }
    }
    
    # Upload files
    $uploadUrl = "http://$DEVICE_IP/api/upload"
    $uploaded = 0
    
    Write-Info "Uploading $($filesToUpload.Count) files..."
    
    foreach ($file in $filesToUpload) {
        try {
            $result = curl -X POST -F "file=@$($file.FullName);filename=$($file.Name)" $uploadUrl
            if ($result -match "uploaded|success") {
                Write-Success "Uploaded: $($file.Name)"
                $uploaded++
            } else {
                Write-Error "Failed: $($file.Name) - $result"
            }
        } catch {
            Write-Error "Error uploading $($file.Name): $($_.Exception.Message)"
        }
        Start-Sleep 0.5  # Brief pause
    }
    
    Write-Success "Uploaded $uploaded/$($filesToUpload.Count) files"
    Write-Info "🔒 Program settings preserved on device"
    return $true
}

# Upload all files (dangerous)
function Invoke-UploadAll {
    Write-Warning "DANGER: This will overwrite device program settings!"
    Write-Host "Press Ctrl+C within 5 seconds to cancel..." -ForegroundColor Red
    Start-Sleep 5
    
    Write-Step "Uploading ALL files (including programs)..."
    
    $filesToUpload = Get-ChildItem -Path $DATA_PATH -File -Recurse
    $uploadUrl = "http://$DEVICE_IP/api/upload"
    $uploaded = 0
    
    foreach ($file in $filesToUpload) {
        try {
            $result = curl -X POST -F "file=@$($file.FullName);filename=$($file.Name)" $uploadUrl
            if ($result -match "uploaded|success") {
                Write-Info "Uploaded: $($file.Name)"
                $uploaded++
            } else {
                Write-Error "Failed: $($file.Name)"
            }
        } catch {
            Write-Error "Error: $($file.Name) - $($_.Exception.Message)"
        }
        Start-Sleep 0.5
    }
    
    Write-Warning "Uploaded $uploaded files - device programs may be overwritten"
    return $true
}

# Test device connectivity
function Invoke-Test {
    Write-Step "Testing device connectivity..."
    
    try {
        # Test basic connectivity
        $statusUrl = "http://$DEVICE_IP/api/status"
        $status = curl -s $statusUrl | ConvertFrom-Json
        
        Write-Success "Device is responding"
        Write-Info "Current program: $($status.currentProgram)"
        Write-Info "Current stage: $($status.currentStage)"
        Write-Info "Temperature: $($status.temperature)°C"
        
        # Test file system
        $programsUrl = "http://$DEVICE_IP/programs.json"
        $programs = curl -s $programsUrl | ConvertFrom-Json
        Write-Success "File system accessible - $($programs.Count) programs found"
        
        return $true
    } catch {
        Write-Error "Device test failed: $($_.Exception.Message)"
        return $false
    }
}

# Show device status
function Invoke-Status {
    try {
        $statusUrl = "http://$DEVICE_IP/api/status"
        $statusJson = curl -s $statusUrl
        $status = $statusJson | ConvertFrom-Json
        
        Write-Host "=== Device Status ===" -ForegroundColor Cyan
        Write-Host "IP Address: $DEVICE_IP" -ForegroundColor White
        Write-Host "Current Program: $($status.program)" -ForegroundColor Yellow
        Write-Host "Current Stage: $($status.stage)" -ForegroundColor Yellow
        Write-Host "Temperature: $($status.temperature)°C" -ForegroundColor Green
        Write-Host "Running: $($status.running)" -ForegroundColor $(if($status.running) { "Green" } else { "Gray" })
        
        if ($status.timeLeft) {
            $remaining = [math]::Round($status.timeLeft / 60, 1)
            Write-Host "Time Remaining: $remaining minutes" -ForegroundColor Yellow
        }
        
        if ($status.adjustedTimeLeft) {
            $adjustedRemaining = [math]::Round($status.adjustedTimeLeft / 60, 1)
            Write-Host "Adjusted Time Remaining: $adjustedRemaining minutes" -ForegroundColor Cyan
        }
        
        # Show fermentation factor if active
        if ($status.fermentationFactor -and $status.fermentationFactor -ne 1.0) {
            Write-Host "Fermentation Factor: $($status.fermentationFactor)" -ForegroundColor Magenta
            Write-Host "Fermentation Progress: $([math]::Round($status.fermentAccumulatedMinutes, 1)) min accumulated" -ForegroundColor Magenta
        }
        
        return $true
    } catch {
        Write-Error "Could not get device status: $($_.Exception.Message)"
        return $false
    }
}

# Main execution logic
try {
    if ($Help) {
        Show-Help
        exit 0
    }
    
    Write-Host "=== ESP32 Breadmaker Controller Manager ===" -ForegroundColor Cyan
    Write-Host "Device: $DEVICE_IP" -ForegroundColor Yellow
    Write-Host ""
    
    $success = $true
    
    # Execute operations in logical order
    if ($BackupState) {
        $backupDir = Invoke-BackupState
        $success = $success -and ($backupDir -ne $null)
    }
    
    if ($Clean) {
        Write-Step "Cleaning build artifacts..."
        if (Test-Path $BUILD_PATH) {
            Remove-Item -Path $BUILD_PATH -Recurse -Force
            Write-Success "Build directory cleaned"
        }
    }
    
    if ($Build) {
        $success = $success -and (Invoke-Build)
    }
    
    if ($Upload) {
        $success = $success -and (Invoke-UploadFirmware)
    }
    
    if ($Serial -ne "") {
        $success = $success -and (Invoke-UploadSerial -Port $Serial)
    }
    
    if ($UploadWeb) {
        $success = $success -and (Invoke-UploadWebSafe -SpecificFiles $Files)
    }
    
    if ($UploadAll) {
        $success = $success -and (Invoke-UploadAll)
    }
    
    if ($RestoreState) {
        $success = $success -and (Invoke-RestoreState)
    }
    
    if ($Test) {
        $success = $success -and (Invoke-Test)
    }
    
    if ($Status) {
        $success = $success -and (Invoke-Status)
    }
    
    # If no operations specified, show status
    if (!$Build -and !$Upload -and $Serial -eq "" -and !$UploadWeb -and !$UploadAll -and 
        !$BackupState -and !$RestoreState -and !$Clean -and !$Test -and !$Status) {
        Write-Info "No operations specified. Showing device status..."
        Invoke-Status
    }
    
    if ($success) {
        Write-Success "All operations completed successfully!"
    } else {
        Write-Error "Some operations failed"
        exit 1
    }
    
} catch {
    Write-Error "Unexpected error: $($_.Exception.Message)"
    exit 1
}
