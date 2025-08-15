# OTA Upload Script for ESP32
# Uploads compiled firmware to ESP32 over WiFi

param(
    [Parameter(Mandatory=$true)]
    [string]$IP,                # ESP32 IP address
    [string]$Password = "",     # OTA password (optional)
    [int]$Port = 3232,         # OTA port (default 3232)
    [switch]$Help
)

if ($Help) {
    Write-Host "OTA Upload Script Usage:" -ForegroundColor Cyan
    Write-Host "  .\upload_ota.ps1 -IP <ip_address> [options]" -ForegroundColor White
    Write-Host ""
    Write-Host "Parameters:" -ForegroundColor Yellow
    Write-Host "  -IP <address>       ESP32 IP address (required)" -ForegroundColor White
    Write-Host "  -Password <pwd>     OTA password (if required)" -ForegroundColor White
    Write-Host "  -Port <port>        OTA port (default: 3232)" -ForegroundColor White
    Write-Host "  -Help               Show this help" -ForegroundColor White
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Green
    Write-Host "  .\upload_ota.ps1 -IP 192.168.250.125" -ForegroundColor Gray
    Write-Host "  .\upload_ota.ps1 -IP 192.168.250.125 -Password mypass" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Note: Make sure the ESP32 is connected to WiFi and OTA is enabled." -ForegroundColor Yellow
    exit 0
}

Write-Host "ESP32 OTA Upload Tool" -ForegroundColor Green
Write-Host "Target: $IP`:$Port" -ForegroundColor Cyan

# Find the compiled binary
$binaryPath = "build\esp32.esp32.esp32\breadmaker_controller.ino.bin"
if (-not (Test-Path $binaryPath)) {
    Write-Host "Error: Compiled binary not found at $binaryPath" -ForegroundColor Red
    Write-Host "Please compile the project first using: .\build_esp32.ps1" -ForegroundColor Yellow
    exit 1
}

Write-Host "Binary found: $binaryPath" -ForegroundColor Green
$fileSize = (Get-Item $binaryPath).Length
Write-Host "File size: $($fileSize) bytes" -ForegroundColor White

# Try to find espota.py in Arduino ESP32 installation
$possiblePaths = @(
    "$env:LOCALAPPDATA\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py",
    "$env:APPDATA\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py",
    "C:\Users\*\AppData\Local\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py"
)

$espotaPath = $null
foreach ($pathPattern in $possiblePaths) {
    $found = Get-ChildItem $pathPattern -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        $espotaPath = $found.FullName
        break
    }
}

if (-not $espotaPath) {
    Write-Host "Error: espota.py not found in Arduino ESP32 installation" -ForegroundColor Red
    Write-Host "Please make sure ESP32 Arduino Core is installed via Arduino IDE or arduino-cli" -ForegroundColor Yellow
    exit 1
}

Write-Host "Using espota.py: $espotaPath" -ForegroundColor Green

# Test connectivity
Write-Host "Testing connectivity to $IP`:$Port..." -ForegroundColor Blue
try {
    $tcpClient = New-Object System.Net.Sockets.TcpClient
    $tcpClient.ReceiveTimeout = 3000
    $tcpClient.SendTimeout = 3000
    $tcpClient.Connect($IP, $Port)
    $tcpClient.Close()
    Write-Host "✓ Device is reachable" -ForegroundColor Green
} catch {
    Write-Host "✗ Cannot reach device at $IP`:$Port" -ForegroundColor Red
    Write-Host "Make sure the ESP32 is:" -ForegroundColor Yellow
    Write-Host "  - Connected to WiFi" -ForegroundColor White
    Write-Host "  - Running firmware with OTA enabled" -ForegroundColor White
    Write-Host "  - Accessible on the network" -ForegroundColor White
    exit 1
}

# Build espota command
$cmd = @("python", "`"$espotaPath`"", "-i", $IP, "-p", $Port, "-f", "`"$binaryPath`"")
if ($Password) {
    $cmd += @("-a", $Password)
}

Write-Host "Starting OTA upload..." -ForegroundColor Blue
Write-Host "Command: $($cmd -join ' ')" -ForegroundColor Gray

# Execute upload
$process = Start-Process -FilePath "python" -ArgumentList @("`"$espotaPath`"", "-i", $IP, "-p", $Port, "-f", "`"$binaryPath`"" + $(if ($Password) { @("-a", $Password) } else { @() })) -NoNewWindow -Wait -PassThru -RedirectStandardOutput "ota_output.log" -RedirectStandardError "ota_error.log"

# Check results
if ($process.ExitCode -eq 0) {
    Write-Host "✓ OTA upload completed successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  - The ESP32 will restart automatically" -ForegroundColor White
    Write-Host "  - Wait for it to reconnect to WiFi (~10-30 seconds)" -ForegroundColor White
    Write-Host "  - Access web interface at: http://$IP" -ForegroundColor White
} else {
    Write-Host "✗ OTA upload failed!" -ForegroundColor Red
    Write-Host "Exit code: $($process.ExitCode)" -ForegroundColor Red
    
    if (Test-Path "ota_error.log") {
        $errorContent = Get-Content "ota_error.log"
        if ($errorContent) {
            Write-Host "Error details:" -ForegroundColor Yellow
            Write-Host $errorContent -ForegroundColor Red
        }
    }
    
    if (Test-Path "ota_output.log") {
        $outputContent = Get-Content "ota_output.log"
        if ($outputContent) {
            Write-Host "Output:" -ForegroundColor Yellow
            Write-Host $outputContent -ForegroundColor White
        }
    }
    exit 1
}

# Cleanup log files
Remove-Item "ota_output.log" -ErrorAction SilentlyContinue
Remove-Item "ota_error.log" -ErrorAction SilentlyContinue

Write-Host "OTA upload process completed!" -ForegroundColor Green
