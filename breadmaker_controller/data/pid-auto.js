// Global variables for auto-tune functionality
let autoTuneRunning = false;
let autoTunePhase = 0;
let autoTuneMethod = '';
let autoTuneData = [];
let autoTuneStepStart = null;
let autoTuneStartTime = null;
let autoTuneBaseTemp = null;
let autoTuneSkipStabilization = false;
let ultimateGainTest = {};
let updateInterval = null;

// Dummy calculateGainIncrease to prevent ReferenceError if not defined elsewhere
if (typeof calculateGainIncrease !== 'function') {
  function calculateGainIncrease(responseData) {
    // Default: fixed small increment, user can override for adaptive logic
    return 0.01;
  }
}
// Auto-tune functionality for PID Controller Tuning

// Start auto-tune process
async function startAutoTune() {
  if (autoTuneRunning) return;
  
  const baseTemp = parseFloat(document.getElementById('autoTuneBaseTemp').value);
  autoTuneMethod = document.getElementById('autoTuneMethod').value;
  
  if (isNaN(baseTemp)) {
    showMessage('Invalid base temperature', 'error');
    return;
  }
  
  if (autoTuneMethod === 'step') {
    const stepSize = parseFloat(document.getElementById('autoTuneStepSize').value);
    if (isNaN(stepSize) || stepSize <= 0) {
      showMessage('Invalid step size for step response method', 'error');
      return;
    }
  }

  // Check current PID parameters and set minimum working values if all are zero
  try {
    const currentParams = await fetchWithTimeout('/api/pid_params', 5000);
    const params = await currentParams.json();
    
    if (params.kp === 0 && params.ki === 0 && params.kd === 0) {
      // All parameters are zero - set minimum working values for auto-tune
      const initialKp = 1.0; // Start with reasonable value for most systems
      await fetchWithTimeout(`/api/pid_params?kp=${initialKp}&ki=0&kd=0&sample_time=1000&window_size=30000`, 5000);
      showMessage(`⚠️ PID parameters were all zero. Set initial Kp=${initialKp} for auto-tune characterization.`, 'warning');
      console.log('Set initial PID parameters for auto-tune: Kp=1.0, Ki=0, Kd=0');
    }
  } catch (error) {
    showMessage('Warning: Could not check current PID parameters', 'warning');
  }

  // Smart base temperature selection
  let actualBaseTemp = baseTemp;
  let skipStabilization = false;
  
  try {
    const statusResponse = await fetchWithTimeout('/api/pid_debug', 5000);
    const status = await statusResponse.json();
    const currentTemp = status.current_temp;
    
    // Check if current temperature is reasonable for auto-tune
    const tempDifference = Math.abs(currentTemp - baseTemp);
    
    if (currentTemp >= 25 && currentTemp <= 80) {
      // Current temperature is in a reasonable range
      if (tempDifference <= 2.0) {
        // Very close to target base temp - use current temperature and skip stabilization
        actualBaseTemp = currentTemp;
        skipStabilization = true;
        showMessage(`🎯 Using current temperature ${currentTemp.toFixed(1)}°C as base (close to target ${baseTemp}°C)`, 'info');
      } else if (currentTemp > baseTemp && currentTemp <= baseTemp + 15) {
        // Current temp is higher but reasonable - use it as base
        actualBaseTemp = currentTemp;
        skipStabilization = true;
        showMessage(`🌡️ Using current temperature ${currentTemp.toFixed(1)}°C as base (higher than specified ${baseTemp}°C but suitable)`, 'info');
      } else if (tempDifference <= 10) {
        // Moderate difference - suggest using current temp
        actualBaseTemp = currentTemp;
        skipStabilization = true;
        showMessage(`✨ Auto-selected current temperature ${currentTemp.toFixed(1)}°C as base (efficient vs waiting for ${baseTemp}°C)`, 'info');
      }
    }
    
    console.log(`Smart base temperature: specified=${baseTemp}°C, current=${currentTemp.toFixed(1)}°C, using=${actualBaseTemp.toFixed(1)}°C, skip_stabilization=${skipStabilization}`);
  } catch (error) {
    console.warn('Could not get current temperature for smart base selection:', error);
    // Fall back to user-specified base temperature
  }
  
  autoTuneRunning = true;
  autoTunePhase = 1;
  autoTuneStartTime = Date.now();
  autoTuneData = [];
  autoTuneStepStart = null;
  
  // Store the actual base temperature being used for auto-tune handlers
  autoTuneBaseTemp = actualBaseTemp;
  autoTuneSkipStabilization = skipStabilization;
  
  // Reset ultimate gain test variables with intelligent parameter detection
  ultimateGainTest = {
    currentKp: null,    // Will be auto-detected based on system characterization
    maxKp: 20.0,        // Allow higher maximum
    stepSize: null,     // Will be auto-detected based on initial response
    baseStepSize: 0.05, // Base increment for adaptive stepping
    oscillationData: [],
    lastPeaks: [],
    lastValleys: [],
    stableOscillations: 0,
    requiredOscillations: 3,
    burstPhase: 'characterization', // 'characterization', 'heating' or 'cooling'
    burstStartTime: 0,
    burstStartTemp: 0,
    peakTemp: 0,
    peakTime: 0,
    responseHistory: [], // Store response data for each gain level
    testCycles: 0,       // Track number of test cycles completed
    heatingTimeConstant: null, // Measured heating response time
    coolingTimeConstant: null, // Measured passive cooling time
    asymmetryRatio: 1.0,       // Ratio of cooling vs heating time constants
    intelligentCooling: false, // Use intelligent cooling phase timing
    lastOvershootCycle: -1,    // Track when overshoot started occurring
    characterizationComplete: false, // Track initial system characterization
    thermalLagCompensation: true,    // Enable enhanced thermal lag compensation
    thermalInertiaDetected: false,   // Track if significant thermal inertia detected
    systemResponsiveness: 'unknown', // 'slow', 'medium', 'fast'
    detectedSystemType: 'unknown',   // 'sluggish', 'normal', 'responsive'
    gainIncreaseStrategy: 'normal' // 'aggressive', 'normal', 'conservative'
  };
  
  // Update UI
  document.getElementById('autoTuneBtn').disabled = true;
  document.getElementById('stopAutoTuneBtn').disabled = false;
  document.getElementById('autoTuneStatus').style.display = 'block';
  
  try {
    // Enable manual mode and start with base temperature
    await fetchWithTimeout('/api/manual_mode?on=1', 5000);
    await fetchWithTimeout(`/api/temperature?setpoint=${actualBaseTemp}`, 5000);
    
    if (autoTuneMethod === 'step') {
      const stepSize = parseFloat(document.getElementById('autoTuneStepSize').value);
      if (skipStabilization) {
        updateAutoTuneStatus('Phase 1: Using current temperature as base - no stabilization needed', 25);
        showMessage(`Step Response Auto-tune started: ${actualBaseTemp.toFixed(1)}°C → ${actualBaseTemp + stepSize}°C (using current temp)`, 'success');
      } else {
        updateAutoTuneStatus('Phase 1: Stabilizing at base temperature...', 10);
        showMessage(`Step Response Auto-tune started: ${actualBaseTemp}°C → ${actualBaseTemp + stepSize}°C`, 'success');
      }
    } else {
      // Ultimate Gain method - will auto-detect parameters during characterization
      if (skipStabilization) {
        updateAutoTuneStatus('Phase 1: Using current temperature as base - no stabilization needed', 15);
        showMessage(`🧠 Intelligent Ultimate Gain Auto-tune started at ${actualBaseTemp.toFixed(1)}°C (current temp) - will auto-detect optimal parameters`, 'success');
      } else {
        updateAutoTuneStatus('Phase 1: Stabilizing at base temperature...', 5);
        showMessage(`🧠 Intelligent Ultimate Gain Auto-tune started at ${actualBaseTemp}°C - will auto-detect optimal parameters`, 'success');
      }
    }
    
    // Start auto-tune monitoring
    startStatusUpdates();
    
  } catch (error) {
    showAutoTuneError('Error starting auto-tune: ' + error.message);
    stopAutoTune();
  }
}

