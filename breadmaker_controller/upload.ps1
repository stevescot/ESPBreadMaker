# ESP32 Breadmaker Unified Upload Script
# 
# This unified script handles all upload scenarios:
# - Single file uploads
# - Multiple file uploads 
# - Full directory uploads
# - OTA firmware uploads
# - Different upload methods (HTTP API, debug endpoint)
#
# Usage examples:
#   .\upload.ps1                                    # Upload all files from ./data
#   .\upload.ps1 -Files "script.js"                # Upload single file
#   .\upload.ps1 -Files "script.js","style.css"    # Upload multiple files
#   .\upload.ps1 -Directory "./data"               # Upload from specific directory
#   .\upload.ps1 -OTA -FirmwareFile "firmware.bin" # OTA firmware upload
#   .\upload.ps1 -Method Debug                     # Use debug endpoint instead of API
#   .\upload.ps1 -ServerIP "192.168.1.100"        # Upload to different IP
#
param(
    # File/Directory options
    [string[]]$Files = @(),              # Specific files to upload (relative to Directory)
    [string]$Directory = "./data",       # Source directory for files
    [string]$ServerIP = "192.168.250.125", # Target ESP32 IP address
    
    # Upload behavior
    [ValidateSet("API", "Debug")]
    [string]$Method = "API",             # Upload method: API (/api/upload) or Debug (/debug/upload)
    [int]$DelayBetweenFiles = 500,       # Milliseconds between file uploads
    [int]$MaxRetries = 3,                # Max retries per file
    [int]$RetryDelay = 2000,             # Milliseconds between retries
    [switch]$PreserveDirectories,        # Keep directory structure when uploading
    
    # OTA options
    [switch]$OTA,                        # Enable OTA firmware upload mode
    [string]$FirmwareFile = "",          # Firmware file for OTA (auto-detect if empty)
    [securestring]$Password,             # OTA password (use Read-Host -AsSecureString or ConvertTo-SecureString)
    [int]$Port = 3232,                   # OTA port
    
    # Output options
    [switch]$Quiet,                      # Reduce output verbosity
    [switch]$Help                        # Show help
)

