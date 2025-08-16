# Temperature Data Sources Clarification

## API Temperature Fields Explained

After investigation, the temperature readings in the API have been clarified and updated:

### Field Names and Sources:

1. **`temp_raw_adc`** - True raw ADC reading (0-4095 range)
   - Direct output from `analogRead(PIN_RTD)`
   - Unprocessed hardware reading

2. **`temp_calibrated_current`** - Current calibrated temperature
   - Output of `readTemperature()` function
   - Uses calibration table to convert ADC to °C
   - This is the "true" current temperature

3. **`averaged_temperature`** - EWMA smoothed temperature
   - Exponentially weighted moving average result
   - Uses α=0.1 for smooth temperature tracking
   - Best for control algorithms (reduces noise)

4. **`temp_last_accepted`** - Last accepted calibrated temperature
   - Previous calibrated reading used in EWMA calculation
   - Used for spike detection comparison
   - Was previously misleadingly named "temp_last_raw"

## Previous Confusion

The original API had misleading field names:
- `temp_last_raw` actually contained **calibrated temperature**
- No true raw ADC values were exposed
- Made debugging temperature issues difficult

## Updated Data Flow

```
Raw ADC (0-4095) → Calibration Table → Calibrated Temp (°C) → EWMA Filter → Smoothed Temp (°C)
      ↓                    ↓                      ↓                    ↓
 temp_raw_adc    temp_calibrated_current   temp_last_accepted   averaged_temperature
```

## EWMA Spike Detection

The spike detection compares:
- **Current calibrated temp** vs **Last accepted calibrated temp**
- If difference > 5°C threshold, reading is rejected as spike
- After 10 consecutive "spikes", EWMA resets (stuck-state recovery)

## Rapid Heating Analysis

For rapid heating (20°C to 180°C in 10-15 minutes):
- Rate ≈ 10-16°C/minute = 0.17-0.27°C/second
- With 500ms sampling: max change = 0.08-0.13°C per sample
- Current 5°C spike threshold is appropriate (60x safety margin)
- EWMA α=0.1 provides good balance of smoothing vs responsiveness

## Conclusion

Temperature readings are now properly labeled and expose all levels of processing for better diagnostics and tuning.
