// --- Real-time chart update logic ---
async function updateStatus() {
  try {
    const response = await fetchWithTimeout('/api/pid_debug', 5000);
    if (!response.ok) throw new Error('Failed to fetch status');
    const data = await response.json();

    // Time axis: seconds since test start, or just now if not running test
    let now = Date.now();
    let t0 = testStartTime || now;
    let elapsed = Math.round((now - t0) / 1000);
    chartData.labels.push(elapsed);
    if (chartData.labels.length > 300) chartData.labels.shift();

    // Averaged temp
    chartData.datasets[0].data.push(data.current_temp);
    if (chartData.datasets[0].data.length > 300) chartData.datasets[0].data.shift();
    // Raw temp
    chartData.datasets[1].data.push(data.raw_temp);
    if (chartData.datasets[1].data.length > 300) chartData.datasets[1].data.shift();
    // Setpoint
    chartData.datasets[2].data.push(data.setpoint);
    if (chartData.datasets[2].data.length > 300) chartData.datasets[2].data.shift();
    // PID output
    chartData.datasets[3].data.push(data.output * 100);
    if (chartData.datasets[3].data.length > 300) chartData.datasets[3].data.shift();

    temperatureChart.update();
  } catch (err) {
    showMessage('Error updating chart: ' + err.message, 'error');
  }
}

function startStatusUpdates() {
  if (updateInterval) clearInterval(updateInterval);
  updateInterval = setInterval(updateStatus, 2000);
}

function stopStatusUpdates() {
  if (updateInterval) {
    clearInterval(updateInterval);
    updateInterval = null;
  }
}
// PID Controller Tuning Page JavaScript

// Global variables
let temperatureChart;
let testStartTime = null;
let testStartTemp = null;
let updateInterval = null;
let autoTuneRunning = false;
let autoTunePhase = 0; // 0=idle, 1=stabilizing, 2=step response/oscillation search, 3=analysis
let autoTuneStartTime = null;
let autoTuneData = [];
let autoTuneStepStart = null;
let autoTuneMethod = 'step'; // 'step' or 'ultimate'
let autoTuneBaseTemp = null; // Actual base temperature being used (may differ from user input)
let autoTuneSkipStabilization = false; // Whether to skip stabilization phase

// Ultimate gain method variables (declared in autotune file)
let ultimateGainTest;

// Chart data
let chartData = {
  labels: [],
  datasets: [{
    label: 'Averaged Temperature',
    data: [],
    borderColor: 'rgb(255, 99, 132)',
    backgroundColor: 'rgba(255, 99, 132, 0.1)',
    tension: 0.1,
    fill: false,
    pointRadius: 0,
    borderWidth: 2
  }, {
    label: 'Raw Temperature',
    data: [],
    borderColor: 'rgb(255, 152, 164)',
    backgroundColor: 'rgba(255, 152, 164, 0.1)',
    tension: 0.1,
    fill: false,
    pointRadius: 0,
    borderWidth: 1,
    borderDash: [5, 5] // Dashed line to distinguish from averaged
  }, {
    label: 'Target Temperature',
    data: [],
    borderColor: 'rgb(54, 162, 235)',
    backgroundColor: 'rgba(54, 162, 235, 0.1)',
    tension: 0.1,
    fill: false,
    pointRadius: 0,
    borderWidth: 2
  }, {
    label: 'PID Output (%)',
    data: [],
    borderColor: 'rgb(75, 192, 192)',
    backgroundColor: 'rgba(75, 192, 192, 0.1)',
    tension: 0.1,
    fill: false,
    pointRadius: 0,
    borderWidth: 1,
    yAxisID: 'y1'
  }]
};

// Initialize chart
function initChart() {
  const ctx = document.getElementById('temperatureChart').getContext('2d');
  temperatureChart = new Chart(ctx, {
    type: 'line',
    data: chartData,
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: {
        mode: 'index',
        intersect: false,
      },
      plugins: {
        title: {
          display: true,
          text: 'Real-Time Temperature Control Performance',
          font: { size: 16, weight: 'bold' }
        },
        legend: {
          display: true,
          position: 'top'
        }
      },
      scales: {
        x: {
          display: true,
          title: {
            display: true,
            text: 'Time (seconds)',
            font: { weight: 'bold' }
          },
          grid: { color: 'rgba(0,0,0,0.1)' }
        },
        y: {
          type: 'linear',
          display: true,
          position: 'left',
          title: {
            display: true,
            text: 'Temperature (°C)',
            font: { weight: 'bold' }
          },
          grid: { color: 'rgba(0,0,0,0.1)' }
        },
        y1: {
          type: 'linear',
          display: true,
          position: 'right',
          title: {
            display: true,
            text: 'PID Output (%)',
            font: { weight: 'bold' }
          },
          min: 0,
          max: 100,
          grid: { drawOnChartArea: false },
        }
      },
      animation: false,
      elements: {
        point: { radius: 0 }
      }
    }
  });
}

