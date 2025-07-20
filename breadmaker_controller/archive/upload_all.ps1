# ESP32 Breadmaker Complete Upload Script
# 
# This script can upload web files and perform OTA firmware updates.
# It automatically detects whether to upload files or firmware based on parameters.
#
# Usage examples:
#   .\upload_all.ps1                                    # Upload all files from ./data to 192.168.250.125
#   .\upload_all.ps1 -ServerIP "192.168.1.100"          # Upload to different IP
#   .\upload_all.ps1 -FirmwareFile "firmware.bin"       # Upload firmware via OTA
#   .\upload_all.ps1 -BuildFirst                        # Build firmware first, then upload
#   .\upload_all.ps1 -UploadFiles -UploadFirmware       # Upload both files and firmware
#
param(
    [string]$Directory = "./data",
    [string]$ServerIP = "192.168.250.125",
    [string]$FirmwareFile = "",
    [switch]$BuildFirst,
    [switch]$UploadFiles,
    [switch]$UploadFirmware,
    [int]$DelayBetweenFiles = 100,    # Milliseconds between file uploads
    [int]$MaxRetries = 3,            # Max retries per file
    [int]$RetryDelay = 2000,         # Milliseconds between retries
    [switch]$PreserveDirectories     # Keep directory structure when uploading
)

# Global tracking variables
$totalFiles = 0
$successfulUploads = 0
$failedUploads = 0
$firmwareUploadSuccess = $false

Write-Host "=== ESP32 Breadmaker Complete Upload Script ===" -ForegroundColor Cyan
Write-Host "Target Server: $ServerIP" -ForegroundColor White

# Determine what to upload if not explicitly specified
if (-not $UploadFiles -and -not $UploadFirmware -and -not $FirmwareFile) {
    if (Test-Path -Path $Directory) {
        $UploadFiles = $true
        Write-Host "Auto-detected: Will upload files from $Directory" -ForegroundColor Yellow
    }
}

if ($FirmwareFile -and -not $UploadFirmware) {
    $UploadFirmware = $true
    Write-Host "Auto-detected: Will upload firmware from $FirmwareFile" -ForegroundColor Yellow
}

if ($BuildFirst -and -not $UploadFirmware) {
    $UploadFirmware = $true
    Write-Host "Auto-detected: Will build and upload firmware" -ForegroundColor Yellow
}

# Function to build firmware if requested
function Build-Firmware {
    Write-Host "Building firmware..." -ForegroundColor Cyan
    
    if (-not (Test-Path -Path "build_esp32.ps1")) {
        Write-Host "Error: build_esp32.ps1 not found in current directory!" -ForegroundColor Red
        return $false
    }
    
    try {
        & .\build_esp32.ps1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Firmware build completed successfully" -ForegroundColor Green
            return $true
        } else {
            Write-Host "Firmware build failed with exit code: $LASTEXITCODE" -ForegroundColor Red
            return $false
        }
    }
    catch {
        Write-Host "Error running build script: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
}

# Function to find firmware file
function Find-FirmwareFile {
    # Look for main firmware file first (highest priority)
    $mainFirmware = @(
        "build\breadmaker_controller.ino.bin",
        "breadmaker_controller.ino.bin"
    )
    
    foreach ($path in $mainFirmware) {
        if (Test-Path $path) {
            return (Get-Item $path).FullName
        }
    }
    
    # Fallback: Look for any .bin file but exclude bootloader and partition files
    $buildFiles = Get-ChildItem -Path "build\*.bin" -ErrorAction SilentlyContinue | 
                  Where-Object { $_.Name -notmatch "(bootloader|partition)" } |
                  Sort-Object LastWriteTime -Descending
    
    if ($buildFiles) {
        return $buildFiles[0].FullName
    }
    
    return $null
}

