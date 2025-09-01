// common.js - Shared UI logic for breadmaker controller
// Provides: device status icons, utility functions, and status polling

// ---- Device Status Icons ----
const ICONS = {
  motor: String.raw`<svg id="mixIcon" class="icon" viewBox="0 0 40 40"><circle id="mixGroup" cx="20" cy="20" r="16" fill="#888"/><text x="20" y="25" text-anchor="middle" font-size="16" fill="#fff">M</text></svg>`,
  heater: String.raw`<svg id="heaterIcon" class="icon" viewBox="0 0 40 40"><rect x="10" y="10" width="20" height="20" rx="6" fill="#888"/><text x="20" y="25" text-anchor="middle" font-size="16" fill="#fff">H</text></svg>`,
  light: String.raw`<svg id="lightIcon" class="icon" viewBox="0 0 40 40"><circle id="bulb" cx="20" cy="20" r="12" fill="#888"/><text x="20" y="25" text-anchor="middle" font-size="16" fill="#fff">L</text></svg>`,
  buzzer: String.raw`<svg id="buzzerIcon" class="icon" viewBox="0 0 40 40"><ellipse id="buzzBody" cx="20" cy="20" rx="12" ry="8" fill="#888"/><text x="20" y="25" text-anchor="middle" font-size="16" fill="#fff">B</text></svg>`
};

function renderIcons(status, iconRow) {
  if (!iconRow) return;
  iconRow.innerHTML = '';
  [
    {key: 'motor', label: 'Motor', on: status && status.motor},
    {key: 'heater', label: 'Heater', on: status && status.heater},
    {key: 'light', label: 'Light', on: status && status.light},
    {key: 'buzzer', label: 'Buzzer', on: status && status.buzzer}
  ].forEach(dev => {
    const div = document.createElement('div');
    div.className = 'dev-icon-box ' + (dev.on ? 'on' : 'off');
    div.innerHTML = ICONS[dev.key] + `<div class="dev-label">${dev.label}</div>`;
    if (dev.on) div.querySelector('.icon').style.filter = 'brightness(1.5)';
    iconRow.appendChild(div);
  });
}

// ---- Utility Functions ----
function formatDuration(totalSeconds) {
  let s = totalSeconds % 60;
  let m = Math.floor(totalSeconds / 60) % 60;
  let h = Math.floor(totalSeconds / 3600) % 24;
  let d = Math.floor(totalSeconds / 86400);
  let out = "";
  if (d) out += d + "d ";
  if (d || h) out += h + "h ";
  if (d || h || m) out += m + "m ";
  out += s + "s";
  return out;
}
function formatDateTime(ts) {
  const d = new Date(ts);
  let date = d.toLocaleDateString(undefined, { weekday: 'short', month: 'short', day: 'numeric' });
  let time = d.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' });
  return date + ' ' + time;
}

// ---- Status Polling ----
function pollStatusAndRenderIcons(iconRow, intervalMs=2000) {
  function poll() {
    fetch('/status').then(r => r.json()).then(status => renderIcons(status, iconRow));
  }
  poll();
  return setInterval(poll, intervalMs);
}

// ---- Shared Device Icons ----
// Returns HTML string for a device icon (motor, heater, light, buzzer)
function getDeviceIconHtml(type) {
  switch (type) {
    case 'motor':
      return `<svg viewBox="0 0 32 32" width="32" height="32"><g id="mixGroup" style="transform-origin: 16px 16px;"><circle cx="16" cy="16" r="13" stroke="#888" stroke-width="3" fill="none"/><rect x="15" y="4" width="2" height="10" rx="1" fill="#888"/></g></svg>`;
    case 'heater':
      return `<svg viewBox="0 0 32 32" width="32" height="32"><path id="flamePath" d="M16 29C8 22 12 14 16 3C20 14 24 22 16 29Z" fill="#888"/></svg>`;
    case 'light':
      return `<svg viewBox="0 0 32 32" width="32" height="32"><ellipse id="bulb" cx="16" cy="17" rx="8" ry="10" fill="#888" /><rect x="13" y="26" width="6" height="4" rx="2" fill="#444"/></svg>`;
    case 'buzzer':
      return `<svg viewBox="0 0 32 32" width="32" height="32"><polygon id="buzzBody" points="8,20 16,20 22,27 22,5 16,12 8,12" fill="#888"/><path id="buzzWave" d="M25 12 Q29 16 25 20" stroke="#888" stroke-width="2" fill="none"/></svg>`;
    default:
      return '';
  }
}

