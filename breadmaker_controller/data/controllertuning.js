// --- Real-time chart update logic ---
async function updateStatus() {
    // PID P, I, D terms
    if (document.getElementById('pidPTerm')) {
      document.getElementById('pidPTerm').textContent =
        (data.pid_p !== undefined ? data.pid_p.toFixed(3) : '--');
    }
    if (document.getElementById('pidITerm')) {
      document.getElementById('pidITerm').textContent =
        (data.pid_i !== undefined ? data.pid_i.toFixed(3) : '--');
    }
    if (document.getElementById('pidDTerm')) {
      document.getElementById('pidDTerm').textContent =
        (data.pid_d !== undefined ? data.pid_d.toFixed(3) : '--');
    }
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

    // --- PID Component Chart ---
    if (window.pidComponentChart && window.pidComponentData) {
      window.pidComponentData.labels.push(elapsed);
      if (window.pidComponentData.labels.length > 300) window.pidComponentData.labels.shift();
      window.pidComponentData.datasets[0].data.push(data.pid_p);
      if (window.pidComponentData.datasets[0].data.length > 300) window.pidComponentData.datasets[0].data.shift();
      window.pidComponentData.datasets[1].data.push(data.pid_i);
      if (window.pidComponentData.datasets[1].data.length > 300) window.pidComponentData.datasets[1].data.shift();
      window.pidComponentData.datasets[2].data.push(data.pid_d);
      if (window.pidComponentData.datasets[2].data.length > 300) window.pidComponentData.datasets[2].data.shift();
      window.pidComponentChart.update();
    }

    // --- Update status display fields ---
    // Current Temp
    if (document.getElementById('currentTemp')) {
      document.getElementById('currentTemp').textContent =
        (data.current_temp !== undefined ? data.current_temp.toFixed(1) : '--.-') + '°C';
    }
    // Target Temp (Setpoint)
    if (document.getElementById('targetTemp')) {
      document.getElementById('targetTemp').textContent =
        (data.setpoint !== undefined ? data.setpoint.toFixed(1) : '--.-') + '°C';
    }
    // Temperature Error
    if (document.getElementById('tempError')) {
      if (data.setpoint !== undefined && data.current_temp !== undefined) {
        document.getElementById('tempError').textContent = (data.setpoint - data.current_temp).toFixed(1) + '°C';
      } else {
        document.getElementById('tempError').textContent = '--.-°C';
      }
    }
    // PID Output
    if (document.getElementById('pidOutput')) {
      document.getElementById('pidOutput').textContent =
        (data.output !== undefined ? (data.output * 100).toFixed(2) : '--.-------') + '%';
    }
    // Heater State
    if (document.getElementById('heaterState')) {
      document.getElementById('heaterState').textContent =
        (data.heater_state === true ? 'ON' : data.heater_state === false ? 'OFF' : '--');
    }
    // Control Phase (show manual/auto/running)
    if (document.getElementById('phaseStatus')) {
      let phase = '--';
      if (data.manual_mode === true) phase = 'Manual';
      else if (data.is_running === true) phase = 'Program';
      else if (data.manual_mode === false && data.is_running === false) phase = 'Idle';
      document.getElementById('phaseStatus').textContent = phase;
    }
    // Window Progress
    if (document.getElementById('windowStatus')) {
      if (data.window_elapsed_ms !== undefined && data.window_size_ms !== undefined) {
        document.getElementById('windowStatus').textContent =
          `${data.window_elapsed_ms} / ${data.window_size_ms} ms`;
      } else {
        document.getElementById('windowStatus').textContent = '--';
      }
    }
    // ON Time (calc)
    if (document.getElementById('onTimeStatus')) {
      document.getElementById('onTimeStatus').textContent =
        (data.on_time_ms !== undefined ? data.on_time_ms + ' ms' : '--');
    }
    // Dynamic Restarts (not available, so show --)
    if (document.getElementById('dynamicRestartStatus')) {
      document.getElementById('dynamicRestartStatus').textContent = '--';
    }
    // Manual Mode
    if (document.getElementById('manualMode')) {
      document.getElementById('manualMode').textContent =
        (data.manual_mode === true ? 'ON' : data.manual_mode === false ? 'OFF' : '--');
    }
    // PID Settings
    if (document.getElementById('currentKp')) {
      document.getElementById('currentKp').textContent =
        (data.kp !== undefined ? data.kp.toFixed(6) : '--.------');
    }
    if (document.getElementById('currentKi')) {
      document.getElementById('currentKi').textContent =
        (data.ki !== undefined ? data.ki.toFixed(6) : '--.------');
    }
    if (document.getElementById('currentKd')) {
      document.getElementById('currentKd').textContent =
        (data.kd !== undefined ? data.kd.toFixed(6) : '--.------');
    }
    if (document.getElementById('currentSampleTime')) {
      document.getElementById('currentSampleTime').textContent =
        (data.sample_time_ms !== undefined ? data.sample_time_ms + ' ms' : '-- ms');
    }
    // Temperature Averaging Status
    if (document.getElementById('currentTempSamples')) {
      document.getElementById('currentTempSamples').textContent =
        (data.temp_sample_count !== undefined ? data.temp_sample_count + ' samples' : '-- samples');
    }
    if (document.getElementById('currentTempReject')) {
      document.getElementById('currentTempReject').textContent =
        (data.temp_reject_count !== undefined ? data.temp_reject_count + ' each end' : '-- each end');
    }
    if (document.getElementById('currentTempInterval')) {
      document.getElementById('currentTempInterval').textContent =
        (data.temp_sample_interval_ms !== undefined ? data.temp_sample_interval_ms + ' ms' : '-- ms');
    }
    if (document.getElementById('currentTempEffective')) {
      if (data.temp_sample_count !== undefined && data.temp_reject_count !== undefined) {
        let eff = data.temp_sample_count - 2 * data.temp_reject_count;
        document.getElementById('currentTempEffective').textContent = eff + ' used';
      } else {
        document.getElementById('currentTempEffective').textContent = '-- used';
      }
    }
    temperatureChart.update();
  } catch (err) {
    showMessage('Error updating chart: ' + err.message, 'error');
  }
