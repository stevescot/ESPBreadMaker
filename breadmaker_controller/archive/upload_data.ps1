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
    
    try {
        $formData = @{
            file = Get-Item $file.FullName
        }

        $response = Invoke-RestMethod -Uri "http://$ESP32_IP/upload" -Method Post -Form $formData
        Write-Host "OK" -ForegroundColor Green
        $uploadedFiles++
}

Write-Host "`nUpload Summary:"
Write-Host "Total files: $totalFiles"
Write-Host "Successfully uploaded: $uploadedFiles" -ForegroundColor Green
Write-Host "Failed: $failedFiles" -ForegroundColor Red

if ($failedFiles -gt 0) {
    exit 1
} else {
    Write-Host "`nAll files uploaded successfully!" -ForegroundColor Green
    Write-Host "You can verify the files at http://$ESP32_IP/debug/fs"
    exit 0
}
