# Test PowerShell Upload Method
$DEVICE_IP = "192.168.250.125"
$WEB_OTA_ENDPOINT = "http://$DEVICE_IP/api/ota/upload"
$FIRMWARE_FILE = "build\breadmaker_controller.ino.bin"

# Check current directory and adjust path if needed
if (!(Test-Path $FIRMWARE_FILE)) {
    Write-Warning "Firmware not found at $FIRMWARE_FILE, trying absolute path..."
    $FIRMWARE_FILE = "c:\Users\steve\OneDrive\Documents\GitHub\ESPBreadMaker\breadmaker_controller\build\breadmaker_controller.ino.bin"
}

function Write-Success($message) { Write-Host "✅ $message" -ForegroundColor Green }
function Write-Error($message) { Write-Host "❌ $message" -ForegroundColor Red }
function Write-Warning($message) { Write-Host "⚠️ $message" -ForegroundColor Yellow }
function Write-Info($message) { Write-Host "ℹ️ $message" -ForegroundColor Cyan }

function Test-PowerShellUpload {
    Write-Info "Testing PowerShell multipart upload method..."
    
    if (!(Test-Path $FIRMWARE_FILE)) {
        Write-Error "Firmware binary not found: $FIRMWARE_FILE"
        return $false
    }
    
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
        
        Write-Info "Total payload size: $($bodyBytes.Length) bytes"
        Write-Info "Firmware size: $($fileBinary.Length) bytes"
        Write-Info "Boundary: $boundary"
        
        $response = Invoke-WebRequest -Uri $WEB_OTA_ENDPOINT -Method POST -Body $bodyBytes -ContentType "multipart/form-data; boundary=$boundary" -TimeoutSec 60
        
        if ($response.StatusCode -eq 200) {
            Write-Success "PowerShell upload successful!"
            Write-Info "Response: $($response.Content)"
            return $true
        } else {
            Write-Error "Upload failed with status: $($response.StatusCode)"
            Write-Error "Response: $($response.Content)"
            return $false
        }
    }
    catch {
        Write-Error "PowerShell upload failed: $($_.Exception.Message)"
        Write-Error "Exception details: $($_.Exception.GetType().FullName)"
        if ($_.Exception.InnerException) {
            Write-Error "Inner exception: $($_.Exception.InnerException.Message)"
        }
        return $false
    }
}

# Run the test
Test-PowerShellUpload