// ...existing code...
// --- PID Component Chart Setup ---
window.pidComponentData = {
  labels: [],
  datasets: [
    {
      label: 'P Term',
      data: [],
      borderColor: 'rgb(255, 99, 132)',
      backgroundColor: 'rgba(255, 99, 132, 0.1)',
      tension: 0.1,
      fill: false,
      pointRadius: 0,
      borderWidth: 2
    },
    {
      label: 'I Term',
      data: [],
      borderColor: 'rgb(54, 162, 235)',
      backgroundColor: 'rgba(54, 162, 235, 0.1)',
      tension: 0.1,
      fill: false,
      pointRadius: 0,
      borderWidth: 2
    },
    {
      label: 'D Term',
      data: [],
      borderColor: 'rgb(255, 206, 86)',
      backgroundColor: 'rgba(255, 206, 86, 0.1)',
      tension: 0.1,
      fill: false,
      pointRadius: 0,
      borderWidth: 2
    }
  ]
};

window.pidComponentChart = null;

function initPIDComponentChart() {
  const ctx = document.getElementById('pidComponentChart').getContext('2d');
  window.pidComponentChart = new Chart(ctx, {
    type: 'line',
    data: window.pidComponentData,
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
          text: 'PID Component Terms (P, I, D)',
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
            text: 'Component Value',
            font: { weight: 'bold' }
          },
          grid: { color: 'rgba(0,0,0,0.1)' }
        }
      },
      animation: false,
      elements: {
        point: { radius: 0 }
      }
    }
  });
}

function clearPIDGraph() {
  window.pidComponentData.labels = [];
  window.pidComponentData.datasets.forEach(dataset => {
    dataset.data = [];
  });
  if (window.pidComponentChart) window.pidComponentChart.update();
  showMessage('PID component graph cleared', 'info');
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
    showMessage('Error loading PID parameters: ' + error.message, 'error');
  }

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
  initPIDComponentChart();
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
