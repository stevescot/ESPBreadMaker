# ESP32 TTGO T-Display Migration Guide

## Pin Mapping Summary

### ESP8266 → ESP32 TTGO T-Display Pin Changes

| Function | ESP8266 Pin | ESP32 Pin | Notes |
|----------|-------------|-----------|-------|
| **Heater** | D1 (GPIO5) | **GPIO32** | ADC1_CH4, excellent for PWM |
| **Motor** | D2 (GPIO4) | **GPIO33** | ADC1_CH5, excellent for PWM |
| **Light** | D5 (GPIO14) | **GPIO25** | DAC1, excellent for PWM |
| **Buzzer** | D6 (GPIO12) | **GPIO26** | DAC2, excellent for PWM |
| **RTD Sensor** | A0 (ADC) | **GPIO36** | ADC1_CH0, analog input only |

### TTGO T-Display Reserved Pins (DO NOT USE)
- **Display SPI**: GPIO19(MOSI), GPIO18(SCK), GPIO5(CS), GPIO16(DC), GPIO23(RST), GPIO4(BL)
- **Buttons**: GPIO0, GPIO35 (check your board silkscreen)
- **Flash**: GPIO6-11 (internal flash)

### Available GPIO Pins for Future Expansion
- **GPIO27**: Good for digital/PWM
- **GPIO14**: Good for digital/PWM  
- **GPIO12**: Good for digital/PWM
- **GPIO13**: Good for digital/PWM
- **GPIO15**: Good for digital/PWM (but pull-up required)
- **GPIO2**: Good for digital/PWM (but has onboard LED)
- **GPIO21**: Default I2C SDA
- **GPIO22**: Default I2C SCL
- **GPIO36, GPIO39**: Analog input only (ADC1_CH0, ADC1_CH3)

## Memory & Performance Improvements

### ESP32 Advantages
- **320KB RAM** vs ~40KB on ESP8266 (8x more memory)
- **Dual-core CPU** at 240MHz vs single-core 80MHz
- **More GPIO pins** with better PWM capabilities
- **Better WiFi performance** and stability
- **Built-in Bluetooth** (optional feature)

### Code Simplifications Possible
1. **Single programs.json**: You can revert to single 20KB+ JSON file
2. **Larger buffers**: Increase JSON parsing buffers to 16KB+ safely
3. **Better multitasking**: Use FreeRTOS tasks for concurrent operations
4. **Display integration**: Add ST7789 TFT display for local UI

## Migration Checklist

### 1. Hardware Changes
- [ ] Replace ESP8266 with ESP32 TTGO T-Display
- [ ] Update wiring to new pin assignments
- [ ] Verify 3.3V logic levels (no 5V tolerant pins)
- [ ] Test PWM outputs with new pins

### 2. Software Changes  
- [ ] Update Arduino IDE board to "ESP32 Dev Module"
- [ ] Install ESP32 libraries: `esp32` board package
- [ ] Update pin definitions (already done in this session)
- [ ] Test compilation and upload

### 3. Library Updates
- [ ] WiFi: `WiFi.h` → `WiFi.h` (same, but ESP32 version)
- [ ] WebServer: `ESPAsyncWebServer` → same (ESP32 compatible)
- [ ] Filesystem: `LittleFS.h` → same (ESP32 compatible)
- [ ] JSON: `ArduinoJson.h` → same (works on ESP32)
- [ ] Display: Add `TFT_eSPI` library for ST7789 display

### 4. Optional Enhancements
- [ ] Add local TFT display UI
- [ ] Implement touch interface (if available)
- [ ] Add Bluetooth connectivity
- [ ] Use dual-core for better performance
- [ ] Implement OTA updates

## Code Changes Already Applied

### Pin Definitions Updated
```cpp
// outputs_manager.cpp
const int PIN_HEATER = 32;     // GPIO32 (was D1)
const int PIN_MOTOR  = 33;     // GPIO33 (was D2)  
const int PIN_LIGHT  = 25;     // GPIO25 (was D5)
const int PIN_BUZZER = 26;     // GPIO26 (was D6)

// globals.h
constexpr int PIN_RTD = 34;    // GPIO34 (was A0)
```

### Files Modified
- `outputs_manager.cpp`: Updated pin assignments
- `globals.h`: Updated RTD pin definition
- `calibration.cpp`: Updated analogRead calls
- `web_endpoints.cpp`: Updated RTD reading

## Testing Recommendations

1. **Start with basic GPIO test**: Test each output pin individually
2. **Verify analog input**: Test RTD sensor reading on GPIO34
3. **Check PWM functionality**: Verify heater/motor PWM control
4. **Test memory usage**: Monitor heap usage with larger JSON files
5. **WiFi stability**: Test web interface under load
6. **Display integration**: Add basic status display (optional)

## Troubleshooting

### Common Issues
- **Flash mode**: Use DIO or QIO flash mode for stability
- **Brown-out**: ESP32 may be more sensitive to power supply noise
- **ADC accuracy**: ESP32 ADC may need calibration for precise readings
- **PWM frequency**: Default PWM frequency may differ from ESP8266

### Pin Conflicts
- Avoid using display pins (4,5,16,18,19,23)
- GPIO34-39 are input-only (no PWM/digital output)
- GPIO6-11 are connected to flash (do not use)

## Future Expansion Possibilities

With ESP32's additional resources:
- **Local display**: Status, temperature, progress bars
- **Touch interface**: Program selection, manual control
- **Bluetooth**: Mobile app control
- **More sensors**: Humidity, pH, additional temperature probes
- **Camera**: Time-lapse bread making
- **SD card**: Data logging, recipe storage
- **WiFi mesh**: Multi-unit coordination
