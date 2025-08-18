@echo off
REM HTTP Upload Script for ESP32 Breadmaker Controller
REM Uses the working curl method for file uploads

set TARGET_IP=192.168.250.125

if "%1"=="" (
    echo Usage: upload_files_http.bat ^<file_path^> [target_ip]
    echo Example: upload_files_http.bat data\script.js
    exit /b 1
)

if "%2" NEQ "" set TARGET_IP=%2

echo === ESP32 File Upload ===
echo Target IP: %TARGET_IP%
echo File: %1
echo.

if not exist "%1" (
    echo Error: File not found: %1
    exit /b 1
)

for %%f in ("%1") do set FILENAME=%%~nxf

echo   Uploading: %FILENAME%

curl -X POST -F "file=@%1" -F "path=/%FILENAME%" "http://%TARGET_IP%/api/upload" > temp_result.txt 2>&1

type temp_result.txt | findstr "uploaded" >nul
if %errorlevel%==0 (
    echo     SUCCESS: %FILENAME% uploaded successfully!
    echo.
    echo Upload complete!
    echo Web interface: http://%TARGET_IP%
) else (
    echo     FAILED: 
    type temp_result.txt
    del temp_result.txt
    exit /b 1
)

del temp_result.txt
