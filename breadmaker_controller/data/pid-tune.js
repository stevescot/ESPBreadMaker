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

// Register Chart.js plugins after page load
document.addEventListener('DOMContentLoaded', function() {
  if (window.Chart && window['chartjs-plugin-annotation']) {
    Chart.register(window['chartjs-plugin-annotation']);
  }
});

// Simple message display function (standalone version)
function showMessage(message, type = 'info', duration = 3000) {
  // Remove any existing messages
  const existing = document.querySelector('.pid-message');
  if (existing) existing.remove();
  
  // Create new message element
  const msg = document.createElement('div');
  msg.className = `pid-message pid-message-${type}`;
  msg.textContent = message;
  msg.style.cssText = `
    position: fixed;
    top: 20px;
    right: 20px;
    z-index: 9999;
    padding: 12px 20px;
    border-radius: 4px;
    max-width: 300px;
    font-size: 14px;
    box-shadow: 0 4px 6px rgba(0,0,0,0.1);
    ${type === 'error' ? 'background: #f44336; color: white;' : ''}
    ${type === 'warning' ? 'background: #ff9800; color: white;' : ''}
    ${type === 'success' ? 'background: #4caf50; color: white;' : ''}
    ${type === 'info' ? 'background: #2196f3; color: white;' : ''}
  `;
  
  document.body.appendChild(msg);
  
  // Auto-remove after duration
  if (duration > 0) {
    setTimeout(() => {
      if (msg.parentNode) msg.remove();
    }, duration);
  }
}

// --- Change Log and Chart Annotations ---
function logChange(action, details = {}) {
  const timestamp = new Date();
  const entry = {
    time: timestamp,
    action: action,
    details: details,
    chartTime: Date.now() // For chart annotation positioning
  };
  
  changeLog.unshift(entry); // Add to beginning of array
  
  // Keep only last 50 entries
  if (changeLog.length > 50) {
    changeLog = changeLog.slice(0, 50);
  }
  
  // Update the UI
  updateChangeLogDisplay();
  
  // Add chart annotation
  addChartAnnotation(entry);
  
  console.log('Logged change:', action, details);
}

function updateChangeLogDisplay() {
  const container = document.getElementById('changeLogEntries');
  if (!container) return;
  
  if (changeLog.length === 0) {
    container.innerHTML = '<div class="log-entry" style="color: #6c757d; font-style: italic; text-align: center; padding: 20px;">No changes logged yet. Start tuning to see your adjustments here!</div>';
    return;
  }
  
  const entriesHtml = changeLog.map(entry => {
    const timeStr = entry.time.toLocaleTimeString('en-US', { 
      hour12: false, 
      hour: '2-digit', 
      minute: '2-digit', 
      second: '2-digit' 
    });
    
    let icon = '📝';
    let color = '#495057';
    
    // Assign icons and colors based on action type
    if (entry.action.includes('Parameter')) { icon = '🔧'; color = '#007bff'; }
    else if (entry.action.includes('Range')) { icon = '🌡️'; color = '#28a745'; }
    else if (entry.action.includes('Test')) { icon = '🧪'; color = '#ffc107'; }
    else if (entry.action.includes('Reset')) { icon = '🔄'; color = '#dc3545'; }
    else if (entry.action.includes('Profile')) { icon = '📊'; color = '#6f42c1'; }
    
    const detailsStr = Object.keys(entry.details).length > 0 
      ? ` - ${Object.entries(entry.details).map(([k,v]) => `${k}: ${v}`).join(', ')}`
      : '';
    
    return `
      <div class="log-entry" style="padding: 8px 12px; border-left: 3px solid ${color}; margin-bottom: 6px; background: white; border-radius: 4px; font-size: 14px;">
        <div style="display: flex; justify-content: between; align-items: center;">
          <span style="color: ${color}; font-weight: bold;">${icon} ${entry.action}</span>
          <span style="color: #6c757d; font-size: 12px; margin-left: auto;">${timeStr}</span>
        </div>
        ${detailsStr ? `<div style="color: #6c757d; font-size: 12px; margin-top: 2px;">${detailsStr}</div>` : ''}
      </div>
    `;
  }).join('');
  
  container.innerHTML = entriesHtml;
}

function addChartAnnotation(entry) {
  if (!temperatureChart) return;
  
  // Store annotation for tracking
  const annotation = {
    time: entry.chartTime,
    label: entry.action,
    details: entry.details,
    color: getAnnotationColor(entry.action)
  };
  
  chartAnnotations.push(annotation);
  
  // Keep only last 20 annotations to avoid clutter
  if (chartAnnotations.length > 20) {
    chartAnnotations = chartAnnotations.slice(-20);
  }
  
  // Add visual annotation to the chart only if chartData exists
  if (!chartData || !chartData.labels) {
    console.log(`Chart annotation queued: ${entry.action} - ${entry.details}`);
    return;
  }
  
  const currentTime = chartData.labels.length - 1;
  const annotationId = `annotation_${Date.now()}_${Math.random().toString(36).substr(2, 5)}`;
  
  const chartAnnotation = {
    type: 'line',
    xMin: currentTime,
    xMax: currentTime,
    borderColor: annotation.color,
    borderWidth: 2,
    borderDash: [5, 5],
    label: {
      display: true,
      content: `${entry.action}`,
      position: 'start',
      backgroundColor: annotation.color,
      color: 'white',
      font: { size: 9 },
      padding: 3,
      rotation: 90
    }
  };
  
  // Add to chart if annotation plugin is available
  if (temperatureChart.options.plugins && temperatureChart.options.plugins.annotation) {
    temperatureChart.options.plugins.annotation.annotations[annotationId] = chartAnnotation;
    temperatureChart.update('none'); // Update without animation for performance
  }
}

