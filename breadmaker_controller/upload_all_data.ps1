# Comprehensive Data Upload Script for ESP32 Breadmaker Controller
# ⚠️ WARNING: THIS SCRIPT UPLOADS ALL FILES INCLUDING PROGRAM SETTINGS ⚠️
# This script uploads all data files and forces cache refresh
# USE WITH CAUTION - IT WILL OVERWRITE DEVICE PROGRAM SETTINGS

param(
    [string]$DeviceIP = "192.168.250.125",
    [string]$DataPath = "data",
    [switch]$IncludePrograms = $false  # Must be explicitly enabled to upload program files
)

Write-Host "=== ESP32 Data Upload Script ===" -ForegroundColor Cyan
Write-Host "Device IP: $DeviceIP" -ForegroundColor Yellow
Write-Host "Data Path: $DataPath" -ForegroundColor Yellow
Write-Host ""

# Function to upload a single file with retry logic
function Upload-File {
    param(
        [string]$FilePath,
        [string]$FileName,
        [string]$DeviceIP,
        [int]$MaxRetries = 3
    )
    
    $attempt = 1
    while ($attempt -le $MaxRetries) {
        try {
            Write-Host "  Uploading $FileName (attempt $attempt/$MaxRetries)..." -ForegroundColor White
            
            # Delete cached version first
            try {
                $deleteResult = curl -X DELETE "http://$DeviceIP/$FileName" 2>$null
                Start-Sleep 1
            } catch {
                # Ignore delete errors - file might not exist
            }
            
            # Upload new version
            $uploadResult = curl -X POST -F "file=@$FilePath;filename=$FileName" "http://$DeviceIP/api/upload"
            
            if ($uploadResult -match "uploaded") {
                Write-Host "    ✅ $FileName uploaded successfully" -ForegroundColor Green
                return $true
            } else {
                Write-Host "    ❌ Upload failed: $uploadResult" -ForegroundColor Red
                $attempt++
                Start-Sleep 2
            }
        } catch {
            Write-Host "    ❌ Upload error: $($_.Exception.Message)" -ForegroundColor Red
            $attempt++
            Start-Sleep 2
        }
    }
    
    Write-Host "    ❌ Failed to upload $FileName after $MaxRetries attempts" -ForegroundColor Red
    return $false
}

# Check if data directory exists
if (-not (Test-Path $DataPath)) {
    Write-Host "❌ Data directory '$DataPath' not found!" -ForegroundColor Red
    Write-Host "Make sure you're running this script from the breadmaker_controller directory" -ForegroundColor Yellow
    exit 1
}

# Get all files in data directory
if ($IncludePrograms) {
    Write-Host "⚠️ WARNING: Including program files - device settings will be overwritten!" -ForegroundColor Red
    $dataFiles = Get-ChildItem -Path $DataPath -File | Where-Object { $_.Extension -match '\.(html|js|css|json|svg)$' }
} else {
    Write-Host "Excluding program files (safe mode)" -ForegroundColor Green
    $dataFiles = Get-ChildItem -Path $DataPath -File | Where-Object { 
        $_.Extension -match '\.(html|js|css|json|svg)$' -and
        $_.Name -notmatch '^(programs|program_\d+)\.json$'
    }
}

Write-Host "Found $($dataFiles.Count) files to upload:" -ForegroundColor Cyan
foreach ($file in $dataFiles) {
    Write-Host "  - $($file.Name)" -ForegroundColor Gray
}
Write-Host ""

# Upload each file
$successCount = 0
$totalCount = $dataFiles.Count

Write-Host "Starting upload process..." -ForegroundColor Cyan
Write-Host ""

foreach ($file in $dataFiles) {
    $result = Upload-File -FilePath $file.FullName -FileName $file.Name -DeviceIP $DeviceIP
    if ($result) {
        $successCount++
    }
    Start-Sleep 1  # Brief pause between uploads
}

Write-Host ""
Write-Host "=== Upload Summary ===" -ForegroundColor Cyan
Write-Host "Successfully uploaded: $successCount/$totalCount files" -ForegroundColor $(if ($successCount -eq $totalCount) { "Green" } else { "Yellow" })

if ($successCount -eq $totalCount) {
    Write-Host "🎉 All files uploaded successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Cache refresh recommendations:" -ForegroundColor Yellow
    Write-Host "1. Clear browser cache (Ctrl+Shift+R)" -ForegroundColor White
    Write-Host "2. Wait 10-15 seconds for ESP32 to process files" -ForegroundColor White
    Write-Host "3. Try accessing: http://$DeviceIP/pid-tune.html" -ForegroundColor White
} else {
    Write-Host "⚠️  Some files failed to upload. Check the errors above." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Testing pid-tune.js upload specifically..." -ForegroundColor Cyan
$pidTuneResult = curl -s "http://$DeviceIP/pid-tune.js" | Select-String "/api/status" | Select-Object -First 1
if ($pidTuneResult) {
    Write-Host "✅ pid-tune.js appears to have correct API endpoints" -ForegroundColor Green
} else {
    $pidTuneOld = curl -s "http://$DeviceIP/pid-tune.js" | Select-String "/api/pid_debug" | Select-Object -First 1
    if ($pidTuneOld) {
        Write-Host "❌ pid-tune.js still contains old API endpoints - cache issue persists" -ForegroundColor Red
        Write-Host "Try restarting the ESP32 device or waiting longer for cache refresh" -ForegroundColor Yellow
    } else {
        Write-Host "⚠️  Unable to determine pid-tune.js status" -ForegroundColor Yellow
    }
}
