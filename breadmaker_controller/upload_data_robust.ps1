# Robust Data Upload Script for ESP32 Breadmaker Controller
# Fixed to use the correct upload method that the firmware actually implements

param(
    [string]$targetIP = "192.168.250.125",
    [string]$specificFile = ""
)

Write-Host "=== ESP32 Data Upload (Fixed Method) ===" -ForegroundColor Cyan
Write-Host "Target IP: $targetIP" -ForegroundColor Yellow
Write-Host ""

# Function to upload a single file using the correct multipart method
function Upload-File {
    param(
        [string]$FilePath,
        [string]$TargetName,
        [string]$DeviceIP
    )
    
    try {
        Write-Host "  Uploading: $TargetName" -ForegroundColor White
        
        # Use the correct multipart form upload method that the firmware expects
        $result = curl -X POST -F "file=@$FilePath;filename=$TargetName" "http://$DeviceIP/api/upload"
        
        if ($result -match "uploaded") {
            Write-Host "    ✅ SUCCESS: $TargetName" -ForegroundColor Green
            return $true
        } else {
            Write-Host "    ❌ FAILED: $result" -ForegroundColor Red
            return $false
        }
    } catch {
        Write-Host "    ❌ ERROR: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
}

# Check if data directory exists
if (!(Test-Path "data")) {
    Write-Host "❌ Error: 'data' directory not found!" -ForegroundColor Red
    exit 1
}

$successCount = 0
$failCount = 0

if ($specificFile -ne "") {
    # Upload specific file
    if (Test-Path $specificFile) {
        $fileName = Split-Path $specificFile -Leaf
        if (Upload-File $specificFile $fileName $targetIP) {
            $successCount++
        } else {
            $failCount++
        }
    } else {
        Write-Host "❌ File not found: $specificFile" -ForegroundColor Red
        exit 1
    }
} else {
    # Upload all files in data directory
    Write-Host "Uploading all files from data directory..." -ForegroundColor Cyan
    
    $files = Get-ChildItem -Path "data" -File -Recurse
    
    foreach ($file in $files) {
        $relativePath = $file.FullName.Substring((Get-Location).Path.Length + 6) # Remove "data\" prefix
        $targetName = "/" + $relativePath.Replace("\", "/")
        
        if (Upload-File $file.FullName $targetName $targetIP) {
            $successCount++
        } else {
            $failCount++
        }
        
        Start-Sleep 1  # Brief pause between uploads
    }
}

Write-Host ""
Write-Host "=== Upload Summary ===" -ForegroundColor Cyan
Write-Host "✅ Successful: $successCount" -ForegroundColor Green
Write-Host "❌ Failed: $failCount" -ForegroundColor Red

if ($failCount -eq 0) {
    Write-Host ""
    Write-Host "🎉 All files uploaded successfully!" -ForegroundColor Green
    Write-Host "Web interface: http://$targetIP" -ForegroundColor Yellow
} else {
    Write-Host ""
    Write-Host "⚠️  Some uploads failed. Check the errors above." -ForegroundColor Yellow
    exit 1
}