// ---- Enhanced Finish-By Functionality ----
let finishByConfig = {
  weekdayHour: 17,
  weekdayMinute: 30,
  weekendHour: 9,
  weekendMinute: 0,
  useSmartDefaults: true,
  defaultMinTemp: 15.0,
  defaultMaxTemp: 35.0
};

let currentProgram = null;
let finishByState = {
  active: false,
  preview: null
};

// Load finish-by configuration from server
function loadFinishByConfig() {
  fetch('/api/finish-by/config')
    .then(response => response.json())
    .then(config => {
      finishByConfig = config;
      console.log('Loaded finish-by config:', config);
    })
    .catch(error => {
      console.warn('Could not load finish-by config, using defaults:', error);
    });
}

// Get smart default end time based on day of week
function getSmartDefaultEndTime() {
  const now = new Date();
  const tomorrow = new Date(now);
  tomorrow.setDate(tomorrow.getDate() + 1);
  
  const isWeekend = (tomorrow.getDay() === 0 || tomorrow.getDay() === 6);
  const hour = isWeekend ? finishByConfig.weekendHour : finishByConfig.weekdayHour;
  const minute = isWeekend ? finishByConfig.weekendMinute : finishByConfig.weekdayMinute;
  
  tomorrow.setHours(hour, minute, 0, 0);
  return tomorrow;
}

// Set smart default time in the datetime input
function setSmartDefaultTime() {
  const defaultTime = getSmartDefaultEndTime();
  const finishByInput = document.getElementById('finishByTime');
  
  // Only set the default time if:
  // 1. Input exists
  // 2. Input is not currently focused (user isn't actively setting it)
  // 3. User is not currently interacting with it
  // 4. Input is empty or user hasn't started modifying it recently
  const userIsInteracting = window.finishByUserInteracting && window.finishByUserInteracting();
  
  if (finishByInput && 
      document.activeElement !== finishByInput && 
      !userIsInteracting) {
    const currentValue = finishByInput.value;
    
    // If there's no value, set the default
    if (!currentValue) {
      // Format as datetime-local string
      const isoString = defaultTime.toISOString();
      const localDateTime = isoString.substring(0, 16); // Remove seconds and timezone
      finishByInput.value = localDateTime;
      
      console.log('Set smart default time:', defaultTime.toLocaleString());
    }
  }
}

// Calculate required temperature adjustment
function calculateTemperatureAdjustment() {
  const finishByInput = document.getElementById('finishByTime');
  const tempDeltaInput = document.getElementById('tempDelta');
  const programSelect = document.getElementById('programSelect');
  
  // Get the currently selected program
  let selectedProgram = null;
  if (programSelect && programSelect.value) {
    const programId = parseInt(programSelect.value);
    selectedProgram = window.cachedPrograms?.find(p => p.id === programId);
  }
  
  if (!finishByInput.value) {
    alert('Please set a target finish time first.');
    return;
  }
  
  if (!selectedProgram) {
    alert('Please select a program first.');
    return;
  }
  
  if (!selectedProgram.customStages || !Array.isArray(selectedProgram.customStages)) {
    alert('Selected program does not have valid stage data.');
    return;
  }
  
  const targetEndTime = new Date(finishByInput.value);
  const now = new Date();
  
  if (targetEndTime <= now) {
    alert('Target time must be in the future.');
    return;
  }
  
  // Calculate total program time and time available
  const totalProgramTime = selectedProgram.customStages.reduce((sum, stage) => sum + (stage.min * 60), 0);
  const timeAvailable = Math.floor((targetEndTime - now) / 1000);
  
  // Simple heuristic: if we have less time, increase temperature; if more time, decrease
  const timeDifference = timeAvailable - totalProgramTime;
  const hoursOverUnder = timeDifference / 3600;
  
  // Rough estimate: 1°C change per 2 hours difference (for fermentation stages)
  const suggestedDelta = Math.round((-hoursOverUnder / 2) * 2) / 2; // Round to 0.5°C
  
  if (tempDeltaInput) {
    // Clamp to reasonable range
    const clampedDelta = Math.max(-10, Math.min(10, suggestedDelta));
    tempDeltaInput.value = clampedDelta;
    
    console.log(`Temperature calculation:
      Program: ${selectedProgram.name}
      Program time: ${(totalProgramTime/3600).toFixed(1)}h
      Time available: ${(timeAvailable/3600).toFixed(1)}h
      Difference: ${hoursOverUnder.toFixed(1)}h
      Suggested adjustment: ${clampedDelta}°C`);
      
    // Show feedback to user
    const status = hoursOverUnder > 0 ? 'more time than needed' : 'less time than needed';
    alert(`Temperature adjustment calculated: ${clampedDelta}°C\n\nYou have ${Math.abs(hoursOverUnder).toFixed(1)} hours ${status}.`);
  }
}

