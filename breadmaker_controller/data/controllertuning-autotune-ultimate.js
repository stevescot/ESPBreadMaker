// Ultimate Gain Auto-tuning for PID Controller Tuning
// Intelligent thermal response analysis with system characterization

// Get detailed stage information for Ultimate Gain method
function getUltimateGainStageInfo() {
  if (autoTunePhase === 1) {
    return 'Waiting for temperature stability';
  } else if (autoTunePhase === 2) {
    if (!ultimateGainTest.characterizationComplete) {
      // System characterization phase
      const characterizationData = ultimateGainTest.oscillationData?.filter(d => d.phase === 'characterization') || [];
      const dataPoints = characterizationData.length;
      return `Analyzing thermal characteristics (${dataPoints} data points)`;
    } else {
      // Ultimate gain search phase
      const currentKp = ultimateGainTest.currentKp;
      const testCycle = ultimateGainTest.testCycles || 0;
      const burstPhase = ultimateGainTest.burstPhase;
      const systemType = ultimateGainTest.systemResponsiveness || 'unknown';
      
      if (burstPhase === 'heating') {
        return `Cycle ${testCycle + 1}: Heating test (Kp=${currentKp?.toFixed(4)}, ${systemType} system)`;
      } else if (burstPhase === 'cooling') {
        return `Cycle ${testCycle + 1}: Cooling phase (Kp=${currentKp?.toFixed(4)})`;
      } else {
        return `Cycle ${testCycle}: Preparing next test (${systemType} system)`;
      }
    }
  } else if (autoTunePhase === 3) {
    return 'Calculating optimal PID parameters';
  }
  return '';
}

// Calculate variance helper function
function calculateVariance(values) {
  if (values.length < 2) return 0;
  const mean = values.reduce((sum, val) => sum + val, 0) / values.length;
  const variance = values.reduce((sum, val) => sum + Math.pow(val - mean, 2), 0) / values.length;
  return variance;
}

