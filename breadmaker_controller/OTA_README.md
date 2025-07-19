# OTA (Over-The-Air) Update Support

This ESP32 breadmaker controller now supports OTA (Over-The-Air) firmware updates, allowing you to update the firmware wirelessly without needing to physically connect to the device.

## Features

- **Wireless Updates**: Upload new firmware over WiFi
- **Password Protection**: Secure OTA updates with password authentication
- **Web Management**: Configure OTA settings through the web interface
- **Progress Monitoring**: Real-time update progress display
- **Status Indicators**: Visual feedback during update process

## How to Use OTA Updates

### 1. Initial Setup

1. Make sure your ESP32 is connected to WiFi
2. Navigate to the main web interface at `http://breadmaker.local` or the device's IP address
3. Click the OTA update icon (↓) in the top-right corner
4. Configure OTA settings:
   - **Enable/Disable OTA**: Toggle OTA functionality
   - **Hostname**: Set the network hostname (default: `breadmaker-controller`)
   - **Password**: Set a secure password (minimum 8 characters)

### 2. Uploading Firmware

#### Using Arduino IDE:

1. Open your Arduino IDE
2. Make sure your ESP32 is connected to the same WiFi network as your computer
3. Go to `Tools → Port` and select the network port for your device
   - It should appear as something like `esp32-ota at 192.168.1.xxx (breadmaker-controller)`
4. Click the Upload button - the firmware will be uploaded wirelessly

#### Using PlatformIO:

1. Add these lines to your `platformio.ini` file:
   ```ini
   upload_protocol = espota
   upload_port = 192.168.1.xxx  ; Replace with your device's IP address
   ```
2. Use the "Upload" command to upload wirelessly

#### Using Manual OTA Tools:

You can also use the `espota.py` script directly:
```bash
python espota.py -i 192.168.1.xxx -p 3232 -a breadmaker2024 -f firmware.bin
```

### 3. Security Features

- **Password Protection**: All OTA uploads require the configured password
- **Enable/Disable**: OTA can be completely disabled for security
- **Network Isolation**: OTA only works on the local network

## Web Interface

The OTA management page (`/ota.html`) provides:

- **Real-time Status**: Shows if OTA is enabled/disabled
- **Progress Monitoring**: Visual progress bar during updates
- **Configuration**: Change hostname and password
- **Connection Details**: Shows IP address and port for manual tools
- **Instructions**: Built-in help for Arduino IDE and PlatformIO

## Default Settings

- **Hostname**: `breadmaker-controller`
- **Password**: `breadmaker2024`
- **Port**: 3232 (standard Arduino OTA port)
- **Enabled**: Yes

## Troubleshooting

### OTA Port Not Appearing in Arduino IDE
1. Ensure both computer and ESP32 are on the same network
2. Check that OTA is enabled in the web interface
3. Verify the device is connected to WiFi
4. Try restarting Arduino IDE

### Upload Fails
1. Check the OTA password is correct
2. Ensure no other device is using the same hostname
3. Verify network connectivity
4. Try uploading a smaller sketch first

### Network Discovery Issues
1. Check your router's mDNS/Bonjour settings
2. Try using the IP address directly instead of hostname
3. Ensure no firewall is blocking port 3232

## Safety Notes

- **Power**: Ensure stable power during OTA updates
- **Network**: Maintain stable WiFi connection during updates
- **Backup**: Keep a working firmware backup for recovery
- **Testing**: Test OTA updates with simple sketches first

## API Endpoints

For advanced users, the following API endpoints are available:

- `GET /api/ota/status` - Get OTA status and progress
- `POST /api/ota/enable` - Enable/disable OTA
- `POST /api/ota/password` - Set OTA password
- `POST /api/ota/hostname` - Set OTA hostname
- `GET /api/ota/info` - Get connection information

## Integration with Breadmaker Controller

The OTA system is fully integrated with the breadmaker controller:

- **Status Display**: OTA progress shown on TFT display
- **Safe Updates**: Updates are paused during critical operations
- **Settings Persistence**: OTA settings are saved to flash memory
- **Error Handling**: Comprehensive error reporting and recovery

This OTA implementation provides a professional-grade update system for your ESP32 breadmaker controller, making firmware maintenance much easier and more convenient.
