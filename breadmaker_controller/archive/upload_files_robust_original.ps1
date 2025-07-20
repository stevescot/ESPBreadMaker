# ESP32 Breadmaker Web Files Upload Script
# 
# This is the main script for uploading web files to the ESP32 via HTTP.
# Uses the /api/upload endpoint with retry logic and progress tracking.
#
# Usage examples:
#   .\upload_files_robust.ps1                          # Upload all files from ./data to 192.168.250.125
#   .\upload_files_robust.ps1 -ServerIP "192.168.1.100" # Upload to different IP
#   .\upload_files_robust.ps1 -PreserveDirectories     # Keep folder structure (if server supports it)
#
param(
    [string]$Directory = "./data",
    [string]$ServerIP = "192.168.250.125",
    [int]$DelayBetweenFiles = 500,    # Milliseconds between file uploads
    [int]$MaxRetries = 3,            # Max retries per file
    [int]$RetryDelay = 2000,         # Milliseconds between retries
    [switch]$PreserveDirectories     # Keep directory structure when uploading
)

# Progress tracking variables
$totalFiles = 0
$successfulUploads = 0
$failedUploads = 0
$filesRequiringRetry = 0

# Create a function for uploading a file with retry logic
function Send-FileToESP32 {
    param(
        [Parameter(Mandatory=$true)]
        [string]$FilePath,
        
        [Parameter(Mandatory=$true)]
        [string]$ServerIP,
        
        [string]$RelativePath = "",
        [int]$MaxRetries = 3,
        [int]$RetryDelay = 2000
    )
    
    # Handle destination path based on whether we're preserving directories
    if ($RelativePath -and $PreserveDirectories) {
        $fileName = $RelativePath
        # Ensure proper formatting with leading slash
        if (-not $fileName.StartsWith("/")) {
            $fileName = "/" + $fileName
        }
    } else {
        $fileName = Split-Path -Leaf $FilePath
    }
    
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
            $fileBytes = [System.IO.File]::ReadAllBytes($FilePath)
            $fileContent = [System.Net.Http.ByteArrayContent]::new($fileBytes)
            
            $boundary = [System.Guid]::NewGuid().ToString()
            $contentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::new("multipart/form-data")
            $contentType.Parameters.Add([System.Net.Http.Headers.NameValueHeaderValue]::new("boundary", $boundary))
            
            $content = [System.Net.Http.MultipartFormDataContent]::new($boundary)
            $content.Add($fileContent, "file", $fileName)
            
            # Use HttpClient for better connection handling
            $httpClient = New-Object System.Net.Http.HttpClient
            $httpClient.Timeout = [System.TimeSpan]::FromSeconds(30)
            
            # Send the request
            $response = $httpClient.PostAsync($uri, $content).Result
            $httpClient.Dispose()
            
            if ($response.IsSuccessStatusCode) {
                Write-Host " Success! ($([int]$response.StatusCode))" -ForegroundColor Green
                $success = $true
                $script:successfulUploads++
                return $true
            } else {
                Write-Host " Failed with status code: $([int]$response.StatusCode)" -ForegroundColor Yellow
                $retryCount++
                $script:filesRequiringRetry++
            }
        }
        catch {
            Write-Host " Error: $($_.Exception.Message)" -ForegroundColor Red
            $retryCount++
            $script:filesRequiringRetry++
        }
    }
    
    if (-not $success) {
        Write-Host "Failed to upload $fileName after $MaxRetries retries" -ForegroundColor Red
        $script:failedUploads++
        return $false
    }
}

# Function to get directory size
function Get-DirectorySize {
    param([string]$Path)
    
    $size = (Get-ChildItem -Path $Path -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1KB
    return [math]::Round($size, 2)
}

# Main execution block
try {
    $startTime = Get-Date
    
    # Check if directory exists
    if (-not (Test-Path -Path $Directory)) {
        throw "Directory '$Directory' not found!"
    }
    
    # Get all files
    $files = Get-ChildItem -Path $Directory -Recurse -File
    $totalFiles = $files.Count
    $totalSize = Get-DirectorySize -Path $Directory
    
    Write-Host "Starting upload of $totalFiles files ($totalSize KB) to http://$ServerIP" -ForegroundColor Cyan
    Write-Host "Using $DelayBetweenFiles ms delay between files, max $MaxRetries retries with $RetryDelay ms retry delay" -ForegroundColor Cyan
    if ($PreserveDirectories) {
        Write-Host "Directory structure will be preserved on upload" -ForegroundColor Cyan
    } else {
        Write-Host "Files will be uploaded to root directory" -ForegroundColor Cyan
    }
    Write-Host "-------------------------------------------------------------------------" -ForegroundColor Cyan
    
    # Check connectivity first
    try {
        $testConnection = Invoke-WebRequest -Uri "http://$ServerIP/" -Method Head -TimeoutSec 5 -ErrorAction Stop
        Write-Host "ESP32 server at $ServerIP is reachable, status: $($testConnection.StatusCode)" -ForegroundColor Green
    }
    catch {
        Write-Host "Warning: Could not reach ESP32 server at $ServerIP. Continuing anyway..." -ForegroundColor Yellow
        Write-Host "Error details: $($_.Exception.Message)" -ForegroundColor Yellow
    }
    
    # Upload each file
    foreach ($file in $files) {
        # Calculate the relative path if preserving directories
        $relativePath = ""
        if ($PreserveDirectories) {
            $relativePath = $file.FullName.Replace($Directory, "").TrimStart("\", "/").Replace("\", "/")
            Write-Host "Processing $($file.Name) as $relativePath" -ForegroundColor Gray
        }
        
        # Upload the file with retry logic
        Send-FileToESP32 -FilePath $file.FullName -ServerIP $ServerIP -RelativePath $relativePath -MaxRetries $MaxRetries -RetryDelay $RetryDelay
        
        # Delay between uploads to avoid overwhelming the ESP32
        if ($DelayBetweenFiles -gt 0) {
            Start-Sleep -Milliseconds $DelayBetweenFiles
        }
    }
    
    $endTime = Get-Date
    $duration = ($endTime - $startTime).TotalSeconds
    
    # Summary
    Write-Host "-------------------------------------------------------------------------" -ForegroundColor Cyan
    Write-Host "Upload Summary:" -ForegroundColor Cyan
    Write-Host "  Total files: $totalFiles ($totalSize KB)" -ForegroundColor White
    Write-Host "  Successfully uploaded: $successfulUploads" -ForegroundColor Green
    Write-Host "  Failed uploads: $failedUploads" -ForegroundColor Red
    Write-Host "  Files requiring retry: $filesRequiringRetry" -ForegroundColor Yellow
    Write-Host "  Duration: $([math]::Round($duration, 2)) seconds" -ForegroundColor White
    Write-Host "-------------------------------------------------------------------------" -ForegroundColor Cyan
}
catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