function getAnnotationColor(action) {
  if (action.includes('Parameter')) return '#007bff';
  if (action.includes('Range')) return '#28a745';
  if (action.includes('Test')) return '#ffc107';
  if (action.includes('Reset')) return '#dc3545';
  if (action.includes('Profile')) return '#6f42c1';
  return '#6c757d';
}

function clearChangeLog() {
  if (confirm('Clear all change log entries?')) {
    changeLog = [];
    chartAnnotations = [];
    
    // Clear chart annotations if chart exists
    if (temperatureChart && temperatureChart.options.plugins && temperatureChart.options.plugins.annotation) {
      temperatureChart.options.plugins.annotation.annotations = {};
      temperatureChart.update('none');
    }
    
    updateChangeLogDisplay();
    showMessage('Change log cleared', 'info', 3000);
  }
}

// --- Real-time chart update logic ---
let updateInProgress = false;
async function updateStatus() {
  // Prevent request backup - skip if already updating
  if (updateInProgress) {
    console.log('Skipping update - previous request still in progress');
    return;
  }
  
  updateInProgress = true;
  try {
    // Use lightweight PID status endpoint instead of full status
    const response = await fetchWithTimeout('/api/pid_status', 10000);
    if (!response.ok) throw new Error('Failed to fetch PID status');
    const data = await response.json();
    
    // Use raw temperature directly from lightweight endpoint
    let rawTemperature = data.rawTemperature || data.temperature;

    // PID P, I, D terms - use available data from status API
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

    // Time axis: seconds since test start, or just now if not running test
    let now = Date.now();
    let t0 = testStartTime || now;
    let elapsed = Math.round((now - t0) / 1000);
    chartData.labels.push(elapsed);
    if (chartData.labels.length > 300) chartData.labels.shift();

    // Averaged temp (map from temperature field)
    chartData.datasets[0].data.push(data.temperature || data.temp);
    if (chartData.datasets[0].data.length > 300) chartData.datasets[0].data.shift();
    // Raw temp (use raw temperature from calibration endpoint)
    chartData.datasets[1].data.push(rawTemperature);
    if (chartData.datasets[1].data.length > 300) chartData.datasets[1].data.shift();
    // Setpoint
    chartData.datasets[2].data.push(data.setpoint || 0);
    if (chartData.datasets[2].data.length > 300) chartData.datasets[2].data.shift();
    // PID output (map from pid_output field)
    chartData.datasets[3].data.push((data.pid_output || 0) * 100);
    if (chartData.datasets[3].data.length > 300) chartData.datasets[3].data.shift();

    // --- PID Component Chart ---
    // Use actual P, I, D terms from status API
    if (window.pidComponentChart && window.pidComponentData) {
      window.pidComponentData.labels.push(elapsed);
      if (window.pidComponentData.labels.length > 300) window.pidComponentData.labels.shift();
      // P term (Proportional)
      window.pidComponentData.datasets[0].data.push(data.pid_p || 0);
      if (window.pidComponentData.datasets[0].data.length > 300) window.pidComponentData.datasets[0].data.shift();
      // I term (Integral)
      window.pidComponentData.datasets[1].data.push(data.pid_i || 0);
      if (window.pidComponentData.datasets[1].data.length > 300) window.pidComponentData.datasets[1].data.shift();
      // D term (Derivative)
      window.pidComponentData.datasets[2].data.push(data.pid_d || 0);
      if (window.pidComponentData.datasets[2].data.length > 300) window.pidComponentData.datasets[2].data.shift();
      
      // Debug: Log PID values every 10 updates
      if (window.pidComponentData.labels.length % 10 === 0) {
        console.log('PID Update:', {
          elapsed: elapsed,
          pid_p: data.pid_p,
          pid_i: data.pid_i, 
          pid_d: data.pid_d,
          dataPoints: window.pidComponentData.datasets[0].data.length
        });
      }
      
      window.pidComponentChart.update();
    } else {
      console.log('PID chart not available:', {
        chart: !!window.pidComponentChart,
        data: !!window.pidComponentData
      });
    }

    // --- Update status display fields ---
    // Current Temp (map from temperature field)
    if (document.getElementById('currentTemp')) {
      document.getElementById('currentTemp').textContent =
        (data.temperature !== undefined ? data.temperature.toFixed(1) : '--.-') + '°C';
    }
    // Target Temp (Setpoint)
    if (document.getElementById('targetTemp')) {
      document.getElementById('targetTemp').textContent =
        (data.setpoint !== undefined ? data.setpoint.toFixed(1) : '--.-') + '°C';
    }
    // Temperature Error
    if (document.getElementById('tempError')) {
      if (data.setpoint !== undefined && data.temperature !== undefined) {
        document.getElementById('tempError').textContent = (data.setpoint - data.temperature).toFixed(1) + '°C';
      } else {
        document.getElementById('tempError').textContent = '--.-°C';
      }
    }
    // PID Output (map from pid_output field)
    if (document.getElementById('pidOutput')) {
      document.getElementById('pidOutput').textContent =
        (data.pid_output !== undefined ? (data.pid_output * 100).toFixed(2) : '--.-------') + '%';
    }
    // Heater State (map from heater field)
    if (document.getElementById('heaterState')) {
      document.getElementById('heaterState').textContent =
        (data.heater === true ? 'ON' : data.heater === false ? 'OFF' : '--');
    }
    // Control Phase (map from running and manualMode fields)
    if (document.getElementById('phaseStatus')) {
      let phase = '--';
      if (data.manualMode === true) phase = 'Manual';
      else if (data.running === true) phase = 'Program';
      else if (data.manualMode === false && data.running === false) phase = 'Idle';
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

    // --- Update Live PID Status Section ---
    // Update live PID values from the same data to avoid extra requests
    if (document.getElementById('liveKp')) {
      document.getElementById('liveKp').textContent = data.kp !== undefined ? data.kp.toFixed(3) : '--';
    }
    if (document.getElementById('liveKi')) {
      document.getElementById('liveKi').textContent = data.ki !== undefined ? data.ki.toFixed(6) : '--';
    }
    if (document.getElementById('liveKd')) {
      document.getElementById('liveKd').textContent = data.kd !== undefined ? data.kd.toFixed(3) : '--';
    }
    if (document.getElementById('liveSetpoint')) {
      document.getElementById('liveSetpoint').textContent = data.setpoint !== undefined ? data.setpoint.toFixed(1) : '--';
    }
    if (document.getElementById('liveOutput')) {
      document.getElementById('liveOutput').textContent = data.pid_output !== undefined ? data.pid_output.toFixed(3) : '--';
    }
    if (document.getElementById('liveCurrentTemp')) {
      document.getElementById('liveCurrentTemp').textContent = data.temperature !== undefined ? data.temperature.toFixed(1) : '--';
    }
    if (document.getElementById('liveRawTemp')) {
      document.getElementById('liveRawTemp').textContent = rawTemperature !== undefined ? rawTemperature.toFixed(1) : '--';
    }
    if (document.getElementById('liveSampleTime')) {
      document.getElementById('liveSampleTime').textContent = data.pid_sample_time || data.temp_sample_interval_ms || '--';
    }
    if (document.getElementById('liveWindowSize')) {
      document.getElementById('liveWindowSize').textContent = data.window_size_ms || '--';
    }
    if (document.getElementById('liveOnTime')) {
      document.getElementById('liveOnTime').textContent = data.on_time || '--';
    }
    if (document.getElementById('liveTempAlpha')) {
      document.getElementById('liveTempAlpha').textContent = data.temp_alpha || '--';
    }
    if (document.getElementById('liveTempInterval')) {
      document.getElementById('liveTempInterval').textContent = data.temp_sample_interval || '--';
    }
    if (document.getElementById('liveManualMode')) {
      document.getElementById('liveManualMode').textContent = data.manualMode !== undefined ? (data.manualMode ? 'Yes' : 'No') : '--';
    }
    if (document.getElementById('livePhase')) {
      let phase = '--';
      if (data.manualMode === true) phase = 'Manual';
      else if (data.running === true) phase = 'Program';
      else if (data.manualMode === false && data.running === false) phase = 'Idle';
      document.getElementById('livePhase').textContent = phase;
    }
    if (document.getElementById('liveHeater')) {
      document.getElementById('liveHeater').textContent = data.heater !== undefined ? (data.heater ? 'ON' : 'OFF') : '--';
    }
    if (document.getElementById('livePidP')) {
      document.getElementById('livePidP').textContent = data.pid_p !== undefined ? data.pid_p.toFixed(3) : '--';
    }
    if (document.getElementById('livePidI')) {
      document.getElementById('livePidI').textContent = data.pid_i !== undefined ? data.pid_i.toFixed(3) : '--';
    }
    if (document.getElementById('livePidD')) {
      document.getElementById('livePidD').textContent = data.pid_d !== undefined ? data.pid_d.toFixed(3) : '--';
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
  } finally {
    updateInProgress = false;
  }
}

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
  console.log('Initializing PID component chart...');
  const canvas = document.getElementById('pidComponentChart');
  if (!canvas) {
    console.error('PID component chart canvas not found!');
    return;
  }
  console.log('PID canvas element found:', canvas);
  
  const ctx = canvas.getContext('2d');
  if (!ctx) {
    console.error('Could not get 2D context from PID canvas!');
    return;
  }
  console.log('PID canvas context obtained:', ctx);
  
  try {
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
  console.log('PID component chart created successfully:', window.pidComponentChart);
  } catch (error) {
    console.error('Error creating PID component chart:', error);
  }
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
  // Reduced from 2000ms to 4000ms for better performance
  updateInterval = setInterval(updateStatus, 4000);
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

// Change log and chart annotations
let changeLog = [];
let chartAnnotations = [];

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
  console.log('Initializing temperature chart...');
  const canvas = document.getElementById('temperatureChart');
  if (!canvas) {
    console.error('Temperature chart canvas not found!');
    return;
  }
  console.log('Canvas element found:', canvas);
  
  const ctx = canvas.getContext('2d');
  if (!ctx) {
    console.error('Could not get 2D context from canvas!');
    return;
  }
  console.log('Canvas context obtained:', ctx);
  
  try {
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
        },
        annotation: {
          annotations: {}
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
  console.log('Temperature chart created successfully:', temperatureChart);
  } catch (error) {
    console.error('Error creating temperature chart:', error);
  }
}

// Update EWMA temperature parameters only
async function updateTempEWMA() {
  const tempAlpha = parseFloat(document.getElementById('tempAlpha').value);
  const tempUpdateInterval = parseInt(document.getElementById('tempUpdateInterval').value);

  // Validate EWMA parameters
  if (tempAlpha < 0.01 || tempAlpha > 0.5) {
    showMessage('Invalid alpha: must be between 0.01 and 0.5', 'error');
    return;
  }

  try {
    const url = `/api/pid_params?temp_alpha=${tempAlpha}&temp_interval=${tempUpdateInterval}`;
    console.log('Updating EWMA parameters:', { tempAlpha, tempUpdateInterval });
    
    const response = await fetchWithTimeout(url, 10000);
    const result = await response.json();
    console.log('EWMA update response:', result);
    
    if (response.ok) {
      showMessage('EWMA temperature parameters updated successfully', 'success');
      
      // Multiple refresh attempts to ensure backend has processed the update
      const refreshWithRetry = async (attempt = 1, maxAttempts = 3) => {
        console.log(`Refreshing status after EWMA update (attempt ${attempt}/${maxAttempts})...`);
        
        if (typeof updateStatus === 'function') {
          await updateStatus();
        }
        
        // Check if the values were actually updated
        const currentAlpha = document.getElementById('currentTempAlpha').textContent;
        const currentInterval = document.getElementById('currentTempInterval').textContent;
        
        const alphaMatch = currentAlpha.includes(tempAlpha.toString());
        const intervalMatch = currentInterval.includes(tempUpdateInterval.toString());
        
        if (!alphaMatch || !intervalMatch) {
          console.log(`EWMA values not yet updated (attempt ${attempt}): alpha=${alphaMatch}, interval=${intervalMatch}`);
          if (attempt < maxAttempts) {
            setTimeout(() => refreshWithRetry(attempt + 1, maxAttempts), 1500);
          } else {
            console.warn('EWMA values may not have updated after multiple retries');
          }
        } else {
          console.log('EWMA values successfully updated and confirmed');
        }
      };
      
      // Start refresh attempts after initial delay
      setTimeout(() => refreshWithRetry(), 2500);
    } else {
      // Handle validation errors from firmware
      console.error('EWMA validation errors:', result);
      if (result.errors) {
        showMessage('EWMA validation errors: ' + result.errors, 'error');
      } else {
        showMessage('Failed to update EWMA parameters', 'error');
      }
    }
  } catch (error) {
    console.error('Network error:', error);
    showMessage('Error updating EWMA parameters: ' + error.message, 'error');
  }
}

// Update PID parameters with memory-efficient GET requests (safer for ESP32)
async function updatePIDParams() {
  const kp = parseFloat(document.getElementById('kpInput').value);
  const ki = parseFloat(document.getElementById('kiInput').value);
  const kd = parseFloat(document.getElementById('kdInput').value);
  const tempAlpha = parseFloat(document.getElementById('tempAlpha').value);
  const tempUpdateInterval = parseInt(document.getElementById('tempUpdateInterval').value);

  // Validate parameters first
  if (isNaN(kp) || isNaN(ki) || isNaN(kd)) {
    showMessage('Invalid PID values. Please enter valid numbers.', 'error');
    return;
  }

  if (tempAlpha < 0.01 || tempAlpha > 0.5) {
    showMessage('Invalid alpha: must be between 0.01 and 0.5', 'error');
    return;
  }

  console.log('Updating PID params (profile-aware):', { kp, ki, kd, tempAlpha, tempUpdateInterval });

  try {
    showMessage('Updating PID profile... (1/3)', 'info');
    
    // First, update the PID profile for the current temperature range
    // This ensures the changes persist across auto-switching cycles
    await updatePIDProfileOnController('current', { kp, ki, kd, windowMs: 10000 });
    
    showMessage('Updating controller parameters... (2/3)', 'info');
    
    // Then update the current controller parameters for immediate effect and reset integral
    const pidUrl = `/api/pid_params?kp=${kp.toFixed(6)}&ki=${ki.toFixed(6)}&kd=${kd.toFixed(6)}&reset_integral=1`;
    console.log('PID update URL:', pidUrl);
    
    const pidResponse = await fetchWithTimeout(pidUrl, 8000);
    
    if (!pidResponse.ok) {
      throw new Error(`Current PID update failed: ${pidResponse.status}`);
    }
    
    showMessage('Updating temperature settings... (3/3)', 'info');
    
    // Finally update temperature averaging parameters
    const tempUrl = `/api/pid_params?temp_alpha=${tempAlpha.toFixed(3)}&temp_interval=${tempUpdateInterval}`;
    console.log('Temp update URL:', tempUrl);
    
    const tempResponse = await fetchWithTimeout(tempUrl, 8000);
    
    if (!tempResponse.ok) {
      throw new Error(`Temperature settings update failed: ${tempResponse.status}`);
    }
    
    // Success!
    showMessage('All parameters updated successfully! Profile changes will persist and integral component was reset.', 'success');
    
    // Log the parameter change
    logChange('Parameters Updated', {
      'Kp': kp.toFixed(6),
      'Ki': ki.toFixed(6), 
      'Kd': kd.toFixed(6),
      'Alpha': tempAlpha.toFixed(3),
      'Interval': tempUpdateInterval + 'ms'
    });
    
    // Update display values
    document.getElementById('currentKp').textContent = kp.toFixed(6);
    document.getElementById('currentKi').textContent = ki.toFixed(6);
    document.getElementById('currentKd').textContent = kd.toFixed(6);
    
    // Small delay before refreshing status to show the updated profile
    setTimeout(() => {
      if (typeof updateStatus === 'function') {
        updateStatus();
      }
    }, 2000);
    
  } catch (error) {
    console.error('Parameter update error:', error);
    showMessage('Error updating parameters: ' + error.message, 'error');
  }
}

// Load current PID parameters from controller
async function loadCurrentParams() {
  try {
    // Load PID parameters
    const pidResponse = await fetchWithTimeout('/api/pid', 8000);
    const pidData = await pidResponse.json();
    
    document.getElementById('kpInput').value = pidData.kp.toFixed(6);
    document.getElementById('kiInput').value = pidData.ki.toFixed(6);
    document.getElementById('kdInput').value = pidData.kd.toFixed(6);
    
    if (pidData.sample_time_ms !== undefined) {
      document.getElementById('sampleTimeInput').value = pidData.sample_time_ms.toString();
    }
    if (pidData.window_size_ms !== undefined) {
      document.getElementById('windowSizeInput').value = pidData.window_size_ms.toString();
    }
    
    // Load EWMA temperature parameters
    const ewmaResponse = await fetchWithTimeout('/api/pid_params', 8000);
    const ewmaData = await ewmaResponse.json();
    
    if (ewmaData.temp_alpha !== undefined) {
      document.getElementById('tempAlpha').value = ewmaData.temp_alpha.toFixed(4);
    }
    if (ewmaData.temp_sample_interval !== undefined) {
      document.getElementById('tempUpdateInterval').value = ewmaData.temp_sample_interval.toString();
    }
    
    // Update current PID display
    document.getElementById('currentKp').textContent = pidData.kp.toFixed(6);
    document.getElementById('currentKi').textContent = pidData.ki.toFixed(6);
    document.getElementById('currentKd').textContent = pidData.kd.toFixed(6);
    document.getElementById('currentSampleTime').textContent = (pidData.sample_time_ms || 1000) + ' ms';
    
    // Update current EWMA display
    if (ewmaData.temp_alpha !== undefined) {
      document.getElementById('currentTempAlpha').textContent = ewmaData.temp_alpha.toFixed(4);
    }
    if (ewmaData.temp_sample_interval !== undefined) {
      document.getElementById('currentTempInterval').textContent = ewmaData.temp_sample_interval + ' ms';
    }
    if (ewmaData.temp_sample_count_total !== undefined) {
      document.getElementById('currentTempTotalSamples').textContent = ewmaData.temp_sample_count_total.toString();
    }
    if (ewmaData.averaged_temperature !== undefined) {
      document.getElementById('currentSmoothedTemp').textContent = ewmaData.averaged_temperature.toFixed(2) + ' °C';
    }
    if (ewmaData.temp_last_raw !== undefined) {
      document.getElementById('currentLastRawTemp').textContent = ewmaData.temp_last_raw.toFixed(2) + ' °C';
    }
    
    showMessage('Current PID and EWMA parameters loaded', 'success');
    console.log('Loaded PID params:', pidData);
    console.log('Loaded EWMA params:', ewmaData);
  } catch (error) {
    showMessage('Error loading parameters: ' + error.message, 'error');
  }
}

// Reset PID parameters to safe defaults and reload profiles
async function resetPIDToDefaults() {
  if (!confirm('Reset PID parameters to defaults? This will:\n• Clear all profile customizations\n• Reset to factory temperature ranges\n• Restart PID controller with safe values\n\nContinue?')) {
    return;
  }
  
  try {
    showMessage('Resetting PID to defaults...', 'info');
    
    // Reset via API endpoint (this should trigger profile reset)
    const response = await fetchWithTimeout('/api/pid_profiles/reset', 8000);
    
    if (!response.ok) {
      // If reset endpoint doesn't exist, use manual reset
      throw new Error('Reset endpoint not available, using manual reset');
    }
    
    showMessage('PID reset successful! Reloading page...', 'success');
    
    // Reload the page to refresh all data
    setTimeout(() => {
      location.reload();
    }, 2000);
    
  } catch (error) {
    console.warn('API reset failed, attempting manual reset:', error);
    
    // Manual reset - set safe default values
    try {
      // Set Low Temperature Range defaults
      await updatePIDProfileOnController('low', { 
        kp: 0.5, 
        ki: 0.001, 
        kd: 2.0, 
        windowMs: 10000 
      });
      
      // Reload profiles and current params
      await loadPIDProfiles();
      await loadCurrentParams();
      
      showMessage('Manual reset completed successfully!', 'success');
      
    } catch (manualError) {
      showMessage('Reset failed: ' + manualError.message, 'error');
    }
  }
}

// Reset PID integral component to clear accumulated windup
async function resetIntegralComponent() {
  try {
    showMessage('Resetting integral component...', 'info');
    
    // Reset the integral component via API
    const response = await fetchWithTimeout('/api/pid_params?reset_integral=1', 8000);
    
    if (!response.ok) {
      throw new Error(`Failed to reset integral component: ${response.status}`);
    }
    
    showMessage('✅ Integral component reset successfully!', 'success');
    
    // Log the integral reset
    logChange('Integral Reset', { 'Action': 'Integral windup cleared' });
    
    // Update the current display to show the reset
    try {
      setTimeout(() => {
        if (typeof updateStatus === 'function') {
          updateStatus();
        }
      }, 1000);
    } catch (timeoutError) {
      console.warn('Non-critical timeout error:', timeoutError);
      // Continue execution - this is not a critical error
    }
    
  } catch (error) {
    console.error('Error resetting integral component:', error);
    const errorMsg = error.message.includes('timeout') ? 
      'Request timed out - please try again' : 
      'Error resetting integral component: ' + error.message;
    showMessage(errorMsg, 'error');
  }
}

// Validate EWMA parameters (simplified since EWMA has fewer constraints)
function validateEWMAParams() {
  const alpha = parseFloat(document.getElementById('tempAlpha').value);
  
  if (alpha < 0.01 || alpha > 0.5) {
    document.getElementById('tempAlpha').value = Math.max(0.01, Math.min(0.5, alpha));
    showMessage('Alpha constrained to valid range (0.01 - 0.5)', 'warning');
  }
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
document.addEventListener('DOMContentLoaded', async function() {
  console.log('DOM Content Loaded - starting PID tune page initialization');
  
  // Load PID profiles from device first
  const profilesLoaded = await loadPIDProfiles();
  if (!profilesLoaded) {
    console.error('Failed to load PID profiles from device - using empty array');
    showMessage('Failed to load PID profiles from device', 'error', 5000);
  } else {
    // Initialize range management after profiles are loaded
    console.log('PID profiles loaded successfully, initializing range management...');
    await initializeRangeManagement();
  }
  
  // Check if Chart.js is loaded
  if (typeof Chart === 'undefined') {
    console.error('Chart.js library not loaded! Charts will not work.');
    // Try to show an error message to the user
    const chartContainers = document.querySelectorAll('.chart-container');
    chartContainers.forEach(container => {
      container.innerHTML = '<div style="text-align: center; color: red; padding: 20px;"><h3>⚠️ Chart.js library failed to load</h3><p>Real-time graphs are unavailable. Check your internet connection.</p></div>';
    });
    return;
  }
  console.log('Chart.js version:', Chart.version);
  
  initChart();
  initPIDComponentChart();
  loadCurrentParams();
  
  // Add auto-tune method change handler
  document.getElementById('autoTuneMethod').addEventListener('change', function() {
    updateAutoTuneMethodDescription();
  });
  
  // Add temperature averaging validation handlers
  document.getElementById('tempAlpha').addEventListener('change', validateEWMAParams);
  
  // Initialize method description
  updateAutoTuneMethodDescription();
  
  // Start periodic status updates (slower when not testing)
  setInterval(() => {
    // Don't start new updates if there's already a faster update interval running
    // or if an update is already in progress to prevent request backup
    if (!updateInterval && !updateInProgress) {
      updateStatus();
    }
  }, 8000); // Increased from 5000ms to 8000ms for reduced load
  
  console.log('PID Controller Tuning page initialized');
});

// Additional initialization for range management
document.addEventListener('DOMContentLoaded', function() {
  // Wait a bit for other initializations to complete
  setTimeout(() => {
    console.log('Range management event handlers initialized (auto-detection via controller setpoint)');
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
// PID profiles loaded dynamically from the device
let temperaturePIDSuggestions = [];
let pidProfilesLoaded = false;

// Load PID profiles from the device
async function loadPIDProfiles() {
  try {
    const response = await fetchWithTimeout('/api/pid_profiles', 10000);
    if (!response.ok) throw new Error('Failed to fetch PID profiles');
    const data = await response.json();
    
    // Convert server profiles to the format expected by the UI
    temperaturePIDSuggestions = data.profiles.map(profile => ({
      key: profile.name.toLowerCase().replace(/[^a-z0-9]/g, ''), // Generate simple key
      min: profile.minTemp,
      max: profile.maxTemp,
      name: profile.name,
      kp: profile.kp,
      ki: profile.ki,
      kd: profile.kd,
      windowMs: profile.windowMs,
      description: profile.description
    }));
    
    pidProfilesLoaded = true;
    console.log('Loaded', temperaturePIDSuggestions.length, 'PID profiles from device');
    
    // Initialize range configuration status based on loaded profiles
    rangeConfigurationStatus = {};
    temperaturePIDSuggestions.forEach(profile => {
      rangeConfigurationStatus[profile.key] = {
        configured: true, // All server profiles are considered configured
        kp: profile.kp,
        ki: profile.ki,
        kd: profile.kd,
        windowMs: profile.windowMs
      };
    });
    
    return true;
  } catch (error) {
    console.error('Failed to load PID profiles:', error);
    // Fallback to empty array if loading fails
    temperaturePIDSuggestions = [];
    pidProfilesLoaded = false;
    return false;
  }
}

// Range configuration tracking - dynamically initialized when profiles are loaded
let rangeConfigurationStatus = {};

let currentRangeIndex = 0; // Start with first range (will be updated when profiles load)
let isRangeConfigurationMode = false;

// Detect temperature range from setpoint and highlight it
async function detectRangeFromSetpoint() {
  // Ensure profiles are loaded
  if (!pidProfilesLoaded || temperaturePIDSuggestions.length === 0) {
    showMessage('PID profiles not loaded yet. Please wait...', 'warning', 3000);
    return;
  }
  
  try {
    // Get setpoint from controller instead of removed UI element
    const statusResponse = await fetchWithTimeout('/api/status', 5000);
    const statusData = await statusResponse.json();
    const setpoint = statusData.setpoint || statusData.stageTemperature || 27.0;
    
    console.log(`Detecting range for controller setpoint: ${setpoint}°C`);

    // Find the appropriate temperature range
    const detectedRange = temperaturePIDSuggestions.find(range => 
      setpoint >= range.min && setpoint < range.max
    );

    if (!detectedRange) {
      const minTemp = Math.min(...temperaturePIDSuggestions.map(r => r.min));
      const maxTemp = Math.max(...temperaturePIDSuggestions.map(r => r.max));
      showMessage(`Temperature out of supported range (${minTemp}-${maxTemp}°C)`, 'error', 3000);
      return;
    }

    // Update the range selector to match detected range
    document.getElementById('tempRangeSelect').value = detectedRange.key;
  
    // Set current range index for navigation
    currentRangeIndex = temperaturePIDSuggestions.findIndex(range => range.key === detectedRange.key);
    
    // Load the range settings
    loadTemperatureRangeSettings();
    
    // Update the range status display
    updateRangeStatusDisplay();
    
    showMessage(`🎯 Auto-detected range: ${detectedRange.name}`, 'success', 3000);
    
    // Log the range detection
    logChange('Range Auto-Detected', {
      'Range': detectedRange.name,
      'Setpoint': setpoint.toFixed(1) + '°C',
      'Suggested Kp': detectedRange.kp,
      'Suggested Ki': detectedRange.ki,
      'Suggested Kd': detectedRange.kd
    });
  
  } catch (error) {
    console.error('Error detecting range from setpoint:', error);
    showMessage('Error getting controller setpoint for range detection', 'error', 3000);
  }
}

// Load temperature range settings into the UI form fields
function loadTemperatureRangeSettings() {
  if (currentRangeIndex < 0 || currentRangeIndex >= temperaturePIDSuggestions.length) {
    console.warn('Invalid range index:', currentRangeIndex);
    return;
  }
  
  const range = temperaturePIDSuggestions[currentRangeIndex];
  console.log('Loading range settings:', range);
  
  // Update PID parameter fields
  if (document.getElementById('pidKp')) {
    document.getElementById('pidKp').value = range.kp.toFixed(6);
  }
  if (document.getElementById('pidKi')) {
    document.getElementById('pidKi').value = range.ki.toFixed(6);
  }
  if (document.getElementById('pidKd')) {
    document.getElementById('pidKd').value = range.kd.toFixed(1);
  }
  
  // Update window size if there's a field for it
  if (document.getElementById('windowSize')) {
    document.getElementById('windowSize').value = range.windowMs;
  }
  
  console.log(`Loaded settings for ${range.name}: Kp=${range.kp}, Ki=${range.ki}, Kd=${range.kd}, Window=${range.windowMs}ms`);
}

// Update the range configuration status display
function updateRangeStatusDisplay() {
  const statusGrid = document.getElementById('rangeStatusGrid');
  if (!statusGrid) {
    console.error('rangeStatusGrid element not found');
    return;
  }
  
  console.log('Updating range status display with', temperaturePIDSuggestions.length, 'profiles');
  statusGrid.innerHTML = '';

  temperaturePIDSuggestions.forEach((range, index) => {
    const status = rangeConfigurationStatus[range.key];
    const isActive = index === currentRangeIndex;
    
    console.log(`Creating status item for ${range.name} (${range.key})`, status);
    
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
  
  console.log('Range status display updated, grid now has', statusGrid.children.length, 'items');
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
    // Get current setpoint from the controller to determine which profile to update
    const statusResponse = await fetchWithTimeout('/api/status', 5000);
    const statusData = await statusResponse.json();
    const currentTemp = statusData.setpoint || statusData.stageTemperature || 27.0;
    
    const response = await fetch(`/api/pid_profile/update_range?temp=${currentTemp}&kp=${params.kp}&ki=${params.ki}&kd=${params.kd}&windowMs=${params.windowMs}`, {
      method: 'GET'
    });
    
    if (!response.ok) {
      const errorData = await response.json().catch(() => ({}));
      throw new Error(errorData.error || 'Failed to update PID profile on controller');
    }
    
    const result = await response.json();
    console.log(`Updated profile for ${currentTemp}°C:`, result);
    showMessage(`Profile updated successfully for ${currentTemp}°C range`, 'success', 3000);
    
    // Reload profiles to refresh the display
    await loadPIDProfiles();
    updateRangeStatusDisplay();
    
  } catch (error) {
    console.error('Error updating PID profile:', error);
    showMessage(`Failed to save profile: ${error.message}`, 'error', 5000);
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
async function initializeRangeManagement() {
  console.log('initializeRangeManagement called with', temperaturePIDSuggestions.length, 'profiles');
  loadRangeConfigurationStatus();
  updateRangeStatusDisplay();
  console.log('Range status display updated');
  
  // Auto-detect range based on controller setpoint
  try {
    const statusResponse = await fetchWithTimeout('/api/status', 5000);
    const statusData = await statusResponse.json();
    
    // Use the actual setpoint from the controller - this determines which range should be active
    const actualSetpoint = statusData.setpoint || statusData.stageTemperature || 25.0;
    console.log(`Initialized with actual controller setpoint: ${actualSetpoint}°C`);
    await detectRangeFromSetpoint();
  } catch (error) {
    console.error('Error getting controller status for initialization:', error);
    // Fallback - just use the first available range
    if (temperaturePIDSuggestions.length > 0) {
      selectRange(0);
      console.log('Fallback: Using first available range');
    }
  }
}

// Apply temperature range settings to PID controls
function applyTemperatureRangeSettings() {
  const rangeSelect = document.getElementById('tempRangeSelector');
  const selectedRange = rangeSelect.value;
  
  if (!selectedRange) {
    showMessage('Please select a temperature range first', 'error', 3000);
    return;
  }
  
  // Find the corresponding range configuration
  const range = temperaturePIDSuggestions.find(r => r.key === selectedRange);
  if (!range) {
    showMessage('Invalid temperature range selected', 'error', 3000);
    return;
  }
  
  // Check if this range has saved settings
  const savedSettings = rangeConfigurationStatus[selectedRange];
  if (savedSettings && savedSettings.configured) {
    // Apply saved settings
    document.getElementById('kpInput').value = savedSettings.kp.toFixed(6);
    document.getElementById('kiInput').value = savedSettings.ki.toFixed(6);
    document.getElementById('kdInput').value = savedSettings.kd.toFixed(3);
    document.getElementById('windowSizeInput').value = savedSettings.windowMs;
    
    showMessage(`Applied saved settings for ${range.name}`, 'success', 3000);
  } else {
    // Apply default suggestions for this range
    document.getElementById('kpInput').value = range.kp.toFixed(6);
    document.getElementById('kiInput').value = range.ki.toFixed(6);
    document.getElementById('kdInput').value = range.kd.toFixed(3);
    document.getElementById('windowSizeInput').value = range.windowMs;
    
    showMessage(`Applied default suggestions for ${range.name}`, 'info', 3000);
  }
  
  // Update the current range index
  currentRangeIndex = temperaturePIDSuggestions.findIndex(r => r.key === selectedRange);
  updateRangeStatusDisplay();
}

// Start manual temperature test
async function startManualTest() {
  const targetTemp = parseFloat(document.getElementById('targetTempInput').value);
  
  if (isNaN(targetTemp) || targetTemp < 0 || targetTemp > 250) {
    showMessage('Please enter a valid target temperature (0-250°C)', 'error', 3000);
    return;
  }
  
  try {
    // Enable manual mode
    const manualModeResponse = await fetch('/api/manual_mode?on=1', {
      method: 'GET'
    });
    
    if (!manualModeResponse.ok) {
      throw new Error('Failed to enable manual mode');
    }
    
    // Set the temperature setpoint
    const tempResponse = await fetch(`/api/temperature?setpoint=${targetTemp}`, {
      method: 'GET'
    });
    
    if (!tempResponse.ok) {
      throw new Error('Failed to set temperature setpoint');
    }
    
    // Reset test start time for chart
    testStartTime = Date.now();
    
    // Enable status updates if not already running
    if (!updateInterval) {
      startStatusUpdates();
    }
    
    showMessage(`Manual test started - target: ${targetTemp}°C`, 'success', 3000);
    
    // Log the test start
    logChange('Test Started', {
      'Type': 'Manual PID Test',
      'Target': targetTemp.toFixed(1) + '°C'
    });
    
    // Update button states
    const startBtn = document.querySelector('button[onclick="startManualTest()"]');
    const stopBtn = document.querySelector('button[onclick="stopTest()"]');
    if (startBtn) startBtn.disabled = true;
    if (stopBtn) stopBtn.disabled = false;
    
  } catch (error) {
    console.error('Failed to start manual test:', error);
    showMessage('Failed to start manual test: ' + error.message, 'error', 5000);
  }
}

// Stop any running test
async function stopTest() {
  try {
    // Set setpoint to 0 to turn off heating
    await fetch('/api/temperature?setpoint=0', {
      method: 'GET'
    });
    
    // Disable manual mode
    const response = await fetch('/api/manual_mode?on=0', {
      method: 'GET'
    });
    
    if (!response.ok) {
      throw new Error('Failed to stop test');
    }
    
    showMessage('Test stopped', 'info', 3000);
    
    // Log the test stop
    logChange('Test Stopped', { 'Action': 'Manual mode disabled, setpoint set to 0°C' });
    
    // Update button states for both manual and quick test
    const startBtn = document.querySelector('button[onclick="startManualTest()"]');
    const quickStartBtn = document.querySelector('button[onclick="startQuickTest()"]');
    const stopBtn = document.querySelector('button[onclick="stopTest()"]');
    
    if (startBtn) startBtn.disabled = false;
    if (quickStartBtn) {
      quickStartBtn.innerHTML = '🚀 Start Test & Show Graphs';
      quickStartBtn.disabled = false;
      quickStartBtn.style.background = '';
    }
    if (stopBtn) stopBtn.disabled = true;
    
    // Clear any success messages
    const successMsg = document.querySelector('.success-message');
    if (successMsg) successMsg.remove();
    
  } catch (error) {
    console.error('Failed to stop test:', error);
    showMessage('Failed to stop test: ' + error.message, 'error', 5000);
  }
}

// Test beep function
async function testBeep() {
  try {
    const response = await fetch('/beep', {
      method: 'GET'
    });
    
    if (!response.ok) {
      throw new Error('Failed to trigger test beep');
    }
    
    showMessage('Test beep sent', 'info', 2000);
    
  } catch (error) {
    console.error('Failed to trigger test beep:', error);
    showMessage('Failed to trigger test beep: ' + error.message, 'error', 3000);
  }
}