// Handle ultimate gain auto-tune method with intelligent parameter detection
async function handleUltimateGainAutoTune(currentTemp, baseTemp, timeElapsed, pidData) {
  if (autoTunePhase === 1) {
    // Phase 1: Wait for stabilization at base temperature (or skip if using current temp)
    const tempError = Math.abs(currentTemp - baseTemp);
    const progress = Math.max(5, Math.min(20, 20 - tempError * 5));
    
    // Adaptive stabilization criteria based on time elapsed (similar to step response)
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
    
    // Check if we can skip stabilization or if we're stable enough
    const isStableEnough = tempError < requiredStability && timeElapsed > minStabilizationTime;
    const canProceed = autoTuneSkipStabilization || isStableEnough;
    
    if (!canProceed) {
      updateAutoTuneStatus(`Stabilizing at ${baseTemp.toFixed(1)}°C (Error: ${tempError.toFixed(1)}°C, Target: ±${requiredStability}°C)`, progress);
      console.log(`Ultimate Gain stabilization: tempError=${tempError.toFixed(2)}, required=${requiredStability}, timeElapsed=${timeElapsed}`);
    }
    
    if (canProceed) {
      // Temperature stabilized or skip requested, start system characterization
      autoTunePhase = 2;
      autoTuneStepStart = timeElapsed;
      ultimateGainTest.burstPhase = 'characterization';
      ultimateGainTest.burstStartTime = timeElapsed;
      ultimateGainTest.burstStartTemp = currentTemp;
      
      // Start characterization with higher gain to ensure heating occurs
      const characterizationKp = 2.0; // Higher starting point to ensure heating response
      fetchWithTimeout(`/api/pid_params?kp=${characterizationKp}&ki=0&kd=0&sample_time=1000&window_size=30000`, 5000).then(() => {
        // Start with a small temperature step for characterization
        const testTarget = baseTemp + 5; // Larger step for better characterization
        return fetchWithTimeout(`/api/temperature?setpoint=${testTarget}`, 5000);
      }).then(() => {
        updateAutoTuneStatus(`🔬 System Characterization - Measuring thermal response characteristics...`, 25);
        console.log('System characterization started: Measuring thermal response with Kp=2.0 for intelligent parameter detection');
      }).catch(err => {
        showAutoTuneError('Error starting characterization: ' + err.message);
      });
      
    } else if (timeElapsed > 600) {
      showAutoTuneError('Timeout waiting for temperature stabilization (10 minutes) - Consider starting closer to target temperature');
      stopAutoTune();
    } else if (timeElapsed > 300) {
      // Warning after 5 minutes but continue
      updateAutoTuneStatus(`⏳ Taking longer than expected to stabilize at ${baseTemp}°C (${(timeElapsed/60).toFixed(1)} min) - Please wait...`, 25);
    }
    
  } else if (autoTunePhase === 2) {
    // Phase 2: System characterization or thermal response testing
    const searchElapsed = timeElapsed - autoTuneStepStart;
    // For cooling, measure elapsed from when cooling phase started (not peak discovery time)
    let burstElapsed;
    if (ultimateGainTest.burstPhase === 'cooling' && ultimateGainTest.coolingStartTime !== undefined && ultimateGainTest.coolingStartTime !== null) {
      burstElapsed = timeElapsed - ultimateGainTest.coolingStartTime;
    } else {
      burstElapsed = timeElapsed - ultimateGainTest.burstStartTime;
    }

    // Store response data
    ultimateGainTest.oscillationData.push({
      time: timeElapsed,
      temperature: currentTemp,
      setpoint: pidData.setpoint,
      output: pidData.output,
      kp: ultimateGainTest.currentKp || 0.1,
      phase: ultimateGainTest.burstPhase
    });

    // Handle system characterization phase
    if (ultimateGainTest.burstPhase === 'characterization' && !ultimateGainTest.characterizationComplete) {
      handleSystemCharacterization(currentTemp, baseTemp, timeElapsed, pidData, burstElapsed);
      return; // Exit early during characterization
    }

    // Regular thermal response testing (after characterization is complete)
    // Manage heating burst cycles
    if (ultimateGainTest.burstPhase === 'heating') {
      // Use enhanced heating phase with thermal inertia detection
      await handleHeatingPhaseWithInertiaDetection(currentTemp, baseTemp, timeElapsed, pidData, burstElapsed);

    } else if (ultimateGainTest.burstPhase === 'cooling') {
      // During cooling phase - monitor passive cooling and use intelligent timing
      // --- FIX: Track the true peak temperature reached but don't change timing baseline ---
      if (currentTemp > ultimateGainTest.peakTemp) {
        ultimateGainTest.peakTemp = currentTemp;
        ultimateGainTest.peakTime = timeElapsed; // Track peak time for reference, but don't use for timing calculations
      }
      const tempDrop = ultimateGainTest.peakTemp - currentTemp;
      const actualTargetDrop = ultimateGainTest.peakTemp - baseTemp; // Actual drop needed from peak
      const intendedTargetDrop = (baseTemp + 3) - baseTemp; // Original intended target (3°C rise)

      // Use intended target for percentage calculation to avoid negative percentages from overshoot
      const progressTargetDrop = Math.max(actualTargetDrop, intendedTargetDrop, 0.01); // Avoid divide by zero
      let tempDropPercent = (tempDrop / progressTargetDrop) * 100;
      // Clamp to [0, 100]
      tempDropPercent = Math.max(0, Math.min(100, tempDropPercent));

      // For completion logic, use actual drop needed from peak
      const completionTargetDrop = actualTargetDrop;

      // Measure cooling time constant on first cycle for future reference
      if (ultimateGainTest.coolingTimeConstant === null && tempDrop >= completionTargetDrop * 0.63) {
        ultimateGainTest.coolingTimeConstant = burstElapsed;
        ultimateGainTest.intelligentCooling = true;
        console.log('Cooling time constant measured:', ultimateGainTest.coolingTimeConstant.toFixed(1) + 's');
      }

      // Calculate adaptive cooling timeout based on measured cooling characteristics
      let adaptiveCoolingTimeout;
      if (ultimateGainTest.intelligentCooling && ultimateGainTest.coolingTimeConstant) {
        // Use measured cooling time constant with safety margin
        adaptiveCoolingTimeout = Math.min(240, ultimateGainTest.coolingTimeConstant * 3.0); // Increased timeout

        // Adjust based on test cycle - less patient as gain increases
        if (ultimateGainTest.testCycles > 5) {
          adaptiveCoolingTimeout *= 0.8; // 20% less waiting for higher gains
        }

        // For cycles with known overshoot, allow more cooling time
        if (ultimateGainTest.lastOvershootCycle >= 0 && 
            ultimateGainTest.testCycles - ultimateGainTest.lastOvershootCycle <= 2) {
          adaptiveCoolingTimeout *= 1.5; // 50% more time after overshoot (increased)
        }
      } else {
        // Fallback to more generous adaptive timeout for systems with thermal lag
        adaptiveCoolingTimeout = 180 + ultimateGainTest.testCycles * 20; // Increased base timeout
      }

      // More patient cooling completion criteria with thermal lag compensation
      const coolingComplete = tempDrop >= completionTargetDrop * 0.75 ||  // Reduced from 0.8 to 0.75
                             burstElapsed > adaptiveCoolingTimeout ||
                             (ultimateGainTest.intelligentCooling && 
                              burstElapsed > ultimateGainTest.coolingTimeConstant * 2.0 && // Increased from 1.5 to 2.0
                              tempDrop >= completionTargetDrop * 0.6); // Reduced from 0.6 to allow earlier completion if time constant met

      // Enhanced cooling status with overshoot-aware progress calculation
      const overshootInfo = ultimateGainTest.peakTemp > (baseTemp + 3.5) ? 
        ` (overshoot: +${(ultimateGainTest.peakTemp - (baseTemp + 3)).toFixed(1)}°C)` : '';
      const currentCycle = ultimateGainTest.testCycles + 1;
      const progress = 50 + (ultimateGainTest.currentKp / ultimateGainTest.maxKp) * 40;
      updateAutoTuneStatus(`❄️ Cycle ${currentCycle} Cooling - Drop: ${tempDrop.toFixed(1)}°C/${progressTargetDrop.toFixed(1)}°C (${tempDropPercent.toFixed(0)}%) - Time: ${burstElapsed.toFixed(0)}s/${adaptiveCoolingTimeout.toFixed(0)}s${overshootInfo}`, progress);
      console.log(`Cooling progress: ${tempDropPercent.toFixed(1)}% (from peak ${ultimateGainTest.peakTemp.toFixed(1)}°C), elapsed: ${burstElapsed.toFixed(0)}s, timeout: ${adaptiveCoolingTimeout.toFixed(0)}s, complete: ${coolingComplete}`);

      if (coolingComplete) {
        // Analyze this burst cycle
        const responseData = analyzeThermalResponse(ultimateGainTest.currentKp);

        // Track overshoot occurrence
        if (responseData.overshootPercent > 5 && ultimateGainTest.lastOvershootCycle === -1) {
          ultimateGainTest.lastOvershootCycle = ultimateGainTest.testCycles;
          console.log('First significant overshoot detected at cycle', ultimateGainTest.testCycles);
        }

        // Store response data for analysis
        ultimateGainTest.responseHistory = ultimateGainTest.responseHistory || [];
        ultimateGainTest.responseHistory.push(responseData);

        if (responseData.isOverResponsive || ultimateGainTest.testCycles >= 15) {
          // System is becoming unstable OR we've tested enough gains
          autoTunePhase = 3;

          if (responseData.isOverResponsive) {
            updateAutoTuneStatus('🎯 Critical Gain Found - System showing instability. Calculating optimal PID parameters...', 95);
          } else {
            updateAutoTuneStatus('⏱️ Maximum Test Cycles Reached - Calculating PID parameters from collected data...', 95);
          }

          setTimeout(() => {
            const pidParams = calculateFromThermalResponse(
              ultimateGainTest.currentKp,
              responseData
            );
            if (pidParams) {
              applyAutoTuneResults(pidParams);
            } else {
              showAutoTuneError('Ultimate gain auto-tune failed - unable to calculate parameters');
            }
            stopAutoTune();
          }, 1000);

        } else {
          // Increase gain and continue testing
          const gainIncrease = calculateGainIncrease(responseData);
          ultimateGainTest.currentKp += gainIncrease;
          ultimateGainTest.currentKp = Math.min(ultimateGainTest.currentKp, ultimateGainTest.maxKp);
          ultimateGainTest.testCycles++;

          if (ultimateGainTest.currentKp >= ultimateGainTest.maxKp) {
            showAutoTuneError('Ultimate gain search failed - max gain reached without instability');
            stopAutoTune();
            return;
          }

          // Apply new gain and start next burst cycle
          continueUltimateGainTest(baseTemp);
        }
      }
    }

    // Timeout check
    if (searchElapsed > 1800) { // 30 minutes max
      showAutoTuneError('Thermal response test timeout - unable to find critical gain');
      stopAutoTune();
    }
  }
}

