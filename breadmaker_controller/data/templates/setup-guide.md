# Breadmaker Controller Setup Guide

This guide will help you set up the Breadmaker Controller and integrate it with Home Assistant.

## Prerequisites

- ESP32 TTGO T-Display board
- Home Assistant installation
- Local network access
- Basic understanding of YAML configuration

## Step 1: Hardware Setup

1. **Flash the firmware** to your ESP32 board
2. **Connect the hardware** according to the circuit diagram
3. **Power on** the device

## Step 2: Initial Configuration

1. **Connect to WiFi setup**
   - Device creates "BreadmakerSetup" AP on first boot
   - Connect to this network
   - Configure your WiFi credentials

2. **Access web interface**
   - Find the device IP address (check router or use network scanner)
   - Open `http://DEVICE_IP/` in your browser
   - Verify all functions work properly

## Step 3: Home Assistant Integration

### Method 1: Direct Configuration

1. **Edit configuration.yaml**
   - Add the contents of `Home Assistant/configuration.yaml`
   - Replace `YOUR_BREADMAKER_IP` with actual IP address
   - Replace `your_device` with your mobile app device name

2. **Restart Home Assistant**
   - Go to Settings → System → Restart
   - Wait for restart to complete

3. **Verify sensors**
   - Check that all sensors appear in Developer Tools → States
   - Look for entities starting with `sensor.breadmaker_`

### Method 2: Package Configuration

1. **Create packages directory**
   ```bash
   mkdir -p /config/packages
   ```

2. **Enable packages in configuration.yaml**
   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```

3. **Copy package file**
   - Copy `Home Assistant/packages/breadmaker.yaml` to `/config/packages/`
   - Update IP address in the file

4. **Restart Home Assistant**

## Step 4: Dashboard Setup

1. **Create new dashboard**
   - Go to Settings → Dashboards → Add Dashboard
   - Choose "Start with empty dashboard"
   - Name it "Breadmaker"

2. **Add dashboard cards**
   - Copy contents from `Home Assistant/dashboard.yaml`
   - Edit dashboard in raw configuration mode
   - Paste the card configurations

3. **Customize as needed**
   - Adjust card layout
   - Modify colors and icons
   - Add/remove cards based on preferences

## Step 5: Testing

1. **Test basic functionality**
   - Check temperature sensor readings
   - Test start/stop controls
   - Verify status updates

2. **Test automations**
   - Trigger stage changes
   - Check notifications work
   - Test emergency stops

3. **Test manual controls**
   - Enable manual mode
   - Test individual outputs
   - Verify safety features

## Step 6: Advanced Configuration

### Notifications

1. **Mobile app notifications**
   - Install Home Assistant mobile app
   - Enable notifications in app settings
   - Update automation device names

2. **Email notifications**
   ```yaml
   notify:
     - name: email
       platform: smtp
       server: smtp.gmail.com
       port: 587
       timeout: 15
       sender: your-email@gmail.com
       encryption: starttls
       username: your-email@gmail.com
       password: your-app-password
       recipient: your-email@gmail.com
   ```

### Network Configuration

1. **Static IP assignment**
   - Configure router DHCP reservation
   - Or set static IP on device
   - Update Home Assistant configuration

2. **Firewall rules**
   - Allow HTTP traffic on port 80
   - Allow WebSocket traffic on port 81
   - Test connectivity from Home Assistant

### Security

1. **Network security**
   - Use dedicated IoT VLAN if available
   - Implement firewall rules
   - Regular firmware updates

2. **Access control**
   - Consider authentication if needed
   - Monitor access logs
   - Use HTTPS proxy if required

## Troubleshooting

### Common Issues

1. **Sensors show "unavailable"**
   - Check IP address is correct
   - Verify network connectivity
   - Test endpoint manually: `curl http://IP/ha`

2. **Temperature readings incorrect**
   - Check sensor wiring
   - Perform calibration via web interface
   - Monitor raw temperature values

3. **Controls not working**
   - Check manual mode settings
   - Verify API endpoints respond
   - Test individual outputs

4. **Notifications not working**
   - Check mobile app setup
   - Verify automation triggers
   - Test notification services

### Debugging Steps

1. **Check Home Assistant logs**
   - Settings → System → Logs
   - Look for REST sensor errors
   - Check automation traces

2. **Monitor breadmaker logs**
   - Connect serial monitor
   - Enable debug serial output
   - Check for error messages

3. **Test API endpoints**
   ```bash
   curl -X GET http://BREADMAKER_IP/ha
   curl -X GET http://BREADMAKER_IP/status
   curl -X GET http://BREADMAKER_IP/start
   ```

4. **Network diagnostics**
   - Check network connectivity
   - Monitor bandwidth usage
   - Test from different devices

## Maintenance

### Regular Tasks

1. **Monitor system health**
   - Check free memory levels
   - Monitor WiFi signal strength
   - Review error logs

2. **Update firmware**
   - Check for new versions
   - Test in development environment
   - Backup configuration before update

3. **Calibrate temperature**
   - Check temperature accuracy
   - Recalibrate if needed
   - Document calibration points

### Backup and Recovery

1. **Backup configuration**
   - Export Home Assistant config
   - Save breadmaker programs
   - Document custom settings

2. **Recovery procedures**
   - Firmware reflash process
   - Configuration restore
   - Network reconfiguration

## Support

### Resources

- **Web interface**: `http://BREADMAKER_IP/`
- **GitHub repository**: Link to project repository
- **Documentation**: Additional technical docs
- **Community**: Support forums or Discord

### Getting Help

1. **Check documentation first**
2. **Search existing issues**
3. **Provide detailed error information**
4. **Include logs and configuration**

## Next Steps

After successful setup, consider:

1. **Adding more sensors** for monitoring
2. **Creating custom programs** for different recipes
3. **Integrating with other home automation** systems
4. **Setting up data logging** for analysis
5. **Creating voice control** integration

---

*Last updated: [Date]*
*Version: 1.0*