// Stop auto-tune process
async function stopAutoTune() {
  console.log('stopAutoTune() called - Current phase:', autoTunePhase, 'Method:', autoTuneMethod);
  
  autoTuneRunning = false;
  autoTunePhase = 0;
  
  // Update UI
  document.getElementById('autoTuneBtn').disabled = false;
  document.getElementById('stopAutoTuneBtn').disabled = true;
  document.getElementById('autoTuneStatus').style.display = 'none';
  
  try {
    await fetchWithTimeout('/api/temperature?setpoint=0', 5000);
    await fetchWithTimeout('/api/manual_mode?on=0', 5000);
    
    if (updateInterval) {
      clearInterval(updateInterval);
      updateInterval = null;
    }
    
    showMessage('Auto-tune stopped', 'warning');
  } catch (error) {
    console.error('Error stopping auto-tune:', error);
  }
}

// Update auto-tune status display with detailed stage information
function updateAutoTuneStatus(message, progress, details = null) {
  document.getElementById('autoTuneMessage').textContent = message;
  document.getElementById('autoTuneProgressBar').style.width = progress + '%';
  
  // Show/hide additional details
  const detailsElement = document.getElementById('autoTuneDetails');
  if (details && detailsElement) {
    detailsElement.textContent = details;
    detailsElement.style.display = 'block';
  } else if (detailsElement) {
    detailsElement.style.display = 'none';
  }
  
  // Enhanced phase names with detailed stage information
  let phaseNames, stageInfo;
  
  if (autoTuneMethod === 'step') {
    phaseNames = ['Idle', 'Stabilizing', 'Step Response', 'Analysis Complete'];
    // getStepResponseStageInfo is defined in controllertuning-autotune-step.js
    stageInfo = typeof getStepResponseStageInfo === 'function' ? getStepResponseStageInfo() : '';
  } else {
    // Ultimate gain method with intelligent parameter detection
    if (!ultimateGainTest.characterizationComplete && autoTunePhase === 2) {
      phaseNames = ['Idle', 'Stabilizing', 'System Characterization', 'Analysis Complete'];
    } else {
      phaseNames = ['Idle', 'Stabilizing', 'Ultimate Gain Search', 'Analysis Complete'];
    }
    // getUltimateGainStageInfo is defined in controllertuning-autotune-ultimate.js
    stageInfo = typeof getUltimateGainStageInfo === 'function' ? getUltimateGainStageInfo() : '';
  }
  
  const phaseName = phaseNames[autoTunePhase] || 'Unknown';
  document.getElementById('autoTunePhase').textContent = `Phase: ${phaseName}${stageInfo ? ` - ${stageInfo}` : ''}`;
}

