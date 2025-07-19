# File Upload System for ESP32

This documentation explains how the ESP32 file upload system works and provides information on using the various upload scripts.

## Server-Side Implementation

The file upload handler in `web_endpoints_new.cpp` has been enhanced with the following improvements:

1. **Proper FFat Initialization**: The filesystem is now properly initialized at server startup
2. **Content Length Setting**: Server is configured to accept larger uploads (up to 10MB)
3. **Improved Error Handling**: Better detection and reporting of upload failures
4. **Yield During Upload**: Prevents watchdog timer from resetting the ESP32 during large uploads
5. **Proper MIME Type Handling**: Added support for additional file types including SVG and ICO

### Key Code Segments

```cpp
// FFat initialization once at startup
if (!FFat.begin(true)) {  // true = format on failure
    if (debugSerial) Serial.println(F("[ERROR] Failed to mount FFat filesystem"));
}

// Configure for larger uploads
server.setContentLength(10 * 1024 * 1024); // Allow up to 10MB uploads

// Upload handler with proper error checking and yields
server.onFileUpload([&](){
    HTTPUpload& upload = server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        // File initialization...
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Write data and yield to prevent watchdog resets
        size_t written = uploadFile.write(upload.buf, upload.currentSize);
        yield();
    }
    // Error handling and cleanup...
});
```

## Client-Side Upload Scripts

There are three PowerShell scripts for uploading files to the ESP32:

1. **upload_files.ps1**: Basic script for uploading all files in the data directory
2. **upload_files_robust.ps1**: Enhanced script with retry logic and connection handling
3. **test_upload_robust.ps1**: Single file upload test script for debugging

### Using upload_files_robust.ps1

This is the recommended script for reliable uploads. It features:

- Configurable delays between files
- Retry mechanism for failed uploads
- Better error handling
- Progress tracking and summary
- Directory structure preservation option

```powershell
# Upload all files with default settings
./upload_files_robust.ps1

# Custom parameters
./upload_files_robust.ps1 -ServerIP 192.168.4.1 -DelayBetweenFiles 500 -MaxRetries 3 -RetryDelay 2000

# Preserve directory structure
./upload_files_robust.ps1 -PreserveDirectories
```

Parameters:
- `ServerIP`: ESP32's IP address (default: 192.168.4.1)
- `Directory`: Directory containing files to upload (default: ./data)
- `DelayBetweenFiles`: Milliseconds to wait between file uploads (default: 500)
- `MaxRetries`: Number of retry attempts for failed uploads (default: 3)
- `RetryDelay`: Milliseconds to wait before retrying (default: 2000)
- `PreserveDirectories`: Switch to maintain directory structure when uploading (default: false)

### Testing Individual File Uploads

To test a specific file:

```powershell
./test_upload_robust.ps1 -ServerIP 192.168.4.1 -TestFile ./data/index.html -Verbose
```

## Troubleshooting

If you encounter upload issues:

1. **Connection Closed**: Increase the delay between uploads (try 1000ms or higher)
2. **Timeout Errors**: Check that the ESP32 is reachable at the specified IP
3. **Upload Failures**: 
   - Try uploading smaller files first
   - Use the test script with a single small file to verify functionality
   - Check the ESP32 serial monitor for error messages

## Next Steps and Improvements

Future enhancements could include:

1. Web-based file upload interface
2. Better progress reporting
3. Checksum verification for uploaded files
4. Parallel uploads for smaller files
