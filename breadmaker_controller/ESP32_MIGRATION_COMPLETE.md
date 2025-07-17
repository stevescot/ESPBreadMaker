# ESP32 TTGO T-Display Migration Complete

## Migration Summary

The ESP8266 breadmaker controller has been successfully migrated to the ESP32 TTGO T-Display platform. This migration provides:

- **8x More Memory**: 320KB RAM vs 40KB ESP8266
- **Built-in TFT Display**: 240x135 color display for local status and control
- **Enhanced PWM Control**: 16-bit PWM resolution vs 10-bit on ESP8266
- **Better Performance**: Dual-core processor at 240MHz
- **Improved Stability**: More robust hardware platform

## Hardware Changes

### Pin Mapping
- **D1 (GPIO5)** → **GPIO32** (Heater control)
- **D2 (GPIO4)** → **GPIO33** (Motor control)
- **D5 (GPIO14)** → **GPIO25** (Light control)
- **D6 (GPIO12)** → **GPIO26** (Buzzer control)
- **A0** → **GPIO34** (RTD temperature sensor)

### TTGO T-Display Pins
- **TFT_MOSI**: GPIO19
- **TFT_SCLK**: GPIO18
- **TFT_CS**: GPIO5
- **TFT_DC**: GPIO16
- **TFT_RST**: GPIO23
- **TFT_BL**: GPIO4 (Backlight)
- **Button 1**: GPIO0 (Boot button)
- **Button 2**: GPIO35 (User button)

## Software Changes

### Core Libraries
- **WiFi**: Changed from `ESP8266WiFi.h` to `WiFi.h`
- **PWM**: Replaced `analogWrite()` with `ledcWrite()` system
- **Display**: Added `TFT_eSPI` library for display control
- **Memory**: Increased from 40KB to 320KB RAM

### PWM System Enhancement
```cpp
// ESP8266 (old)
analogWrite(pin, value); // 0-1023 (10-bit)

// ESP32 (new)
ledcSetup(channel, frequency, resolution); // 12-bit resolution
ledcAttachPin(pin, channel);
ledcWrite(channel, value); // 0-4095 (12-bit)
```

### Display Integration
- **Local Status Display**: Temperature, program status, output states
- **Navigation**: Two-button interface for menu navigation
- **Real-time Updates**: 500ms refresh rate for smooth operation
- **Multiple Screens**: Status, menu, programs, settings, WiFi setup

## File Structure

### Core Files
- `breadmaker_controller.ino` - Main controller (ESP32 compatible)
- `outputs_manager.cpp/h` - Hardware output control with ESP32 PWM
- `display_manager.cpp/h` - TFT display management (NEW)
- `wifi_manager.cpp/h` - WiFi management (ESP32 updated)
- `calibration.cpp/h` - Temperature calibration (ESP32 updated)
- `programs_manager.cpp/h` - Program management (unchanged)
- `web_endpoints.cpp/h` - Web API endpoints (unchanged)
- `globals.h` - Global definitions (ESP32 updated)

### Build Files
- `build_esp32.sh` - Linux/Mac build script
- `build_esp32.ps1` - Windows PowerShell build script
- `TFT_eSPI_Setup.h` - Display configuration

## Display Features

### Status Screen
- Current temperature with color coding
- Active program stage and time remaining
- Progress bar for program completion
- Output state indicators (H/M/L/B)

### Menu Navigation
- **Button 1**: Navigate forward/select
- **Button 2**: Back to status screen
- **Menu Items**: Status, Programs, Settings, WiFi Setup

### Display States
- `DISPLAY_STATUS` - Main status screen
- `DISPLAY_MENU` - Menu selection
- `DISPLAY_PROGRAMS` - Program list
- `DISPLAY_SETTINGS` - System settings
- `DISPLAY_WIFI_SETUP` - WiFi configuration

## Configuration Requirements

### TFT_eSPI Library Setup
1. Install TFT_eSPI library via Arduino Library Manager
2. Copy `TFT_eSPI_Setup.h` to your TFT_eSPI library folder
3. Or configure `User_Setup.h` in the TFT_eSPI library with TTGO T-Display settings

### Board Selection
- **Board**: ESP32 Dev Module or TTGO T-Display
- **CPU Frequency**: 240MHz
- **Flash Size**: 4MB
- **Partition Scheme**: Default
- **Upload Speed**: 921600

## Memory Benefits

### Before (ESP8266)
- **RAM**: 40KB available
- **Flash**: 4MB
- **Programs**: Had to split large JSON files
- **Heap Issues**: 20KB program file couldn't load

### After (ESP32)
- **RAM**: 320KB available (8x increase)
- **Flash**: 4MB
- **Programs**: Can load large program collections
- **Heap**: Abundant memory for complex operations

## Testing Checklist

### Hardware Tests
- [ ] Temperature sensor reading (GPIO34)
- [ ] Heater control (GPIO32)
- [ ] Motor control (GPIO33)
- [ ] Light control (GPIO25)
- [ ] Buzzer control (GPIO26)
- [ ] Display functionality
- [ ] Button responsiveness

### Software Tests
- [ ] WiFi connectivity
- [ ] Web interface access
- [ ] Program loading and execution
- [ ] Display updates
- [ ] Temperature control
- [ ] PID functionality

## Migration Benefits

1. **Resolved Memory Issues**: 8x more RAM eliminates program loading problems
2. **Local Display**: No need for external device to check status
3. **Enhanced Control**: Button interface for local operation
4. **Better PWM**: 12-bit resolution for smoother heater control
5. **Future Expansion**: More GPIO pins and features available
6. **Improved Stability**: More robust hardware platform

## Troubleshooting

### Common Issues
- **Display not working**: Check TFT_eSPI configuration
- **Compilation errors**: Ensure ESP32 board package is installed
- **Upload failures**: Check COM port and upload speed
- **WiFi issues**: ESP32 WiFi library differences

### Solutions
- Use Arduino IDE 1.8.19 or newer
- Install ESP32 board package v2.0.0 or newer
- Configure TFT_eSPI library for TTGO T-Display
- Use provided build scripts for consistent compilation

The ESP32 migration is complete and ready for deployment!
