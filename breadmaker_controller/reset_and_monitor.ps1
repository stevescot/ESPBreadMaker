# ESP32 Reset and Monitor Script
param(
    [string]$Port = "COM3"
)

Write-Host "Resetting ESP32 and monitoring serial output..." -ForegroundColor Green

# First, trigger a reset using esptool
try {
    Write-Host "Triggering ESP32 reset..." -ForegroundColor Yellow
    # Use esptool to reset the ESP32
    $resetResult = python -m esptool --chip esp32 --port $Port run 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Reset successful" -ForegroundColor Green
    } else {
        Write-Host "Reset command completed" -ForegroundColor Yellow
    }
    
    Write-Host "Waiting for ESP32 to boot..." -ForegroundColor Yellow
    Start-Sleep -Seconds 3
    
    # Now open port to capture output
    $serialPort = New-Object System.IO.Ports.SerialPort
    $serialPort.PortName = $Port
    $serialPort.BaudRate = 115200
    $serialPort.Open()
    
    Write-Host "Capturing serial output for 15 seconds..." -ForegroundColor Cyan
    Write-Host "----------------------------------------" -ForegroundColor Gray
    
    $endTime = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $endTime) {
        if ($serialPort.BytesToRead -gt 0) {
            $data = $serialPort.ReadExisting()
            Write-Host $data -NoNewline
        }
        Start-Sleep -Milliseconds 50
    }
    
    $serialPort.Close()
    Write-Host "`n----------------------------------------" -ForegroundColor Gray
    Write-Host "Serial capture completed" -ForegroundColor Green
    
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    if ($serialPort -and $serialPort.IsOpen) {
        $serialPort.Close()
    }
}