// Show auto-tune error
function showAutoTuneError(message) {
  document.getElementById('autoTuneMessage').textContent = message;
  document.getElementById('autoTuneStatus').style.background = '#ffebee';
  document.getElementById('autoTuneStatus').style.borderColor = '#f44336';
}

// Handle auto-tune update logic
function handleAutoTuneUpdate(pidData, timeElapsed) {
  try {
    const currentTemp = pidData.current_temp;
    const baseTemp = autoTuneBaseTemp || parseFloat(document.getElementById('autoTuneBaseTemp').value);
    
    // Store data point
    autoTuneData.push({
      time: timeElapsed,
      temperature: currentTemp,
      setpoint: pidData.setpoint,
      output: pidData.output
    });
    
    if (autoTuneMethod === 'step') {
      // handleStepResponseAutoTune is defined in controllertuning-autotune-step.js
      if (typeof handleStepResponseAutoTune === 'function') {
        handleStepResponseAutoTune(currentTemp, baseTemp, timeElapsed);
      } else {
        console.error('Step response auto-tune function not available');
        showMessage('Step response auto-tune not available - ensure controllertuning-autotune-step.js is loaded', 'error');
      }
    } else if (autoTuneMethod === 'ultimate') {
      // handleUltimateGainAutoTune is defined in controllertuning-autotune-ultimate.js
      if (typeof handleUltimateGainAutoTune === 'function') {
        handleUltimateGainAutoTune(currentTemp, baseTemp, timeElapsed, pidData);
      } else {
        console.error('Ultimate gain auto-tune function not available');
        showMessage('Ultimate gain auto-tune not available - ensure controllertuning-autotune-ultimate.js is loaded', 'error');
      }
    }
  } catch (error) {
    console.error('Error in auto-tune update:', error);
    // Don't stop auto-tune for minor errors - just log and continue
  }
}