// Update temperature averaging parameters only
async function updateTempAveraging() {
  const tempSampleCount = parseInt(document.getElementById('tempSampleCount').value);
  const tempRejectCount = parseInt(document.getElementById('tempRejectCount').value);
  const tempSampleInterval = parseInt(document.getElementById('tempSampleInterval').value);

  // Validate temperature averaging parameters
  const remainingSamples = tempSampleCount - (2 * tempRejectCount);
  if (remainingSamples < 3) {
    showMessage('Invalid temperature averaging: must have at least 3 samples after rejection', 'error');
    return;
  }

  try {
    const url = `/api/pid_params?temp_samples=${tempSampleCount}&temp_reject=${tempRejectCount}&temp_interval=${tempSampleInterval}`;
    console.log('Updating temperature averaging:', { tempSampleCount, tempRejectCount, tempSampleInterval });
    
    const response = await fetchWithTimeout(url, 10000);
    const result = await response.json();
    console.log('Temperature averaging update response:', result);
    
    if (response.ok) {
      // Enhanced feedback using intelligent update information
      let successMessage = 'Temperature averaging parameters updated successfully';
      
      if (result.update_details) {
        successMessage = result.update_details;
        
        if (result.data_preservation) {
          successMessage += ` (${result.data_preservation})`;
        }
      } else if (!result.updated) {
        successMessage = 'Temperature averaging verified - no changes required';
      }
      
      showMessage(successMessage, 'success');
      
      // Log detailed preservation information
      if (result.data_preservation) {
        console.log('Data preservation status:', result.data_preservation);
      }
      
      // Multiple refresh attempts to ensure backend has processed the update
      const refreshWithRetry = async (attempt = 1, maxAttempts = 3) => {
        console.log(`Refreshing status after temperature averaging update (attempt ${attempt}/${maxAttempts})...`);
        
        if (typeof updateStatus === 'function') {
          await updateStatus();
        }
        
        // Check if the values were actually updated by comparing with what we sent
        const currentSamples = document.getElementById('currentTempSamples').textContent;
        const currentReject = document.getElementById('currentTempReject').textContent;
        const currentInterval = document.getElementById('currentTempInterval').textContent;
        
        const samplesMatch = currentSamples.includes(tempSampleCount.toString());
        const rejectMatch = currentReject.includes(tempRejectCount.toString());
        const intervalMatch = currentInterval.includes(tempSampleInterval.toString());
        
        if (!samplesMatch || !rejectMatch || !intervalMatch) {
          console.log(`Values not yet updated (attempt ${attempt}): samples=${samplesMatch}, reject=${rejectMatch}, interval=${intervalMatch}`);
          if (attempt < maxAttempts) {
            setTimeout(() => refreshWithRetry(attempt + 1, maxAttempts), 1500); // Retry with increasing delay
          } else {
            console.warn('Temperature averaging values may not have updated after multiple retries');
          }
        } else {
          console.log('Temperature averaging values successfully updated and confirmed');
        }
      };
      
      // Start refresh attempts after initial delay
      setTimeout(() => refreshWithRetry(), 2500);
    } else {
      // Handle validation errors from firmware
      console.error('Temperature averaging validation errors:', result);
      if (result.errors) {
        showMessage('Temperature averaging validation errors: ' + result.errors, 'error');
      } else {
        showMessage('Failed to update temperature averaging parameters', 'error');
      }
    }
  } catch (error) {
    console.error('Network error:', error);
    showMessage('Error updating temperature averaging: ' + error.message, 'error');
  }
}

