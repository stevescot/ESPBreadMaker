# Configuration
$ESP32_IP = "192.168.250.125"
$DELAY_BETWEEN_FILES = 1000  # milliseconds

Write-Host "Uploading files to ESP32 at $ESP32_IP..."
Write-Host "Scanning data directory..."

# Get all files recursively from the data directory
$files = Get-ChildItem -Path ".\data" -File -Recurse

$totalFiles = $files.Count
$uploadedFiles = 0
$failedFiles = 0

# Function to create web boundary for multipart/form-data
function Get-WebBoundary {
    return [string]::Format("---------------------------{0}", [DateTime]::Now.Ticks.ToString("x"))
}

# Function to upload a single file
function Upload-File {
    param (
        [string]$filePath,
        [string]$targetFileName
    )
    
    try {
        $boundary = Get-WebBoundary
        $boundarybytes = [System.Text.Encoding]::ASCII.GetBytes("`r`n--$boundary`r`n")
        $request = [System.Net.HttpWebRequest]::Create("http://$ESP32_IP/upload")
        $request.Method = "POST"
        $request.ContentType = "multipart/form-data; boundary=$boundary"
        $request.KeepAlive = $false
        $request.Timeout = 60000  # 60 seconds

        # Set up request stream
        $requestStream = $request.GetRequestStream()
        
        # Add file form data
        $headerTemplate = "Content-Disposition: form-data; name=`"file`"; filename=`"{0}`"`r`nContent-Type: application/octet-stream`r`n`r`n"
        $header = [System.Text.Encoding]::UTF8.GetBytes([string]::Format($headerTemplate, $targetFileName))
        
        $requestStream.Write($boundarybytes, 0, $boundarybytes.Length)
        $requestStream.Write($header, 0, $header.Length)
        
        # Read and write file content in smaller chunks
        $fileStream = [System.IO.File]::OpenRead($filePath)
        $buffer = New-Object byte[] 4096
        $bytesRead = 0
        
        while (($bytesRead = $fileStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $requestStream.Write($buffer, 0, $bytesRead)
            Start-Sleep -Milliseconds 10  # Small delay between chunks
        }
        
        $fileStream.Close()
        
        # Add closing boundary
        $boundarybytes = [System.Text.Encoding]::ASCII.GetBytes("`r`n--$boundary--`r`n")
        $requestStream.Write($boundarybytes, 0, $boundarybytes.Length)
        $requestStream.Close()
        
        # Get response
        $response = $request.GetResponse()
        $response.Close()
        
        return $true
    }
    catch {
        Write-Host "  Error: $_" -ForegroundColor Red
        return $false
    }
}

foreach ($file in $files) {
    $relativePath = $file.FullName.Replace($PWD.Path + "\data\", "").Replace("\", "/")
    Write-Host "Uploading $($relativePath)... " -NoNewline
    
    if (Upload-File -filePath $file.FullName -targetFileName $relativePath) {
        Write-Host "OK" -ForegroundColor Green
        $uploadedFiles++
    }
    else {
        Write-Host "Failed" -ForegroundColor Red
        $failedFiles++
    }
    
    # Add delay between uploads to avoid overwhelming the ESP
    Start-Sleep -Milliseconds $DELAY_BETWEEN_FILES
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
