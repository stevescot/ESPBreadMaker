param(
    [string]$ServerIP = "192.168.250.125",
    [string]$TestFile = "./data/index.html",
    [switch]$Verbose
)

# Show help if no file specified
if (-not (Test-Path -Path $TestFile)) {
    Write-Host "Test file not found: $TestFile"
    Write-Host "Usage: ./test_upload_robust.ps1 -ServerIP 192.168.4.1 -TestFile ./data/index.html [-Verbose]"
    exit 1
}

# Construct the URL
$uri = "http://$ServerIP/upload"

Write-Host "Testing file upload to $uri..." -ForegroundColor Cyan
Write-Host "Using test file: $TestFile" -ForegroundColor Cyan

# Test connectivity first
try {
    Write-Host "Testing connectivity..." -NoNewline
    $testConnection = Invoke-WebRequest -Uri "http://$ServerIP/" -Method Head -TimeoutSec 5 -ErrorAction Stop
    Write-Host "OK! Status: $($testConnection.StatusCode)" -ForegroundColor Green
}
catch {
    Write-Host "FAILED!" -ForegroundColor Red
    Write-Host "Could not connect to ESP32 server at $ServerIP" -ForegroundColor Red
    Write-Host "Error details: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# Measure file size
$fileInfo = Get-Item -Path $TestFile
$fileSize = $fileInfo.Length
Write-Host "File size: $fileSize bytes" -ForegroundColor Cyan

# Upload the file
try {
    Write-Host "Uploading file..." -NoNewline
    
    # Using System.Net.Http.HttpClient for better connection handling
    $fileName = Split-Path -Leaf $TestFile
    $fileBytes = [System.IO.File]::ReadAllBytes($TestFile)
    $fileContent = [System.Net.Http.ByteArrayContent]::new($fileBytes)
    
    $boundary = [System.Guid]::NewGuid().ToString()
    $contentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::new("multipart/form-data")
    $contentType.Parameters.Add([System.Net.Http.Headers.NameValueHeaderValue]::new("boundary", $boundary))
    
    $content = [System.Net.Http.MultipartFormDataContent]::new($boundary)
    $content.Add($fileContent, "file", $fileName)
    
    $httpClient = New-Object System.Net.Http.HttpClient
    $httpClient.Timeout = [System.TimeSpan]::FromSeconds(30)
    
    $startTime = Get-Date
    if ($Verbose) {
        Write-Host ""
        Write-Host "Sending $fileName ($fileSize bytes) to $uri" -ForegroundColor Gray
    }
    
    $response = $httpClient.PostAsync($uri, $content).Result
    $httpClient.Dispose()
    
    $endTime = Get-Date
    $duration = ($endTime - $startTime).TotalSeconds
    
    if ($response.IsSuccessStatusCode) {
        $responseContent = $response.Content.ReadAsStringAsync().Result
        Write-Host " Success! ($([int]$response.StatusCode))" -ForegroundColor Green
        Write-Host "Upload completed in $([math]::Round($duration, 2)) seconds ($([math]::Round($fileSize / $duration / 1024, 2)) KB/s)" -ForegroundColor Green
        
        if ($Verbose -and $responseContent) {
            Write-Host "Response: $responseContent" -ForegroundColor Gray
        }
        
        return $true
    } else {
        Write-Host " Failed with status code: $([int]$response.StatusCode)" -ForegroundColor Red
        $responseContent = $response.Content.ReadAsStringAsync().Result
        if ($responseContent) {
            Write-Host "Response: $responseContent" -ForegroundColor Red
        }
        return $false
    }
}
catch {
    Write-Host " ERROR!" -ForegroundColor Red
    Write-Host "Upload failed: $($_.Exception.Message)" -ForegroundColor Red
    if ($Verbose) {
        Write-Host $_.Exception -ForegroundColor Red
    }
    return $false
}
