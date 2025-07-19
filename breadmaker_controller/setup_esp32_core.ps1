# ESP32 Core Setup Script
# Ensures ESP32 Arduino Core 2.0.17 is installed and removes incompatible versions

Write-Host "ESP32 Arduino Core Setup for Breadmaker Controller" -ForegroundColor Green
Write-Host "=================================================" -ForegroundColor Green
Write-Host ""

# Check if arduino-cli is installed
try {
    $arduinoVersion = arduino-cli version 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Arduino CLI not found"
    }
    Write-Host "✓ Arduino CLI found: $arduinoVersion" -ForegroundColor Green
} catch {
    Write-Host "✗ Arduino CLI not found!" -ForegroundColor Red
    Write-Host "Please install Arduino CLI first:" -ForegroundColor Yellow
    Write-Host "  https://arduino.github.io/arduino-cli/latest/installation/" -ForegroundColor White
    exit 1
}

Write-Host ""

# Update core index
Write-Host "Updating Arduino core index..." -ForegroundColor Blue
arduino-cli core update-index

# Check current ESP32 cores
Write-Host ""
Write-Host "Checking installed ESP32 cores..." -ForegroundColor Blue
$installedCores = arduino-cli core list | Select-String "esp32:esp32"

if ($installedCores) {
    Write-Host "Currently installed ESP32 cores:" -ForegroundColor Yellow
    $installedCores | ForEach-Object { Write-Host "  $($_.ToString())" -ForegroundColor White }
    
    # Check if wrong version is installed
    $wrongVersions = $installedCores | Where-Object { $_ -notmatch "2\.0\.17" }
    if ($wrongVersions) {
        Write-Host ""
        Write-Host "⚠️  INCOMPATIBLE ESP32 CORE VERSIONS DETECTED!" -ForegroundColor Red
        Write-Host "The following versions will cause compilation errors or crashes:" -ForegroundColor Red
        $wrongVersions | ForEach-Object { Write-Host "  $($_.ToString())" -ForegroundColor Red }
        
        $response = Read-Host "Remove incompatible versions? (y/N)"
        if ($response -eq "y" -or $response -eq "Y") {
            $wrongVersions | ForEach-Object {
                $version = $_.ToString().Split()[1]
                Write-Host "Removing ESP32 core version $version..." -ForegroundColor Yellow
                arduino-cli core uninstall "esp32:esp32@$version"
            }
        }
    }
}

# Check if 2.0.17 is installed
$hasCorrectVersion = arduino-cli core list | Select-String "esp32:esp32.*2\.0\.17"
if (-not $hasCorrectVersion) {
    Write-Host ""
    Write-Host "Installing ESP32 Arduino Core 2.0.17..." -ForegroundColor Green
    arduino-cli core install esp32:esp32@2.0.17
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ ESP32 Arduino Core 2.0.17 installed successfully!" -ForegroundColor Green
    } else {
        Write-Host "✗ Failed to install ESP32 Arduino Core 2.0.17" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host ""
    Write-Host "✓ ESP32 Arduino Core 2.0.17 already installed!" -ForegroundColor Green
}

# Final verification
Write-Host ""
Write-Host "Final verification..." -ForegroundColor Blue
$finalCheck = arduino-cli core list | Select-String "esp32:esp32"
Write-Host "Installed ESP32 cores:" -ForegroundColor Green
$finalCheck | ForEach-Object { Write-Host "  $($_.ToString())" -ForegroundColor White }

# Check for correct version
$correctVersion = $finalCheck | Where-Object { $_ -match "2\.0\.17" }
if ($correctVersion) {
    Write-Host ""
    Write-Host "🎉 Setup complete! ESP32 Arduino Core 2.0.17 is ready." -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "  1. Run: .\build_esp32.ps1 [port]" -ForegroundColor White
    Write-Host "  2. Example: .\build_esp32.ps1 COM3" -ForegroundColor White
} else {
    Write-Host ""
    Write-Host "⚠️  Warning: ESP32 Arduino Core 2.0.17 not found!" -ForegroundColor Red
    Write-Host "Please check the installation manually." -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "IMPORTANT: Never update the ESP32 core beyond 2.0.17!" -ForegroundColor Yellow
Write-Host "Newer versions (3.x) have breaking changes that cause crashes." -ForegroundColor Yellow
