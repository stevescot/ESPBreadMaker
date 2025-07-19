// Step Response Auto-tuning for PID Controller Tuning
// Ziegler-Nichols step response method implementation

// Handle step response auto-tune method
function handleStepResponseAutoTune(currentTemp, baseTemp, timeElapsed) {
  const stepSize = parseFloat(document.getElementById('autoTuneStepSize').value);
  const targetTemp = baseTemp + stepSize;
  
  if (autoTunePhase === 1) {
    // Phase 1: Wait for stabilization at base temperature
    const tempError = Math.abs(currentTemp - baseTemp);
    const progress = Math.max(10, Math.min(40, 40 - tempError * 10));
    
    // Adaptive stabilization criteria based on time elapsed
    let requiredStability = 0.5; // Start with 0.5°C tolerance
    let minStabilizationTime = 30; // Start with 30 seconds
    
    if (timeElapsed > 120) {
      // After 2 minutes, be more lenient
      requiredStability = 1.0;
      minStabilizationTime = 20;
    }
    if (timeElapsed > 240) {
      // After 4 minutes, be even more lenient
      requiredStability = 1.5;
      minStabilizationTime = 15;
    }
    
    updateAutoTuneStatus(`Stabilizing at ${baseTemp}°C (Error: ${tempError.toFixed(1)}°C, Target: ±${requiredStability}°C)`, progress);
    console.log(`Stabilization check: tempError=${tempError.toFixed(2)}, required=${requiredStability}, timeElapsed=${timeElapsed}`);
    
    if (tempError < requiredStability && timeElapsed > minStabilizationTime) {
      // Temperature stabilized, make step change
      autoTunePhase = 2;
      autoTuneStepStart = timeElapsed;
      
      fetchWithTimeout(`/api/temperature?setpoint=${targetTemp}`, 5000).then(() => {
        updateAutoTuneStatus(`Step change to ${targetTemp}°C - Recording response...`, 50);
        console.log(`Step response started: baseTemp=${baseTemp}, targetTemp=${targetTemp}, stepSize=${stepSize}`);
      }).catch(err => {
        showAutoTuneError('Error setting step temperature: ' + err.message);
      });
      
    } else if (timeElapsed > 600) {
      showAutoTuneError('Timeout waiting for temperature stabilization (10 minutes) - Consider starting closer to target temperature');
      stopAutoTune();
    } else if (timeElapsed > 300) {
      // Warning after 5 minutes but continue
      updateAutoTuneStatus(`⏳ Taking longer than expected to stabilize at ${baseTemp}°C (${(timeElapsed/60).toFixed(1)} min) - Please wait...`, 35);
    }
    
  } else if (autoTunePhase === 2) {
    // Phase 2: Record step response
    const stepElapsed = timeElapsed - autoTuneStepStart;
    const progress = Math.min(90, 50 + (stepElapsed / 120) * 40); // 2 minutes for step response
    updateAutoTuneStatus(`Recording step response (${stepElapsed.toFixed(0)}s)`, progress);
    
    if (stepElapsed > 120) {
      // Analyze step response and calculate PID parameters
      autoTunePhase = 3;
      updateAutoTuneStatus('Analyzing step response data...', 95);
      
      setTimeout(() => {
        const pidParams = analyzeStepResponse();
        if (pidParams) {
          applyAutoTuneResults(pidParams);
        } else {
          showAutoTuneError('Step response auto-tune failed - insufficient data');
        }
        stopAutoTune();
      }, 1000);
    }
  }
}

