// Status monitoring and performance metrics for PID Controller Tuning

// Start status updates
function startStatusUpdates() {
  if (updateInterval) clearInterval(updateInterval);
  updateInterval = setInterval(updateStatus, 1000);
}

// Update status and chart
async function updateStatus() {
  try {
    const pidResponse = await fetchWithTimeout('/api/pid_debug', 6000);
    const pidData = await pidResponse.json();
    
    // Debug: Log complete response to identify what's missing
    console.log('Full pidData response for debugging:', JSON.stringify(pidData, null, 2));
    
    // Log temperature comparison for debugging
    if (pidData.raw_temp !== undefined && pidData.current_temp !== undefined) {
      const tempDiff = Math.abs(pidData.raw_temp - pidData.current_temp);
      if (tempDiff > 0.1) { // Only log if there's a meaningful difference
        console.log(`Temperature comparison: Raw=${pidData.raw_temp.toFixed(2)}°C, Averaged=${pidData.current_temp.toFixed(2)}°C, Diff=${tempDiff.toFixed(2)}°C`);
      }
    }
    
    // Update status display
    document.getElementById('currentTemp').textContent = pidData.current_temp.toFixed(2) + '°C';
    document.getElementById('targetTemp').textContent = pidData.setpoint.toFixed(2) + '°C';
    document.getElementById('tempError').textContent = (pidData.setpoint - pidData.current_temp).toFixed(2) + '°C';
    document.getElementById('pidOutput').textContent = (pidData.output * 100).toFixed(6) + '%';
    document.getElementById('heaterState').textContent = pidData.heater_state ? 'ON' : 'OFF';
    document.getElementById('manualMode').textContent = pidData.manual_mode ? 'ON' : 'OFF';
    
    // Update window timing display for heater control insight
    if (pidData.window_size_ms !== undefined && pidData.window_elapsed_ms !== undefined && pidData.on_time_ms !== undefined) {
      const windowProgress = (pidData.window_elapsed_ms / pidData.window_size_ms) * 100;
      const onTimePercent = (pidData.on_time_ms / pidData.window_size_ms) * 100;
      const shouldBeOn = pidData.window_elapsed_ms < pidData.on_time_ms;
      
      // Create detailed window status
      const windowStatus = `${(pidData.window_elapsed_ms/1000).toFixed(1)}s/${(pidData.window_size_ms/1000).toFixed(0)}s (${windowProgress.toFixed(1)}%)`;
      const onTimeStatus = `${(pidData.on_time_ms/1000).toFixed(1)}s (${onTimePercent.toFixed(1)}%)`;
      const phaseStatus = shouldBeOn ? 'ON Phase' : 'OFF Phase';
      
      // Update display elements (create these if they don't exist)
      const windowElement = document.getElementById('windowStatus');
      const onTimeElement = document.getElementById('onTimeStatus'); 
      const phaseElement = document.getElementById('phaseStatus');
      
      if (windowElement) windowElement.textContent = windowStatus;
      if (onTimeElement) onTimeElement.textContent = onTimeStatus;
      if (phaseElement) {
        phaseElement.textContent = phaseStatus;
        phaseElement.style.color = shouldBeOn ? '#4CAF50' : '#FF9800'; // Green for ON, Orange for OFF
      }
      
      // Log window timing for debugging when heater state doesn't match expectation
      if (pidData.heater_state !== shouldBeOn && pidData.output > 0.8) {
        console.warn('Heater state mismatch:', {
          physical_state: pidData.heater_state,
          should_be_on: shouldBeOn,
          window_elapsed: pidData.window_elapsed_ms,
          on_time: pidData.on_time_ms,
          pid_output: (pidData.output * 100).toFixed(1) + '%'
        });
      }
    }
    
    // Update current PID parameters display with enhanced debugging
    if (pidData.kp !== undefined) {
      document.getElementById('currentKp').textContent = pidData.kp.toFixed(6);
      document.getElementById('currentKi').textContent = pidData.ki.toFixed(6);
      document.getElementById('currentKd').textContent = pidData.kd.toFixed(6);
    }
    
    // Update sample time from pidData
    if (pidData.sample_time_ms !== undefined) {
      document.getElementById('currentSampleTime').textContent = pidData.sample_time_ms + ' ms';
    } else {
      console.warn('sample_time_ms missing from pidData');
    }
    
    // Update temperature averaging status display with enhanced debugging
    if (pidData.temp_sample_count !== undefined) {
      document.getElementById('currentTempSamples').textContent = pidData.temp_sample_count + ' samples';
      console.log('Updated temp samples display:', pidData.temp_sample_count);
    } else {
      console.warn('temp_sample_count missing from pidData:', pidData);
    }
    if (pidData.temp_reject_count !== undefined) {
      document.getElementById('currentTempReject').textContent = pidData.temp_reject_count + ' each end';
      console.log('Updated temp reject display:', pidData.temp_reject_count);
      
      // Calculate and show effective sample count
      const totalSamples = pidData.temp_sample_count || 10;
      const rejectCount = pidData.temp_reject_count || 2;
      const effectiveCount = totalSamples - (2 * rejectCount);
      document.getElementById('currentTempEffective').textContent = effectiveCount + ' used';
      console.log('Updated effective count display:', effectiveCount);
    } else {
      console.warn('temp_reject_count missing from pidData:', pidData);
    }
    if (pidData.temp_sample_interval_ms !== undefined) {
      document.getElementById('currentTempInterval').textContent = pidData.temp_sample_interval_ms + ' ms';
      console.log('Updated temp interval display:', pidData.temp_sample_interval_ms);
    } else {
      console.warn('temp_sample_interval_ms missing from pidData:', pidData);
    }

    // Update chart
    const timeElapsed = autoTuneRunning && autoTuneStartTime ? 
      (Date.now() - autoTuneStartTime) / 1000 : 
      testStartTime ? (Date.now() - testStartTime) / 1000 : Date.now() / 1000;
    
    // Add data point
    chartData.labels.push(timeElapsed.toFixed(0));
    chartData.datasets[0].data.push(pidData.current_temp); // Averaged temperature
    chartData.datasets[1].data.push(pidData.raw_temp || pidData.current_temp); // Raw temperature (fallback to averaged if not available)
    chartData.datasets[2].data.push(pidData.setpoint); // Target temperature
    chartData.datasets[3].data.push(pidData.output * 100); // PID output
    
    // Keep only last 300 points (5 minutes at 1 second intervals)
    if (chartData.labels.length > 300) {
      chartData.labels.shift();
      chartData.datasets.forEach(dataset => {
        dataset.data.shift();
      });
    }
    
    temperatureChart.update('none');
    
    // Handle auto-tune logic
    if (autoTuneRunning) {
      handleAutoTuneUpdate(pidData, timeElapsed);
    }
    
    // Calculate performance metrics for manual tests
    if (testStartTime && pidData.setpoint > 0) {
      calculatePerformanceMetrics(pidData, timeElapsed);
    }
    
    // Check for dynamic restart activity (less frequently to avoid overhead)
    if (Math.random() < 0.2) { // Check 20% of the time (every ~5 seconds on average)
      checkDynamicRestartActivity();
    }

  } catch (error) {
    console.error('Error updating status:', error);
    document.getElementById('currentTemp').textContent = 'Conn Error';
    
    // Don't show connection errors during auto-tune to avoid confusion
    if (!autoTuneRunning && !testStartTime) {
      showMessage('Connection error - check device', 'error');
    } else if (autoTuneRunning) {
      console.warn('Connection error during auto-tune - retrying...');
    }
  }
}

