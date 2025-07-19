# Configuration
$ESP32_IP = "192.168.250.125"

Write-Host "Uploading files to ESP32 at $ESP32_IP..."
Write-Host "Scanning data directory..."

# Get all files recursively from the data directory
$files = Get-ChildItem -Path ".\data" -File -Recurse

$totalFiles = $files.Count
$uploadedFiles = 0
$failedFiles = 0

foreach ($file in $files) {
    $relativePath = $file.FullName.Replace($PWD.Path + "\data\", "").Replace("\", "/")
    Write-Host "Uploading $($relativePath)... " -NoNewline
    
    # Use a simpler approach with Invoke-WebRequest
    try {
        # Use -InFile parameter for binary uploads
        $response = Invoke-WebRequest -Uri "http://$ESP32_IP/upload" -Method POST -InFile $file.FullName -ContentType "multipart/form-data" -TimeoutSec 30
        Write-Host "OK" -ForegroundColor Green
        $uploadedFiles++
    }
    catch {
        Write-Host "Failed" -ForegroundColor Red
        Write-Host "  Error: $_" -ForegroundColor Red
        $failedFiles++
    }
}

Write-Host "`nUpload Summary:"
Write-Host "Total files: $totalFiles"
Write-Host "Successfully uploaded: $uploadedFiles" -ForegroundColor Green
Write-Host "Failed: $failedFiles" -ForegroundColor Red

if ($failedFiles -gt 0) {
    Write-Host "`nSome files failed to upload. Check http://$ESP32_IP/debug/fs to verify the uploads." -ForegroundColor Yellow
    exit 1
} else {
    Write-Host "`nAll files uploaded successfully!" -ForegroundColor Green
    Write-Host "You can verify the files at http://$ESP32_IP/debug/fs"
    exit 0
}
