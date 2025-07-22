# Screensaver Implementation - ESP32 Breadmaker Controller

## Overview

The screensaver system automatically turns off the display backlight and puts the screen to sleep after 1 hour (3,600,000 milliseconds) of inactivity to save power and extend display life.

## Features

### Automatic Activation
- **Timeout**: 1 hour of no activity
- **Power Saving**: Turns off backlight (brightness = 0)
- **Display Sleep**: Puts LCD in sleep mode
- **Memory Efficient**: Stops display updates while active

### Activity Detection
The screensaver timer resets on any of these activities:
- **Physical Buttons**: TTGO T-Display buttons (GPIO 0, 35)
- **Capacitive Touch**: Touch sensors (GPIO 2, 12, 13, 15, 27, 22)
- **Web Interface**: Any web API access automatically resets timer
- **Manual API**: Direct API calls to update activity

### Wake-up Behavior
- **Instant Wake**: Any activity immediately disables screensaver
- **Full Restore**: Backlight restored to 100%, display wakes up
- **Clean Redraw**: Forces complete screen refresh
- **Button Handling**: First button press after wake-up is consumed (doesn't trigger action)

## API Endpoints

### Status Check
```
GET /api/display/screensaver/status
Response: {"active": true|false}
```

### Manual Control
```
POST /api/display/screensaver/enable
Response: {"status": "screensaver_enabled"}

POST /api/display/screensaver/disable  
Response: {"status": "screensaver_disabled"}
```

### Activity Update
```
POST /api/display/activity
Response: {"status": "activity_updated"}
```

## Implementation Details

### Files Modified
1. **display_manager.h** - Added screensaver function declarations
2. **display_manager.cpp** - Core screensaver logic
3. **capacitive_buttons.cpp** - Activity tracking for touch inputs
4. **web_endpoints_new.cpp** - API endpoints and web activity tracking

### Key Functions
- `updateActivityTime()` - Resets activity timer
- `checkScreensaver()` - Monitors timeout and activates screensaver
- `enableScreensaver()` - Manually activates screensaver
- `disableScreensaver()` - Wakes up from screensaver
- `isScreensaverActive()` - Returns current state
- `trackWebActivity()` - Web request activity tracking

### Variables
- `lastActivityTime` - Timestamp of last activity (millis())
- `screensaverActive` - Current screensaver state (boolean)
- `SCREENSAVER_TIMEOUT` - 3,600,000ms (1 hour)

## Display Hardware

### TTGO T-Display Configuration
- **Panel**: ST7789 LCD (240x135)
- **Backlight**: GPIO 4 (PWM controlled)
- **Sleep Mode**: LovyanGFX sleep() function
- **Brightness**: 0-255 (0 = off, 255 = full brightness)

### Power Consumption
- **Active**: Full backlight + display updates
- **Screensaver**: Backlight off + display sleep
- **Estimated Savings**: ~50-70% display power consumption

## Integration

### Main Loop Integration
The screensaver is integrated into the main display update loop:

```cpp
void updateDisplay() {
    checkScreensaver();           // Check timeout
    if (screensaverActive) {
        handleButtons();          // Still handle wake-up buttons
        return;                   // Skip display updates
    }
    // Normal display updates...
}
```

### Web Activity Auto-Tracking
Key web endpoints automatically call `trackWebActivity()`:
- `/status` - Most frequently accessed endpoint
- `/api/status` - API status endpoint
- All screensaver control endpoints

### Button Integration
Both physical and capacitive buttons automatically reset the activity timer:
- Physical buttons call `updateActivityTime()` on press
- Capacitive touch calls `updateActivityTime()` via `checkButtonPress()`
- Wake-up button presses are consumed (don't trigger normal actions)

## Testing

### Test Interface
Access the test interface at: `http://192.168.250.125/screensaver_test.html`

Features:
- Real-time status display
- Manual enable/disable controls
- Activity timer reset
- Response logging
- Auto-refresh every 10 seconds

### Test Scenarios
1. **Manual Activation**: Use test interface to immediately enable screensaver
2. **Physical Wake**: Press any button on TTGO T-Display to wake up
3. **Touch Wake**: Touch any capacitive sensor to wake up
4. **Web Activity**: Any web page access should reset timer
5. **Automatic**: Wait 1 hour without activity for auto-activation

### Verification
- Monitor serial output for screensaver debug messages
- Check display backlight turns off/on appropriately
- Verify button presses wake up the display
- Confirm web activity resets the timer

## Troubleshooting

### Common Issues
1. **Display not sleeping**: Check LovyanGFX sleep() function compatibility
2. **Backlight not dimming**: Verify PWM channel 7 configuration
3. **Buttons not waking**: Check `updateActivityTime()` calls in button handlers
4. **Web activity not tracked**: Ensure `trackWebActivity()` in endpoint handlers

### Debug Output
Enable debug serial to see screensaver messages:
- `[Display] Activity detected, waking up from screensaver`
- `[Display] No activity for 60 minutes, activating screensaver`
- `[Display] Enabling/Disabling screensaver` messages

### Performance Impact
- **Minimal**: Screensaver check is lightweight (single millis() comparison)
- **Memory**: No additional dynamic allocations
- **CPU**: Negligible overhead in main loop
- **Power**: Significant savings when active

## Future Enhancements

### Possible Improvements
1. **Configurable Timeout**: Web UI setting for timeout duration
2. **Progressive Dimming**: Gradually dim before full sleep
3. **Motion Detection**: PIR sensor integration for presence detection
4. **Schedule-Based**: Different timeouts based on time of day
5. **Display Patterns**: Clock or status display during screensaver

### Hardware Considerations
- Compatible with TTGO T-Display ESP32 board
- Uses LovyanGFX display library features
- PWM backlight control via GPIO 4
- No additional hardware required

## Configuration

### Compile-Time Settings
```cpp
static const unsigned long SCREENSAVER_TIMEOUT = 3600000; // 1 hour in milliseconds
```

### Runtime Settings
Currently fixed at compile time. Future versions could make this configurable via:
- Web interface setting
- JSON configuration file
- EEPROM/NVS storage

## Conclusion

The screensaver implementation provides automatic power saving for the display system while maintaining full functionality and responsive wake-up behavior. It integrates seamlessly with the existing button handling and web interface systems.