// Intelligent system characterization to detect optimal starting parameters
function handleSystemCharacterization(currentTemp, baseTemp, timeElapsed, pidData, burstElapsed) {
  const targetTemp = pidData.setpoint;
  const tempRise = currentTemp - ultimateGainTest.burstStartTemp;
  const expectedRise = targetTemp - ultimateGainTest.burstStartTemp;
  
  // Check if we have enough data for characterization (minimum 60 seconds or good response)
  const hasGoodResponse = tempRise >= expectedRise * 0.8;
  const hasEnoughTime = burstElapsed >= 60;
  const hasTimeout = burstElapsed >= 180; // Max 3 minutes for characterization
  
  if (hasGoodResponse || hasEnoughTime || hasTimeout) {
    // Analyze the characterization data
    const characterizationData = ultimateGainTest.oscillationData.filter(d => d.phase === 'characterization');
    
    if (characterizationData.length >= 10) {
      const systemCharacteristics = analyzeSystemCharacteristics(characterizationData, baseTemp, targetTemp);
      
      // Determine optimal starting parameters based on system characteristics
      const optimalParams = calculateOptimalStartingParameters(systemCharacteristics);
      
      // Apply the detected parameters
      ultimateGainTest.currentKp = optimalParams.startingKp;
      ultimateGainTest.stepSize = optimalParams.stepSize;
      ultimateGainTest.baseStepSize = optimalParams.baseStepSize;
      ultimateGainTest.systemResponsiveness = optimalParams.responsiveness;
      ultimateGainTest.detectedSystemType = optimalParams.systemType;
      ultimateGainTest.characterizationComplete = true;
      
      console.log('System characterization complete:', {
        responsiveness: optimalParams.responsiveness,
        systemType: optimalParams.systemType,
        startingKp: optimalParams.startingKp.toFixed(4),
        stepSize: optimalParams.stepSize.toFixed(4),
        baseStepSize: optimalParams.baseStepSize.toFixed(4),
        characteristics: systemCharacteristics
      });
      
      // Start the actual ultimate gain testing
      updateAutoTuneStatus(`✅ Characterization Complete - Detected ${optimalParams.responsiveness} ${optimalParams.systemType} system. Starting optimized gain search...`, 40, 
        `Starting Kp: ${optimalParams.startingKp.toFixed(4)}, Step Size: ${optimalParams.stepSize.toFixed(4)}, Rise Time: ${systemCharacteristics.riseTime.toFixed(1)}s`);
      setTimeout(() => startActualUltimateGainTesting(baseTemp), 2000); // Brief pause to show completion message
      
    } else {
      // Not enough data yet, continue characterization
      const progress = 25 + (burstElapsed / 180) * 15; // 25-40% during characterization
      updateAutoTuneStatus(`🔬 System Characterization - Analyzing thermal response (${burstElapsed.toFixed(0)}s)...`, progress);
    }
  } else {
    // Still in characterization phase
    const progress = 25 + (burstElapsed / 180) * 15; // 25-40% during characterization
    updateAutoTuneStatus(`🔬 System Characterization - Measuring thermal response (${burstElapsed.toFixed(0)}s/180s max)...`, progress);
  }
}

// Analyze system characteristics from characterization data
function analyzeSystemCharacteristics(data, baseTemp, targetTemp) {
  const startTemp = data[0].temperature;
  const finalTemp = data[data.length - 1].temperature;
  const tempRise = finalTemp - startTemp;
  const expectedRise = targetTemp - startTemp;
  const testDuration = data[data.length - 1].time - data[0].time;
  
  // Calculate rise time (time to reach 63% of expected rise)
  const target63 = startTemp + 0.63 * expectedRise;
  let riseTime = testDuration; // Default to full duration if not reached
  
  for (let i = 1; i < data.length; i++) {
    if (data[i].temperature >= target63) {
      riseTime = data[i].time - data[0].time;
      break;
    }
  }
  
  // Calculate response rate (degrees per second)
  const avgResponseRate = tempRise / testDuration;
  
  // Calculate system gain (temperature rise per unit output)
  const avgOutput = data.reduce((sum, d) => sum + d.output, 0) / data.length;
  const systemGain = avgOutput > 0.1 ? tempRise / avgOutput : 0;
  
  // Analyze output behavior
  const outputVariance = calculateVariance(data.map(d => d.output));
  const outputSaturation = data.filter(d => d.output > 0.95).length / data.length;
  
  // Measure response linearity
  const temperatureSlopes = [];
  for (let i = 1; i < data.length; i++) {
    const dt = data[i].time - data[i-1].time;
    const dtemp = data[i].temperature - data[i-1].temperature;
    if (dt > 0) temperatureSlopes.push(dtemp / dt);
  }
  const slopeVariance = calculateVariance(temperatureSlopes);
  
  // Calculate deadtime (time before significant response begins)
  let deadTime = 0;
  const significantRiseThreshold = startTemp + 0.1; // 0.1°C rise threshold
  for (let i = 1; i < data.length; i++) {
    if (data[i].temperature >= significantRiseThreshold) {
      deadTime = data[i].time - data[0].time;
      break;
    }
  }
  
  return {
    riseTime: riseTime,
    avgResponseRate: avgResponseRate,
    systemGain: systemGain,
    tempRise: tempRise,
    expectedRise: expectedRise,
    responseRatio: expectedRise > 0 ? tempRise / expectedRise : 0,
    outputVariance: outputVariance,
    outputSaturation: outputSaturation,
    slopeVariance: slopeVariance,
    deadTime: deadTime,
    testDuration: testDuration,
    avgOutput: avgOutput
  };
}