// Apply auto-tune results to the system
async function applyAutoTuneResults(pidParams) {
  try {
    if (!pidParams) {
      showAutoTuneError('Auto-tune failed - no parameters calculated');
      return;
    }
    
    console.log('Applying auto-tune results:', pidParams);
    
    // Update PID parameters
    const response = await fetchWithTimeout(`/api/pid_params?kp=${pidParams.kp}&ki=${pidParams.ki}&kd=${pidParams.kd}&sample_time=1000&window_size=30000`, 10000);
    
    if (!response.ok) {
      throw new Error(`Failed to apply PID parameters: ${response.status}`);
    }
    
    // Update UI input fields
    document.getElementById('kpInput').value = pidParams.kp.toFixed(6);
    document.getElementById('kiInput').value = pidParams.ki.toFixed(6);
    document.getElementById('kdInput').value = pidParams.kd.toFixed(6);
    
    // Show success message with detailed results
    const characteristics = pidParams.characteristics || {};
    let resultsMessage = `✅ Auto-tune Complete!\n\n` +
                        `Method: ${pidParams.method}\n` +
                        `Kp: ${pidParams.kp.toFixed(6)}\n` +
                        `Ki: ${pidParams.ki.toFixed(6)}\n` +
                        `Kd: ${pidParams.kd.toFixed(6)}`;
    
    if (characteristics.riseTime !== undefined) {
      resultsMessage += `\n\nCharacteristics:\n` +
                       `Rise Time: ${characteristics.riseTime.toFixed(1)}s\n` +
                       `Overshoot: ${characteristics.overshoot?.toFixed(1) || '0'}%\n` +
                       `Settling Time: ${characteristics.settlingTime?.toFixed(1) || 'N/A'}s`;
    }
    
    if (characteristics.thermalInertia) {
      resultsMessage += `\n\nThermal Compensation Applied:\n` +
                       `Inertia: ${characteristics.thermalInertia.peakRise?.toFixed(1) || 'N/A'}°C lag\n` +
                       `Severity: ${characteristics.thermalInertia.severity || 'unknown'}`;
    }
    
    showMessage(resultsMessage, 'success');
    
    // Load current parameters to update status display
    setTimeout(() => {
      loadCurrentParams();
    }, 1000);
    
  } catch (error) {
    console.error('Error applying auto-tune results:', error);
    showAutoTuneError('Failed to apply auto-tune results: ' + error.message);
  }
}

// Dummy updateGainIncreaseStrategy to prevent ReferenceError if not defined elsewhere
if (typeof updateGainIncreaseStrategy !== 'function') {
  function updateGainIncreaseStrategy(responseData) {
    // This is a placeholder. If you want to implement adaptive gain increase, add logic here.
    // For now, this prevents ReferenceError.
  }
}