// Update PID parameters with high precision
async function updatePIDParams() {
  const kp = parseFloat(document.getElementById('kpInput').value);
  const ki = parseFloat(document.getElementById('kiInput').value);
  const kd = parseFloat(document.getElementById('kdInput').value);
  const sampleTime = parseInt(document.getElementById('sampleTimeInput').value);
  const windowSize = parseInt(document.getElementById('windowSizeInput').value);
  const tempSampleCount = parseInt(document.getElementById('tempSampleCount').value);
  const tempRejectCount = parseInt(document.getElementById('tempRejectCount').value);
  const tempSampleInterval = parseInt(document.getElementById('tempSampleInterval').value);

  // Validate temperature averaging parameters
  const remainingSamples = tempSampleCount - (2 * tempRejectCount);
  if (remainingSamples < 3) {
    showMessage('Invalid temperature averaging: must have at least 3 samples after rejection', 'error');
    return;
  }

  console.log('Updating PID params:', { kp, ki, kd, sampleTime, windowSize, tempSampleCount, tempRejectCount, tempSampleInterval });

  try {
    const url = `/api/pid_params?kp=${kp.toFixed(6)}&ki=${ki.toFixed(6)}&kd=${kd.toFixed(6)}&sample_time=${sampleTime}&window_size=${windowSize}&temp_samples=${tempSampleCount}&temp_reject=${tempRejectCount}&temp_interval=${tempSampleInterval}`;
    console.log('Request URL:', url);
    
    const response = await fetchWithTimeout(url, 10000);
    const data = await response.json();
    
    if (response.ok) {
      console.log('PID parameters updated:', data);
      
      // Enhanced feedback based on firmware response
      let successMessage = 'Parameters updated successfully';
      
      if (data.update_details) {
        successMessage = data.update_details;
        
        // Add data preservation info if available
        if (data.data_preservation) {
          successMessage += ` (${data.data_preservation})`;
        }
      } else if (data.updated) {
        successMessage = 'PID parameters and temperature averaging updated successfully';
      } else {
        successMessage = 'Parameters verified - no changes required';
      }
      
      showMessage(successMessage, 'success');
      
      // Log detailed information for debugging
      if (data.update_details || data.data_preservation) {
        console.log('Update details:', data.update_details);
        console.log('Data preservation:', data.data_preservation);
      }
      
      // Update display
      document.getElementById('currentKp').textContent = kp.toFixed(6);
      document.getElementById('currentKi').textContent = ki.toFixed(6);
      document.getElementById('currentKd').textContent = kd.toFixed(6);
      document.getElementById('currentSampleTime').textContent = sampleTime + ' ms';
      
      // Refresh the current status display to show updated temperature averaging values
      setTimeout(() => {
        console.log('Refreshing status after PID parameters update...');
        if (typeof updateStatus === 'function') {
          updateStatus();
        }
      }, 1000); // Increased delay to ensure backend has processed the update
    } else {
      // Handle validation errors from firmware
      console.error('Parameter validation errors:', data);
      if (data.errors) {
        showMessage('Parameter validation errors: ' + data.errors, 'error');
      } else {
        showMessage('Failed to update PID parameters', 'error');
      }
    }
  } catch (error) {
    console.error('Network error:', error);
    showMessage('Error updating PID parameters: ' + error.message, 'error');
  }
}

// Load current PID parameters from controller
async function loadCurrentParams() {
  try {
    const response = await fetchWithTimeout('/api/pid_debug', 8000);
    const data = await response.json();
    
    document.getElementById('kpInput').value = data.kp.toFixed(6);
    document.getElementById('kiInput').value = data.ki.toFixed(6);
    document.getElementById('kdInput').value = data.kd.toFixed(6);
    
    if (data.sample_time_ms !== undefined) {
      document.getElementById('sampleTimeInput').value = data.sample_time_ms.toString();
    }
    if (data.window_size_ms !== undefined) {
      document.getElementById('windowSizeInput').value = data.window_size_ms.toString();
    }
    
    // Load temperature averaging parameters if available
    if (data.temp_sample_count !== undefined) {
      document.getElementById('tempSampleCount').value = data.temp_sample_count.toString();
    }
    if (data.temp_reject_count !== undefined) {
      document.getElementById('tempRejectCount').value = data.temp_reject_count.toString();
    }
    if (data.temp_sample_interval_ms !== undefined) {
      document.getElementById('tempSampleInterval').value = data.temp_sample_interval_ms.toString();
    }
    
    // Update current display
    document.getElementById('currentKp').textContent = data.kp.toFixed(6);
    document.getElementById('currentKi').textContent = data.ki.toFixed(6);
    document.getElementById('currentKd').textContent = data.kd.toFixed(6);
    document.getElementById('currentSampleTime').textContent = (data.sample_time_ms || 1000) + ' ms';
    
    // Validate temperature averaging after loading
    validateTempAveraging();
    
    showMessage('Current PID parameters and settings loaded', 'success');
    console.log('Loaded PID params:', data);
  } catch (error) {
    console.error('Error loading PID parameters:', error);
    showMessage('Error loading PID parameters', 'error');
  }
}

