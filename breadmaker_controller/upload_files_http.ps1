# HTTP Upload Script for ESP32 Breadmaker Controller
# Uses the working curl method for file uploads

param(
    [string]$FilePath = "",
    [string]$targetIP = "192.168.250.125"
)

if ($FilePath -eq "") {
    Write-Host "Usage: .\upload_files_http.ps1 <file_path> [target_ip]" -ForegroundColor Yellow
    Write-Host "Example: .\upload_files_http.ps1 data/script.js" -ForegroundColor Yellow
    exit 1
}

Write-Host "=== ESP32 File Upload ===" -ForegroundColor Cyan
Write-Host "Target IP: $targetIP" -ForegroundColor Yellow
Write-Host "File: $FilePath" -ForegroundColor Yellow
Write-Host ""

# Check if file exists
if (!(Test-Path $FilePath)) {
    Write-Host "❌ Error: File not found: $FilePath" -ForegroundColor Red
    exit 1
}

try {
    $fileName = Split-Path $FilePath -Leaf
    Write-Host "  Uploading: $fileName" -ForegroundColor White
    
    # Use the correct multipart form upload method that the firmware expects
    $result = curl -X POST -F "file=@$FilePath" -F "path=/$fileName" "http://$targetIP/api/upload"
    
    if ($result -match "uploaded") {
        Write-Host "    ✅ SUCCESS: $fileName uploaded successfully!" -ForegroundColor Green
        Write-Host ""
        Write-Host "🎉 Upload complete!" -ForegroundColor Green
        Write-Host "Web interface: http://$targetIP" -ForegroundColor Yellow
    } else {
        Write-Host "    ❌ FAILED: $result" -ForegroundColor Red
        exit 1
    }
} catch {
    Write-Host "    ❌ ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
