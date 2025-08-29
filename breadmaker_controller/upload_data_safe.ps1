# Safe Data Upload Script - LEGACY - Use esp32-manager.ps1 instead
# Redirects to the new unified manager for consistency

param(
    [string]$targetIP = "192.168.250.125",
    [string]$specificFile = ""
)

Write-Host "⚠️ Legacy script detected!" -ForegroundColor Yellow
Write-Host "Redirecting to esp32-manager.ps1 for better safety and features..." -ForegroundColor Cyan
Write-Host ""

if ($specificFile -ne "") {
    & ".\esp32-manager.ps1" -UploadWeb -Files $specificFile -IP $targetIP
} else {
    & ".\esp32-manager.ps1" -UploadWeb -IP $targetIP
}

# Function to upload a single file
function Upload-File {
    param(
        [string]$FilePath,
        [string]$TargetName,
        [string]$DeviceIP
    )
    
    try {
        Write-Host "  Uploading: $TargetName" -ForegroundColor White
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
    # Upload specific file only if it's not a program file
    if ($specificFile -match "(programs|program_\d+)\.json$") {
        Write-Host "❌ Error: Cannot upload program files with safe mode!" -ForegroundColor Red
        Write-Host "Program file detected: $specificFile" -ForegroundColor Yellow
        Write-Host "Use upload_data_robust.ps1 -IncludeJSON if you really need to overwrite programs" -ForegroundColor Gray
        exit 1
    }
    
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
    # Upload all files except program files
    Write-Host "Uploading web interface files (excluding program settings)..." -ForegroundColor Cyan
    
    $files = Get-ChildItem -Path "data" -File -Recurse | Where-Object { 
        $_.Name -notmatch "(programs|program_\d+)\.json$" 
    }
    
    if ($files.Count -eq 0) {
        Write-Host "No files found to upload." -ForegroundColor Yellow
        exit 0
    }
    
    Write-Host "Found $($files.Count) files to upload" -ForegroundColor Yellow
    
    # List excluded files for transparency
    $excludedFiles = Get-ChildItem -Path "data" -File -Recurse | Where-Object { 
        $_.Name -match "(programs|program_\d+)\.json$" 
    }
    if ($excludedFiles.Count -gt 0) {
        Write-Host "🔒 Excluded files (preserved on device):" -ForegroundColor Gray
        foreach ($excluded in $excludedFiles) {
            Write-Host "    - $($excluded.Name)" -ForegroundColor Gray
        }
    }
    Write-Host ""
    
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
Write-Host "🔒 Program settings: PRESERVED" -ForegroundColor Green

if ($failCount -eq 0) {
    Write-Host ""
    Write-Host "🎉 Web interface updated successfully!" -ForegroundColor Green
    Write-Host "📋 Your program settings remain unchanged on device" -ForegroundColor Green
    Write-Host "🌐 Web interface: http://$targetIP" -ForegroundColor Yellow
} else {
    Write-Host ""
    Write-Host "⚠️  Some uploads failed. Check the errors above." -ForegroundColor Yellow
    exit 1
}
