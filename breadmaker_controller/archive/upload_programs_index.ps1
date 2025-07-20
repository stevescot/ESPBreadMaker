# Quick upload script for programs_index.json
# Uses the /debug/upload endpoint which works reliably for small files

param(
    [string]$ServerIP = "192.168.250.125"
)

$filePath = "./data/programs_index.json"

# Check if file exists
if (-not (Test-Path $filePath)) {
    Write-Host "ERROR: File not found: $filePath" -ForegroundColor Red
    exit 1
}

# Read file content
$content = Get-Content $filePath -Raw -Encoding UTF8

Write-Host "Uploading programs_index.json to ESP32 at $ServerIP..." -ForegroundColor Yellow

try {
    # Use the debug upload endpoint with form data
    $uri = "http://$ServerIP/debug/upload"
    
    $body = @{
        filename = "programs_index.json"
        content = $content
    }
    
    $response = Invoke-RestMethod -Uri $uri -Method Post -Body $body -ContentType "application/x-www-form-urlencoded"
    
    Write-Host "SUCCESS: $response" -ForegroundColor Green
    
    # Verify the upload by checking filesystem
    Write-Host "Verifying upload..." -ForegroundColor Yellow
    $fsResponse = Invoke-RestMethod -Uri "http://$ServerIP/debug/fs" -Method Get
    
    if ($fsResponse -match "programs_index.json") {
        Write-Host "VERIFIED: programs_index.json is now on the device!" -ForegroundColor Green
    } else {
        Write-Host "WARNING: File may not have been uploaded correctly" -ForegroundColor Orange
    }
    
} catch {
    Write-Host "ERROR: Upload failed - $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Device may not be ready yet after firmware update" -ForegroundColor Yellow
    exit 1
}

Write-Host "Upload complete! Now test program selection on the web interface." -ForegroundColor Cyan