// Calculate optimal starting parameters based on system characteristics
function calculateOptimalStartingParameters(characteristics) {
  let responsiveness, systemType, startingKp, stepSize, baseStepSize;
  
  // Classify system responsiveness based on rise time and response rate
  if (characteristics.riseTime < 30 && characteristics.avgResponseRate > 0.15) {
    responsiveness = 'fast';
    systemType = 'responsive';
  } else if (characteristics.riseTime < 90 && characteristics.avgResponseRate > 0.05) {
    responsiveness = 'medium';
    systemType = 'normal';
  } else {
    responsiveness = 'slow';
    systemType = 'sluggish';
  }
  
  // Additional classification refinements
  if (characteristics.deadTime > 15) {
    systemType = 'sluggish'; // High deadtime indicates sluggish system
  }
  
  if (characteristics.outputSaturation > 0.8) {
    systemType = 'power-limited'; // Output saturated, power-limited system
  }
  
  if (characteristics.slopeVariance > 0.01) {
    systemType = 'noisy'; // High slope variance indicates noisy system
  }
  
  // Calculate starting Kp based on system characteristics
  switch (responsiveness) {
    case 'fast':
      startingKp = 0.02; // Very conservative for fast systems
      baseStepSize = 0.01; // Small increments
      stepSize = 0.01;
      break;
      
    case 'medium':
      startingKp = 0.05; // Moderate starting point
      baseStepSize = 0.025; // Medium increments
      stepSize = 0.025;
      break;
      
    case 'slow':
    default:
      startingKp = 0.1; // Higher starting point for slow systems
      baseStepSize = 0.05; // Larger increments for efficiency
      stepSize = 0.05;
      break;
  }
  
  // Adjust based on system gain
  if (characteristics.systemGain > 0) {
    // Higher system gain = more sensitive, use smaller gains
    const gainFactor = Math.min(2.0, Math.max(0.5, 1.0 / characteristics.systemGain));
    startingKp *= gainFactor;
    stepSize *= gainFactor;
    baseStepSize *= gainFactor;
  }
  
  // Adjust based on deadtime
  if (characteristics.deadTime > 10) {
    // High deadtime systems need more conservative approach
    const deadtimeFactor = Math.max(0.3, 1.0 - (characteristics.deadTime / 60));
    startingKp *= deadtimeFactor;
    stepSize *= deadtimeFactor;
  }
  
  // Adjust for power-limited systems
  if (characteristics.outputSaturation > 0.7) {
    // Power-limited systems need higher gains to be effective
    startingKp *= 1.5;
    stepSize *= 1.2;
  }
  
  // Adjust for noisy systems
  if (characteristics.slopeVariance > 0.01) {
    // Noisy systems need more conservative approach
    startingKp *= 0.8;
    stepSize *= 0.8;
  }
  
  // Safety bounds
  startingKp = Math.max(0.005, Math.min(0.5, startingKp));
  stepSize = Math.max(0.005, Math.min(0.2, stepSize));
  baseStepSize = Math.max(0.005, Math.min(0.1, baseStepSize));
  
  return {
    responsiveness,
    systemType,
    startingKp,
    stepSize,
    baseStepSize
  };
}

// Start actual ultimate gain testing after characterization
async function startActualUltimateGainTesting(baseTemp) {
  try {
    // Apply the detected starting parameters
    await fetchWithTimeout(`/api/pid_params?kp=${ultimateGainTest.currentKp}&ki=0&kd=0&sample_time=1000&window_size=30000`, 5000);
    
    // Return to base temperature first
    await fetchWithTimeout(`/api/temperature?setpoint=${baseTemp}`, 5000);
    
    // Set up for heating burst testing
    ultimateGainTest.burstPhase = 'heating';
    ultimateGainTest.burstStartTime = Date.now() / 1000 - (autoTuneStartTime ? (autoTuneStartTime - Date.now()) / 1000 : 0);
    
    // Wait a moment for stabilization, then start first heating burst
    setTimeout(async () => {
      const testTarget = baseTemp + 3;
      await fetchWithTimeout(`/api/temperature?setpoint=${testTarget}`, 5000);
      ultimateGainTest.burstStartTemp = baseTemp;
      
      updateAutoTuneStatus(`🚀 Optimized Ultimate Gain Search - Kp: ${ultimateGainTest.currentKp.toFixed(4)} (${ultimateGainTest.systemResponsiveness} system, auto-tuned parameters)`, 45);
      console.log(`Starting optimized ultimate gain testing with detected parameters: Kp=${ultimateGainTest.currentKp.toFixed(4)}, stepSize=${ultimateGainTest.stepSize.toFixed(4)}`);
      
    }, 5000); // 5 second pause for stabilization
    
  } catch (err) {
    showAutoTuneError('Error starting optimized testing: ' + err.message);
  }
}

