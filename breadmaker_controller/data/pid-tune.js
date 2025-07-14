// Utility function for fetch with timeout
function fetchWithTimeout(url, timeout = 10000) {
  return new Promise((resolve, reject) => {
    const controller = new AbortController();
    const id = setTimeout(() => {
      controller.abort();
      reject(new Error(`Request timeout after ${timeout}ms`));
    }, timeout);
    
    fetch(url, { signal: controller.signal })
      .then(response => {
        clearTimeout(id);
        resolve(response);
      })
      .catch(error => {
        clearTimeout(id);
        reject(error);
      });
  });
}

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
}

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

// Show status messages to user
function showMessage(message, type = 'info', duration = 3000) {
  // Create message element if it doesn't exist
  let messageContainer = document.getElementById('messageContainer');
  if (!messageContainer) {
    messageContainer = document.createElement('div');
    messageContainer.id = 'messageContainer';
    messageContainer.style.cssText = `
      position: fixed;
      top: 20px;
      right: 20px;
      z-index: 1000;
      max-width: 400px;
    `;
    document.body.appendChild(messageContainer);
  }
  
  // Create message element
  const messageElement = document.createElement('div');
  messageElement.style.cssText = `
    padding: 12px 16px;
    margin-bottom: 10px;
    border-radius: 6px;
    box-shadow: 0 4px 8px rgba(0,0,0,0.2);
    color: white;
    font-weight: bold;
    animation: slideIn 0.3s ease-out;
  `;
  
  // Set background color based on type
  switch (type) {
    case 'success':
      messageElement.style.backgroundColor = '#4CAF50';
      break;
    case 'error':
      messageElement.style.backgroundColor = '#f44336';
      break;
    case 'warning':
      messageElement.style.backgroundColor = '#ff9800';
      break;
    case 'info':
    default:
      messageElement.style.backgroundColor = '#2196f3';
      break;
  }
  
  messageElement.textContent = message;
  messageContainer.appendChild(messageElement);
  
  // Auto-remove after duration
  setTimeout(() => {
    if (messageElement.parentNode) {
      messageElement.style.animation = 'slideOut 0.3s ease-in';
      setTimeout(() => {
        if (messageElement.parentNode) {
          messageElement.parentNode.removeChild(messageElement);
        }
      }, 300);
    }
  }, duration);
}

