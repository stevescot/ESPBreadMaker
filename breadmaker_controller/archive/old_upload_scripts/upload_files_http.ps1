#!/usr/bin/env pwsh
param(
    [string]$ESP32_IP = "192.168.250.125",
    [string]$DataPath = "data",
    [switch]$Verbose
)

Write-Host "ESP32 HTTP File Uploader" -ForegroundColor Green
Write-Host "=========================" -ForegroundColor Green
Write-Host "Target: http://$ESP32_IP/debug/upload" -ForegroundColor Cyan
Write-Host ""

# Test ESP32 connectivity first
Write-Host "Testing connection to ESP32..." -ForegroundColor Yellow
try {
    $response = Invoke-WebRequest -Uri "http://$ESP32_IP/debug/fs" -Method GET -TimeoutSec 10 -ErrorAction Stop
    Write-Host "✓ ESP32 is reachable!" -ForegroundColor Green
} catch {
    Write-Host "✗ Cannot reach ESP32 at $ESP32_IP" -ForegroundColor Red
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# Function to upload a single file
function Send-File {
    param(
        [string]$FilePath,
        [string]$TargetPath,
        [string]$ESP32_IP
    )
    
    try {
        $content = Get-Content -Path $FilePath -Raw -Encoding UTF8
        $filename = $TargetPath
        
        # Ensure filename starts with /
        if (-not $filename.StartsWith("/")) {
            $filename = "/$filename"
        }
        
        # URL encode the parameters
        $encodedFilename = [System.Web.HttpUtility]::UrlEncode($filename)
        $encodedContent = [System.Web.HttpUtility]::UrlEncode($content)
        
        $body = "filename=$encodedFilename&content=$encodedContent"
        
        $response = Invoke-WebRequest -Uri "http://$ESP32_IP/debug/upload" -Method POST -Body $body -ContentType "application/x-www-form-urlencoded" -TimeoutSec 30
        
        if ($response.StatusCode -eq 200) {
            $fileSize = [math]::Round((Get-Item $FilePath).Length / 1KB, 1)
            Write-Host "✓ $filename ($fileSize KB)" -ForegroundColor Green
            return $true
        } else {
            Write-Host "✗ $filename - HTTP $($response.StatusCode)" -ForegroundColor Red
            return $false
        }
    } catch {
        Write-Host "✗ $filename - Error: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
}

# Function to upload files recursively
function Send-Directory {
    param(
        [string]$SourcePath,
        [string]$TargetBasePath = "",
        [string]$ESP32_IP
    )
    
    $uploadedCount = 0
    $failedCount = 0
    
    Get-ChildItem -Path $SourcePath -Recurse -File | ForEach-Object {
        $relativePath = $_.FullName.Substring((Resolve-Path $DataPath).Path.Length + 1)
        $relativePath = $relativePath.Replace('\', '/')
        
        if ($TargetBasePath) {
            $targetPath = "$TargetBasePath/$relativePath"
        } else {
            $targetPath = $relativePath
        }
        
        if ($Verbose) {
            Write-Host "Uploading: $($_.FullName) -> $targetPath" -ForegroundColor Gray
        }
        
        if (Send-File -FilePath $_.FullName -TargetPath $targetPath -ESP32_IP $ESP32_IP) {
            $script:uploadedCount++
        } else {
            $script:failedCount++
        }
    }
}

# Add System.Web assembly for URL encoding
Add-Type -AssemblyName System.Web

# Check if data directory exists
if (-not (Test-Path $DataPath)) {
    Write-Host "✗ Data directory '$DataPath' not found!" -ForegroundColor Red
    exit 1
}

Write-Host "Starting file upload from '$DataPath'..." -ForegroundColor Yellow
Write-Host ""

$script:uploadedCount = 0
$script:failedCount = 0

# Upload all files
Send-Directory -SourcePath $DataPath -ESP32_IP $ESP32_IP

Write-Host ""
Write-Host "Upload Summary:" -ForegroundColor Cyan
Write-Host "  Uploaded: $script:uploadedCount files" -ForegroundColor Green
Write-Host "  Failed: $script:failedCount files" -ForegroundColor $(if ($script:failedCount -eq 0) { "Green" } else { "Red" })

if ($script:uploadedCount -gt 0) {
    Write-Host ""
    Write-Host "✓ Files uploaded successfully!" -ForegroundColor Green
    Write-Host "Access your ESP32 web interface at: http://$ESP32_IP/" -ForegroundColor Cyan
    Write-Host "View filesystem status at: http://$ESP32_IP/debug/fs" -ForegroundColor Cyan
} else {
    Write-Host ""
    Write-Host "✗ No files were uploaded successfully!" -ForegroundColor Red
}