// Continue ultimate gain test with new parameters and enhanced status tracking
async function continueUltimateGainTest(baseTemp) {
  const testTarget = baseTemp + 3;
  
  try {
    // Apply new gain
    await fetchWithTimeout(`/api/pid_params?kp=${ultimateGainTest.currentKp}&ki=0&kd=0&sample_time=1000&window_size=30000`, 5000);
    
    // Enhanced stabilization logic with detailed status
    const currentTemp = ultimateGainTest.oscillationData.length > 0 ? 
      ultimateGainTest.oscillationData[ultimateGainTest.oscillationData.length - 1].temperature : baseTemp;
    
    const tempError = Math.abs(currentTemp - baseTemp);
    let waitTime = 15000; // Default 15 second pause
    let stabilizationReason = 'standard';
    
    // Enhanced residual thermal detection and compensation
    if (tempError > 1.5) {
      // Significant temperature deviation from base
      const deviationSeverity = tempError / 5.0; // Normalize deviation impact
      waitTime = Math.min(60000, 20000 + (tempError * 3000)); // Up to 60 seconds
      stabilizationReason = 'thermal_deviation';
      
      console.log(`Residual thermal deviation detected: ${tempError.toFixed(1)}°C from base. Enhanced stabilization: ${(waitTime/1000).toFixed(1)}s`);
    }
    
    // Check previous cycle history for residual energy patterns
    if (ultimateGainTest.testCycles > 0 && ultimateGainTest.responseHistory.length > 0) {
      const previousResponse = ultimateGainTest.responseHistory[ultimateGainTest.responseHistory.length - 1];
      
      // If previous cycle had significant overshoot, extend stabilization
      if (previousResponse.overshootPercent > 15) {
        const overshootPenalty = Math.min(20000, previousResponse.overshootPercent * 800); // Up to 20s extra
        waitTime = Math.max(waitTime, 25000 + overshootPenalty);
        stabilizationReason = 'previous_overshoot';
        console.log(`Previous cycle overshoot compensation: ${previousResponse.overshootPercent.toFixed(1)}% adds ${(overshootPenalty/1000).toFixed(1)}s stabilization`);
      }
      
      // If previous cycle ended with high temperature, ensure proper cooldown
      if (previousResponse.maxTemp > baseTemp + 3) {
        const cooldownTime = Math.min(30000, (previousResponse.maxTemp - baseTemp) * 2500); // Up to 30s
        waitTime = Math.max(waitTime, cooldownTime);
        stabilizationReason = 'thermal_cooldown';
        console.log(`Thermal cooldown compensation: Previous max ${previousResponse.maxTemp.toFixed(1)}°C requires ${(cooldownTime/1000).toFixed(1)}s cooldown`);
      }
      
      // If we detect carryover patterns from recent cycles
      if (ultimateGainTest.testCycles >= 2) {
        const recentResponses = ultimateGainTest.responseHistory.slice(-2);
        const carryoverPattern = recentResponses.every(r => r.residualThermalInfluence);
        
        if (carryoverPattern) {
          waitTime = Math.max(waitTime, 35000); // Minimum 35s for persistent carryover
          stabilizationReason = 'carryover_pattern';
          console.log('Persistent thermal carryover pattern detected - extended stabilization');
        }
      }
    }
    
    // Progressive stabilization: later cycles with higher gains need more time
    if (ultimateGainTest.testCycles > 3) {
      const progressivePenalty = Math.min(15000, ultimateGainTest.testCycles * 2000); // Up to 15s extra
      waitTime = Math.max(waitTime, 25000 + progressivePenalty);
      
      if (stabilizationReason === 'standard') {
        stabilizationReason = 'progressive_gain';
      }
      console.log(`Progressive stabilization: Cycle ${ultimateGainTest.testCycles} adds ${(progressivePenalty/1000).toFixed(1)}s`);
    }
    
    // Temperature-dependent stabilization for extreme conditions
    if (currentTemp > baseTemp + 4) {
      // Very hot starting conditions require extended passive cooling
      const extremeHeatPenalty = (currentTemp - baseTemp - 4) * 3000; // 3s per degree above base+4
      waitTime = Math.max(waitTime, 40000 + extremeHeatPenalty);
      stabilizationReason = 'extreme_heat';
      console.log(`Extreme temperature condition: ${currentTemp.toFixed(1)}°C requires extended cooling: ${(waitTime/1000).toFixed(1)}s`);
    }
    
    // Adaptive wait time based on system responsiveness and gain level
    const gainFactor = ultimateGainTest.currentKp / 5.0; // Normalize gain impact
    const responsivenessPenalty = Math.min(10000, gainFactor * 8000); // Up to 10s for high gains
    waitTime = Math.max(waitTime, 20000 + responsivenessPenalty);
    
    console.log(`Enhanced stabilization strategy: ${stabilizationReason}, total wait: ${(waitTime/1000).toFixed(1)}s (temp error: ${tempError.toFixed(1)}°C, gain: ${ultimateGainTest.currentKp.toFixed(3)}, cycle: ${ultimateGainTest.testCycles})`);
    
    // Display detailed stabilization status to user
    const stabilizationMessages = {
      'standard': 'standard thermal stabilization',
      'thermal_deviation': `cooling from ${tempError.toFixed(1)}°C deviation`,
      'previous_overshoot': 'compensating for previous overshoot',
      'thermal_cooldown': 'waiting for thermal cooldown',
      'carryover_pattern': 'addressing thermal carryover pattern',
      'progressive_gain': 'progressive stabilization for higher gain',
      'extreme_heat': 'extreme temperature cooling'
    };
    
    const currentCycle = ultimateGainTest.testCycles + 1;
    const progress = 45 + (ultimateGainTest.currentKp / ultimateGainTest.maxKp) * 45;
    updateAutoTuneStatus(`⏸️ Cycle ${currentCycle} Prep - Kp: ${ultimateGainTest.currentKp.toFixed(4)} (${stabilizationMessages[stabilizationReason]} - ${(waitTime/1000).toFixed(1)}s)`, progress);
    
    // Wait for temperature to stabilize based on enhanced thermal analysis
    setTimeout(async () => {
      // Double-check temperature stability before starting next test
      try {
        const stabilityCheckResponse = await fetchWithTimeout('/api/pid_debug', 5000);
        const stabilityData = await stabilityCheckResponse.json();
        const finalTempError = Math.abs(stabilityData.current_temp - baseTemp);
        
        if (finalTempError > 1.5) {
          // Still not stable - give it more time or adjust strategy
          console.warn(`Temperature still unstable after wait: ${finalTempError.toFixed(1)}°C error. Proceeding with caution.`);
          
          // If we're consistently having stability issues, reduce gain increase rate
          if (finalTempError > 3.0 && ultimateGainTest.testCycles > 2) {
            console.log('Switching to conservative gain increase strategy due to thermal instability');
            ultimateGainTest.gainIncreaseStrategy = 'conservative';
          }
        }
        
        // Start the heating burst
        await fetchWithTimeout(`/api/temperature?setpoint=${testTarget}`, 5000);
        ultimateGainTest.burstPhase = 'heating';
        ultimateGainTest.burstStartTime = Date.now() / 1000 - (autoTuneStartTime ? (autoTuneStartTime - Date.now()) / 1000 : 0);
        ultimateGainTest.burstStartTemp = stabilityData.current_temp; // Use actual current temp, not assumed base temp
        
        const nextCycleProgress = 50 + (ultimateGainTest.currentKp / ultimateGainTest.maxKp) * 40;
        updateAutoTuneStatus(`🔥 Cycle ${currentCycle} - Kp: ${ultimateGainTest.currentKp.toFixed(4)} (heating burst started - measuring response)`, nextCycleProgress);
                          
      } catch (err) {
        console.error('Error in stability check:', err);
        // Fallback - proceed anyway but with caution
        fetchWithTimeout(`/api/temperature?setpoint=${testTarget}`, 5000);
        ultimateGainTest.burstPhase = 'heating';
        ultimateGainTest.burstStartTime = Date.now() / 1000 - (autoTuneStartTime ? (autoTuneStartTime - Date.now()) / 1000 : 0);
        ultimateGainTest.burstStartTemp = baseTemp; // Fallback to assumed base temp
        
        updateAutoTuneStatus(`🔥 Cycle ${currentCycle} - Kp: ${ultimateGainTest.currentKp.toFixed(4)} (heating burst started)`, 50);
      }
    }, waitTime);
    
  } catch (err) {
    showAutoTuneError('Error updating gain: ' + err.message);
  }
}