// Reset to conservative default values
function resetToDefaults() {
  document.getElementById('kpInput').value = '0.500000';
  document.getElementById('kiInput').value = '0.010000';
  document.getElementById('kdInput').value = '0.000000';
  document.getElementById('sampleTimeInput').value = '1000';
  document.getElementById('windowSizeInput').value = '30000';
  document.getElementById('tempSampleCount').value = '10';
  document.getElementById('tempRejectCount').value = '2';
  document.getElementById('tempSampleInterval').value = '500';
  validateTempAveraging();
  showMessage('Reset to thermal system defaults', 'info');
}

// Start manual temperature test
async function startManualTest() {
  const targetTemp = parseFloat(document.getElementById('targetTempInput').value);
  
  if (isNaN(targetTemp) || targetTemp < 0 || targetTemp > 250) {
    showMessage('Invalid target temperature (0-250°C)', 'error');
    return;
  }
  
  try {
    console.log(`Starting manual test: Target ${targetTemp}°C`);
    
    // First ensure we have current PID parameters applied
    console.log('Ensuring PID parameters are applied...');
    await updatePIDParams();
    
    // Wait a moment for parameters to be applied
    await new Promise(resolve => setTimeout(resolve, 500));
    
    // Enable manual mode and set temperature
    console.log('Setting manual mode ON...');
    const manualResponse = await fetchWithTimeout('/api/manual_mode?on=1', 5000);
    console.log('Manual mode response:', await manualResponse.text());
    
    console.log(`Setting temperature setpoint to ${targetTemp}°C...`);
    const tempResponse = await fetchWithTimeout(`/api/temperature?setpoint=${targetTemp}`, 5000);
    console.log('Temperature setpoint response:', await tempResponse.text());
    
    testStartTime = Date.now();
    testStartTemp = null;
    maxTemp = null;
    
    // Clear previous performance metrics
    resetMetrics();
    
    // Stop any existing status updates to avoid conflicts
    if (updateInterval) {
      clearInterval(updateInterval);
      updateInterval = null;
    }
    
    // Start continuous updates
    startStatusUpdates();
    
    showMessage(`Manual test started - Target: ${targetTemp}°C. Check status panel for updates.`, 'success');
    console.log('Manual test initialization complete');
    
    // Force immediate status update to verify everything is working
    setTimeout(() => {
      console.log('Forcing immediate status update...');
      updateStatus();
    }, 1000);
    
  } catch (error) {
    console.error('Error starting manual test:', error);
    showMessage('Error starting test: ' + error.message, 'error');
  }
}

// Stop temperature test
async function stopTest() {
  try {
    await fetchWithTimeout('/api/temperature?setpoint=0', 5000);
    await fetchWithTimeout('/api/manual_mode?on=0', 5000);
    
    if (updateInterval) {
      clearInterval(updateInterval);
      updateInterval = null;
    }
    
    testStartTime = null;
    showMessage('Test stopped - Manual mode disabled', 'info');
  } catch (error) {
    showMessage('Error stopping test: ' + error.message, 'error');
  }
}

// Clear graph data
function clearGraph() {
  chartData.labels = [];
  chartData.datasets.forEach(dataset => {
    dataset.data = [];
  });
  temperatureChart.update();
  resetMetrics();
  showMessage('Graph cleared', 'info');
}

// Reset performance metrics
function resetMetrics() {
  document.getElementById('riseTime').textContent = '--';
  document.getElementById('overshoot').textContent = '--';
  document.getElementById('settlingTime').textContent = '--';
  document.getElementById('steadyStateError').textContent = '--';
}

// Fetch with timeout wrapper
async function fetchWithTimeout(url, timeout = 8000) {
  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), timeout);
  
  try {
    const response = await fetch(url, { 
      signal: controller.signal,
      cache: 'no-cache'
    });
    clearTimeout(timeoutId);
    return response;
  } catch (error) {
    clearTimeout(timeoutId);
    throw error;
  }
}

