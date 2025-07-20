# ESP32 Endpoint Test Script
# Tests the streaming JSON endpoints and file upload functionality
#
param(
    [string]$ServerIP = "192.168.250.125",
    [switch]$Verbose
)

Write-Host "=== ESP32 Breadmaker Endpoint Test ===" -ForegroundColor Cyan
Write-Host "Target Server: $ServerIP" -ForegroundColor White
Write-Host ""

# Test connectivity
Write-Host "Testing connectivity..." -NoNewline
try {
    $response = Invoke-WebRequest -Uri "http://$ServerIP/" -Method Head -TimeoutSec 5 -ErrorAction Stop
    Write-Host " OK (Status: $($response.StatusCode))" -ForegroundColor Green
}
catch {
    Write-Host " FAILED" -ForegroundColor Red
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Make sure the ESP32 is running and accessible at $ServerIP" -ForegroundColor Yellow
    exit 1
}

# Test status endpoints
$endpoints = @(
    @{ Path = "/status"; Description = "Legacy status endpoint" },
    @{ Path = "/api/status"; Description = "API status endpoint" },
    @{ Path = "/api/firmware_info"; Description = "Firmware info endpoint" },
    @{ Path = "/api/ota/status"; Description = "OTA status endpoint" }
)

Write-Host ""
Write-Host "Testing JSON endpoints..." -ForegroundColor Cyan

foreach ($endpoint in $endpoints) {
    Write-Host "  $($endpoint.Path) ($($endpoint.Description))..." -NoNewline
    
    try {
        $response = Invoke-WebRequest -Uri "http://$ServerIP$($endpoint.Path)" -TimeoutSec 10 -ErrorAction Stop
        
        if ($response.StatusCode -eq 200) {
            # Try to parse as JSON to validate format
            try {
                $json = $response.Content | ConvertFrom-Json
                Write-Host " OK" -ForegroundColor Green
                
                if ($Verbose) {
                    Write-Host "    Size: $($response.Content.Length) bytes" -ForegroundColor Gray
                    if ($endpoint.Path -match "status") {
                        Write-Host "    Contains:" -ForegroundColor Gray
                        if ($json.temperature) { Write-Host "      - Temperature: $($json.temperature)" -ForegroundColor Gray }
                        if ($json.running) { Write-Host "      - Running: $($json.running)" -ForegroundColor Gray }
                        if ($json.program) { Write-Host "      - Program: $($json.program)" -ForegroundColor Gray }
                        if ($json.stage) { Write-Host "      - Stage: $($json.stage)" -ForegroundColor Gray }
                    }
                }
            }
            catch {
                Write-Host " INVALID JSON" -ForegroundColor Red
                if ($Verbose) {
                    Write-Host "    Response: $($response.Content.Substring(0, [Math]::Min(100, $response.Content.Length)))..." -ForegroundColor Gray
                }
            }
        } else {
            Write-Host " FAILED (Status: $($response.StatusCode))" -ForegroundColor Red
        }
    }
    catch {
        Write-Host " ERROR" -ForegroundColor Red
        if ($Verbose) {
            Write-Host "    Error: $($_.Exception.Message)" -ForegroundColor Gray
        }
    }
}

# Test file upload endpoint
Write-Host ""
Write-Host "Testing file upload endpoint..." -ForegroundColor Cyan

# Create a small test file
$testFile = "test_upload.txt"
$testContent = "Test file created at $(Get-Date) for endpoint testing"
$testContent | Out-File -FilePath $testFile -Encoding UTF8

try {
    Write-Host "  Creating test file: $testFile..." -NoNewline
    Write-Host " OK" -ForegroundColor Green
    
    Write-Host "  Testing /api/upload endpoint..." -NoNewline
    
    # Create multipart form data
    $boundary = [System.Guid]::NewGuid().ToString()
    $LF = "`r`n"
    
    $fileBytes = [System.IO.File]::ReadAllBytes($testFile)
    
    $bodyLines = @(
        "--$boundary",
        "Content-Disposition: form-data; name=`"file`"; filename=`"$testFile`"",
        "Content-Type: text/plain",
        "",
        [System.Text.Encoding]::UTF8.GetString($fileBytes),
        "--$boundary--"
    )
    
    $body = $bodyLines -join $LF
    $bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($body)
    
    $response = Invoke-WebRequest -Uri "http://$ServerIP/api/upload" -Method POST -Body $bodyBytes -ContentType "multipart/form-data; boundary=$boundary" -TimeoutSec 30
    
    if ($response.StatusCode -eq 200) {
        Write-Host " OK" -ForegroundColor Green
        if ($Verbose) {
            Write-Host "    Response: $($response.Content)" -ForegroundColor Gray
        }
    } else {
        Write-Host " FAILED (Status: $($response.StatusCode))" -ForegroundColor Red
    }
}
catch {
    Write-Host " ERROR" -ForegroundColor Red
    if ($Verbose) {
        Write-Host "    Error: $($_.Exception.Message)" -ForegroundColor Gray
    }
}
finally {
    # Clean up test file
    if (Test-Path $testFile) {
        Remove-Item $testFile -Force
        Write-Host "  Cleaned up test file" -ForegroundColor Gray
    }
}

# Test OTA upload endpoint
Write-Host ""
Write-Host "Testing OTA upload endpoint..." -ForegroundColor Cyan
Write-Host "  /api/ota/upload endpoint..." -NoNewline

try {
    # Just test that the endpoint exists (don't actually upload firmware)
    $response = Invoke-WebRequest -Uri "http://$ServerIP/api/ota/upload" -Method POST -Body "" -TimeoutSec 5 -ErrorAction Stop
    
    # We expect this to fail with a specific error since we're not sending valid firmware
    Write-Host " EXISTS" -ForegroundColor Yellow
    if ($Verbose) {
        Write-Host "    Note: Endpoint exists and responds (empty upload test)" -ForegroundColor Gray
    }
}
catch {
    $errorMessage = $_.Exception.Message
    if ($errorMessage -match "timeout" -or $errorMessage -match "no response") {
        Write-Host " TIMEOUT" -ForegroundColor Yellow
        if ($Verbose) {
            Write-Host "    Note: Endpoint exists but timed out (normal for empty OTA upload)" -ForegroundColor Gray
        }
    } else {
        Write-Host " ERROR" -ForegroundColor Red
        if ($Verbose) {
            Write-Host "    Error: $errorMessage" -ForegroundColor Gray
        }
    }
}

Write-Host ""
Write-Host "=== Test Complete ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "If all endpoints show OK/EXISTS, the streaming JSON implementation" -ForegroundColor Green
Write-Host "and upload functionality are working correctly." -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Test file uploads: .\quick_upload.ps1" -ForegroundColor White  
Write-Host "  2. Test firmware OTA: .\quick_upload.ps1 -firmware" -ForegroundColor White
Write-Host "  3. Test both: .\quick_upload.ps1 -both" -ForegroundColor White
