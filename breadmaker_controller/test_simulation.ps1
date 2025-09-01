# ESP32 Breadmaker Simulation Test Script
# This script builds and runs the ESP32 code in a native simulation environment

param(
    [switch]$Build,
    [switch]$Run,
    [switch]$Test,
    [switch]$Clean,
    [double]$TimeAcceleration = 60,
    [double]$RoomTemp = 20.0,
    [string]$TestDuration = "10m",
    [switch]$Help
)

if ($Help) {
    Write-Host @"
ESP32 Breadmaker Simulation Test Script

Usage:
  .\test_simulation.ps1 [options]

Options:
  -Build                Build the simulation executable
  -Run                  Run the simulation
  -Test                 Run automated test sequence
  -Clean                Clean build files
  -TimeAcceleration     Set time acceleration factor (default: 60x)
  -RoomTemp            Set room temperature in °C (default: 20.0)
  -TestDuration        Duration for test runs (e.g., "10m", "1h")
  -Help                Show this help

Examples:
  .\test_simulation.ps1 -Build -Run                    # Build and run simulation
  .\test_simulation.ps1 -Test -TimeAcceleration 300    # Run tests at 300x speed
  .\test_simulation.ps1 -Run -RoomTemp 25.0           # Run with 25°C room temp
  .\test_simulation.ps1 -Clean                        # Clean build files

The simulation will:
- Compile your ESP32 code for PC execution
- Mock all hardware components (sensors, heater, motor, display)
- Run PID control, fermentation calculations, and web server
- Provide accelerated time for fast testing
- Log all state changes and sensor readings
"@
    return
}

$ErrorActionPreference = "Stop"

Write-Host "🍞 ESP32 Breadmaker Simulation" -ForegroundColor Green
Write-Host "==============================" -ForegroundColor Green

# Check if PlatformIO is available
try {
    $platformio = Get-Command pio -ErrorAction Stop
    Write-Host "✓ PlatformIO found: $($platformio.Source)" -ForegroundColor Green
} catch {
    Write-Host "❌ PlatformIO not found. Please install PlatformIO CLI" -ForegroundColor Red
    Write-Host "   Install with: pip install platformio" -ForegroundColor Yellow
    exit 1
}

# Clean build files
if ($Clean) {
    Write-Host "🧹 Cleaning build files..." -ForegroundColor Yellow
    try {
        pio run -e native_sim -t clean
        Write-Host "✓ Build files cleaned" -ForegroundColor Green
    } catch {
        Write-Host "⚠️  Clean failed (might be normal if no previous build)" -ForegroundColor Yellow
    }
}

# Build simulation
if ($Build -or $Run -or $Test) {
    Write-Host "🔨 Building simulation..." -ForegroundColor Yellow
    Write-Host "   Target: native_sim" -ForegroundColor Gray
    Write-Host "   Time acceleration: ${TimeAcceleration}x" -ForegroundColor Gray
    Write-Host "   Room temperature: ${RoomTemp}°C" -ForegroundColor Gray
    
    try {
        # Build with custom flags
        $env:PLATFORMIO_BUILD_FLAGS = "-DSIMULATION_TIME_ACCELERATION=$TimeAcceleration -DSIMULATION_ROOM_TEMP=$RoomTemp"
        pio run -e native_sim
        Write-Host "✓ Build completed successfully" -ForegroundColor Green
    } catch {
        Write-Host "❌ Build failed" -ForegroundColor Red
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 1
    }
}

# Run simulation
if ($Run -or $Test) {
    Write-Host "🚀 Starting simulation..." -ForegroundColor Yellow
    
    # Find the executable
    $exePath = ".pio\build\native_sim\program.exe"
    if (-not (Test-Path $exePath)) {
        $exePath = ".pio\build\native_sim\program"  # Linux/Mac
        if (-not (Test-Path $exePath)) {
            Write-Host "❌ Simulation executable not found" -ForegroundColor Red
            exit 1
        }
    }
    
    Write-Host "   Executable: $exePath" -ForegroundColor Gray
    Write-Host "" -ForegroundColor Gray
    Write-Host "Simulation Controls:" -ForegroundColor Cyan
    Write-Host "  - Time runs at ${TimeAcceleration}x speed" -ForegroundColor Cyan
    Write-Host "  - Room temperature: ${RoomTemp}°C" -ForegroundColor Cyan
    Write-Host "  - Press Ctrl+C to stop simulation" -ForegroundColor Cyan
    Write-Host "  - Monitor output for sensor readings and PID control" -ForegroundColor Cyan
    Write-Host "" -ForegroundColor Gray
    
    try {
        if ($Test) {
            Write-Host "🧪 Running automated test sequence..." -ForegroundColor Magenta
            # Run with timeout for automated testing
            $process = Start-Process -FilePath $exePath -NoNewWindow -PassThru
            
            # Convert test duration to seconds
            $durationSeconds = switch -Regex ($TestDuration) {
                '(\d+)s$' { [int]$matches[1] }
                '(\d+)m$' { [int]$matches[1] * 60 }
                '(\d+)h$' { [int]$matches[1] * 3600 }
                default { 600 } # Default 10 minutes
            }
            
            Write-Host "   Test duration: $TestDuration (${durationSeconds}s real time)" -ForegroundColor Gray
            
            # Wait for test completion or timeout
            if (-not $process.WaitForExit($durationSeconds * 1000)) {
                Write-Host "⏰ Test duration reached, stopping simulation..." -ForegroundColor Yellow
                $process.Kill()
            }
            
            Write-Host "✓ Test completed" -ForegroundColor Green
        } else {
            # Run interactively
            & $exePath
        }
    } catch {
        Write-Host "❌ Simulation failed to start" -ForegroundColor Red
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 1
    }
}

Write-Host "" -ForegroundColor Gray
Write-Host "🎯 Simulation Tips:" -ForegroundColor Blue
Write-Host "   • Use higher time acceleration (300x+) for long fermentation tests" -ForegroundColor Blue
Write-Host "   • Monitor PID output for temperature control validation" -ForegroundColor Blue
Write-Host "   • Check fermentation factor calculations at different temperatures" -ForegroundColor Blue
Write-Host "   • Test stage advancement and timing accuracy" -ForegroundColor Blue
Write-Host "   • Validate web server endpoints during simulation" -ForegroundColor Blue
