@echo off
echo Uploading programs_index.json to ESP32...

if not exist "data\programs_index.json" (
    echo ERROR: data\programs_index.json not found!
    pause
    exit /b 1
)

echo Using curl to upload file...
curl -X POST -F "filename=programs_index.json" -F "content=@data/programs_index.json" http://192.168.250.125/debug/upload

echo.
echo Checking if upload succeeded...
curl -s http://192.168.250.125/debug/fs | findstr "programs_index.json"

if %errorlevel%==0 (
    echo SUCCESS: programs_index.json uploaded!
) else (
    echo WARNING: Upload may have failed
)

echo.
echo Test program selection in web interface now.
pause
