# ESP32 Web Files Upload Script - Sequential Upload Version
# 
# This script uploads files one by one with delays to prevent ESP32 memory issues
# Uses the same approach as config.html web interface
#
param(
    [string]$Directory = "./data",
    [string]$ServerIP = "192.168.250.125",
    [int]$DelayBetweenFiles = 500,    # Milliseconds between file uploads (same as config.html)
    [int]$MaxRetries = 3,            # Max retries per file
    [int]$RetryDelay = 2000,         # Milliseconds between retries
    [switch]$SequentialOnly          # Force sequential upload (recommended for large files)
)

# Progress tracking variables
$totalFiles = 0
$successfulUploads = 0
$failedUploads = 0

# Create a function for uploading a file with retry logic
function Send-FileToESP32-Sequential {
    param(
        [Parameter(Mandatory=$true)]
        [string]$FilePath,
        
        [Parameter(Mandatory=$true)]
        [string]$ServerIP,
        
        [int]$MaxRetries = 3,
        [int]$RetryDelay = 2000
    )
    
    $fileName = Split-Path -Leaf $FilePath
    $uri = "http://$ServerIP/api/upload"
    $retryCount = 0
    $success = $false
    
    Write-Host "Uploading $fileName..." -NoNewline
    
    while (-not $success -and $retryCount -le $MaxRetries) {
        if ($retryCount -gt 0) {
            Write-Host "`rRetry $retryCount of ${MaxRetries}: $fileName..." -NoNewline
            Start-Sleep -Milliseconds $RetryDelay
        }
        
        try {
            # Use HttpClient with multipart form data (like browser fetch API)
            $httpClient = New-Object System.Net.Http.HttpClient
            $httpClient.Timeout = [System.TimeSpan]::FromSeconds(60)
            
            # Read file as bytes
            $fileBytes = [System.IO.File]::ReadAllBytes($FilePath)
            $fileContent = [System.Net.Http.ByteArrayContent]::new($fileBytes)
            
            # Create multipart form data content (exactly like fetch API)
            $boundary = [System.Guid]::NewGuid().ToString()
            $content = [System.Net.Http.MultipartFormDataContent]::new($boundary)
            $content.Add($fileContent, "file", $fileName)
            
            # Send the request (same as fetch API)
            $response = $httpClient.PostAsync($uri, $content).Result
            
            if ($response.IsSuccessStatusCode) {
                Write-Host " Success!" -ForegroundColor Green
                $success = $true
                $script:successfulUploads++
                return $true
            } else {
                Write-Host " Failed with status: $([int]$response.StatusCode)" -ForegroundColor Yellow
                $retryCount++
            }
        }
        catch {
            Write-Host " Error: $($_.Exception.Message)" -ForegroundColor Red
            $retryCount++
        }
        finally {
            if ($httpClient) {
                $httpClient.Dispose()
            }
        }
    }
    
    if (-not $success) {
        Write-Host "Failed to upload $fileName after $MaxRetries retries" -ForegroundColor Red
        $script:failedUploads++
        return $false
    }
}

# Main execution block
try {
    $startTime = Get-Date
    
    # Check if directory exists
    if (-not (Test-Path -Path $Directory)) {
        Write-Host "Error: Directory '$Directory' does not exist" -ForegroundColor Red
        exit 1
    }
    
    # Get all files to upload
    $files = Get-ChildItem -Path $Directory -File -Recurse
    $totalFiles = $files.Count
    
    if ($totalFiles -eq 0) {
        Write-Host "No files found in directory '$Directory'" -ForegroundColor Yellow
        exit 0
    }
    
    # Display file list and total size
    Write-Host ""
    Write-Host "ESP32 Web Files Sequential Upload" -ForegroundColor Cyan
    Write-Host "=================================" -ForegroundColor Cyan
    Write-Host "Server: http://$ServerIP" -ForegroundColor Gray
    Write-Host "Directory: $Directory" -ForegroundColor Gray
    Write-Host "Files to upload: $totalFiles" -ForegroundColor Gray
    
    $totalSize = ($files | Measure-Object -Property Length -Sum).Sum / 1KB
    Write-Host "Total size: $([math]::Round($totalSize, 2)) KB" -ForegroundColor Gray
    Write-Host "Upload method: Sequential (like config.html)" -ForegroundColor Gray
    Write-Host ""
    
    # List files with sizes
    Write-Host "Files to upload:" -ForegroundColor Yellow
    $files | ForEach-Object {
        $size = [math]::Round($_.Length / 1024, 1)
        Write-Host "  $($_.Name) ($size KB)" -ForegroundColor Gray
    }
    Write-Host ""
    
    # Test server connectivity
    Write-Host "Testing server connectivity..." -NoNewline
    try {
        $testResponse = Invoke-WebRequest -Uri "http://$ServerIP/api/files" -Method GET -TimeoutSec 10 -UseBasicParsing
        Write-Host " Success!" -ForegroundColor Green
    }
    catch {
        Write-Host " Failed!" -ForegroundColor Red
        Write-Host "Error: Cannot connect to ESP32 at $ServerIP" -ForegroundColor Red
        Write-Host "Please check that the ESP32 is running and accessible at that IP address." -ForegroundColor Yellow
        exit 1
    }
    
    Write-Host ""
    Write-Host "Starting sequential upload..." -ForegroundColor Yellow
    Write-Host ""
    
    # Upload files sequentially
    $fileNumber = 1
    foreach ($file in $files) {
        Write-Host "[$fileNumber/$totalFiles] " -NoNewline -ForegroundColor Cyan
        
        $result = Send-FileToESP32-Sequential -FilePath $file.FullName -ServerIP $ServerIP -MaxRetries $MaxRetries -RetryDelay $RetryDelay
        
        if ($result -and $fileNumber -lt $totalFiles) {
            Write-Host "  Waiting $DelayBetweenFiles ms before next file..." -ForegroundColor Gray
            Start-Sleep -Milliseconds $DelayBetweenFiles
        }
        
        $fileNumber++
    }
    
    # Results summary
    $endTime = Get-Date
    $duration = ($endTime - $startTime).TotalSeconds
    
    Write-Host ""
    Write-Host "Upload Results:" -ForegroundColor Cyan
    Write-Host "===============" -ForegroundColor Cyan
    Write-Host "✅ Successfully uploaded: $successfulUploads files" -ForegroundColor Green
    Write-Host "❌ Failed uploads: $failedUploads files" -ForegroundColor Red
    Write-Host "⏱️  Total time: $([math]::Round($duration, 1)) seconds" -ForegroundColor Gray
    
    if ($failedUploads -eq 0) {
        Write-Host ""
        Write-Host "🎉 All files uploaded successfully!" -ForegroundColor Green
        Write-Host "Access your ESP32 web interface at: http://$ServerIP/" -ForegroundColor Cyan
    } else {
        Write-Host ""
        Write-Host "⚠️  Some files failed to upload. Check the ESP32 serial output for details." -ForegroundColor Yellow
        exit 1
    }
}
catch {
    Write-Host ""
    Write-Host "❌ Script error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
