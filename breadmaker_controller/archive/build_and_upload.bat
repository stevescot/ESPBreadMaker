@echo off
REM ESP8266 Breadmaker Controller Build and Upload Script (Batch version)
REM Usage: build_and_upload.bat [--skip-compile] [--skip-rom] [--skip-files]

setlocal enabledelayedexpansion

set PORT=COM4
set BOARD=esp8266:esp8266:nodemcuv2
set BAUD=921600
set SKIP_COMPILE=0
set SKIP_ROM=0
set SKIP_FILES=0

REM Parse command line arguments
:parse_args
if "%1"=="--skip-compile" set SKIP_COMPILE=1 & shift & goto parse_args
if "%1"=="--skip-rom" set SKIP_ROM=1 & shift & goto parse_args
if "%1"=="--skip-files" set SKIP_FILES=1 & shift & goto parse_args
if "%1"=="" goto start

:start
echo ESP8266 Breadmaker Controller Build Script
echo ===========================================

REM Step 1: Compile firmware
if %SKIP_COMPILE%==0 (
    echo.
    echo Step 1: Compiling firmware...
    arduino-cli compile --fqbn %BOARD% --output-dir build breadmaker_controller.ino
    if errorlevel 1 (
        echo ERROR: Compilation failed
        exit /b 1
    )
    echo ✓ Firmware compiled successfully
) else (
    echo Step 1: Skipping compilation
)

REM Step 2: Create LittleFS image
if %SKIP_FILES%==0 (
    echo.
    echo Step 2: Creating LittleFS image...
    if exist littlefs.bin del littlefs.bin
    "%USERPROFILE%\AppData\Local\Arduino15\packages\esp8266\tools\mklittlefs\3.1.0-gcc10.3-e5f9fec\mklittlefs.exe" -c data -s 2097152 littlefs.bin
    if errorlevel 1 (
        echo ERROR: LittleFS image creation failed
        exit /b 1
    )
    echo ✓ LittleFS image created successfully
) else (
    echo Step 2: Skipping LittleFS image creation
)

REM Step 3: Upload firmware
if %SKIP_ROM%==0 (
    echo.
    echo Step 3: Uploading firmware...
    python -m esptool --chip esp8266 --port %PORT% --baud %BAUD% write_flash 0x0 build\breadmaker_controller.ino.bin
    if errorlevel 1 (
        echo ERROR: Firmware upload failed
        exit /b 1
    )
    echo ✓ Firmware uploaded successfully
) else (
    echo Step 3: Skipping firmware upload
)

REM Step 4: Upload LittleFS
if %SKIP_FILES%==0 (
    echo.
    echo Step 4: Uploading LittleFS...
    python -m esptool --chip esp8266 --port %PORT% --baud %BAUD% write_flash 0x200000 littlefs.bin
    if errorlevel 1 (
        echo ERROR: LittleFS upload failed
        exit /b 1
    )
    echo ✓ LittleFS uploaded successfully
) else (
    echo Step 4: Skipping LittleFS upload
)

echo.
echo 🎉 Build and upload completed successfully!
echo Your ESP8266 should now be running the updated firmware.
echo.
echo Next steps:
echo 1. Open Serial Monitor (115200 baud) to see boot messages
echo 2. Connect to WiFi or configure WiFi settings
echo 3. Navigate to the ESP8266's IP address in a web browser
echo 4. Test the manual mode and device controls

pause