// Thermal inertia detection and compensation system
async function detectThermalInertia(currentTemp, pidData, timeElapsed) {
  // Initialize thermal inertia tracking if not present
  if (!ultimateGainTest.thermalInertiaData) {
    ultimateGainTest.thermalInertiaData = {
      heatingStoppedTime: null,
      heatingStoppedTemp: null,
      postHeatingPeakTemp: null,
      postHeatingPeakTime: null,
      thermalRiseAfterStop: 0,
      thermalLagTime: 0,
      inertiaDetected: false,
      monitoringPhase: 'none', // 'none', 'monitoring_rise', 'waiting_for_peak'
      consecutiveLowOutput: 0,  // Track consecutive low output readings
      temperatureStabilizedTime: null // Track when temp stops rising
    };
  }
  
  const inertiaData = ultimateGainTest.thermalInertiaData;
  const recentOutputData = autoTuneData.slice(-8); // Increased window for better stability
  const avgRecentOutput = recentOutputData.length > 0 ? 
    recentOutputData.reduce((sum, d) => sum + d.output, 0) / recentOutputData.length : pidData.output;
  
  // Enhanced detection of when heating effectively stops
  if (inertiaData.monitoringPhase === 'none') {
    // Count consecutive low output readings for more reliable detection
    if (pidData.output < 0.25 && avgRecentOutput < 0.2) {
      inertiaData.consecutiveLowOutput++;
    } else {
      inertiaData.consecutiveLowOutput = 0;
    }
    
    // Require multiple consecutive low readings to avoid false triggers
    if (inertiaData.consecutiveLowOutput >= 3) {
      // Heating has effectively stopped - start monitoring for thermal inertia
      inertiaData.heatingStoppedTime = timeElapsed;
      inertiaData.heatingStoppedTemp = currentTemp;
      inertiaData.monitoringPhase = 'monitoring_rise';
      
      console.log(`🔬 Thermal inertia detection started: heating stopped at ${currentTemp.toFixed(1)}°C, avg output=${(avgRecentOutput*100).toFixed(1)}%, consecutive low readings=${inertiaData.consecutiveLowOutput}`);
      
      // Temporarily reduce setpoint to prevent further heating
      const thermalInertiaSetpoint = Math.max(currentTemp - 2, ultimateGainTest.burstStartTemp);
      await fetchWithTimeout(`/api/temperature?setpoint=${thermalInertiaSetpoint}`, 5000);
      
      updateAutoTuneStatus(`🔬 Detecting thermal inertia - monitoring temperature rise after heating stopped (${currentTemp.toFixed(1)}°C baseline)`, 25);
      return true; // Indicate we're now monitoring thermal inertia
    }
  }
  
  // Monitor post-heating temperature rise with enhanced logic
  if (inertiaData.monitoringPhase === 'monitoring_rise') {
    const tempRiseAfterStop = currentTemp - inertiaData.heatingStoppedTemp;
    const timeElapsedSinceStop = timeElapsed - inertiaData.heatingStoppedTime;
    
    // Track the actual peak temperature dynamically
    if (!inertiaData.postHeatingPeakTemp || currentTemp > inertiaData.postHeatingPeakTemp) {
      inertiaData.postHeatingPeakTemp = currentTemp;
      inertiaData.postHeatingPeakTime = timeElapsed;
    }
    
    // Check if temperature is still rising significantly
    if (tempRiseAfterStop > 0.3 && timeElapsedSinceStop < 180) { // Extended monitoring time and lowered threshold
      inertiaData.thermalRiseAfterStop = tempRiseAfterStop;
      inertiaData.thermalLagTime = timeElapsedSinceStop;
      
      // Enhanced status with rate of rise
      const recentTempData = autoTuneData.slice(-5);
      let riseRate = 0;
      if (recentTempData.length >= 2) {
        const timeDiff = recentTempData[recentTempData.length - 1].time - recentTempData[0].time;
        const tempDiff = recentTempData[recentTempData.length - 1].temperature - recentTempData[0].temperature;
        riseRate = timeDiff > 0 ? (tempDiff / timeDiff) * 60 : 0; // °C per minute
      }
      
      console.log(`🌡️ Thermal inertia active: +${tempRiseAfterStop.toFixed(2)}°C rise in ${timeElapsedSinceStop.toFixed(0)}s (rate: ${riseRate.toFixed(2)}°C/min)`);
      updateAutoTuneStatus(`🔬 Thermal inertia: +${tempRiseAfterStop.toFixed(1)}°C rise after heating stopped (${timeElapsedSinceStop.toFixed(0)}s, ${riseRate.toFixed(1)}°C/min)`, 30);
      
    } else if (timeElapsedSinceStop > 90 || (tempRiseAfterStop < 0.2 && timeElapsedSinceStop > 30)) {
      // Temperature has stabilized or enough time has passed
      inertiaData.monitoringPhase = 'waiting_for_peak';
      inertiaData.temperatureStabilizedTime = timeElapsed;
      
      const totalTempRise = inertiaData.postHeatingPeakTemp - inertiaData.heatingStoppedTemp;
      
      if (totalTempRise > 0.8) // Lowered threshold for more sensitive detection
      {
        inertiaData.inertiaDetected = true;
        ultimateGainTest.thermalInertiaDetected = true;
        
        console.log(`✅ Significant thermal inertia confirmed: ${totalTempRise.toFixed(2)}°C total rise (peak: ${inertiaData.postHeatingPeakTemp.toFixed(1)}°C)`);
        updateAutoTuneStatus(`✅ Thermal inertia measured: ${totalTempRise.toFixed(1)}°C lag - compensation will be applied to tuning`, 35);
        
        // Enhanced compensation parameters with more nuanced scaling
        const lagSeverity = Math.min(1.0, totalTempRise / 3.0); // Scale from 0-1 based on magnitude
        ultimateGainTest.thermalCompensation = {
          lagTime: inertiaData.thermalLagTime,
          temperatureRise: totalTempRise,
          peakRise: totalTempRise,
          compensationFactor: Math.min(0.9, lagSeverity * 0.6),
          severity: lagSeverity > 0.7 ? 'high' : lagSeverity > 0.3 ? 'medium' : 'low'
        };
        
        console.log(`🔧 Thermal compensation parameters:`, ultimateGainTest.thermalCompensation);
        
      } else {
        console.log(`ℹ️ Minimal thermal inertia detected: ${totalTempRise.toFixed(2)}°C rise (below 0.8°C threshold)`);
        updateAutoTuneStatus(`ℹ️ Minimal thermal inertia detected: ${totalTempRise.toFixed(1)}°C lag - no compensation needed`, 35);
      }
      
      return false; // Monitoring complete, resume normal auto-tune
    }
    
    return true; // Still monitoring
  }
  
  return false; // Not monitoring thermal inertia
}