# Function to upload firmware via OTA
function Upload-Firmware {
    param(
        [string]$FilePath,
        [string]$ServerIP
    )
    
    if (-not (Test-Path -Path $FilePath)) {
        Write-Host "Error: Firmware file not found: $FilePath" -ForegroundColor Red
        return $false
    }
    
    $fileSize = (Get-Item $FilePath).Length
    Write-Host "Uploading firmware: $FilePath ($fileSize bytes)" -ForegroundColor Cyan
    
    try {
        # Create multipart form data for OTA upload
        $boundary = [System.Guid]::NewGuid().ToString()
        $LF = "`r`n"
        
        $fileBytes = [System.IO.File]::ReadAllBytes($FilePath)
        $fileName = Split-Path -Leaf $FilePath
        
        $bodyLines = @(
            "--$boundary",
            "Content-Disposition: form-data; name=`"file`"; filename=`"$fileName`"",
            "Content-Type: application/octet-stream",
            "",
            [System.Text.Encoding]::Latin1.GetString($fileBytes),
            "--$boundary--"
        )
        
        $body = $bodyLines -join $LF
        $bodyBytes = [System.Text.Encoding]::Latin1.GetBytes($body)
        
        # Upload via OTA endpoint
        $uri = "http://$ServerIP/api/ota/upload"
        Write-Host "Sending OTA request to: $uri" -ForegroundColor Gray
        
        $response = Invoke-WebRequest -Uri $uri -Method POST -Body $bodyBytes -ContentType "multipart/form-data; boundary=$boundary" -TimeoutSec 120
        
        if ($response.StatusCode -eq 200) {
            Write-Host "Firmware upload successful!" -ForegroundColor Green
            Write-Host "ESP32 will restart with new firmware" -ForegroundColor Yellow
            return $true
        } else {
            Write-Host "Firmware upload failed with status: $($response.StatusCode)" -ForegroundColor Red
            return $false
        }
    }
    catch {
        Write-Host "Error uploading firmware: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
}

# Function to upload a file with retry logic
function Send-FileToESP32 {
    param(
        [Parameter(Mandatory=$true)]
        [string]$FilePath,
        
        [Parameter(Mandatory=$true)]
        [string]$ServerIP,
        
        [string]$RelativePath = "",
        [int]$MaxRetries = 3,
        [int]$RetryDelay = 2000
    )
    
    # Handle destination path based on whether we're preserving directories
    if ($RelativePath -and $PreserveDirectories) {
        $fileName = $RelativePath
        # Ensure proper formatting with leading slash
        if (-not $fileName.StartsWith("/")) {
            $fileName = "/" + $fileName
        }
    } else {
        $fileName = Split-Path -Leaf $FilePath
    }
    
    $fileSize = (Get-Item $FilePath).Length
    Write-Host "Uploading $fileName ($fileSize bytes)..." -NoNewline -ForegroundColor White
    
    $retryCount = 0
    $success = $false
    
    while ($retryCount -lt $MaxRetries -and -not $success) {
        if ($retryCount -gt 0) {
            Write-Host " Retry $retryCount..." -NoNewline -ForegroundColor Yellow
            Start-Sleep -Milliseconds $RetryDelay
        }
        
        try {
            # Create multipart form data
            $boundary = [System.Guid]::NewGuid().ToString()
            $LF = "`r`n"
            
            $fileBytes = [System.IO.File]::ReadAllBytes($FilePath)
            
            $bodyLines = @(
                "--$boundary",
                "Content-Disposition: form-data; name=`"file`"; filename=`"$fileName`"",
                "Content-Type: application/octet-stream",
                "",
                [System.Text.Encoding]::Latin1.GetString($fileBytes),
                "--$boundary--"
            )
            
            $body = $bodyLines -join $LF
            $bodyBytes = [System.Text.Encoding]::Latin1.GetBytes($body)
            
            # Use the /api/upload endpoint
            $uri = "http://$ServerIP/api/upload"
            $response = Invoke-WebRequest -Uri $uri -Method POST -Body $bodyBytes -ContentType "multipart/form-data; boundary=$boundary" -TimeoutSec 30
            
            if ($response.StatusCode -eq 200) {
                Write-Host " Success! ($([int]$response.StatusCode))" -ForegroundColor Green
                $success = $true
                $script:successfulUploads++
                return $true
            } else {
                Write-Host " Failed with status code: $([int]$response.StatusCode)" -ForegroundColor Yellow
                $retryCount++
            }
        }
        catch {
            Write-Host " Error: $($_.Exception.Message)" -ForegroundColor Red
            $retryCount++
        }
    }
    
    if (-not $success) {
        Write-Host "Failed to upload $fileName after $MaxRetries retries" -ForegroundColor Red
        $script:failedUploads++
        return $false
    }
}

# Function to get directory size
function Get-DirectorySize {
    param([string]$Path)
    
    $size = (Get-ChildItem -Path $Path -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1KB
    return [math]::Round($size, 2)
}

# Main execution
try {
    $startTime = Get-Date
    
    # Check connectivity first
    try {
        $testConnection = Invoke-WebRequest -Uri "http://$ServerIP/" -Method Head -TimeoutSec 5 -ErrorAction Stop
        Write-Host "ESP32 server at $ServerIP is reachable, status: $($testConnection.StatusCode)" -ForegroundColor Green
    }
    catch {
        Write-Host "Warning: Could not reach ESP32 server at $ServerIP. Continuing anyway..." -ForegroundColor Yellow
        Write-Host "Error details: $($_.Exception.Message)" -ForegroundColor Yellow
    }
    
    # Build firmware if requested
    if ($BuildFirst) {
        Write-Host "-------------------------------------------------------------------------" -ForegroundColor Cyan
        if (-not (Build-Firmware)) {
            Write-Host "Build failed, exiting" -ForegroundColor Red
            exit 1
        }
        Write-Host "-------------------------------------------------------------------------" -ForegroundColor Cyan
    }
    
    # Upload firmware if requested
    if ($UploadFirmware) {
        Write-Host "Starting firmware upload..." -ForegroundColor Cyan
        
        if (-not $FirmwareFile) {
            $FirmwareFile = Find-FirmwareFile
            if (-not $FirmwareFile) {
                Write-Host "Error: No firmware file found. Please specify -FirmwareFile or run -BuildFirst" -ForegroundColor Red
                exit 1
            }
            Write-Host "Auto-detected firmware file: $FirmwareFile" -ForegroundColor Yellow
        }
        
        $firmwareUploadSuccess = Upload-Firmware -FilePath $FirmwareFile -ServerIP $ServerIP
        
        if ($firmwareUploadSuccess) {
            Write-Host "Firmware upload completed successfully!" -ForegroundColor Green
            if ($UploadFiles) {
                Write-Host "Waiting 10 seconds for ESP32 to restart before uploading files..." -ForegroundColor Yellow
                Start-Sleep -Seconds 10
            }
        } else {
            Write-Host "Firmware upload failed!" -ForegroundColor Red
            if (-not $UploadFiles) {
                exit 1
            }
        }
        Write-Host "-------------------------------------------------------------------------" -ForegroundColor Cyan
    }
    
    # Upload files if requested
    if ($UploadFiles) {
        Write-Host "Starting file upload..." -ForegroundColor Cyan
        
        # Check if directory exists
        if (-not (Test-Path -Path $Directory)) {
            Write-Host "Error: Directory '$Directory' not found!" -ForegroundColor Red
            exit 1
        }
        
        # Get all files
        $files = Get-ChildItem -Path $Directory -Recurse -File
        $totalFiles = $files.Count
        $totalSize = Get-DirectorySize -Path $Directory
        
        Write-Host "Uploading $totalFiles files ($totalSize KB) from $Directory" -ForegroundColor Cyan
        Write-Host "Using $DelayBetweenFiles ms delay between files, max $MaxRetries retries with $RetryDelay ms retry delay" -ForegroundColor Cyan
        if ($PreserveDirectories) {
            Write-Host "Directory structure will be preserved on upload" -ForegroundColor Cyan
        } else {
            Write-Host "Files will be uploaded to root directory" -ForegroundColor Cyan
        }
        Write-Host "-------------------------------------------------------------------------" -ForegroundColor Cyan
        
        # Upload each file
        foreach ($file in $files) {
            # Calculate the relative path if preserving directories
            $relativePath = ""
            if ($PreserveDirectories) {
                $relativePath = $file.FullName.Replace($Directory, "").TrimStart("\", "/").Replace("\", "/")
                Write-Host "Processing $($file.Name) as $relativePath" -ForegroundColor Gray
            }
            
            # Upload the file with retry logic
            Send-FileToESP32 -FilePath $file.FullName -ServerIP $ServerIP -RelativePath $relativePath -MaxRetries $MaxRetries -RetryDelay $RetryDelay
            
            # Delay between uploads to avoid overwhelming the ESP32
            if ($DelayBetweenFiles -gt 0) {
                Start-Sleep -Milliseconds $DelayBetweenFiles
            }
        }
    }
    
    $endTime = Get-Date
    $duration = ($endTime - $startTime).TotalSeconds
    
    # Summary
    Write-Host "=========================================================================" -ForegroundColor Cyan
    Write-Host "Upload Summary:" -ForegroundColor Cyan
    
    if ($UploadFirmware) {
        Write-Host "  Firmware upload: $(if ($firmwareUploadSuccess) { 'SUCCESS' } else { 'FAILED' })" -ForegroundColor $(if ($firmwareUploadSuccess) { 'Green' } else { 'Red' })
        if ($firmwareUploadSuccess -and $FirmwareFile) {
            Write-Host "    File: $FirmwareFile" -ForegroundColor White
        }
    }
    
    if ($UploadFiles) {
        Write-Host "  File uploads:" -ForegroundColor White
        Write-Host "    Total files: $totalFiles ($totalSize KB)" -ForegroundColor White
        Write-Host "    Successfully uploaded: $successfulUploads" -ForegroundColor Green
        Write-Host "    Failed uploads: $failedUploads" -ForegroundColor Red
    }
    
    Write-Host "  Duration: $([math]::Round($duration, 2)) seconds" -ForegroundColor White
    Write-Host "=========================================================================" -ForegroundColor Cyan
    
    # Exit with appropriate code
    if (($UploadFirmware -and -not $firmwareUploadSuccess) -or ($UploadFiles -and $failedUploads -gt 0)) {
        exit 1
    }
}
catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
