# Web-based Firmware Upload Script for ESP32
# Uploads compiled firmware to ESP32 via HTTP (web-based OTA)

param(
    [string]$IP = "",          # ESP32 IP address
    [int]$Port = 80,           # HTTP port (default 80)
    [switch]$Help
)

if ($Help) {
    Write-Host "Web-based Firmware Upload Script Usage:" -ForegroundColor Cyan
    Write-Host "  .\upload_ota.ps1 -IP <ip_address> [options]" -ForegroundColor White
    Write-Host ""
    Write-Host "Parameters:" -ForegroundColor Yellow
    Write-Host "  -IP <address>       ESP32 IP address (required)" -ForegroundColor White
    Write-Host "  -Port <port>        HTTP port (default: 80)" -ForegroundColor White
    Write-Host "  -Help               Show this help" -ForegroundColor White
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Green
    Write-Host "  .\upload_ota.ps1 -IP 192.168.250.125" -ForegroundColor Gray
    Write-Host "  .\upload_ota.ps1 -IP 192.168.250.125 -Port 8080" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Note: Uses web-based firmware upload (/api/update endpoint)" -ForegroundColor Yellow
    exit 0
}

# Check if IP was provided
if ([string]::IsNullOrEmpty($IP)) {
    Write-Host "Error: IP address is required" -ForegroundColor Red
    Write-Host "Usage: .\upload_ota.ps1 -IP <ip_address>" -ForegroundColor Yellow
    Write-Host "Use -Help for more information" -ForegroundColor Yellow
    exit 1
}

# Validate required IP parameter
if ([string]::IsNullOrWhiteSpace($IP)) {
    Write-Host "Error: IP address is required" -ForegroundColor Red
    Write-Host "Usage: .\upload_ota.ps1 -IP <ip_address>" -ForegroundColor Yellow
    Write-Host "Use -Help for more information" -ForegroundColor Yellow
    exit 1
}

Write-Host "ESP32 Web-based Firmware Upload Tool" -ForegroundColor Green
Write-Host "Target: http://$IP`:$Port/api/update" -ForegroundColor Cyan

# Find the compiled binary
$binaryPath = "build\breadmaker_controller.ino.bin"
if (-not (Test-Path $binaryPath)) {
    Write-Host "Error: Compiled binary not found at $binaryPath" -ForegroundColor Red
    Write-Host "Please compile the project first using: .\build_esp32.ps1" -ForegroundColor Yellow
    exit 1
}

Write-Host "Binary found: $binaryPath" -ForegroundColor Green
$fileSize = (Get-Item $binaryPath).Length
Write-Host "File size: $($fileSize) bytes ($([math]::Round($fileSize/1024/1024, 2)) MB)" -ForegroundColor White