// Apply thermal inertia compensation to gain calculations with enhanced logic
function applyThermalInertiaCompensation(baseKp, responseData) {
  if (!ultimateGainTest.thermalCompensation) {
    return baseKp; // No compensation needed
  }
  
  const compensation = ultimateGainTest.thermalCompensation;
  const compensationFactor = compensation.compensationFactor;
  const severity = compensation.severity;
  
  // Apply variable compensation based on thermal inertia severity
  let kpReduction;
  switch (severity) {
    case 'high':
      kpReduction = 0.4; // Reduce by up to 40% for high inertia
      break;
    case 'medium':
      kpReduction = 0.25; // Reduce by up to 25% for medium inertia
      break;
    case 'low':
      kpReduction = 0.15; // Reduce by up to 15% for low inertia
      break;
    case 'minimal':
      kpReduction = 0.05; // Minimal reduction for slight inertia
      break;
    default:
      kpReduction = 0.2; // Default 20% reduction
  }
  
  // Apply additional compensation based on response characteristics
  if (responseData && responseData.overshootPercent > 10) {
    kpReduction *= 1.2; // Increase compensation for overshooting systems
  }
  
  const compensatedKp = baseKp * (1 - compensationFactor * kpReduction);
  
  console.log(`🔧 Thermal inertia compensation applied: Kp ${baseKp.toFixed(4)} → ${compensatedKp.toFixed(4)} (${severity} inertia, ${(compensationFactor*kpReduction*100).toFixed(1)}% reduction)`);
  
  return compensatedKp;
}

// Enhanced heating phase with thermal inertia detection
async function handleHeatingPhaseWithInertiaDetection(currentTemp, baseTemp, timeElapsed, pidData, burstElapsed) {
  // First check for thermal inertia if we haven't completed detection yet
  const isMonitoringInertia = await detectThermalInertia(currentTemp, pidData, timeElapsed);
  
  if (isMonitoringInertia) {
    return; // Skip normal heating logic while monitoring thermal inertia
  }
  
  // Normal heating phase logic with thermal inertia awareness
  const tempRise = currentTemp - ultimateGainTest.burstStartTemp;
  const targetRise = pidData.setpoint - ultimateGainTest.burstStartTemp;
  
  // Apply thermal inertia compensation to switching logic with enhanced anticipation
  let targetRiseThreshold = targetRise * 0.95;
  let overshootThreshold = targetRise * 1.15;
  
  if (ultimateGainTest.thermalCompensation) {
    // Adjust thresholds based on measured thermal inertia severity
    const compensation = ultimateGainTest.thermalCompensation;
    const anticipationFactor = Math.min(0.3, compensation.peakRise / 8.0); // More sophisticated anticipation
    
    // Variable anticipation based on severity
    let anticipationMultiplier;
    switch (compensation.severity) {
      case 'high':
        anticipationMultiplier = 1.5; // 50% more anticipation for high inertia
        break;
      case 'medium':
        anticipationMultiplier = 1.2; // 20% more anticipation for medium inertia
        break;
      case 'low':
        anticipationMultiplier = 1.0; // Normal anticipation for low inertia
        break;
      case 'minimal':
        anticipationMultiplier = 0.8; // Less anticipation for minimal inertia
        break;
      default:
        anticipationMultiplier = 1.0;
    }
    
    const adjustedAnticipation = anticipationFactor * anticipationMultiplier;
    targetRiseThreshold = targetRise * (0.95 - adjustedAnticipation); // Switch earlier based on severity
    overshootThreshold = targetRise * (1.15 - adjustedAnticipation * 0.6); // More conservative overshoot detection
    
    console.log(`🎯 Enhanced thermal compensation: ${compensation.severity} inertia (${compensation.peakRise.toFixed(1)}°C), target threshold: ${targetRiseThreshold.toFixed(1)}°C, anticipation: ${(adjustedAnticipation*100).toFixed(1)}%`);
  }
  
  // Enhanced PID output monitoring
  const recentOutputData = autoTuneData.slice(-10);
  const avgRecentOutput = recentOutputData.length > 0 ? 
    recentOutputData.reduce((sum, d) => sum + d.output, 0) / recentOutputData.length : pidData.output;
  
  // Improved switching logic
  const hasReachedTarget = tempRise >= targetRiseThreshold;
  const pidOutputIsLow = avgRecentOutput < 0.3;
  const hasOvershoot = tempRise >= overshootThreshold;
  const heatingTooLong = burstElapsed > 120;
  
  const shouldSwitchToCooling = (hasReachedTarget && pidOutputIsLow) || hasOvershoot || heatingTooLong;
  
  console.log(`🔥 Enhanced heating check: tempRise=${tempRise.toFixed(2)}°C, targetRiseThreshold=${targetRiseThreshold.toFixed(2)}°C, avgOutput=${(avgRecentOutput*100).toFixed(1)}%, shouldSwitch=${shouldSwitchToCooling}`);
  
  if (shouldSwitchToCooling) {
    // Switch to cooling phase
    ultimateGainTest.burstPhase = 'cooling';
    ultimateGainTest.peakTemp = Math.max(currentTemp, ultimateGainTest.peakTemp || 0);
    ultimateGainTest.peakTime = timeElapsed;
    ultimateGainTest.coolingStartTime = timeElapsed; // Track when cooling phase started for timing
    
    // Apply enhanced thermal inertia compensation to cooling setpoint
    let coolingSetpoint = baseTemp;
    if (ultimateGainTest.thermalCompensation) {
      // More aggressive cooling for systems with thermal inertia
      const compensation = ultimateGainTest.thermalCompensation;
      const coolingOffset = Math.min(5, compensation.peakRise * 0.5); // Offset proportional to inertia
      coolingSetpoint = Math.max(baseTemp - coolingOffset, baseTemp - 2); // Don't go too far below base
      console.log(`🧊 Thermal inertia cooling compensation: setpoint ${baseTemp}°C → ${coolingSetpoint.toFixed(1)}°C (offset: ${coolingOffset.toFixed(1)}°C)`);
    }
    
    await fetchWithTimeout(`/api/temperature?setpoint=${coolingSetpoint}`, 5000);
    
    const currentCycle = ultimateGainTest.testCycles + 1;
    updateAutoTuneStatus(`🌡️ Cycle ${currentCycle} Peak Reached - Switching to cooling phase (peak: ${ultimateGainTest.peakTemp.toFixed(1)}°C)`, 55);
    console.log(`Heating phase complete: peak ${ultimateGainTest.peakTemp.toFixed(1)}°C, switching to cooling at setpoint ${coolingSetpoint.toFixed(1)}°C`);
  } else {
    // Still in heating phase
    const currentCycle = ultimateGainTest.testCycles + 1;
    const progress = 50 + (ultimateGainTest.currentKp / ultimateGainTest.maxKp) * 30;
    updateAutoTuneStatus(`🔥 Cycle ${currentCycle} Heating - Rise: ${tempRise.toFixed(1)}°C/${targetRise.toFixed(1)}°C (${burstElapsed.toFixed(0)}s)`, progress);
  }
}