# Show help
if ($Help) {
    Write-Host @"
ESP32 Breadmaker Unified Upload Script

USAGE:
    .\upload.ps1 [OPTIONS]

FILE/DIRECTORY OPTIONS:
    -Files <array>           Specific files to upload (e.g., "script.js","style.css")
    -Directory <path>        Source directory (default: "./data")
    -ServerIP <ip>           Target ESP32 IP address (default: "192.168.250.125")

UPLOAD BEHAVIOR:
    -Method <API|Debug>      Upload method (default: "API")
                             API: Uses /api/upload endpoint
                             Debug: Uses /debug/upload endpoint
    -DelayBetweenFiles <ms>  Delay between uploads (default: 500ms)
    -MaxRetries <count>      Max retries per file (default: 3)
    -RetryDelay <ms>         Delay between retries (default: 2000ms)
    -PreserveDirectories     Keep folder structure on upload

OTA OPTIONS:
    -OTA                     Enable OTA firmware upload mode
    -FirmwareFile <path>     Firmware file for OTA (auto-detect if empty)
    -Password <pwd>          OTA password (use: ConvertTo-SecureString "admin" -AsPlainText -Force)
    -Port <port>             OTA port (default: 3232)

OUTPUT OPTIONS:
    -Quiet                   Reduce output verbosity
    -Help                    Show this help

EXAMPLES:
    .\upload.ps1                                    # Upload all files from ./data
    .\upload.ps1 -Files "script.js"                # Upload single file
    .\upload.ps1 -Files "script.js","style.css"    # Upload multiple files
    .\upload.ps1 -Directory "./data"               # Upload from specific directory
    .\upload.ps1 -OTA -FirmwareFile "firmware.bin" # OTA firmware upload
    .\upload.ps1 -Method Debug                     # Use debug endpoint
    .\upload.ps1 -ServerIP "192.168.1.100"        # Upload to different IP
"@ -ForegroundColor Cyan
    exit 0
}

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
        [string]$Method = "API",
        [int]$MaxRetries = 3,
        [int]$RetryDelay = 2000,
        [bool]$PreserveDirectories = $false,
        [bool]$Quiet = $false
    )
    
    # Handle destination path based on whether we're preserving directories
    if ($RelativePath -and $PreserveDirectories) {
        $fileName = $RelativePath
        # Ensure proper formatting with leading slash for API method
        if ($Method -eq "API" -and -not $fileName.StartsWith("/")) {
            $fileName = "/" + $fileName
        }
    } else {
        $fileName = Split-Path -Leaf $FilePath
    }
    
    # Set upload URI based on method
    if ($Method -eq "Debug") {
        $uri = "http://$ServerIP/debug/upload"
    } else {
        $uri = "http://$ServerIP/api/upload"
    }
    
    $retryCount = 0
    $success = $false
    
    if (-not $Quiet) {
        Write-Host "Uploading $fileName..." -NoNewline
    }
    
    while (-not $success -and $retryCount -le $MaxRetries) {
        if ($retryCount -gt 0) {
            if (-not $Quiet) {
                Write-Host "`rRetry $retryCount of ${MaxRetries}: $fileName..." -NoNewline
            }
            Start-Sleep -Milliseconds $RetryDelay
        }
        
        try {
            if ($Method -eq "Debug") {
                # Use curl for debug method (more reliable for some endpoints)
                $curlCommand = "curl -X POST -F 'filename=$fileName' -F 'content=@$FilePath' '$uri'"
                $result = cmd /c $curlCommand 2>&1
                
                if ($result -match "success|ok|uploaded" -or $LASTEXITCODE -eq 0) {
                    if (-not $Quiet) {
                        Write-Host " Success! (Debug)" -ForegroundColor Green
                    }
                    $success = $true
                    $script:successfulUploads++
                    return $true
                } else {
                    if (-not $Quiet) {
                        Write-Host " Failed (Debug): $result" -ForegroundColor Yellow
                    }
                    $retryCount++
                    $script:filesRequiringRetry++
                }
            } else {
                # Use HTTP client for API method
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
                    if (-not $Quiet) {
                        Write-Host " Success! ($([int]$response.StatusCode))" -ForegroundColor Green
                    }
                    $success = $true
                    $script:successfulUploads++
                    return $true
                } else {
                    if (-not $Quiet) {
                        Write-Host " Failed with status code: $([int]$response.StatusCode)" -ForegroundColor Yellow
                    }
                    $retryCount++
                    $script:filesRequiringRetry++
                }
            }
        }
        catch {
            if (-not $Quiet) {
                Write-Host " Error: $($_.Exception.Message)" -ForegroundColor Red
            }
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

# Function to perform OTA upload
function Send-OTAToESP32 {
    param(
        [string]$IP,
        [string]$FirmwareFile,
        [securestring]$Password,
        [int]$Port,
        [bool]$Quiet = $false
    )
    
    # Convert SecureString to plain text for espota.py
    $plainPassword = "admin"  # Default
    if ($Password) {
        $plainPassword = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($Password))
    }
    
    if (-not $Quiet) {
        Write-Host "=== ESP32 OTA Upload ===" -ForegroundColor Cyan
        Write-Host "Target IP: $IP" -ForegroundColor White
        Write-Host "Target Port: $Port" -ForegroundColor White
    }
    
    # Find firmware file if not specified
    if (-not $FirmwareFile) {
        if (-not $Quiet) {
            Write-Host "Searching for firmware file..." -ForegroundColor Yellow
        }
        
        # Look for the main firmware binary
        $patterns = @(
            "build\breadmaker_controller.ino.bin",
            "*.ino.bin",
            "breadmaker_controller.ino.bin", 
            "build/*.ino.bin",
            "*.bin"
        )
        
        foreach ($pattern in $patterns) {
            $files = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Where-Object { 
                $_.Name -notmatch "bootloader|partition|fatfs" -and $_.Length -gt 100KB
            } | Sort-Object LastWriteTime -Descending
            
            if ($files) {
                $FirmwareFile = $files[0].FullName
                if (-not $Quiet) {
                    Write-Host "Found firmware: $($files[0].Name)" -ForegroundColor Green
                }
                break
            }
        }
        
        if (-not $FirmwareFile) {
            Write-Host "ERROR: No firmware file found. Please specify -FirmwareFile" -ForegroundColor Red
            return $false
        }
    }
    
    # Check if firmware file exists
    if (-not (Test-Path $FirmwareFile)) {
        Write-Host "ERROR: Firmware file not found: $FirmwareFile" -ForegroundColor Red
        return $false
    }
    
    $fileSize = (Get-Item $FirmwareFile).Length
    if (-not $Quiet) {
        Write-Host "Firmware file: $FirmwareFile ($([math]::Round($fileSize/1KB, 1)) KB)" -ForegroundColor White
    }
    
    # Try to find espota.py in Arduino ESP32 installation (same method as build_esp32.ps1)
    $possiblePaths = @(
        "$env:LOCALAPPDATA\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py",
        "$env:APPDATA\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py",
        "C:\Users\*\AppData\Local\Arduino15\packages\esp32\hardware\esp32\*\tools\espota.py"
    )
    
    $espotaPath = $null
    foreach ($pathPattern in $possiblePaths) {
        $found = Get-ChildItem $pathPattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) {
            $espotaPath = $found.FullName
            break
        }
    }
    
    if (-not $espotaPath) {
        Write-Host "ERROR: espota.py not found in Arduino ESP32 installation" -ForegroundColor Red
        Write-Host "Please make sure ESP32 Arduino Core is installed via Arduino IDE or arduino-cli" -ForegroundColor Yellow
        return $false
    }
    
    if (-not $Quiet) {
        Write-Host "Using espota.py: $espotaPath" -ForegroundColor Green
    }
    
    # Test connectivity (same method as build_esp32.ps1)
    if (-not $Quiet) {
        Write-Host "Testing connectivity to $IP`:$Port..." -ForegroundColor Blue
    }
    try {
        $tcpClient = New-Object System.Net.Sockets.TcpClient
        $tcpClient.ReceiveTimeout = 3000
        $tcpClient.SendTimeout = 3000
        $tcpClient.Connect($IP, $Port)
        $tcpClient.Close()
        if (-not $Quiet) {
            Write-Host "✓ Device is reachable" -ForegroundColor Green
        }
    } catch {
        Write-Host "✗ Cannot reach device at $IP`:$Port" -ForegroundColor Red
        Write-Host "Make sure the ESP32 is:" -ForegroundColor Yellow
        Write-Host "  - Connected to WiFi" -ForegroundColor White
        Write-Host "  - Running firmware with OTA enabled" -ForegroundColor White
        Write-Host "  - Accessible on the network" -ForegroundColor White
        return $false
    }
    
    # Execute OTA upload (same method as build_esp32.ps1)
    if (-not $Quiet) {
        Write-Host "Starting OTA upload..." -ForegroundColor Blue
    }
    $espotaArgs = @("-i", $IP, "-p", $Port.ToString(), "-f", "`"$FirmwareFile`"")
    if ($plainPassword) {
        $espotaArgs += @("-a", $plainPassword)
    }
    
    if (-not $Quiet) {
        Write-Host "Command: python `"$espotaPath`" $($espotaArgs -join ' ')" -ForegroundColor Gray
    }
    
    $process = Start-Process -FilePath "python" -ArgumentList (@("`"$espotaPath`"") + $espotaArgs) -NoNewWindow -Wait -PassThru -RedirectStandardOutput "ota_output.log" -RedirectStandardError "ota_error.log"
    
    # Check results
    if ($process.ExitCode -eq 0) {
        Write-Host "✓ OTA upload completed successfully!" -ForegroundColor Green
        # Cleanup log files
        Remove-Item "ota_output.log" -ErrorAction SilentlyContinue
        Remove-Item "ota_error.log" -ErrorAction SilentlyContinue
        return $true
    } else {
        Write-Host "✗ OTA upload failed!" -ForegroundColor Red
        Write-Host "Exit code: $($process.ExitCode)" -ForegroundColor Red
        
        if (Test-Path "ota_error.log") {
            $errorContent = Get-Content "ota_error.log"
            if ($errorContent) {
                Write-Host "Error details:" -ForegroundColor Yellow
                Write-Host $errorContent -ForegroundColor Red
            }
        }
        
        if (Test-Path "ota_output.log") {
            $outputContent = Get-Content "ota_output.log"
            if ($outputContent) {
                Write-Host "Output:" -ForegroundColor Yellow
                Write-Host $outputContent -ForegroundColor White
            }
        }
        
        # Cleanup log files
        Remove-Item "ota_output.log" -ErrorAction SilentlyContinue
        Remove-Item "ota_error.log" -ErrorAction SilentlyContinue
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
    
    # Handle OTA upload mode
    if ($OTA) {
        $success = Send-OTAToESP32 -IP $ServerIP -FirmwareFile $FirmwareFile -Password $Password -Port $Port -Quiet $Quiet
        if ($success) {
            Write-Host "OTA upload completed successfully!" -ForegroundColor Green
            exit 0
        } else {
            Write-Host "OTA upload failed!" -ForegroundColor Red
            exit 1
        }
    }
    
    # Check if directory exists
    if (-not (Test-Path -Path $Directory)) {
        throw "Directory '$Directory' not found!"
    }
    
    # Determine which files to upload
    $filesToUpload = @()
    
    if ($Files.Count -gt 0) {
        # Upload specific files
        foreach ($file in $Files) {
            $fullPath = Join-Path $Directory $file
            if (Test-Path $fullPath) {
                $filesToUpload += Get-Item $fullPath
            } else {
                Write-Host "Warning: File not found: $fullPath" -ForegroundColor Yellow
            }
        }
    } else {
        # Upload all files from directory
        $filesToUpload = Get-ChildItem -Path $Directory -Recurse -File
    }
    
    if ($filesToUpload.Count -eq 0) {
        Write-Host "No files to upload!" -ForegroundColor Yellow
        exit 0
    }
    
    $totalFiles = $filesToUpload.Count
    $totalSize = ($filesToUpload | Measure-Object -Property Length -Sum).Sum / 1KB
    $totalSize = [math]::Round($totalSize, 2)
    
    if (-not $Quiet) {
        Write-Host "Starting upload of $totalFiles files ($totalSize KB) to http://$ServerIP" -ForegroundColor Cyan
        Write-Host "Using $DelayBetweenFiles ms delay between files, max $MaxRetries retries with $RetryDelay ms retry delay" -ForegroundColor Cyan
        Write-Host "Upload method: $Method" -ForegroundColor Cyan
        if ($PreserveDirectories) {
            Write-Host "Directory structure will be preserved on upload" -ForegroundColor Cyan
        } else {
            Write-Host "Files will be uploaded to root directory" -ForegroundColor Cyan
        }
        Write-Host "-------------------------------------------------------------------------" -ForegroundColor Cyan
    }
    
    # Check connectivity first
    if (-not $Quiet) {
        try {
            $testConnection = Invoke-WebRequest -Uri "http://$ServerIP/" -Method Head -TimeoutSec 5 -ErrorAction Stop
            Write-Host "ESP32 server at $ServerIP is reachable, status: $($testConnection.StatusCode)" -ForegroundColor Green
        }
        catch {
            Write-Host "Warning: Could not reach ESP32 server at $ServerIP. Continuing anyway..." -ForegroundColor Yellow
            Write-Host "Error details: $($_.Exception.Message)" -ForegroundColor Yellow
        }
    }
    
    # Upload each file
    foreach ($file in $filesToUpload) {
        # Calculate the relative path if preserving directories
        $relativePath = ""
        if ($PreserveDirectories) {
            $relativePath = $file.FullName.Replace((Resolve-Path $Directory).Path, "").TrimStart("\", "/").Replace("\", "/")
            if (-not $Quiet) {
                Write-Host "Processing $($file.Name) as $relativePath" -ForegroundColor Gray
            }
        }
        
        # Upload the file with retry logic
        Send-FileToESP32 -FilePath $file.FullName -ServerIP $ServerIP -RelativePath $relativePath -Method $Method -MaxRetries $MaxRetries -RetryDelay $RetryDelay -PreserveDirectories $PreserveDirectories -Quiet $Quiet
        
        # Delay between uploads to avoid overwhelming the ESP32
        if ($DelayBetweenFiles -gt 0) {
            Start-Sleep -Milliseconds $DelayBetweenFiles
        }
    }
    
    $endTime = Get-Date
    $duration = ($endTime - $startTime).TotalSeconds
    
    # Summary
    if (-not $Quiet) {
        Write-Host "-------------------------------------------------------------------------" -ForegroundColor Cyan
    }
    Write-Host "Upload Summary:" -ForegroundColor Cyan
    Write-Host "  Total files: $totalFiles ($totalSize KB)" -ForegroundColor White
    Write-Host "  Successfully uploaded: $successfulUploads" -ForegroundColor Green
    Write-Host "  Failed uploads: $failedUploads" -ForegroundColor Red
    Write-Host "  Files requiring retry: $filesRequiringRetry" -ForegroundColor Yellow
    Write-Host "  Duration: $([math]::Round($duration, 2)) seconds" -ForegroundColor White
    if (-not $Quiet) {
        Write-Host "-------------------------------------------------------------------------" -ForegroundColor Cyan
    }
    
    # Exit with appropriate code
    if ($failedUploads -gt 0) {
        exit 1
    } else {
        exit 0
    }
}
catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
