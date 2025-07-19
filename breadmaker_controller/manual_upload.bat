@echo off
echo Manual ESP32 Upload Script
echo.
echo Please follow these steps:
echo 1. Press and hold the BOOT button on your ESP32
echo 2. Press and release the RESET button while holding BOOT
echo 3. Release the BOOT button
echo 4. Press Enter to continue with upload
echo.
pause

echo Starting upload...
"C:\Users\Steve.Aitken\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe" --chip esp32 --port COM3 --baud 115200 --before no_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x1000 "build\breadmaker_controller.ino.bootloader.bin" 0x8000 "build\breadmaker_controller.ino.partitions.bin" 0xe000 "C:\Users\Steve.Aitken\AppData\Local\Arduino15\packages\esp32\hardware\esp32\2.0.17\tools\partitions\boot_app0.bin" 0x10000 "build\breadmaker_controller.ino.bin"

echo.
echo Upload complete! Press any key to exit.
pause