// Show message to user
function showMessage(message, type) {
  console.log(`${type.toUpperCase()}: ${message}`);
  
  // Create toast notification
  const toast = document.createElement('div');
  toast.style.cssText = `
    position: fixed;
    top: 20px;
    right: 20px;
    padding: 12px 20px;
    border-radius: 6px;
    color: white;
    font-weight: bold;
    z-index: 1000;
    max-width: 300px;
    box-shadow: 0 4px 6px rgba(0,0,0,0.1);
    transform: translateX(100%);
    transition: transform 0.3s ease;
  `;
  
  const colors = {
    success: '#28a745',
    error: '#dc3545',
    warning: '#ffc107',
    info: '#17a2b8'
  };
  
  toast.style.background = colors[type] || colors.info;
  if (type === 'warning') toast.style.color = '#212529';
  
  toast.textContent = message;
  document.body.appendChild(toast);
  
  // Animate in
  setTimeout(() => toast.style.transform = 'translateX(0)', 10);
  
  // Animate out and remove
  setTimeout(() => {
    toast.style.transform = 'translateX(100%)';
    setTimeout(() => document.body.removeChild(toast), 300);
  }, 4000);
}

// Validate temperature averaging parameters
function validateTempAveraging() {
  const sampleCount = parseInt(document.getElementById('tempSampleCount').value);
  const rejectCount = parseInt(document.getElementById('tempRejectCount').value);
  
  // Ensure we have at least 3 samples after rejection
  const remainingSamples = sampleCount - (2 * rejectCount);
  
  if (remainingSamples < 3) {
    // Adjust reject count to leave at least 3 samples
    const maxRejectCount = Math.floor((sampleCount - 3) / 2);
    document.getElementById('tempRejectCount').value = Math.max(0, maxRejectCount);
    showMessage(`Reject count adjusted to ${maxRejectCount} to maintain minimum 3 samples for averaging`, 'warning');
  }
  
  // Update max value for reject count input
  const maxReject = Math.floor((sampleCount - 3) / 2);
  document.getElementById('tempRejectCount').setAttribute('max', maxReject);
}

// Update auto-tune method description based on selection
function updateAutoTuneMethodDescription() {
  const method = document.getElementById('autoTuneMethod').value;
  const stepSizeRow = document.getElementById('stepSizeRow');
  const stepSizeDesc = document.getElementById('stepSizeDesc');
  const processText = document.getElementById('autoTuneProcessText');
  
  if (method === 'step') {
    stepSizeRow.style.display = 'grid';
    stepSizeDesc.style.display = 'block';
    processText.textContent = 'The system will first stabilize at the base temperature, then make a step change to base + step size temperature while recording the response. This data is analyzed using the Ziegler-Nichols step response method to calculate optimal PID parameters.';
  } else if (method === 'ultimate') {
    stepSizeRow.style.display = 'none';
    stepSizeDesc.style.display = 'none';
    processText.textContent = 'The system performs controlled heating bursts at progressively higher gains, analyzing the thermal response characteristics. Unlike traditional ultimate gain methods, this focuses on the active heating response rather than passive oscillations, making it ideal for heating-only thermal systems like breadmakers.';
  }
}

// Initialize page
document.addEventListener('DOMContentLoaded', function() {
  initChart();
  loadCurrentParams();
  
  // Add auto-tune method change handler
  document.getElementById('autoTuneMethod').addEventListener('change', function() {
    updateAutoTuneMethodDescription();
  });
  
  // Add temperature averaging validation handlers
  document.getElementById('tempSampleCount').addEventListener('change', validateTempAveraging);
  document.getElementById('tempRejectCount').addEventListener('change', validateTempAveraging);
  
  // Initialize method description
  updateAutoTuneMethodDescription();
  
  // Start periodic status updates (slower when not testing)
  setInterval(() => {
    if (!updateInterval) {
      updateStatus();
    }
  }, 5000);
  
  console.log('PID Controller Tuning page initialized');
});

// Force status refresh (manual) with delayed re-query
function forceStatusRefresh() {
  console.log('Manual status refresh requested...');
  
  // First, immediate refresh
  if (typeof updateStatus === 'function') {
    updateStatus();
  } else {
    console.error('updateStatus function not available');
    return;
  }
  
  // Second, delayed refresh to catch any backend updates that might be slow
  setTimeout(() => {
    console.log('Delayed manual refresh (ensuring backend sync)...');
    if (typeof updateStatus === 'function') {
      updateStatus();
    }
  }, 1500);
}
