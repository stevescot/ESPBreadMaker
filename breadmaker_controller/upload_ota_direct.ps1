# Direct ESP32 OTA Upload Script
# Uses the standard Arduino OTA method via port 3232
#
# Usage: .\upload_ota_direct.ps1 -IP "192.168.250.125" -FirmwareFile "firmware.bin"
#
param(
    [string]$IP = "192.168.250.125",
    [string]$FirmwareFile = "",
    [string]$Password = "admin",
    [int]$Port = 3232
)

Write-Host "=== ESP32 Direct OTA Upload ===" -ForegroundColor Cyan
Write-Host "Target IP: $IP" -ForegroundColor White
Write-Host "Target Port: $Port" -ForegroundColor White

# Find firmware file if not specified
if (-not $FirmwareFile) {
    Write-Host "Searching for firmware file..." -ForegroundColor Yellow
    
    # Look for the main firmware binary (not bootloader)
    $patterns = @(
        "*.ino.bin",
        "breadmaker_controller.ino.bin", 
        "build/*.ino.bin",
        "*.bin"
    )
    
    foreach ($pattern in $patterns) {
        $files = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Where-Object { 
            $_.Name -notlike "*bootloader*" -and 
            $_.Name -notlike "*partitions*" -and
            $_.Name -notlike "fatfs*"
        }
        if ($files) {
            $FirmwareFile = ($files | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
            break
        }
    }
    
    if (-not $FirmwareFile) {
        Write-Host "Error: No firmware file found!" -ForegroundColor Red
        Write-Host "Available .bin files:" -ForegroundColor Yellow
        Get-ChildItem -Recurse -Filter "*.bin" | ForEach-Object { 
            Write-Host "  $($_.FullName) ($($_.Length) bytes)" -ForegroundColor Gray
        }
        exit 1
    }
}

if (-not (Test-Path $FirmwareFile)) {
    Write-Host "Error: Firmware file not found: $FirmwareFile" -ForegroundColor Red
    exit 1
}

$fileInfo = Get-Item $FirmwareFile
Write-Host "Found firmware: $($fileInfo.Name) ($($fileInfo.Length) bytes)" -ForegroundColor Green

# Test connectivity
Write-Host "Testing connectivity to $IP..." -ForegroundColor Yellow
try {
    $ping = Test-Connection -ComputerName $IP -Count 1 -Quiet -TimeoutSeconds 5
    if (-not $ping) {
        Write-Host "Warning: Device at $IP is not responding to ping" -ForegroundColor Yellow
    } else {
        Write-Host "Device is reachable" -ForegroundColor Green
    }
} catch {
    Write-Host "Warning: Could not test connectivity" -ForegroundColor Yellow
}

# Use espota.py for upload (standard Arduino tool)
Write-Host "Attempting OTA upload using espota.py..." -ForegroundColor Cyan

# Try to find espota.py in common Arduino IDE locations
$espotaPaths = @(
    "$env:USERPROFILE\AppData\Local\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py",
    "$env:APPDATA\..\Local\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py",
    "C:\Users\*\AppData\Local\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py",
    ".\espota.py"
)

$espotaPath = $null
foreach ($path in $espotaPaths) {
    $found = Get-ChildItem -Path $path -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        $espotaPath = $found.FullName
        break
    }
}

if ($espotaPath) {
    Write-Host "Found espota.py at: $espotaPath" -ForegroundColor Green
    Write-Host "Running: python `"$espotaPath`" -i $IP -p $Port -a `"$Password`" -f `"$FirmwareFile`"" -ForegroundColor Gray
    
    try {
        & python "$espotaPath" -i $IP -p $Port -a "$Password" -f "$FirmwareFile"
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "OTA upload completed successfully!" -ForegroundColor Green
            Write-Host "The ESP32 should restart with the new firmware." -ForegroundColor Green
        } else {
            Write-Host "OTA upload failed with exit code: $LASTEXITCODE" -ForegroundColor Red
        }
    } catch {
        Write-Host "Error running espota.py: $($_.Exception.Message)" -ForegroundColor Red
    }
} else {
    Write-Host "espota.py not found. Trying alternative method..." -ForegroundColor Yellow
    
    # Alternative: Direct TCP upload (simplified)
    Write-Host "Attempting direct TCP upload to $IP`:$Port" -ForegroundColor Cyan
    
    try {
        # Read firmware file
        $firmwareBytes = [System.IO.File]::ReadAllBytes($FirmwareFile)
        
        # Create TCP client
        $tcpClient = New-Object System.Net.Sockets.TcpClient
        $tcpClient.Connect($IP, $Port)
        $stream = $tcpClient.GetStream()
        
        # Send authentication (if required)
        $authBytes = [System.Text.Encoding]::UTF8.GetBytes("$Password")
        $stream.Write($authBytes, 0, $authBytes.Length)
        
        # Send firmware size
        $sizeBytes = [System.BitConverter]::GetBytes([uint32]$firmwareBytes.Length)
        if ([System.BitConverter]::IsLittleEndian) {
            [Array]::Reverse($sizeBytes)
        }
        $stream.Write($sizeBytes, 0, 4)
        
        # Send firmware data
        Write-Host "Uploading $($firmwareBytes.Length) bytes..." -ForegroundColor Yellow
        $stream.Write($firmwareBytes, 0, $firmwareBytes.Length)
        
        # Wait for response
        Start-Sleep -Seconds 2
        
        $stream.Close()
        $tcpClient.Close()
        
        Write-Host "Direct TCP upload completed!" -ForegroundColor Green
        
    } catch {
        Write-Host "Direct TCP upload failed: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "This may be normal if the ESP32 uses a different OTA protocol." -ForegroundColor Yellow
    }
}

Write-Host "Upload attempt completed." -ForegroundColor Cyan