# Test HTTP connectivity
Write-Host "Testing HTTP connectivity to $IP`:$Port..." -ForegroundColor Blue
try {
    $testUrl = "http://$IP`:$Port/api/firmware_info"
    $response = Invoke-WebRequest -Uri $testUrl -Method GET -TimeoutSec 10 -ErrorAction Stop
    Write-Host "✓ Device is reachable (HTTP $($response.StatusCode))" -ForegroundColor Green
    
    # Parse firmware info if available
    try {
        $firmwareInfo = $response.Content | ConvertFrom-Json
        if ($firmwareInfo.build) {
            Write-Host "Current firmware build: $($firmwareInfo.build)" -ForegroundColor Cyan
        }
    } catch {
        # Ignore JSON parsing errors
    }
} catch {
    Write-Host "✗ Cannot reach device at http://$IP`:$Port" -ForegroundColor Red
    Write-Host "Make sure the ESP32 is:" -ForegroundColor Yellow
    Write-Host "  - Connected to WiFi" -ForegroundColor White
    Write-Host "  - Running web server firmware" -ForegroundColor White
    Write-Host "  - Accessible on the network" -ForegroundColor White
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# Function to upload firmware using HTTP multipart form
function Upload-Firmware {
    param(
        [string]$DeviceIP,
        [int]$DevicePort,
        [string]$BinaryPath
    )
    
    $uploadUrl = "http://$DeviceIP`:$DevicePort/api/update"
    
    # Check if curl is available
    $curlPath = Get-Command curl -ErrorAction SilentlyContinue
    if ($curlPath) {
        Write-Host "Using curl for firmware upload..." -ForegroundColor Blue
        
        # Use curl for reliable multipart upload (same method as data files)
        $curlArgs = @(
            "-X", "POST"
            "-F", "file=@`"$BinaryPath`";filename=firmware.bin"
            "--max-time", "300"  # 5 minute timeout for large uploads
            "--connect-timeout", "30"
            "--retry", "2"
            $uploadUrl
        )
        
        Write-Host "Uploading firmware via curl..." -ForegroundColor Yellow
        Write-Host "Command: curl $($curlArgs -join ' ')" -ForegroundColor Gray
        
        try {
            $result = & curl @curlArgs 2>&1
            $exitCode = $LASTEXITCODE
            
            if ($exitCode -eq 0) {
                Write-Host "✓ Firmware upload completed successfully!" -ForegroundColor Green
                Write-Host "Response: $result" -ForegroundColor Gray
                return $true
            } else {
                Write-Host "✗ Curl upload failed with exit code: $exitCode" -ForegroundColor Red
                Write-Host "Error output: $result" -ForegroundColor Red
                return $false
            }
        } catch {
            Write-Host "✗ Curl execution failed: $($_.Exception.Message)" -ForegroundColor Red
            return $false
        }
    } else {
        # Fallback to PowerShell Invoke-WebRequest (less reliable for large files)
        Write-Host "Using PowerShell Invoke-WebRequest for firmware upload..." -ForegroundColor Blue
        Write-Host "Note: curl is recommended for better reliability with large files" -ForegroundColor Yellow
        
        try {
            # Read binary file
            $fileBytes = [System.IO.File]::ReadAllBytes($BinaryPath)
            $fileName = "firmware.bin"
            
            # Create multipart form data
            $boundary = [System.Guid]::NewGuid().ToString()
            $LF = "`r`n"
            
            $bodyLines = @(
                "--$boundary",
                "Content-Disposition: form-data; name=`"file`"; filename=`"$fileName`"",
                "Content-Type: application/octet-stream",
                "",
                [System.Text.Encoding]::GetEncoding("iso-8859-1").GetString($fileBytes),
                "--$boundary--"
            )
            
            $body = $bodyLines -join $LF
            $bodyBytes = [System.Text.Encoding]::GetEncoding("iso-8859-1").GetBytes($body)
            
            Write-Host "Uploading firmware via PowerShell..." -ForegroundColor Yellow
            
            $response = Invoke-WebRequest -Uri $uploadUrl -Method POST -Body $bodyBytes -ContentType "multipart/form-data; boundary=$boundary" -TimeoutSec 300
            
            if ($response.StatusCode -eq 200) {
                Write-Host "✓ Firmware upload completed successfully!" -ForegroundColor Green
                Write-Host "Response: $($response.Content)" -ForegroundColor Gray
                return $true
            } else {
                Write-Host "✗ Upload failed with HTTP status: $($response.StatusCode)" -ForegroundColor Red
                Write-Host "Response: $($response.Content)" -ForegroundColor Red
                return $false
            }
        } catch {
            Write-Host "✗ PowerShell upload failed: $($_.Exception.Message)" -ForegroundColor Red
            return $false
        }
    }
}

# Perform the upload
Write-Host "Starting web-based firmware upload..." -ForegroundColor Blue
$uploadSuccess = Upload-Firmware -DeviceIP $IP -DevicePort $Port -BinaryPath $binaryPath

if ($uploadSuccess) {
    Write-Host ""
    Write-Host "✓ Firmware upload completed successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  - The ESP32 will restart automatically" -ForegroundColor White
    Write-Host "  - Wait for it to reconnect to WiFi (~10-30 seconds)" -ForegroundColor White
    Write-Host "  - Access web interface at: http://$IP" -ForegroundColor White
    Write-Host "  - Check build date at: http://$IP/api/firmware_info" -ForegroundColor White
} else {
    Write-Host ""
    Write-Host "✗ Firmware upload failed!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Troubleshooting:" -ForegroundColor Yellow
    Write-Host "  - Verify ESP32 is accessible at http://$IP" -ForegroundColor White
    Write-Host "  - Check that firmware size is reasonable (<8MB)" -ForegroundColor White
    Write-Host "  - Try again - sometimes network timeouts occur" -ForegroundColor White
    Write-Host "  - Consider using the web interface at http://$IP/upload" -ForegroundColor White
    exit 1
}

Write-Host "Web-based firmware upload process completed!" -ForegroundColor Green