// Check for dynamic restart activity and display it
async function checkDynamicRestartActivity() {
  try {
    const response = await fetchWithTimeout('/api/heater_debug', 3000);
    const debugData = await response.json();
    
    if (debugData.dynamic_restart_count !== undefined) {
      const restartElement = document.getElementById('dynamicRestartStatus');
      if (restartElement) {
        if (debugData.dynamic_restart_count > 0) {
          const lastRestartAgo = (debugData.last_restart_ago_ms / 1000).toFixed(1);
          const restartText = `${debugData.dynamic_restart_count} total (last: ${lastRestartAgo}s ago)`;
          restartElement.textContent = restartText;
          restartElement.style.color = '#2196F3'; // Blue for active dynamic behavior
          
          // Show reason in tooltip or log it
          if (debugData.last_restart_reason) {
            restartElement.title = `Last restart: ${debugData.last_restart_reason}`;
            console.log(`Dynamic restart #${debugData.dynamic_restart_count}: ${debugData.last_restart_reason}`);
          }
        } else {
          restartElement.textContent = 'None';
          restartElement.style.color = '#666'; // Gray for no activity
          restartElement.title = 'No dynamic restarts detected';
        }
        
        // Log interesting conditions for debugging
        if (debugData.significant_change || debugData.restart_condition_more_heat || debugData.restart_condition_less_heat) {
          console.log('Dynamic restart conditions:', {
            significant_change: debugData.significant_change,
            can_restart_now: debugData.can_restart_now,
            need_more_heat: debugData.restart_condition_more_heat,
            need_less_heat: debugData.restart_condition_less_heat,
            pid_output_change: (debugData.pid_output_change * 100).toFixed(1) + '%',
            threshold: (debugData.change_threshold * 100).toFixed(0) + '%'
          });
        }
      }
    }
  } catch (error) {
    // Don't log errors for this optional feature
    const restartElement = document.getElementById('dynamicRestartStatus');
    if (restartElement) {
      restartElement.textContent = 'N/A';
      restartElement.style.color = '#999';
    }
  }
}

// Calculate performance metrics
function calculatePerformanceMetrics(pidData, timeElapsed) {
  const currentTemp = pidData.current_temp;
  const targetTemp = pidData.setpoint;
  
  // Initialize test start temperature
  if (testStartTemp === null) {
    testStartTemp = currentTemp;
  }
  
  // Track maximum temperature for overshoot calculation
  if (maxTemp === null || currentTemp > maxTemp) {
    maxTemp = currentTemp;
  }
  
  // Rise time (time to reach 90% of target)
  const riseTarget = testStartTemp + 0.9 * (targetTemp - testStartTemp);
  if (currentTemp >= riseTarget && document.getElementById('riseTime').textContent === '--') {
    document.getElementById('riseTime').textContent = timeElapsed.toFixed(1);
  }
  
  // Overshoot
  const overshoot = Math.max(0, maxTemp - targetTemp);
  document.getElementById('overshoot').textContent = overshoot.toFixed(2);
  
  // Settling time (within 2% of target)
  const settlingTolerance = Math.max(0.5, targetTemp * 0.02);
  if (Math.abs(currentTemp - targetTemp) <= settlingTolerance) {
    if (timeElapsed > 30 && document.getElementById('settlingTime').textContent === '--') {
      document.getElementById('settlingTime').textContent = timeElapsed.toFixed(1);
    }
  }
  
  // Steady state error (after 60 seconds)
  if (timeElapsed > 60) {
    const steadyStateError = Math.abs(currentTemp - targetTemp);
    document.getElementById('steadyStateError').textContent = steadyStateError.toFixed(2);
  }
}