// Preview finish-by changes
function previewFinishByChanges() {
  const finishByInput = document.getElementById('finishByTime');
  const tempDeltaInput = document.getElementById('tempDelta');
  const minTempInput = document.getElementById('minTempLimit');
  const maxTempInput = document.getElementById('maxTempLimit');
  const programSelect = document.getElementById('programSelect');
  
  // Get the currently selected program
  let selectedProgram = null;
  if (programSelect && programSelect.value) {
    const programId = parseInt(programSelect.value);
    selectedProgram = window.cachedPrograms?.find(p => p.id === programId);
  }
  
  if (!finishByInput.value) {
    showStatus('Please set a target end time first.', 'error');
    return;
  }
  
  if (!selectedProgram) {
    showStatus('Please select a program first.', 'error');
    return;
  }
  
  if (!selectedProgram.customStages || !Array.isArray(selectedProgram.customStages)) {
    showStatus('Selected program does not have valid stage data.', 'error');
    return;
  }
  
  const targetEndTime = new Date(finishByInput.value);
  const tempDelta = parseFloat(tempDeltaInput.value) || 0;
  const minTemp = parseFloat(minTempInput.value) || finishByConfig.defaultMinTemp;
  const maxTemp = parseFloat(maxTempInput.value) || finishByConfig.defaultMaxTemp;
  
  if (tempDelta === 0) {
    showStatus('Please set a temperature adjustment value first. Use the Calculate button to get a suggestion.', 'error');
    return;
  }
  
  // Calculate affected stages
  const affectedStages = [];
  const stageList = [];
  
  selectedProgram.customStages.forEach((stage, index) => {
    if (!stage.disableAutoAdjust) {
      const originalTemp = stage.targetTemperature || 0;
      let newTemp = originalTemp + tempDelta;
      
      // Apply clamping
      const wasClampedLow = newTemp < minTemp;
      const wasClampedHigh = newTemp > maxTemp;
      if (wasClampedLow) newTemp = minTemp;
      if (wasClampedHigh) newTemp = maxTemp;
      
      affectedStages.push({
        index: index,
        name: stage.label || `Stage ${index + 1}`,
        originalTemp: originalTemp,
        newTemp: newTemp,
        wasClampedLow: wasClampedLow,
        wasClampedHigh: wasClampedHigh
      });
      
      const clampIndicator = wasClampedLow ? ' (⬇️ min)' : wasClampedHigh ? ' (⬆️ max)' : '';
      stageList.push(`${stage.label || `Stage ${index + 1}`}: ${originalTemp}°C → ${newTemp}°C${clampIndicator}`);
    }
  });
  
  if (affectedStages.length === 0) {
    showStatus('No stages can be adjusted in this program.', 'error');
    return;
  }
  
  // Store preview data
  finishByState.preview = {
    targetEndTime: targetEndTime,
    tempDelta: tempDelta,
    minTemp: minTemp,
    maxTemp: maxTemp,
    affectedStages: affectedStages
  };
  
  // Update preview display
  const statusEl = document.getElementById('finishByStatus');
  const detailsEl = document.getElementById('finishByDetails');
  const stageListEl = document.getElementById('stageAdjustmentsList');
  const resultsEl = document.getElementById('finishByResults');
  
  if (statusEl) {
    statusEl.textContent = `🎯 Target: ${targetEndTime.toLocaleString()} (${tempDelta > 0 ? '+' : ''}${tempDelta}°C adjustment)`;
  }
  
  if (detailsEl) {
    const now = new Date();
    const timeUntil = Math.floor((targetEndTime - now) / 1000);
    const totalProgramTime = selectedProgram.customStages.reduce((sum, stage) => sum + (stage.min * 60), 0);
    
    // Check for temperature warnings
    let temperatureWarnings = [];
    let predictedEndTimeWarning = '';
    
    // Check if any adjustments hit the minimum temperature
    const hitMinTemp = affectedStages.some(stage => stage.wasClampedLow);
    const hitMaxTemp = affectedStages.some(stage => stage.wasClampedHigh);
    
    // Get current breadmaker temperature from global status
    let currentBreadmakerTemp = null;
    if (window.currentStatus && typeof window.currentStatus.temperature === 'number') {
      currentBreadmakerTemp = window.currentStatus.temperature;
    }
    
    if (hitMinTemp) {
      temperatureWarnings.push(`⚠️ Some temperatures limited to minimum (${minTemp}°C)`);
      
      // Check if minimum temp is lower than current breadmaker temperature
      if (currentBreadmakerTemp !== null && minTemp < currentBreadmakerTemp) {
        temperatureWarnings.push(`🌡️ Warning: Target ${minTemp}°C is below current temperature (${currentBreadmakerTemp.toFixed(1)}°C)`);
        temperatureWarnings.push(`   The breadmaker cannot cool below ambient temperature`);
        
        // Predict end time if minimum temperature is current temperature
        const adjustedDelta = currentBreadmakerTemp - affectedStages[0].originalTemp;
        const tempRatio = Math.pow(2, adjustedDelta / 10); // Q10 = 2, baseline temp differences
        const adjustedProgramTime = totalProgramTime / tempRatio;
        const predictedEndTime = new Date(now.getTime() + (adjustedProgramTime * 1000));
        
        predictedEndTimeWarning = `📊 If minimum temp is ${currentBreadmakerTemp.toFixed(1)}°C, predicted end: ${predictedEndTime.toLocaleString()}`;
      }
    }
    
    if (hitMaxTemp) {
      temperatureWarnings.push(`⚠️ Some temperatures limited to maximum (${maxTemp}°C)`);
    }
    
    detailsEl.innerHTML = `
<div style="background: #1a1a2e; padding: 12px; border-radius: 8px; margin-bottom: 10px;">
  <h4 style="margin: 0 0 8px 0; color: #4CAF50;">🎯 Finish-By Configuration</h4>
  <div style="color: #ccc; font-size: 0.9em;">
    The program will <strong>stay the same</strong> but temperature adjustments will be applied 
    for this run only to finish close to your target time.
  </div>
</div>

<div style="background: #2a2a3e; padding: 10px; border-radius: 6px;">
  <strong>Program:</strong> ${selectedProgram.name}<br>
  <strong>Target end time:</strong> ${targetEndTime.toLocaleString()}<br>
  <strong>Time available:</strong> ${formatDuration(timeUntil)}<br>
  <strong>Estimated program time:</strong> ${formatDuration(totalProgramTime)}<br>
  <strong>Temperature adjustment:</strong> ${tempDelta > 0 ? '+' : ''}${tempDelta}°C<br>
  <strong>Safety limits:</strong> ${minTemp}°C to ${maxTemp}°C<br>
  <strong>Stages affected:</strong> ${affectedStages.length} of ${selectedProgram.customStages.length}
</div>

${temperatureWarnings.length > 0 ? `
<div style="background: #4a2c2a; padding: 10px; border-radius: 6px; margin-top: 8px; border-left: 4px solid #ff6b6b;">
  ${temperatureWarnings.map(warning => `<div style="margin: 2px 0;">${warning}</div>`).join('')}
</div>` : ''}

${predictedEndTimeWarning ? `
<div style="background: #2a3a4a; padding: 10px; border-radius: 6px; margin-top: 8px; border-left: 4px solid #4dabf7;">
  ${predictedEndTimeWarning}
</div>` : ''}
    `.trim();
  }
  
  if (stageListEl) {
    stageListEl.innerHTML = stageList.map(stage => `<div style="margin:0.2em 0;padding:0.3em;background:#222;border-radius:4px;">${stage}</div>`).join('');
  }
  
  if (resultsEl) {
    resultsEl.style.display = 'block';
  }
  
  // Hide the main finish-by controls
  const finishByRow = document.getElementById('finishByRow');
  if (finishByRow) {
    finishByRow.style.display = 'none';
  }
}

