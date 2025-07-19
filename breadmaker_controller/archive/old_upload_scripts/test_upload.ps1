

param(
    [string]$ESP32_IP = "192.168.250.125"
)

Write-Host "Testing ESP32 streaming upload for all files in .\data..."

# Test connection first
try {
    Invoke-WebRequest -Uri "http://$ESP32_IP/debug/fs" -Method GET -TimeoutSec 5 | Out-Null
    Write-Host "ESP32 is reachable"
} catch {
    Write-Host "ESP32 not reachable: $($_.Exception.Message)"
    exit 1
}

# Upload all files in the data directory using multipart/form-data (streaming)
$files = Get-ChildItem -File .\data
foreach ($file in $files) {
    try {
        $resp = Invoke-WebRequest -Uri "http://$ESP32_IP/upload" -Method POST -InFile $file.FullName -ContentType "multipart/form-data" -TimeoutSec 30
        Write-Host "Uploaded $($file.Name): $($resp.StatusCode)"
    } catch {
        Write-Host "Failed $($file.Name): $($_.Exception.Message)"
    }
}