// Add CSS animations for messages
if (!document.getElementById('messageAnimations')) {
  const style = document.createElement('style');
  style.id = 'messageAnimations';
  style.textContent = `
    @keyframes slideIn {
      from { transform: translateX(100%); opacity: 0; }
      to { transform: translateX(0); opacity: 1; }
    }
    @keyframes slideOut {
      from { transform: translateX(0); opacity: 1; }
      to { transform: translateX(100%); opacity: 0; }
    }
  `;
  document.head.appendChild(style);
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

// Additional initialization for range management
document.addEventListener('DOMContentLoaded', function() {
  // Wait a bit for other initializations to complete
  setTimeout(() => {
    // Initialize range management
    if (typeof initializeRangeManagement === 'function') {
      initializeRangeManagement();
    }
    
    // Add setpoint change handler for auto-detection
    const targetSetpoint = document.getElementById('targetSetpoint');
    if (targetSetpoint) {
      targetSetpoint.addEventListener('change', detectRangeFromSetpoint);
      targetSetpoint.addEventListener('input', detectRangeFromSetpoint);
    }
    
    console.log('Range management initialization completed');
  }, 500);
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

// Temperature-dependent PID parameter suggestions based on user tuning experience
const temperaturePIDSuggestions = [
  { 
    key: "room",
    min: 0, max: 35, 
    name: "Room Temperature (0-35°C)",
    kp: 0.5, ki: 0.00001, kd: 2.0,
    windowMs: 60000, // Longer window for gentle control
    description: "Very gentle control - minimal heat to prevent long thermal mass rises. Use longer windows (60s) to reduce heat frequency."
  },
  { 
    key: "lowFerm",
    min: 35, max: 50, 
    name: "Low Fermentation (35-50°C)",
    kp: 0.3, ki: 0.00002, kd: 3.0,
    windowMs: 45000, // Still long window
    description: "Gentle warming prevents thermal mass overshoot. Even small heat pulses cause long temperature rises at these levels."
  },
  { 
    key: "medFerm",
    min: 50, max: 70, 
    name: "Medium Fermentation (50-70°C)", 
    kp: 0.2, ki: 0.00005, kd: 5.0,
    windowMs: 30000, // Moderate window
    description: "Balanced control for typical fermentation temps. Higher derivative starts to help with thermal prediction."
  },
  { 
    key: "highFerm",
    min: 70, max: 100, 
    name: "High Fermentation (70-100°C)",
    kp: 0.15, ki: 0.00008, kd: 6.0,
    windowMs: 25000,
    description: "More responsive for higher fermentation temps. Thermal mass less problematic, faster response possible."
  },
  { 
    key: "baking",
    min: 100, max: 150, 
    name: "Baking Heat (100-150°C)",
    kp: 0.11, ki: 0.00005, kd: 10.0, // Your current settings
    windowMs: 15000, // Your current setting
    description: "Your current range - balanced for baking. High derivative (10) provides excellent control."
  },
  { 
    key: "highBaking",
    min: 150, max: 200, 
    name: "High Baking (150-200°C)",
    kp: 0.08, ki: 0.00003, kd: 10.0,
    windowMs: 15000,
    description: "Higher derivative to prevent overshoot. Very responsive control needed at high temperatures."
  },
  { 
    key: "extreme",
    min: 200, max: 250, 
    name: "Extreme Heat (200-250°C)",
    kp: 0.015, ki: 0.00015, kd: 10.0, // Your exact high-temp settings
    windowMs: 10000,
    description: "Your exact tuned settings - maximum derivative control. Perfect for extreme temperatures where overshoot must be prevented."
  }
];

// Range configuration tracking
let rangeConfigurationStatus = {
  room: { configured: false, kp: null, ki: null, kd: null, windowMs: null },
  lowFerm: { configured: false, kp: null, ki: null, kd: null, windowMs: null },
  medFerm: { configured: false, kp: null, ki: null, kd: null, windowMs: null },
  highFerm: { configured: false, kp: null, ki: null, kd: null, windowMs: null },
  baking: { configured: true, kp: 0.11, ki: 0.00005, kd: 10.0, windowMs: 15000 }, // Default configured
  highBaking: { configured: false, kp: null, ki: null, kd: null, windowMs: null },
  extreme: { configured: false, kp: null, ki: null, kd: null, windowMs: null }
};

let currentRangeIndex = 4; // Start with baking range
let isRangeConfigurationMode = false;

// Detect temperature range from setpoint and highlight it
function detectRangeFromSetpoint() {
  const setpoint = parseFloat(document.getElementById('targetSetpoint').value);
  if (isNaN(setpoint)) {
    showMessage('Please enter a valid temperature setpoint', 'error', 3000);
    return;
  }

  // Find the appropriate temperature range
  const detectedRange = temperaturePIDSuggestions.find(range => 
    setpoint >= range.min && setpoint < range.max
  );

  if (!detectedRange) {
    showMessage('Temperature out of supported range (0-250°C)', 'error', 3000);
    return;
  }

  // Update the range selector to match detected range
  document.getElementById('tempRangeSelect').value = detectedRange.key;
  
  // Update the detected range info
  const detectedInfo = document.getElementById('detectedRangeInfo');
  detectedInfo.innerHTML = `
    <div style="background: rgba(255, 214, 0, 0.2); padding: 10px; border-radius: 6px; border: 1px solid #ffd600;">
      <strong>🎯 Detected Range:</strong> ${detectedRange.name}<br>
      <small>${detectedRange.description}</small><br>
      <div style="margin-top: 8px; font-family: monospace; font-size: 0.9em;">
        Suggested: Kp=${detectedRange.kp} | Ki=${detectedRange.ki} | Kd=${detectedRange.kd} | Window=${detectedRange.windowMs/1000}s
      </div>
    </div>
  `;

  // Set current range index for navigation
  currentRangeIndex = temperaturePIDSuggestions.findIndex(range => range.key === detectedRange.key);
  
  // Load the range settings
  loadTemperatureRangeSettings();
  
  // Update the range status display
  updateRangeStatusDisplay();
  
  showMessage(`🎯 Auto-detected range: ${detectedRange.name}`, 'success', 3000);
}

// Update the range configuration status display
function updateRangeStatusDisplay() {
  const statusGrid = document.getElementById('rangeStatusGrid');
  statusGrid.innerHTML = '';

  temperaturePIDSuggestions.forEach((range, index) => {
    const status = rangeConfigurationStatus[range.key];
    const isActive = index === currentRangeIndex;
    
    const statusItem = document.createElement('div');
    statusItem.className = `range-status-item ${status.configured ? 'configured' : 'unconfigured'} ${isActive ? 'active' : ''}`;
    statusItem.onclick = () => selectRange(index);
    
    statusItem.innerHTML = `
      <div class="range-status-header">
        <div class="range-status-name">${range.name}</div>
        <div class="range-status-indicator">${status.configured ? '✅' : '❌'}</div>
      </div>
      <div class="range-status-details">
        ${range.min}°C - ${range.max}°C | ${status.configured ? 
          `Kp=${status.kp}, Ki=${status.ki}, Kd=${status.kd}` : 
          'Not configured'}
      </div>
    `;
    
    statusGrid.appendChild(statusItem);
  });
}

// Navigate between temperature ranges
function navigateToRange(direction) {
  const newIndex = currentRangeIndex + direction;
  if (newIndex >= 0 && newIndex < temperaturePIDSuggestions.length) {
    selectRange(newIndex);
  } else {
    showMessage(direction > 0 ? 'Already at the last range' : 'Already at the first range', 'info', 2000);
  }
}

// Select a specific range by index
function selectRange(index) {
  if (index < 0 || index >= temperaturePIDSuggestions.length) return;
  
  currentRangeIndex = index;
  const range = temperaturePIDSuggestions[index];
  
  // Update the dropdown
  document.getElementById('tempRangeSelect').value = range.key;
  
  // Update setpoint to mid-range value for testing
  const midTemp = (range.min + range.max) / 2;
  document.getElementById('targetSetpoint').value = midTemp.toFixed(1);
  
  // Load range settings
  loadTemperatureRangeSettings();
  
  // Update status display
  updateRangeStatusDisplay();
  
  showMessage(`📍 Selected range: ${range.name}`, 'info', 2000);
}

// Confirm and save current range configuration
function confirmRangeConfiguration() {
  const currentRange = temperaturePIDSuggestions[currentRangeIndex];
  if (!currentRange) return;
  
  // Get current PID values from inputs
  const kp = parseFloat(document.getElementById('kpInput').value);
  const ki = parseFloat(document.getElementById('kiInput').value);
  const kd = parseFloat(document.getElementById('kdInput').value);
  const windowMs = parseFloat(document.getElementById('windowSizeInput').value);
  
  if (isNaN(kp) || isNaN(ki) || isNaN(kd) || isNaN(windowMs)) {
    showMessage('Please enter valid PID parameters before confirming', 'error', 3000);
    return;
  }
  
  // Save the configuration
  rangeConfigurationStatus[currentRange.key] = {
    configured: true,
    kp: kp,
    ki: ki,
    kd: kd,
    windowMs: windowMs
  };
  
  // Update the PID profiles on the controller
  updatePIDProfileOnController(currentRange.key, { kp, ki, kd, windowMs });
  
  // Update display
  updateRangeStatusDisplay();
  
  showMessage(`✅ Confirmed configuration for ${currentRange.name}`, 'success', 3000);
  
  // Auto-navigate to next unconfigured range
  const nextUnconfigured = findNextUnconfiguredRange();
  if (nextUnconfigured !== -1) {
    setTimeout(() => {
      selectRange(nextUnconfigured);
      showMessage(`🔄 Moving to next unconfigured range`, 'info', 2000);
    }, 1500);
  } else {
    showMessage(`🎉 All ranges configured! PID system ready.`, 'success', 4000);
  }
}

// Find the next unconfigured range
function findNextUnconfiguredRange() {
  for (let i = 0; i < temperaturePIDSuggestions.length; i++) {
    const range = temperaturePIDSuggestions[i];
    if (!rangeConfigurationStatus[range.key].configured) {
      return i;
    }
  }
  return -1; // All configured
}

// Start the range configuration wizard
function startRangeConfigurationWizard() {
  isRangeConfigurationMode = true;
  const firstUnconfigured = findNextUnconfiguredRange();
  
  if (firstUnconfigured === -1) {
    showMessage('🎉 All ranges are already configured!', 'success', 3000);
    return;
  }
  
  selectRange(firstUnconfigured);
  showMessage(`🧙‍♂️ Configuration wizard started. Configure each range step by step.`, 'info', 4000);
}

// Update PID profile on the controller (API call)
async function updatePIDProfileOnController(rangeKey, params) {
  try {
    const response = await fetch('/api/pid_profile', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        profile: rangeKey,
        kp: params.kp,
        ki: params.ki,
        kd: params.kd,
        windowMs: params.windowMs
      })
    });
    
    if (!response.ok) {
      throw new Error('Failed to update PID profile on controller');
    }
    
    console.log(`Updated ${rangeKey} profile on controller`);
  } catch (error) {
    console.error('Error updating PID profile:', error);
    showMessage('Warning: Failed to save profile to controller', 'warning', 3000);
  }
}

// Load range configuration status from controller
async function loadRangeConfigurationStatus() {
  try {
    const response = await fetch('/api/pid_profile');
    if (response.ok) {
      const data = await response.json();
      
      // Update configuration status based on controller data
      if (data.profiles) {
        data.profiles.forEach(profile => {
          if (rangeConfigurationStatus[profile.key]) {
            rangeConfigurationStatus[profile.key] = {
              configured: true,
              kp: profile.kp,
              ki: profile.ki,
              kd: profile.kd,
              windowMs: profile.windowMs
            };
          }
        });
      }
    }
  } catch (error) {
    console.error('Error loading range configuration status:', error);
  }
}

// Initialize range management when page loads
function initializeRangeManagement() {
  loadRangeConfigurationStatus();
  updateRangeStatusDisplay();
  
  // Set initial setpoint to current baking range
  document.getElementById('targetSetpoint').value = '125';
  detectRangeFromSetpoint();
}