// Confirm and start with finish-by
function confirmFinishBy() {
  if (!finishByState.preview) {
    showStatus('No preview data available. Please preview your changes first.', 'error');
    return;
  }
  
  const preview = finishByState.preview;
  const programSelect = document.getElementById('programSelect');
  
  if (!programSelect.value) {
    showStatus('Please select a program first.', 'error');
    return;
  }
  
  // Prepare form data
  const formData = new FormData();
  formData.append('program', programSelect.value);
  formData.append('finishByTime', preview.targetEndTime.toISOString());
  formData.append('tempDelta', preview.tempDelta.toString());
  formData.append('minTemp', preview.minTemp.toString());
  formData.append('maxTemp', preview.maxTemp.toString());
  
  console.log('Starting program with finish-by parameters:', {
    program: programSelect.value,
    finishByTime: preview.targetEndTime.toISOString(),
    tempDelta: preview.tempDelta,
    minTemp: preview.minTemp,
    maxTemp: preview.maxTemp
  });
  
  // Start the program with finish-by parameters
  fetch('/start', {
    method: 'POST',
    body: formData
  })
  .then(response => response.json())
  .then(result => {
    if (result.status === 'ok') {
      showStatus('✅ Program started with finish-by adjustments!', 'success');
      clearFinishByUI();
      finishByState.active = true;
      
      // Refresh the page or update status
      setTimeout(() => {
        location.reload();
      }, 2000);
    } else {
      showStatus('❌ Error starting program: ' + (result.message || 'Unknown error'), 'error');
    }
  })
  .catch(error => {
    console.error('Error starting program:', error);
    showStatus('❌ Network error starting program.', 'error');
  });
}

