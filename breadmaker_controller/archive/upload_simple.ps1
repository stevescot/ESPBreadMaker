# Simple curl-based upload for programs_index.json
# More reliable than PowerShell's Invoke-RestMethod

param(
    [string]$ServerIP = "192.168.250.125"
)

$filePath = "./data/programs_index.json"

# Check if file exists
if (-not (Test-Path $filePath)) {
    Write-Host "ERROR: File not found: $filePath" -ForegroundColor Red
    exit 1
}

Write-Host "Uploading programs_index.json using curl..." -ForegroundColor Yellow

# Use curl with form data - more reliable than PowerShell web requests
$curlCommand = "curl -X POST -F 'filename=programs_index.json' -F 'content=@$filePath' http://$ServerIP/debug/upload"

Write-Host "Running: $curlCommand" -ForegroundColor Cyan

try {
    $result = cmd /c $curlCommand 2>&1
    Write-Host "Response: $result" -ForegroundColor Green
    
    # Quick verification
    Write-Host "Checking filesystem..." -ForegroundColor Yellow
    $fsCheck = curl -s "http://$ServerIP/debug/fs" 2>&1
    
    if ($fsCheck -match "programs_index.json") {
        Write-Host "SUCCESS: programs_index.json uploaded successfully!" -ForegroundColor Green
    } else {
        Write-Host "WARNING: File upload may have failed" -ForegroundColor Orange
    }
    
} catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "Done. Try testing program selection now." -ForegroundColor Cyan
