# Home Assistant Breadmaker Controller Integration

This directory contains Home Assistant configuration files for integrating the Breadmaker Controller with Home Assistant.

## Files

- `configuration.yaml` - Main configuration with sensors, switches, and automations
- `dashboard.yaml` - Lovelace dashboard configuration
- `README.md` - This file

## Quick Setup

1. **Find your breadmaker IP address**
   - Check your router's admin panel
   - Use the breadmaker's web interface
   - Or use network scanning tools

2. **Update configuration**
   - Replace `YOUR_BREADMAKER_IP` with your actual IP in `configuration.yaml`
   - Replace `your_device` with your mobile app device name for notifications

3. **Add to Home Assistant**
   - Copy the contents of `configuration.yaml` to your Home Assistant `configuration.yaml`
   - Or use the `!include` directive to include the file
   - Restart Home Assistant

4. **Add dashboard**
   - Copy the contents of `dashboard.yaml` to create a new Lovelace dashboard
   - Or add individual cards to your existing dashboard

## Features

### Sensors
- **Main Status**: Overall breadmaker state (on/off)
- **Temperature**: Current temperature reading
- **Stage**: Current program stage
- **Program**: Currently selected program
- **Setpoint**: Target temperature
- **Time Left**: Time remaining in current stage
- **Ready Times**: When stage/program will complete
- **WiFi Signal**: Connection strength
- **System Info**: Memory, uptime, etc.

### Binary Sensors
- **Running State**: Whether breadmaker is active
- **Output States**: Motor, heater, light, buzzer status
- **Manual Mode**: Whether manual control is enabled

### Controls
- **Main Switch**: Start/stop breadmaker
- **Manual Mode**: Enable direct output control
- **Individual Outputs**: Motor, heater, light, buzzer (manual mode only)

### Commands
- **Stop**: Emergency stop
- **Pause/Resume**: Temporary pause
- **Advance/Back**: Skip stages
- **Program Selection**: Choose different programs
- **Beep**: Test buzzer

### Automations
- **Stage Changes**: Notifications when stages change
- **Program Complete**: Alert when bread is done
- **Low Memory**: Warning for system issues
- **Weak WiFi**: Connection quality alerts

## Dashboard Cards

The dashboard includes:
- Status overview
- Temperature gauges
- Timing information
- Output status indicators
- Manual controls
- Control buttons
- Temperature history chart
- System information
- Progress display
- Quick program selection

## API Endpoints

The breadmaker exposes these endpoints:

### Status
- `GET /ha` - Home Assistant optimized status
- `GET /status` - Full status JSON

### Controls
- `GET /start` - Start breadmaker
- `GET /stop` - Stop breadmaker
- `GET /pause` - Pause program
- `GET /resume` - Resume program
- `GET /advance` - Skip to next stage
- `GET /back` - Go to previous stage
- `GET /select?program=NAME` - Select program

### Manual Controls
- `GET /api/manual_mode?state=true/false` - Toggle manual mode
- `GET /api/motor?state=true/false` - Control motor
- `GET /api/heater?state=true/false` - Control heater
- `GET /api/light?state=true/false` - Control light
- `GET /api/buzzer?state=true/false` - Control buzzer

### Diagnostics
- `GET /api/temperature` - Temperature reading
- `GET /api/pid_debug` - PID controller info
- `GET /api/firmware_info` - Firmware version

## Customization

### Adding Programs
To add quick-select buttons for your custom programs:

1. Edit the dashboard.yaml Quick Actions section
2. Add new call-service entities with your program names
3. Programs must exist in the breadmaker's programs.json file

### Notification Services
Replace `notify.mobile_app_your_device` with your preferred notification service:
- `notify.mobile_app_phone_name` - Mobile app
- `notify.email` - Email notifications
- `notify.telegram` - Telegram bot
- `notify.slack` - Slack notifications

### Scan Intervals
Adjust the `scan_interval` in sensors to balance responsiveness vs. network load:
- Default: 10 seconds
- Fast updates: 5 seconds
- Slow updates: 30 seconds

## Troubleshooting

### Common Issues

1. **Sensors show "unavailable"**
   - Check IP address is correct
   - Verify breadmaker is on network
   - Test endpoint manually: `curl http://IP/ha`

2. **Controls don't work**
   - Ensure breadmaker web interface is accessible
   - Check firewall settings
   - Verify network connectivity

3. **Notifications not working**
   - Update device names in automations
   - Check notification service configuration
   - Test notifications manually

### Network Configuration

For static IP assignment:
1. Configure your router to assign a static IP
2. Update the IP in configuration.yaml
3. Restart Home Assistant

### Monitoring

Monitor the integration health:
- Check sensor update times
- Monitor log files for errors
- Test API endpoints periodically

## Safety Features

The integration respects all breadmaker safety features:
- Temperature limits (250°C maximum)
- Thermal runaway protection
- Manual mode restrictions
- Startup delay for sensor stabilization

## Advanced Features

### WebSocket Support
The breadmaker supports WebSocket connections on port 81 for real-time updates.

### Scheduled Start
The breadmaker supports scheduled start times via the web interface.

### Resume State
The breadmaker can resume programs after power loss.

### Calibration
Temperature calibration can be performed via the web interface.

## Integration with Other Systems

### Node-RED
The REST API can be easily integrated with Node-RED for advanced automation.

### Grafana
Temperature and system metrics can be logged to InfluxDB and visualized in Grafana.

### Voice Assistants
Use Home Assistant's voice assistant integration to control the breadmaker with voice commands.

## Updates

When updating the breadmaker firmware:
1. Check for new API endpoints
2. Update sensor configurations if needed
3. Test all integrations after update
4. Update this documentation

## Support

For issues with the breadmaker controller itself, check:
- Web interface at `http://YOUR_IP/`
- Debug logs via serial connection
- Calibration page for temperature issues
- Programs page for program configuration

For Home Assistant integration issues:
- Check Home Assistant logs
- Verify API responses with curl
- Test individual sensors/switches
- Review automation triggers