// Modify finish-by settings (go back to configuration)
function modifyFinishBy() {
  const finishByRow = document.getElementById('finishByRow');
  const finishByResults = document.getElementById('finishByResults');
  
  if (finishByRow) finishByRow.style.display = 'block';
  if (finishByResults) finishByResults.style.display = 'none';
  
  finishByState.preview = null;
}

// Cancel finish-by
function cancelFinishBy() {
  clearFinishByUI();
  finishByState.preview = null;
}

// Clear finish-by UI
function clearFinishByUI() {
  const finishByInput = document.getElementById('finishByTime');
  const tempDeltaInput = document.getElementById('tempDelta');
  const minTempInput = document.getElementById('minTempLimit');
  const maxTempInput = document.getElementById('maxTempLimit');
  const finishByRow = document.getElementById('finishByRow');
  const finishByResults = document.getElementById('finishByResults');
  
  if (finishByInput) finishByInput.value = '';
  if (tempDeltaInput) tempDeltaInput.value = '0';
  if (minTempInput) minTempInput.value = finishByConfig.defaultMinTemp.toString();
  if (maxTempInput) maxTempInput.value = finishByConfig.defaultMaxTemp.toString();
  if (finishByRow) finishByRow.style.display = 'none';
  if (finishByResults) finishByResults.style.display = 'none';
  
  finishByState.preview = null;
}