// Analyze step response data and calculate PID parameters
function analyzeStepResponse() {
  try {
    const stepSize = parseFloat(document.getElementById('autoTuneStepSize').value);
    const baseTemp = autoTuneBaseTemp || parseFloat(document.getElementById('autoTuneBaseTemp').value);
    const targetTemp = baseTemp + stepSize;
    
    // Get step response data (data after the step change)
    const stepData = autoTuneData.filter(d => d.time >= autoTuneStepStart);
    
    if (stepData.length < 20) {
      console.error('Insufficient step response data:', stepData.length, 'points');
      return null;
    }
    
    // Find initial and final values
    const initialTemp = stepData[0].temperature;
    const finalTemp = stepData[stepData.length - 1].temperature;
    const actualStepSize = finalTemp - initialTemp;
    
    // Calculate step response characteristics
    const riseTime = calculateRiseTime(stepData, initialTemp, finalTemp);
    const overshoot = calculateOvershoot(stepData, targetTemp);
    const settlingTime = calculateSettlingTime(stepData, finalTemp);
    
    // Use Ziegler-Nichols step response method
    let kp, ki, kd;
    
    if (riseTime > 0) {
      // Classical Ziegler-Nichols step response tuning
      const tau = riseTime / 2.8; // Time constant approximation
      const deadTime = riseTime * 0.1; // Dead time approximation
      
      if (tau > 0 && deadTime > 0) {
        // Standard ZN formulas for step response
        kp = 1.2 / (actualStepSize / stepSize) * (tau / deadTime);
        ki = kp / (2.0 * deadTime);
        kd = kp * deadTime * 0.5;
        
        // Apply thermal system optimizations
        if (overshoot > 10) {
          // Conservative tuning for overshooting systems
          kp *= 0.7;
          ki *= 0.8;
          kd *= 1.2;
        } else if (overshoot < 2) {
          // More aggressive tuning for stable systems
          kp *= 1.1;
          ki *= 1.2;
          kd *= 0.9;
        }
        
        // Ensure reasonable bounds
        kp = Math.max(0.01, Math.min(10.0, kp));
        ki = Math.max(0.001, Math.min(5.0, ki));
        kd = Math.max(0.0, Math.min(2.0, kd));
        
        console.log('Step response analysis:', {
          riseTime: riseTime.toFixed(1) + 's',
          overshoot: overshoot.toFixed(1) + '%',
          settlingTime: settlingTime.toFixed(1) + 's',
          calculatedKp: kp.toFixed(6),
          calculatedKi: ki.toFixed(6),
          calculatedKd: kd.toFixed(6)
        });
        
        return {
          kp: kp,
          ki: ki,
          kd: kd,
          method: 'Step Response (Ziegler-Nichols)',
          characteristics: {
            riseTime: riseTime,
            overshoot: overshoot,
            settlingTime: settlingTime,
            actualStepSize: actualStepSize,
            expectedStepSize: stepSize
          }
        };
      }
    }
    
    // Fallback if calculation fails
    console.warn('Step response calculation failed, using fallback values');
    return {
      kp: 1.0,
      ki: 0.1,
      kd: 0.01,
      method: 'Step Response (Fallback)',
      characteristics: {
        riseTime: riseTime,
        overshoot: overshoot,
        settlingTime: settlingTime,
        note: 'Calculation failed, using conservative defaults'
      }
    };
    
  } catch (error) {
    console.error('Error in step response analysis:', error);
    return null;
  }
}

// Calculate rise time (10% to 90% of final value)
function calculateRiseTime(stepData, initialTemp, finalTemp) {
  const tempRange = finalTemp - initialTemp;
  const temp10 = initialTemp + 0.1 * tempRange;
  const temp90 = initialTemp + 0.9 * tempRange;
  
  let time10 = null, time90 = null;
  
  for (let i = 0; i < stepData.length; i++) {
    if (time10 === null && stepData[i].temperature >= temp10) {
      time10 = stepData[i].time;
    }
    if (time90 === null && stepData[i].temperature >= temp90) {
      time90 = stepData[i].time;
      break;
    }
  }
  
  return (time10 !== null && time90 !== null) ? time90 - time10 : 0;
}

// Calculate overshoot percentage
function calculateOvershoot(stepData, targetTemp) {
  const maxTemp = Math.max(...stepData.map(d => d.temperature));
  const overshoot = Math.max(0, maxTemp - targetTemp);
  return (overshoot / targetTemp) * 100;
}

// Calculate settling time (within 2% of final value)
function calculateSettlingTime(stepData, finalTemp) {
  const tolerance = Math.abs(finalTemp * 0.02);
  
  for (let i = stepData.length - 1; i >= 0; i--) {
    if (Math.abs(stepData[i].temperature - finalTemp) > tolerance) {
      return stepData[stepData.length - 1].time - stepData[i].time;
    }
  }
  
  return 0; // Already settled
}

// Get detailed stage information for Step Response method
function getStepResponseStageInfo() {
  if (autoTunePhase === 1) {
    return 'Waiting for temperature stability';
  } else if (autoTunePhase === 2) {
    const stepElapsed = (Date.now() - autoTuneStartTime) / 1000 - (autoTuneStepStart || 0);
    return `Recording response (${stepElapsed.toFixed(0)}s/120s)`;
  } else if (autoTunePhase === 3) {
    return 'Calculating PID parameters';
  }
  return '';
}