// Calculate PID parameters from thermal response analysis (Ultimate Gain method) with thermal inertia compensation
function calculateFromThermalResponse(criticalKp, responseData) {
  try {
    // Apply thermal inertia compensation to critical gain if detected
    const compensatedCriticalKp = applyThermalInertiaCompensation(criticalKp, responseData);
    
    // Use Ziegler-Nichols ultimate gain method with thermal system adaptations
    let kp, ki, kd;
    
    if (responseData.overshootPercent > 10) {
      // Conservative tuning for systems with significant overshoot
      kp = compensatedCriticalKp * 0.4;
      ki = kp / (compensatedCriticalKp * 0.8);
      kd = kp * (compensatedCriticalKp * 0.1);
    } else if (responseData.overshootPercent > 5) {
      // Moderate tuning
      kp = compensatedCriticalKp * 0.5;
      ki = kp / (compensatedCriticalKp * 0.6);
      kd = kp * (compensatedCriticalKp * 0.12);
    } else {
      // More aggressive tuning for stable systems
      kp = compensatedCriticalKp * 0.6;
      ki = kp / (compensatedCriticalKp * 0.5);
      kd = kp * (compensatedCriticalKp * 0.125);
    }
    
    // Apply additional thermal inertia compensation to integral and derivative terms
    if (ultimateGainTest.thermalCompensation) {
      const compensation = ultimateGainTest.thermalCompensation;
      const inertiaFactor = compensation.compensationFactor;
      
      // Adjust Ki based on thermal inertia severity
      let kiAdjustment;
      switch (compensation.severity) {
        case 'high':
          kiAdjustment = 0.4; // Reduce Ki significantly for high inertia
          break;
        case 'medium':
          kiAdjustment = 0.7; // Moderate Ki reduction for medium inertia
          break;
        case 'low':
          kiAdjustment = 0.85; // Small Ki reduction for low inertia
          break;
        default:
          kiAdjustment = 0.8; // Default 20% reduction
      }
      
      ki *= kiAdjustment;
      
      // Adjust Kd based on thermal inertia (derivative can help with lag compensation)
      let kdAdjustment;
      switch (compensation.severity) {
        case 'high':
          kdAdjustment = 1.3; // Increase Kd for high inertia to help with lag
          break;
        case 'medium':
          kdAdjustment = 1.15; // Small Kd increase for medium inertia
          break;
        case 'low':
          kdAdjustment = 1.05; // Minimal Kd increase for low inertia
          break;
        default:
          kdAdjustment = 1.1; // Default 10% increase
      }
      
      kd *= kdAdjustment;
      
      console.log(`🔧 Thermal inertia Ki/Kd compensation: Ki adjustment=${(kiAdjustment*100).toFixed(0)}%, Kd adjustment=${(kdAdjustment*100).toFixed(0)}%`);
    }
    
    // Ensure reasonable bounds
    kp = Math.max(0.001, Math.min(20.0, kp));
    ki = Math.max(0.0001, Math.min(10.0, ki));
    kd = Math.max(0.0, Math.min(5.0, kd));
    
    // Generate descriptive method name
    let thermalCompensationInfo = '';
    if (ultimateGainTest.thermalCompensation) {
      thermalCompensationInfo = `, Thermal Inertia Compensated (${ultimateGainTest.thermalCompensation.severity})`;
    }
    
    console.log('Ultimate gain PID calculation:', {
      criticalKp: criticalKp.toFixed(4),
      compensatedKp: compensatedCriticalKp.toFixed(4),
      finalKp: kp.toFixed(6),
      finalKi: ki.toFixed(6),
      finalKd: kd.toFixed(6),
      overshoot: responseData.overshootPercent.toFixed(1) + '%',
      thermalCompensation: ultimateGainTest.thermalCompensation
    });
    
    return {
      kp: kp,
      ki: ki,
      kd: kd,
      method: `Ultimate Gain (Thermal-Optimized${thermalCompensationInfo})`,
      characteristics: {
        criticalGain: criticalKp,
        compensatedGain: compensatedCriticalKp,
        overshoot: responseData.overshootPercent,
        responseType: responseData.isOverResponsive ? 'Over-responsive' : 'Stable',
        thermalInertia: ultimateGainTest.thermalCompensation || null,
        systemType: ultimateGainTest.systemResponsiveness || 'unknown'
      }
    };
    
  } catch (error) {
    console.error('Error calculating PID from thermal response:', error);
    return null;
  }
}

// Analyze thermal response for ultimate gain method
function analyzeThermalResponse(currentKp) {
  try {
    const recentData = autoTuneData.slice(-60); // Last 60 data points
    
    if (recentData.length < 10) {
      return {
        overshootPercent: 0,
        isOverResponsive: false,
        peakTemp: 0,
        settlingTime: 0
      };
    }
    
    // Find peak temperature and overshoot
    const targetTemp = ultimateGainTest.burstStartTemp + 3; // Target was base + 3°C
    const maxTemp = Math.max(...recentData.map(d => d.temperature));
    const overshoot = Math.max(0, maxTemp - targetTemp);
    const overshootPercent = (overshoot / targetTemp) * 100;
    
    // Check for over-responsive behavior with thermal inertia consideration
    let isOverResponsive = overshootPercent > 15 || (overshootPercent > 8 && currentKp > 3.0);
    
    // Adjust responsiveness detection based on thermal inertia
    if (ultimateGainTest.thermalCompensation) {
      const inertiaFactor = ultimateGainTest.thermalCompensation.compensationFactor;
      // Be more conservative about declaring over-responsiveness if thermal inertia is present
      isOverResponsive = overshootPercent > (15 + inertiaFactor * 10) || 
                        (overshootPercent > (8 + inertiaFactor * 5) && currentKp > (3.0 + inertiaFactor * 2));
    }
    
    // Calculate settling characteristics
    const settlingTolerance = targetTemp * 0.02; // 2% of target
    let settlingTime = 0;
    
    for (let i = recentData.length - 1; i >= 0; i--) {
      if (Math.abs(recentData[i].temperature - targetTemp) > settlingTolerance) {
        settlingTime = recentData[recentData.length - 1].time - recentData[i].time;
        break;
      }
    }
    
    return {
      overshootPercent: overshootPercent,
      isOverResponsive: isOverResponsive,
      peakTemp: maxTemp,
      settlingTime: settlingTime,
      currentKp: currentKp,
      thermalInertiaCompensated: !!ultimateGainTest.thermalCompensation
    };
    
  } catch (error) {
    console.error('Error analyzing thermal response:', error);
    return {
      overshootPercent: 0,
      isOverResponsive: false,
      peakTemp: 0,
      settlingTime: 0
    };
  }
}