// Show status message
function showStatus(message, type) {
  console.log(`[${type.toUpperCase()}] ${message}`);
  
  // Try to find a status display element
  const statusElements = ['finishByStatus', 'applyTempStatus', 'status'];
  
  for (const id of statusElements) {
    const el = document.getElementById(id);
    if (el) {
      el.textContent = message;
      el.className = type === 'success' ? 'success' : 'error';
      el.style.display = 'block';
      
      setTimeout(() => {
        el.style.display = 'none';
      }, 5000);
      break;
    }
  }
}

// Initialize finish-by functionality
function initializeFinishBy() {
  console.log('Initializing finish-by functionality...');
  
  // Load configuration
  loadFinishByConfig();
  
  // Set up event listeners
  const btnSmartDefault = document.getElementById('btnSmartDefault');
  const btnCalculateTemp = document.getElementById('btnCalculateTemp');
  const btnPreviewFinishBy = document.getElementById('btnPreviewFinishBy');
  const btnClearFinishBy = document.getElementById('btnClearFinishBy');
  const btnConfirmFinishBy = document.getElementById('btnConfirmFinishBy');
  const btnModifyFinishBy = document.getElementById('btnModifyFinishBy');
  const btnCancelFinishBy = document.getElementById('btnCancelFinishBy');
  
  if (btnSmartDefault) {
    btnSmartDefault.addEventListener('click', setSmartDefaultTime);
  }
  
  if (btnCalculateTemp) {
    btnCalculateTemp.addEventListener('click', calculateTemperatureAdjustment);
  }
  
  if (btnPreviewFinishBy) {
    btnPreviewFinishBy.addEventListener('click', previewFinishByChanges);
  }
  
  if (btnClearFinishBy) {
    btnClearFinishBy.addEventListener('click', clearFinishByUI);
  }
  
  if (btnConfirmFinishBy) {
    btnConfirmFinishBy.addEventListener('click', confirmFinishBy);
  }
  
  // Add protection against value changes during user interaction
  const finishByInput = document.getElementById('finishByTime');
  if (finishByInput) {
    // Track when user is actively interacting with the input
    let userIsInteracting = false;
    let interactionTimeout = null;
    
    finishByInput.addEventListener('focus', () => {
      userIsInteracting = true;
      console.log('User started interacting with finish-by time input');
    });
    
    finishByInput.addEventListener('blur', () => {
      // Add a small delay before allowing automatic changes
      // This prevents issues when the datetime picker causes brief blur events
      clearTimeout(interactionTimeout);
      interactionTimeout = setTimeout(() => {
        userIsInteracting = false;
        console.log('User finished interacting with finish-by time input');
      }, 1000);
    });
    
    finishByInput.addEventListener('input', () => {
      userIsInteracting = true;
      // Reset the interaction timeout
      clearTimeout(interactionTimeout);
      interactionTimeout = setTimeout(() => {
        userIsInteracting = false;
        console.log('User finished modifying finish-by time input');
      }, 2000);
    });
    
    // Make the interaction state available globally
    window.finishByUserInteracting = () => userIsInteracting;
  }
  
  if (btnModifyFinishBy) {
    btnModifyFinishBy.addEventListener('click', modifyFinishBy);
  }
  
  if (btnCancelFinishBy) {
    btnCancelFinishBy.addEventListener('click', cancelFinishBy);
  }
  
  // Show/hide finish-by controls based on program selection
  const programSelect = document.getElementById('programSelect');
  if (programSelect) {
    programSelect.addEventListener('change', function() {
      const finishByRow = document.getElementById('finishByRow');
      if (this.value && finishByRow) {
        finishByRow.style.display = 'block';
        
        // Load the selected program data using the correct filename format
        const programId = parseInt(this.value);
        const selectedProgram = window.cachedPrograms?.find(p => p.id === programId);
        
        if (selectedProgram) {
          currentProgram = selectedProgram;
          console.log('Set current program for finish-by:', selectedProgram.name);
        } else {
          console.warn('Could not find program data for ID:', programId);
          currentProgram = null;
        }
      } else if (finishByRow) {
        finishByRow.style.display = 'none';
        currentProgram = null;
      }
    });
  }
}

// Call initialize when DOM is ready
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', initializeFinishBy);
} else {
  initializeFinishBy();
}
