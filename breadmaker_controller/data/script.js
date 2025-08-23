// ---- WiFi Signal Icon Update ----
function updateWifiIcon(rssi, connected) {
  const wifiSvg = document.getElementById('wifiSvg');
  if (!wifiSvg) return;
  // RSSI: 0 (no signal), -30 (excellent), -67 (good), -70 (okay), -80 (weak), -90 (unusable)
  let bars = 0;
  if (!connected || typeof rssi !== 'number' || rssi < -90) bars = 0;
  else if (rssi >= -60) bars = 4;
  else if (rssi >= -67) bars = 3;
  else if (rssi >= -75) bars = 2;
  else if (rssi >= -85) bars = 1;
  else bars = 0;
  // Set bar colors
  const colors = ['#888', '#ff9800', '#ffd600', '#4caf50'];
  // All bars grey by default
  wifiSvg.querySelector('#wifi-bar-1').setAttribute('fill', bars >= 1 ? colors[Math.min(bars-1,3)] : '#888');
  wifiSvg.querySelector('#wifi-bar-2').setAttribute('stroke', bars >= 2 ? colors[Math.min(bars-1,3)] : '#888');
  wifiSvg.querySelector('#wifi-bar-3').setAttribute('stroke', bars >= 3 ? colors[Math.min(bars-1,3)] : '#888');
  wifiSvg.querySelector('#wifi-bar-4').setAttribute('stroke', bars >= 4 ? colors[Math.min(bars-1,3)] : '#888');
  // Optionally, set opacity if not connected
  wifiSvg.style.opacity = connected ? '1' : '0.4';
}
// ---- Utility: Format Time (HH:MM or HH:MM:SS) ----
function formatTime(date, withSeconds = false) {
  if (!(date instanceof Date)) return '--';
  let h = date.getHours().toString().padStart(2, '0');
  let m = date.getMinutes().toString().padStart(2, '0');
  let s = date.getSeconds().toString().padStart(2, '0');
  return withSeconds ? `${h}:${m}:${s}` : `${h}:${m}`;
}

// ---- Utility Functions ----
function showStartAtStatus(msg) {
  let el = document.getElementById('startAtStatus');
  if (el) el.textContent = msg || '';
}

// ---- Utility: Format Duration ----
function formatDuration(seconds) {
  if (typeof seconds !== 'number' || isNaN(seconds)) return '--';
  seconds = Math.round(seconds);
  let h = Math.floor(seconds / 3600);
  let m = Math.floor((seconds % 3600) / 60);
  let s = seconds % 60;
  if (h > 0) {
    return h + 'h ' + (m < 10 ? '0' : '') + m + 'm';
  } else if (m > 0) {
    return m + 'm';
  } else {
    return s + 's';
  }
}

// ---- Utility: Format Date/Time ----
function formatDateTime(dt) {
  let d = (dt instanceof Date) ? dt : new Date(dt);
  return d.toLocaleString();
}

// ---- Utility: Format Date for Google Calendar ----
function toGCalDate(date) {
  // Returns date in YYYYMMDDTHHmmssZ format (UTC)
  const pad = n => n < 10 ? '0' + n : n;
  return date.getUTCFullYear().toString() +
    pad(date.getUTCMonth() + 1) +
    pad(date.getUTCDate()) + 'T' +
    pad(date.getUTCHours()) +
    pad(date.getUTCMinutes()) +
    pad(date.getUTCSeconds()) + 'Z';
}

// ---- Program Selection Management ----
function populateProgramSelect() {
  const programSelect = document.getElementById('programSelect');
  if (!programSelect || !window.cachedPrograms) return;

  // Clear existing options
  programSelect.innerHTML = '<option value="">Select a program...</option>';
  
  // Get current status to show running program
  const status = window.status;
  const isRunning = status?.running || status?.state === 'Running' || status?.state === 'Paused';
  const currentProgramId = status?.programId;
  
  // Add programs as options
  window.cachedPrograms.forEach(program => {
    if (program.valid !== false) { // Only show valid programs
      const option = document.createElement('option');
      option.value = program.id;
      
      // Show if this program is currently running
      if (isRunning && program.id === currentProgramId) {
        option.textContent = `${program.name} ● RUNNING`;
        option.style.background = '#2a5a2a';
        option.style.color = '#90ff90';
        option.style.fontWeight = 'bold';
      } else {
        option.textContent = program.name;
      }
      
      programSelect.appendChild(option);
    }
  });
  
  // Add change event listener with running program confirmation
  programSelect.onchange = function() {
    const selectedProgramId = parseInt(this.value);
    const status = window.status;
    const isRunning = status?.running || status?.state === 'Running' || status?.state === 'Paused';
    const currentProgramId = status?.programId;
    const currentProgramName = status?.program || 'Unknown';
    
    // If a program is running and user selects a different one, ask for confirmation
    if (isRunning && selectedProgramId !== currentProgramId && selectedProgramId >= 0) {
      const selectedProgram = window.cachedPrograms.find(p => p.id === selectedProgramId);
      const selectedProgramName = selectedProgram?.name || 'Unknown';
      
      const confirmed = confirm(
        `⚠️ Program Currently Running!\n\n` +
        `Current Program: ${currentProgramName}\n` +
        `Selected Program: ${selectedProgramName}\n\n` +
        `Changing programs will STOP the current program and lose all progress.\n\n` +
        `Do you want to continue?`
      );
      
      if (!confirmed) {
        // Revert selection to current running program
        this.value = String(currentProgramId);
        return;
      }
    }
    
    if (selectedProgramId >= 0) {
      // Find the program index by ID for populateStageDropdown (it expects array index, not ID)
      const programIdx = window.cachedPrograms.findIndex(p => p.id === selectedProgramId);
      if (programIdx >= 0) {
        populateStageDropdown(programIdx);
      }
    } else {
      // Hide stage selection if no program selected
      const startAtStageRow = document.getElementById('startAtStageRow');
      if (startAtStageRow) startAtStageRow.style.display = 'none';
    }
  };

  // Note: Auto-selection logic moved to updateStatusInternal() to run after status is loaded
}

// ---- Stage Dropdown Population ----
function populateStageDropdown(programIdx) {
  const stageSelect = document.getElementById('stageSelect');
  const startAtStageRow = document.getElementById('startAtStageRow');
  const combinedStartRow = document.getElementById('combinedStartRow');
  
  if (!stageSelect || !window.cachedPrograms) return;
  
  // Remember the current selected value
  const currentValue = stageSelect.value;
  
  // Clear existing options
  stageSelect.innerHTML = '<option value="">Start from beginning</option>';
  
  // Get the selected program
  const programs = Array.isArray(window.cachedPrograms) ? window.cachedPrograms : [];
  const program = programs[programIdx];
  if (!program || !program.customStages || !Array.isArray(program.customStages)) {
    // Hide the start at stage row if no custom stages
    if (startAtStageRow) startAtStageRow.style.display = 'none';
    if (combinedStartRow) combinedStartRow.style.display = 'none';
    return;
  }
  
  // Populate stages
  program.customStages.forEach((stage, idx) => {
    const option = document.createElement('option');
    option.value = idx;
    option.textContent = `${idx + 1}. ${stage.label}`;
    stageSelect.appendChild(option);
  });
  
  // Restore the selected value if it still exists
  if (currentValue !== undefined && currentValue !== "") {
    const stageIdx = parseInt(currentValue);
    if (stageIdx >= 0 && stageIdx < program.customStages.length) {
      stageSelect.value = currentValue;
    }
  }
  
  // Show the start at stage controls
  if (startAtStageRow) startAtStageRow.style.display = 'flex';
  
  // Show combined button only if there are stages available
  updateCombinedStartButton();
}

// ---- Update Combined Start Button Visibility ----
function updateCombinedStartButton() {
  const combinedStartRow = document.getElementById('combinedStartRow');
  const startAtInput = document.getElementById('startAt');
  const stageSelect = document.getElementById('stageSelect');
  
  if (!combinedStartRow || !startAtInput || !stageSelect) return;
  
  // Show combined button if both time is set and stage options are available
  const hasTime = startAtInput.value && startAtInput.value.trim() !== '';
  const hasStages = stageSelect.options.length > 1; // More than just "Start from beginning"
  
  if (hasTime && hasStages) {
    combinedStartRow.style.display = 'flex';
  } else {
    combinedStartRow.style.display = 'none';
  }
}

// ---- Handle Stage Click ----
function handleStageClick(stageIdx) {
  if (confirm(`Start the program at stage ${stageIdx + 1}? This will skip the previous stages.`)) {
    fetch(`/start_at_stage?stage=${stageIdx}`)
      .then(r => r.json())
      .then(response => {
        if (response.error) {
          alert(`Error: ${response.error}\n${response.message || ''}`);
        } else {
          console.log(`Started at stage ${stageIdx}: ${response.stageName || ''}`);
          window.updateStatus();
        }
      })
      .catch(err => {
        console.error('Failed to start at stage:', err);
        alert('Failed to start at stage. Please try again.');
      });
  }
}

// ---- Utility: Render Icons (update visual state only) ----
function renderIcons(s) {
  // Don't use common.js renderIcons as it replaces HTML and removes onclick handlers
  // Instead, just update the visual state of existing icons
  if (!s) return;
  
  // Update device icon colors based on state
  const devices = ['motor', 'heater', 'light', 'buzzer'];
  devices.forEach(device => {
    const icon = document.getElementById(device + 'Icon');
    const statusEl = document.getElementById(device);
    
    if (icon) {
      // Update icon color based on device state
      const isOn = s[device];
      const svg = icon.querySelector('svg');
      if (svg) {
        const paths = svg.querySelectorAll('path, circle, rect, ellipse, polygon');
        paths.forEach(path => {
          if (isOn) {
            path.style.fill = device === 'heater' ? '#ff6b35' : '#4caf50';
          } else {
            path.style.fill = '#888';
          }
        });
      }
    }
    
    // Status text is already updated by updateStatusInternal
  });
}

// ---- Stage Progress Bar ----
const STAGE_ORDER = [
  { key: "autolyse", label: "Autolyse" },
  { key: "mix", label: "Mix" },
  { key: "bulk", label: "Bulk" },
  { key: "knockDown", label: "Knock" },
  { key: "rise", label: "Rise" },
  { key: "bake1", label: "B1" },
  { key: "bake2", label: "B2" },
  { key: "cool", label: "Cool" }
];
function updateStageProgress(currentStage, statusData) {
  const cont = document.getElementById('stageProgress');
  if (!cont) return;
  cont.innerHTML = '';
  
  // Get current program and its stages
  let program = null;
  if (statusData && statusData.program && window.cachedPrograms) {
    const programs = Array.isArray(window.cachedPrograms) ? window.cachedPrograms : [];
    program = programs.find(p => p.name === statusData.program);
  }
  
  // If no custom stages available, use the legacy hardcoded stages
  if (!program || !program.customStages || !Array.isArray(program.customStages)) {
    let activeIdx = STAGE_ORDER.findIndex(st => st.key === currentStage);
    let isIdle = activeIdx === -1 || currentStage === "Idle";
    for (let i = 0; i < STAGE_ORDER.length; ++i) {
      const { label } = STAGE_ORDER[i];
      const div = document.createElement('div');
      div.className = 'stage-step';
      div.textContent = label;
      if (isIdle) {
        // All grey/inactive if idle
      } else if (i < activeIdx) {
        div.classList.add('done');
      } else if (i === activeIdx) {
        div.classList.add('active');
      }
      cont.appendChild(div);
    }
    return;
  }
  
  // Use custom stages from the current program
  const customStages = program.customStages;
  let activeIdx = -1;
  let isIdle = currentStage === "Idle" || !statusData.running;
  // Use stageIdx if available, else match by label
  if (!isIdle) {
    if (typeof statusData.stageIdx === 'number') {
      activeIdx = statusData.stageIdx;
    } else {
      activeIdx = customStages.findIndex(stage => stage.label === currentStage);
    }
  }
  // Use actualStageStartTimes, actualStageEndTimes, and stageStartTimes for progress
  const actualStarts = Array.isArray(statusData.actualStageStartTimes) ? statusData.actualStageStartTimes : [];
  const actualEnds = Array.isArray(statusData.actualStageEndTimes) ? statusData.actualStageEndTimes : [];
  const estStarts = Array.isArray(statusData.stageStartTimes) ? statusData.stageStartTimes : [];
  for (let i = 0; i < customStages.length; ++i) {
    const stage = customStages[i];
    const div = document.createElement('div');
    div.className = 'stage-step';
    div.textContent = stage.label;
    // Tooltip: show timing info if available
    let tip = '';
    if (actualStarts[i] && actualStarts[i] > 0) {
      tip += 'Started: ' + formatDateTime(actualStarts[i] * 1000);
    } else if (estStarts[i] && estStarts[i] > 0) {
      tip += 'Est. start: ' + formatDateTime(estStarts[i] * 1000);
    }
    if (tip) div.title = tip;
    // Make stages clickable for direct stage skipping
    if (!isIdle && statusData.running) {
      div.style.cursor = 'pointer';
      div.title = (div.title ? div.title + '\n' : '') + `Click to skip to ${stage.label}`;
      div.onclick = function() {
        handleStageClick(i);
      };
    }
    // Progress coloring
    if (isIdle) {
      // All grey/inactive if idle
    } else if (i < activeIdx) {
      div.classList.add('done');
    } else if (i === activeIdx) {
      div.classList.add('active');
    }
    // Add elapsed time for this stage if available
    if (actualStarts[i] && (i === activeIdx || i < activeIdx)) {
      if (i === activeIdx && typeof statusData.timeLeft === 'number' && statusData.running) {
        // Show time left for the active stage - use adjustedTimeLeft for fermentation stages
        const span = document.createElement('span');
        span.className = 'stage-timeleft';
        
        // For fermentation stages, show the real clock time (adjustedTimeLeft)
        const isCurrentStageFermentation = customStages[i] && customStages[i].isFermentation;
        const timeToShow = isCurrentStageFermentation && typeof statusData.adjustedTimeLeft === 'number' 
                          ? statusData.adjustedTimeLeft 
                          : statusData.timeLeft;
        
        span.textContent = ' (' + formatDuration(timeToShow) + ' left)';
        div.appendChild(span);
      } else {
        // For completed stages, show elapsed time
        let elapsed = 0;
        
        // First try using actual end and start times if both are available
        if (actualEnds[i] && actualStarts[i] && actualEnds[i] > 0 && actualStarts[i] > 0) {
          elapsed = actualEnds[i] - actualStarts[i];
          if (console && console.log) console.log(`Stage ${i}: Using actual end time - ${elapsed} seconds`);
        }
        // Fallback: try using next stage start time minus current stage start time
        else if (i < customStages.length - 1 && actualStarts[i+1] && actualStarts[i] && actualStarts[i+1] > 0 && actualStarts[i] > 0) {
          elapsed = actualStarts[i+1] - actualStarts[i];
          if (console && console.log) console.log(`Stage ${i}: Using next start time - ${elapsed} seconds`);
        } 
        // Fallback: estimate based on program start and stage durations
        else if (actualStarts[i] && actualStarts[0] && statusData.running) {
          // If we have start times but not consecutive ones, estimate based on stage durations
          let estimatedStageEnd = actualStarts[0]; // Program start
          
          // Add up durations of all stages up to and including this one
          for (let j = 0; j <= i; j++) {
            const stageDuration = customStages[j].min * 60;
            
            // Apply fermentation factor if this is a fermentation stage
            if (customStages[j].isFermentation && typeof statusData.fermentationFactor === 'number' && statusData.fermentationFactor > 0) {
              estimatedStageEnd += stageDuration * statusData.fermentationFactor;
            } else {
              estimatedStageEnd += stageDuration;
            }
          }
          
          // Calculate this stage's duration
          let stageDuration = customStages[i].min * 60;
          if (customStages[i].isFermentation && typeof statusData.fermentationFactor === 'number' && statusData.fermentationFactor > 0) {
            elapsed = stageDuration * statusData.fermentationFactor;
          } else {
            elapsed = stageDuration;
          }
        } else {
          // Fallback: just use the planned duration (possibly adjusted for fermentation)
          elapsed = customStages[i].min * 60;
          if (customStages[i].isFermentation && typeof statusData.fermentationFactor === 'number' && statusData.fermentationFactor > 0) {
            elapsed *= statusData.fermentationFactor;
          }
        }
        
        if (elapsed > 0) {
          const span = document.createElement('span');
          span.className = 'stage-elapsed';
          span.textContent = ' (' + formatDuration(elapsed) + ')';
          div.appendChild(span);
        }
      }
    } else if (!isIdle && i > activeIdx) {
      // Show planned duration for future stages using firmware predictions
      const stage = customStages[i];
      let plannedDuration = stage.min * 60; // Default to raw duration
      
      // Use firmware's predictedStageEndTimes if available (already fermentation-adjusted)
      if (Array.isArray(statusData.predictedStageEndTimes) && statusData.predictedStageEndTimes[i] && statusData.predictedStageEndTimes[i-1]) {
        plannedDuration = statusData.predictedStageEndTimes[i] - statusData.predictedStageEndTimes[i-1];
      } else if (Array.isArray(statusData.predictedStageEndTimes) && statusData.predictedStageEndTimes[i] && statusData.programStart && i === 0) {
        // For first stage, use programStart as reference
        plannedDuration = statusData.predictedStageEndTimes[i] - statusData.programStart;
      }
      
      if (plannedDuration > 0) {
        const span = document.createElement('span');
        span.className = 'stage-planned';
        span.textContent = ' (' + formatDuration(plannedDuration) + ')';
        div.appendChild(span);
      }
    }
    cont.appendChild(div);
  }
}

// ---- Program Dropdown ----
let lastProgramList = [];
function populateProgramDropdown(s) {
  let select = document.getElementById('progSelect');
  // Use window.cachedPrograms for program list
  let programs = Array.isArray(window.cachedPrograms) ? window.cachedPrograms : [];
  if (select) {
    // Always clear and repopulate the dropdown to avoid stale options
    const prevValue = select.value;
    select.innerHTML = '';
    programs.forEach((p, idx) => {
      let opt = document.createElement('option');
      opt.value = (typeof p.id === 'number') ? p.id : idx;
      opt.textContent = p.name || `Program ${idx+1}`;
      select.appendChild(opt);
    });
    // Restore previous selection if possible
    if (prevValue && select.querySelector(`option[value="${prevValue}"]`)) {
      select.value = prevValue;
    }
    // Attach change listener only once
    if (!select.hasChangeListener) {
      select.onchange = function () {
        const selectedId = this.value;
        fetch('/select?idx=' + encodeURIComponent(selectedId))
          .then(r => {
            if (r.ok) {
              return fetch('/status').then(r => r.json());
            }
            throw new Error('Selection failed');
          })
          .then(s => {
            window.updateStatus(s);
            // Find program index by id for stage dropdown
            const idx = programs.findIndex(p => String(p.id) === String(selectedId));
            populateStageDropdown(idx);
            updateCombinedStartButton();
          })
          .catch(err => console.error('Program selection failed:', err));
      };
      select.hasChangeListener = true;
    }
    // Update selected option using programId from status if available
    let correctId = null;
    if (s && typeof s.programId === 'number') {
      correctId = s.programId;
    } else if (s && typeof s.program === 'string') {
      // Find by name fallback
      const found = programs.find(p => p.name === s.program);
      if (found) correctId = found.id;
    }
    if (correctId !== null && select.value !== String(correctId)) {
      select.value = String(correctId);
      // Find program index by id for stage dropdown
      const idx = programs.findIndex(p => String(p.id) === String(correctId));
      populateStageDropdown(idx);
    }
  }
  let curProg = document.getElementById('currentProg');
  if (curProg) curProg.textContent = (s && s.program) || "";
}

// Helper to compare arrays
function arraysEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) return false;
  }
  return true;
}

// ---- Update Status ----
let tempHistory = [];
let lastStatus = null;
let lastStatusTime = 0;
let countdownInterval = null;

function updateStatusInternal(s) {
  // Update WiFi icon if present
  if (s && typeof s.wifiRssi !== 'undefined') {
    updateWifiIcon(s.wifiRssi, s.wifiConnected !== false);
  }
  // If no status object provided, fetch it from server
  if (!s || typeof s !== 'object') {
    fetch('/status')
      .then(r => r.json())
      .then(data => updateStatusInternal(data))
      .catch(err => console.error('Failed to fetch status:', err));
    return;
  }
  
  window.lastStatusData = s; // Store for later use when programs are loaded
  lastStatus = s;
  lastStatusTime = Date.now();
  // Start or reset the countdown interval
  if (countdownInterval) clearInterval(countdownInterval);
  countdownInterval = setInterval(updateCountdownDisplay, 1000);
  updateCountdownDisplay();
  // Instantly update Program Ready At
  updateProgramReadyAt(s);
  // ---- Whole program "ready at" time ----
  // Also update after fetching programs (for first load or if cache not ready)
  if (typeof s.program === "string" && s.program.length > 0 && !window.cachedPrograms) {
    fetchProgramsOnce(() => updateProgramReadyAt(s));
  }

  // ---- Program Dropdown & Progress ----
  populateProgramDropdown(s);
  // Update stage dropdown based on current program
  if (s && s.program && window.cachedPrograms) {
    const programs = Array.isArray(window.cachedPrograms) ? window.cachedPrograms : [];
    const programIdx = programs.findIndex(p => p.name === s.program);
    if (programIdx >= 0) {
      populateStageDropdown(programIdx);
    }
  }
  
  // ---- Stage Progress Update ----
  // Ensure programs are loaded before updating stage progress
  if (window.cachedPrograms) {
    updateStageProgress(s.stage, s);
  } else if (typeof s.program === "string" && s.program.length > 0) {
    fetchProgramsOnce(() => updateStageProgress(s.stage, s));
  } else {
    updateStageProgress(s.stage, s); // Still update even without program
  }
  
  // ---- Plan Summary (stages table) ----
  // Ensure programs are loaded before showing plan summary for fermentation calculations
  if (window.cachedPrograms) {
    showPlanSummary(s);
  } else if (typeof s.program === "string" && s.program.length > 0) {
    fetchProgramsOnce(() => showPlanSummary(s));
  } else {
    showPlanSummary(s); // Still show even without program for "No program selected" message
  }
  
  // ---- Manual Mode UI ----
  updateManualModeToggle(s);

  // Update manual temperature input field
  const manualTempInput = document.getElementById('manualTempInput');
  if (manualTempInput && s && s.manualMode && typeof s.setpoint === 'number' && s.setpoint > 0) {
    if (!manualTempInput.value || parseFloat(manualTempInput.value) !== s.setpoint) {
      manualTempInput.value = s.setpoint.toFixed(0);
    }
  }

  // ---- Program Progress Bar ----
  renderProgramProgressBar(s);
  renderProgressBarKey(s);

  let btnPauseResume = document.getElementById('btnPauseResume');
  if (s.stage !== 'Idle' && s.running) {
    btnPauseResume.style.display = '';
    btnPauseResume.textContent = 'Pause';
    btnPauseResume.onclick = () => fetch('/pause').then(r => r.json()).then(window.updateStatus);
  } else if (s.stage !== 'Idle' && !s.running) {
    btnPauseResume.style.display = '';
    btnPauseResume.textContent = 'Resume';
    btnPauseResume.onclick = () => fetch('/resume').then(r => r.json()).then(window.updateStatus);
  } else {
    btnPauseResume.style.display = 'none';
  }

  // ---- Status labels ----
  if (document.getElementById('stage')) {
    let stageText = 'Idle';
    if (s && s.stage && s.stage !== 'Idle' && s.running) {
      // Show custom stage label if customStages present and running
      if (s.programs && s.program && s.programs[s.program] && Array.isArray(s.programs[s.program].customStages)) {
        const prog = s.programs[s.program];
        // Try to find current stage index by s.stageIdx, else by matching s.stage to label (case-insensitive)
        let idx = (typeof s.stageIdx === 'number') ? s.stageIdx : -1;
        if (idx < 0 && typeof s.stage === 'string') {
          idx = prog.customStages.findIndex(st => (st.label||'').toLowerCase() === s.stage.toLowerCase());
        }
        if (idx >= 0 && prog.customStages[idx] && prog.customStages[idx].label) {
          stageText = prog.customStages[idx].label;
        } else if (typeof s.stage === 'string') {
          stageText = s.stage;
        }
      } else if (typeof s.stage === 'string') {
        stageText = s.stage;
      }
    }
    document.getElementById('stage').textContent = stageText || 'Idle';
  }
  if (document.getElementById('temp')) document.getElementById('temp').innerHTML = (typeof s.temp === 'number' ? s.temp.toFixed(1) : '--') + '&deg;C';
  if (document.getElementById('setTemp')) {
    let setTempText = '';
    if (typeof s.setpoint === 'number' && s.setpoint > 0) {
      setTempText = 'Set: ' + s.setpoint.toFixed(1) + '&deg;C';
      if (s.manualMode) {
        setTempText += ' (Manual)';
      }
    }
    document.getElementById('setTemp').innerHTML = setTempText;
  }

  // ---- Fermentation Information ----
  updateFermentationInfo(s);

  if (document.getElementById('heater')) document.getElementById('heater').textContent = s.heater > 0 ? 'ON' : 'OFF';
  if (document.getElementById('motor')) document.getElementById('motor').textContent = s.motor ? 'ON' : 'OFF';
  if (document.getElementById('light')) document.getElementById('light').textContent = s.light ? 'ON' : 'OFF';
  if (document.getElementById('buzzer')) document.getElementById('buzzer').textContent = s.buzzer ? 'ON' : 'OFF';

  // ---- Track temperature history for chart ----
  const now = Date.now();
  if (typeof s.temp === 'number' && typeof s.setpoint === 'number') {
    tempHistory.push({ time: now, temp: s.temp, setTemp: s.setpoint });
    // Keep only last 5 minutes
    const cutoff = now - 5 * 60 * 1000;
    tempHistory = tempHistory.filter(pt => pt.time >= cutoff);
  }

  // ---- Icon states/animation ----
  renderIcons(s);

  // ---- Startup Delay Handling ----
  updateStartupDelayUI(s);

  // ---- Program Auto-Selection (runs after status is loaded) ----
  autoSelectProgram(s);
}

// ---- Program Auto-Selection Function ----
function autoSelectProgram(s) {
  // Only run if programs are loaded and we have a program selector
  if (!window.cachedPrograms || !s) return;
  
  const programSelect = document.getElementById('programSelect');
  if (!programSelect) return;
  
  // Refresh the program dropdown to show running status
  const currentValue = programSelect.value;
  populateProgramSelect();
  
  // Only auto-select if no program is currently selected (empty value or default option)
  // This prevents overriding user's manual selection when they're trying to select a different program
  if (currentValue && currentValue !== "") {
    programSelect.value = currentValue; // Restore previous selection
    console.log('[AUTO-SELECT] Preserving user selection:', currentValue);
    return;
  }
  
  let correctId = null;
  
  // Use programId from status if available (prioritize this over program name)
  if (s && typeof s.programId === 'number' && s.programId >= 0) {
    // Verify this program ID exists in our cached programs
    const found = window.cachedPrograms.find(p => p.id === s.programId);
    if (found) {
      correctId = s.programId;
      console.log('[AUTO-SELECT] Using programId:', correctId, '(' + found.name + ')');
    }
  } else if (s && typeof s.program === 'string' && s.program !== "" && s.program !== "None" && s.program !== "Unknown") {
    // Find by name fallback (only if program name is valid)
    const found = window.cachedPrograms.find(p => p.name === s.program);
    if (found) {
      correctId = found.id;
      console.log('[AUTO-SELECT] Using program name:', s.program, '-> ID:', correctId);
    }
  }
  
  // Set the selected value and populate stage dropdown
  if (correctId !== null && String(programSelect.value) !== String(correctId)) {
    console.log('[AUTO-SELECT] Setting program selector to:', correctId);
    programSelect.value = String(correctId);
    // Find the program index by ID for populateStageDropdown (it expects array index, not ID)
    const programIdx = window.cachedPrograms.findIndex(p => p.id === correctId);
    if (programIdx >= 0) {
      populateStageDropdown(programIdx);
    }
  }
}

// ---- Startup Delay UI Handling ----
function updateStartupDelayUI(s) {
  const messageEl = document.getElementById('startupDelayMessage');
  const btnStart = document.getElementById('btnStart');
  
  if (!s || typeof s.startupDelayComplete !== 'boolean') {
    // No startup delay info available
    if (messageEl) messageEl.style.display = 'none';
    if (btnStart) btnStart.disabled = false;
    return;
  }
  
  if (!s.startupDelayComplete) {
    // Startup delay is active
    if (messageEl) {
      let message = 'Temperature sensor stabilizing...';
      if (typeof s.startupDelayRemainingMs === 'number' && s.startupDelayRemainingMs > 0) {
        const remainingSec = Math.ceil(s.startupDelayRemainingMs / 1000);
        message += ` (${remainingSec}s remaining)`;
      }
      messageEl.textContent = message;
      messageEl.style.display = 'block';
    }
    if (btnStart) {
      btnStart.disabled = true;
      btnStart.title = 'Please wait for temperature sensor to stabilize';
    }
  } else {
    // Startup delay is complete
    if (messageEl) messageEl.style.display = 'none';
    if (btnStart) {
      btnStart.disabled = false;
      btnStart.title = '';
    }
  }
}

// ---- Fermentation Information ----
function updateFermentationInfo(s) {
  const fermentInfoEl = document.getElementById('fermentationInfo');
  const fermentFactorEl = document.getElementById('fermentFactor');
  const predictedCompleteEl = document.getElementById('predictedComplete');
  // Remove initial fermentation temperature from display
  if (!fermentInfoEl) return;
  fermentInfoEl.style.display = 'block';
  // Fermentation factor
  if (fermentFactorEl) fermentFactorEl.textContent = (typeof s.fermentationFactor === 'number') ? s.fermentationFactor.toFixed(2) : '--';
  // Remove predicted complete from UI
  if (predictedCompleteEl) predictedCompleteEl.textContent = '';
  // Do not display initial fermentation temperature
  // If you want to remove the element from the DOM, you can do so here:
  const initialTempEl = document.getElementById('initialTemp');
  if (initialTempEl) initialTempEl.style.display = 'none';
}

// ---- Program Ready At (Custom Stages Only) ----
function updateProgramReadyAt(s) {
  const el = document.getElementById('programReadyAt');
  if (!el) return;
  // Always show a completion time if a program is selected, even if idle (showing 'if started now')
  let showIfStartedNow = false;
  let finishTs = 0;
  let label = 'Program ready at:';
  // If no status or no program, clear
  if (!s || typeof s.program !== 'string' || !s.program.length) {
    el.textContent = '';
    return;
  }
  // Use cached programs if available for fallback
  const data = window.cachedPrograms;
  const progArr = Array.isArray(data) ? data : (data && data.programs ? data.programs : []);
  let prog = null;
  if (Array.isArray(progArr)) {
    prog = progArr.find(p => p.name === s.program);
  } else if (progArr && typeof progArr === 'object') {
    prog = progArr[s.program];
  }
  if (!prog || !prog.customStages || !Array.isArray(prog.customStages) || !prog.customStages.length) {
    el.textContent = '';
    return;
  }
  let totalMin = 0;
  // Calculate total duration considering fermentation factor
  prog.customStages.forEach(st => {
    let duration = st.min || 0;
    if (st.isFermentation && typeof s.fermentationFactor === 'number' && s.fermentationFactor > 0) {
      duration *= s.fermentationFactor;
    }
    totalMin += duration;
  });
  
  // If running or scheduled, show actual finish time
  if (s.running || (s.scheduledStart && s.scheduledStart > Math.floor(Date.now() / 1000))) {
    // Use firmware's calculated programReadyAt if available
    if (typeof s.programReadyAt === 'number' && s.programReadyAt > 0) {
      finishTs = s.programReadyAt * 1000;
      label = 'Program ready at:';
    } else if (typeof s.predictedProgramEnd === 'number' && s.predictedProgramEnd > 0) {
      finishTs = s.predictedProgramEnd * 1000;
      label = 'Program ready at:';
    }
  } else {
    // Not running - use firmware's predictedProgramEnd for "if started now"
    if (typeof s.predictedProgramEnd === 'number' && s.predictedProgramEnd > 0) {
      finishTs = s.predictedProgramEnd * 1000;
      showIfStartedNow = true;
      label = 'If started now, ready at:';
    }
  }
  // If the program ready time is in the past, clear the display
  if (finishTs <= Date.now()) {
    el.textContent = '';
    return;
  }
  el.textContent = label + ' ' + formatDateTime(finishTs);
}

function updateCountdownDisplay() {
  if (!lastStatus) return;
  let s = lastStatus;
  let now = Date.now();
  let stageReadyAt = (typeof s.stageReadyAt === 'number') ? s.stageReadyAt : 0;
  let scheduledStart = (typeof s.scheduledStart === 'number') ? s.scheduledStart : 0;
  let programReadyAt = (typeof s.programReadyAt === 'number') ? s.programReadyAt : 0;
  let timeLeft = 0;

  // If scheduledStart is in the future, show countdown to scheduled start
  if (scheduledStart > Math.floor(now/1000)) {
    let schedLeft = Math.max(0, Math.round(scheduledStart - (now/1000)));
    document.getElementById('timeLeft').textContent = formatDuration(schedLeft);
    document.getElementById('stageReadyAt').textContent = "Scheduled to start at: " + formatDateTime(scheduledStart * 1000);
    
    // Show cancel button when there's a scheduled start
    const cancelRow = document.getElementById('cancelScheduledStartRow');
    if (cancelRow) cancelRow.style.display = 'block';
    
    // Also show program ready at if available
    if (programReadyAt > 0) {
      const progReadyTs = programReadyAt * 1000;
      let el = document.getElementById('programReadyAt');
      if (el) el.textContent = 'Program ready at: ' + formatDateTime(progReadyTs);
    }
    return;
  }

  // Hide cancel button when no scheduled start
  const cancelRow = document.getElementById('cancelScheduledStartRow');
  if (cancelRow) cancelRow.style.display = 'none';

  // FIXED: Use proper stage time and program time fields from backend
  if (s.running && typeof s.stageTimeLeft === 'number' && s.stageTimeLeft > 0) {
    // Use actual stage time remaining (in minutes from backend, convert to seconds)
    timeLeft = s.stageTimeLeft * 60;
  } else if (s.running && typeof s.programTimeLeft === 'number' && s.programTimeLeft > 0) {
    // Fallback to total program time if stage time not available
    timeLeft = s.programTimeLeft * 60;
  } else if (stageReadyAt > 0) {
    // Fallback to timestamp calculation, but only if stageReadyAt is a valid future time
    const nowSeconds = Date.now() / 1000;
    if (stageReadyAt > nowSeconds) {
      timeLeft = Math.max(0, Math.round(stageReadyAt - nowSeconds));
    }
  } else if (typeof s.timeLeft === 'number') {
    // Further fallback if stageReadyAt missing - use adjustedTimeLeft for fermentation stages
    let elapsed = Math.floor((now - lastStatusTime) / 1000);
    
    // For fermentation stages, use adjustedTimeLeft (real clock time)
    let isCurrentStageFermentation = false;
    if (s.stage && window.cachedPrograms) {
      // Find current program to get its customStages
      let currentProgram = null;
      if (typeof s.programId === 'number' && s.programId >= 0) {
        currentProgram = window.cachedPrograms.find(p => p.id === s.programId);
      } else if (typeof s.program === 'string' && s.program !== "" && s.program !== "None") {
        currentProgram = window.cachedPrograms.find(p => p.name === s.program);
      }
      
      if (currentProgram && currentProgram.customStages) {
        isCurrentStageFermentation = currentProgram.customStages.find(stage => 
          stage.label === s.stage && stage.isFermentation);
      }
    }
    
    if (isCurrentStageFermentation && typeof s.adjustedTimeLeft === 'number') {
      timeLeft = Math.max(0, s.adjustedTimeLeft - elapsed);
    } else {
      timeLeft = Math.max(0, s.timeLeft - elapsed);
    }
  }

  // UI logic fix: Only show "stage ready at" if current stage is active and stageReadyAt is valid future time
  let showStageReadyAt = false;
  let stageReadyAtText = '';
  const nowSeconds = Date.now() / 1000; // Current time in seconds
  if (s.stage !== "Idle" && s.running && stageReadyAt > nowSeconds && timeLeft > 0) {
    showStageReadyAt = true;
    const stageReadyTs = stageReadyAt * 1000;
    stageReadyAtText = "Stage ready at: " + formatDateTime(stageReadyTs);
  }

  if (s.stage === "Idle" || !s.running || timeLeft <= 0) {
    document.getElementById('timeLeft').textContent = "--";
    document.getElementById('stageReadyAt').textContent = "";
    // Don't clear programReadyAt here - let updateProgramReadyAt() handle it
    // This was causing the "If started now" text to flash every second
  } else {
    document.getElementById('timeLeft').textContent = formatDuration(timeLeft);
    document.getElementById('stageReadyAt').textContent = showStageReadyAt ? stageReadyAtText : "--";
    // Show program ready at if available
    if (programReadyAt > 0) {
      const progReadyTs = programReadyAt * 1000;
      let el = document.getElementById('programReadyAt');
      if (el) el.textContent = 'Program ready at: ' + formatDateTime(progReadyTs);
    }
  }
}

// ---- Render Mix Pattern Graph ----
function renderMixPatternGraph(mixPattern) {
  if (!Array.isArray(mixPattern) || !mixPattern.length) return '';
  // Support both new (mixSec/waitSec) and old (action/durationSec) formats
  let segments = [];
  // Detect format: if first entry has mixSec or waitSec, use new format
  if (mixPattern[0] && (typeof mixPattern[0].mixSec === 'number' || typeof mixPattern[0].waitSec === 'number')) {
    // New format: expand each entry into ON/OFF segments
    mixPattern.forEach(mp => {
      if (mp.mixSec && mp.mixSec > 0) {
        segments.push({ action: 'mix', durationSec: mp.mixSec });
      }
      if (mp.waitSec && mp.waitSec > 0) {
        segments.push({ action: 'off', durationSec: mp.waitSec });
      }
    });
  } else {
    // Old format: pass through
    segments = mixPattern;
  }
  // Calculate total duration
  const total = segments.reduce((sum, mp) => sum + (mp.durationSec || 0), 0);
  if (!total) return '';
  let html = '<div class="mix-pattern-bar" style="display:flex;height:14px;width:80px;border-radius:6px;overflow:hidden;box-shadow:0 1px 2px #0003;">';
  segments.forEach(mp => {
    const width = ((mp.durationSec || 0) / total * 100).toFixed(1) + '%';
    let color = mp.action === 'mix' || mp.action === 'on' ? '#4caf50' : '#888';
    let title = (mp.action === 'mix' || mp.action === 'on' ? 'Mix ON' : 'Mix OFF') + ` (${mp.durationSec||0}s)`;
    html += `<div style="width:${width};background:${color};height:100%;" title="${title}"></div>`;
  });
  html += '</div>';
  return html;
}

// ---- Program Plan Summary & Google Calendar Link ----
function hideLoadingIndicator() {
  const loadingIndicator = document.getElementById('loadingIndicator');
  if (loadingIndicator) {
    loadingIndicator.style.display = 'none';
  }
}

function showPlanSummary(s) {
  const planSummary = document.getElementById('planSummary');
  if (!planSummary || !s || !s.program) {
    if (planSummary) {
      hideLoadingIndicator();
      planSummary.innerHTML = '<i>No bread program selected.</i>';
    }
    return;
  }
  
  // Try to get program from status or cached programs
  let prog = null;
  if (s.programs && s.programs[s.program]) {
    prog = s.programs[s.program];
  } else if (window.cachedPrograms && Array.isArray(window.cachedPrograms)) {
    // First try to find by programId if available
    if (typeof s.programId === 'number' && s.programId >= 0) {
      prog = window.cachedPrograms.find(p => p.id === s.programId);
    }
    // If not found by ID, try by name
    if (!prog && s.program) {
      prog = window.cachedPrograms.find(p => p.name === s.program);
    }
  } else if (window.cachedPrograms && typeof window.cachedPrograms === 'object') {
    prog = window.cachedPrograms[s.program];
  }
  
  if (!prog) {
    // If we have a program name but no details, show the name
    if (s.program && s.program.trim() !== '') {
      hideLoadingIndicator();
      planSummary.innerHTML = `<div style="padding:15px; background:#1e1e1e; border:1px solid #444; border-radius:8px; margin:10px 0;">
        <h4 style="color:#66bb6a; margin:0 0 8px 0; font-size:1.1em;">${s.program}</h4>
        <p style="color:#bbb; margin:0; font-style:italic;">Program details loading...</p>
      </div>`;
    } else {
      hideLoadingIndicator();
      planSummary.innerHTML = '<i>Program details not available.</i>';
    }
    return;
  }
  // --- Custom Stages Support ---
  if (prog.customStages && Array.isArray(prog.customStages) && prog.customStages.length) {
    const palette = [
      '#b0b0b0', '#ff9933', '#4caf50', '#2196f3', '#ffd600', '#bb8640', '#e57373', '#90caf9', '#ce93d8', '#80cbc4'
    ];
    // Determine current stage index
    let currentStageIdx = -1;
    if (typeof s.stageIdx === 'number') {
      currentStageIdx = s.stageIdx;
    } else if (typeof s.stage === 'string' && s.stage !== 'Idle') {
      currentStageIdx = prog.customStages.findIndex(st => (st.label||'').toLowerCase() === s.stage.toLowerCase());
    }
    let summary = '<table class="plan-summary-table"><tr><th>Stage</th><th>Temp</th><th>Mix Pattern</th><th>Duration</th><th>Instructions</th><th>Calendar</th></tr>';

    // --- Use firmware-provided timing data ---
    // Use firmware-provided predicted stage end times (already fermentation-adjusted)
    const predictedStageEndTimes = Array.isArray(s.predictedStageEndTimes) ? s.predictedStageEndTimes : null;

    prog.customStages.forEach((st, i) => {
      const color = st.color || palette[i % palette.length];
      let calCell = '';
      // Use actual start time if available (stage has started), otherwise use estimated time
      let stStart = null;
      if (Array.isArray(s.actualStageStartTimes) && s.actualStageStartTimes[i] && s.actualStageStartTimes[i] > 0) {
        // Stage has actually started - use actual time
        stStart = new Date(s.actualStageStartTimes[i] * 1000);
      } else if (Array.isArray(s.stageStartTimes) && s.stageStartTimes[i]) {
        // Stage hasn't started yet - use estimated time
        stStart = new Date(s.stageStartTimes[i] * 1000);
      } else if (!s.running && s.fermentationFactor > 0) {
        // Preview: use now or previous stage end time from firmware predictions
        if (i === 0) {
          stStart = new Date();
        } else if (predictedStageEndTimes && predictedStageEndTimes[i-1]) {
          stStart = new Date(predictedStageEndTimes[i-1] * 1000);
        }
      }
      
      // Use firmware-provided predicted stage end times and durations
      let predictedEnd = null;
      let predictedDurationMin = null;
      if (predictedStageEndTimes && predictedStageEndTimes[i]) {
        predictedEnd = new Date(predictedStageEndTimes[i] * 1000);
        // For duration, use difference between this and previous stage end (or start)
        if (i === 0) {
          // First stage: duration is end - start (use stStart if available)
          if (stStart) {
            predictedDurationMin = (predictedStageEndTimes[0] - (stStart.getTime() / 1000)) / 60;
          }
        } else {
          predictedDurationMin = (predictedStageEndTimes[i] - predictedStageEndTimes[i-1]) / 60;
        }
        // Only keep predictedDurationMin if it's a valid positive number
        if (typeof predictedDurationMin !== 'number' || isNaN(predictedDurationMin) || predictedDurationMin < 0) {
          predictedDurationMin = null; // Clear invalid predictions
        }
      }
      // Only show calendar if we can calculate timing and this stage requires user interaction
      let stEnd = null;
      if (predictedEnd) {
        // Use firmware-provided end time
        stEnd = predictedEnd;
      } else if (stStart && predictedDurationMin) {
        // Calculate end time from firmware-provided duration
        stEnd = new Date(stStart.getTime() + (predictedDurationMin * 60000));
      } else if (stStart) {
        // Fallback to planned duration
        stEnd = new Date(stStart.getTime() + (st.min * 60000));
      }
      
      if (stStart && stEnd && st.instructions && st.instructions !== 'No action needed.') {
        let breadmakerUrl = window.location.origin + '/';
        let gcalUrl = `https://calendar.google.com/calendar/render?action=TEMPLATE&text=${encodeURIComponent('Breadmaker: ' + st.label + ' (' + s.program + ')')}` +
          `&dates=${toGCalDate(stStart)}/${toGCalDate(stEnd)}` +
          `&details=${encodeURIComponent('Breadmaker program: ' + s.program + '\nStage: ' + st.label + (st.instructions ? ('\nInstructions: ' + st.instructions) : '') + '\nCheck status: ' + breadmakerUrl)}` +
          `&location=${encodeURIComponent(breadmakerUrl)}`;
        calCell = `<a href="${gcalUrl}" target="_blank" title="Add this stage to Google Calendar" style="vertical-align:middle;display:inline-block;width:22px;height:22px;">` +
          `<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' viewBox='0 0 20 20' style='vertical-align:middle;'><rect x='2' y='4' width='16' height='14' rx='3' fill='#fff' stroke='#4285F4' stroke-width='1.5'/><rect x='2' y='7' width='16' height='3' fill='#4285F4'/><rect x='5' y='11' width='10' height='5' rx='1.2' fill='#e3eafc'/><text x='10' y='16' text-anchor='middle' font-size='7' fill='#4285F4' font-family='Arial' dy='0.2em'>+</text></svg></a>`;
      }
      // Info icon for instructions
      let infoCell = '';
      if (st.instructions && st.instructions !== 'No action needed.') {
        infoCell = `<span class='stage-info-icon' data-stage-idx='${i}' title='Show instructions' style='cursor:pointer;display:inline-block;margin-left:4px;'>` +
          `<svg width='18' height='18' viewBox='0 0 20 20' style='vertical-align:middle;'><circle cx='10' cy='10' r='9' fill='#ffd600' stroke='#888' stroke-width='1.2'/><text x='10' y='15' text-anchor='middle' font-size='13' fill='#232323' font-family='Arial' dy='-0.2em'>i</text></svg>` +
          `</span>`;
      }
      // Row status: done/active/inactive/skipped
      let rowClass = '';
      let skipped = false;
      if (currentStageIdx >= 0) {
        if (i < currentStageIdx) {
          // Stage is before current stage - determine if completed or skipped
          // If we have actualStageStartTimes data, use it for detection
          if (Array.isArray(s.actualStageStartTimes)) {
            if (!s.actualStageStartTimes[i]) {
              // No start time recorded, but check if this could be a backend recording issue
              // If stages after this one have start times, or if current stage is running normally,
              // then this stage probably ran but wasn't recorded properly
              let hasLaterStages = false;
              for (let j = i + 1; j < currentStageIdx && j < s.actualStageStartTimes.length; j++) {
                if (s.actualStageStartTimes[j] > 0) {
                  hasLaterStages = true;
                  break;
                }
              }
              
              // If we're currently in a stage beyond this one and running normally, 
              // assume missing stages were completed (backend recording issue)
              if (s.running && currentStageIdx > i) {
                rowClass = 'done';
                skipped = false; // Assume completed, not skipped
              } else {
                rowClass = 'done';
                skipped = true; // Truly skipped
              }
            } else {
              // Start time exists = stage was completed normally
              rowClass = 'done';
              skipped = false;
            }
          } else {
            // No actualStageStartTimes data - assume completed if we're past it
            rowClass = 'done';
            skipped = false;
          }
        } else if (i === currentStageIdx) rowClass = 'active';
        else rowClass = 'inactive';
      }
      
      // Show actual elapsed time for done stages if available, else planned/adjusted duration
      let durationCell = '';
      if (skipped) {
        durationCell = `<span title="Skipped stage">Skipped</span>`;
      } else if (rowClass === 'done' && Array.isArray(s.actualStageStartTimes) && s.actualStageStartTimes[i]) {
        // For completed stages, calculate actual duration
        let elapsedSec = 0;
        if (s.actualStageStartTimes[i+1]) {
          // Next stage started - use that as end time
          elapsedSec = (s.actualStageStartTimes[i+1] - s.actualStageStartTimes[i]);
        } else if (i === currentStageIdx - 1 && typeof s.programStartTime === 'number' && typeof s.stageStartTime === 'number') {
          // This is the most recently completed stage - use current stage start as end time
          elapsedSec = (s.stageStartTime - s.actualStageStartTimes[i]);
        } else if (i === currentStageIdx - 1 && typeof s.customStageStart === 'number') {
          // Fallback: use current stage start time
          elapsedSec = (s.customStageStart / 1000 - s.actualStageStartTimes[i]);
        }
        
        if (elapsedSec > 0) {
          durationCell = `<span title="Actual elapsed time" style="color: #2d8c2d; font-weight: bold;">${formatDuration(elapsedSec)}</span>`;
        } else {
          // Fallback to planned duration if we can't calculate actual time
          durationCell = `<span title="Planned duration (actual time unavailable)">${formatDuration(st.min * 60)}<br><small>(${st.min.toFixed(1)} min planned)</small></span>`;
        }
      } else if (rowClass === 'active' && typeof s.timeLeft === 'number' && i === currentStageIdx && s.running) {
        // Show actual time left for the active stage - use adjustedTimeLeft for fermentation stages
        const isCurrentStageFermentation = typeof s.stageIdx === 'number' && 
          prog && prog.customStages && 
          prog.customStages[s.stageIdx] && 
          prog.customStages[s.stageIdx].isFermentation;
        
        const timeToShow = isCurrentStageFermentation && typeof s.adjustedTimeLeft === 'number' 
          ? s.adjustedTimeLeft 
          : s.timeLeft;
        
        durationCell = `<span title="Time left in this stage">${formatDuration(timeToShow)}</span>`;
      } else if (typeof predictedDurationMin === 'number' && !isNaN(predictedDurationMin)) {
        // Use firmware-provided predicted duration (already fermentation-adjusted)
        durationCell = `<span title="Predicted by firmware (fermentation-adjusted)">${formatDuration(predictedDurationMin * 60)}<br><small>(${predictedDurationMin.toFixed(1)} min)</small></span>`;
      } else {
        // Simple fallback to planned duration
        durationCell = `<span title="Planned duration">${formatDuration(st.min * 60)}<br><small>(${st.min.toFixed(1)} min)</small></span>`;
      }
      // For completed (done) stages, override background to a grey shade
      let rowStyle = `color:#232323;`;
      if (rowClass === 'done') {
        rowStyle += 'background:#bbb;';
      } else {
        rowStyle += `background:${color};`;
      }
      summary += `<tr class='${rowClass}' style="${rowStyle}">`
        + `<td>${st.label}</td>`
        + `<td>${typeof st.temp === 'number' && st.temp > 0 ? st.temp + '&deg;C' : '--'}</td>`
        + `<td>${renderMixPatternGraph(st.mixPattern) || '--'}</td>`
        + `<td>${durationCell}</td>`
        + `<td>${infoCell}</td>`
        + `<td>${calCell}</td>`
        + '</tr>';
    });
    summary += '</table>';
    hideLoadingIndicator();
    planSummary.innerHTML = summary;
    // Add click listeners for info icons
    document.querySelectorAll('.stage-info-icon').forEach(function(el) {
      el.onclick = function() {
        const idx = parseInt(el.getAttribute('data-stage-idx'));
        const st = prog.customStages[idx];
        if (st && st.instructions) {
          showInstructionModal(st.label, st.instructions);
        }
      };
    });
    return;
  }
  // No classic stage logic remains here.
  hideLoadingIndicator();
  planSummary.innerHTML = '<i>No bread program selected.</i>';
}

// Only fetch /api/programs once and cache it
window.cachedPrograms = null;
function fetchProgramsOnce(callback) {
  if (window.cachedPrograms) {
    callback(window.cachedPrograms);
  } else {
    // If already fetching, queue the callback
    if (!window._programsFetchQueue) window._programsFetchQueue = [];
    window._programsFetchQueue.push(callback);
    if (window._programsFetchInProgress) return;
    window._programsFetchInProgress = true;
    fetch('/programs.json')
      .then(r => r.text())
      .then(data => {
        try {
          const parsedData = JSON.parse(data);
          window.cachedPrograms = parsedData;
          window._programsFetchInProgress = false;
          // Call all queued callbacks
          (window._programsFetchQueue || []).forEach(cb => cb(parsedData));
          window._programsFetchQueue = [];
          
          // Update stage progress if we have a current status
          if (window.lastStatusData) {
            updateStageProgress(window.lastStatusData.stage, window.lastStatusData);
          }
        } catch (e) {
          console.error('Failed to parse programs.json:', e);
          window._programsFetchInProgress = false;
          // Call callbacks with empty array as fallback
          (window._programsFetchQueue || []).forEach(cb => cb([]));
          window._programsFetchQueue = [];
        }
      })
      .catch(err => {
        console.error('Failed to fetch programs:', err);
        window._programsFetchInProgress = false;
        // Call callbacks with empty array as fallback
        (window._programsFetchQueue || []).forEach(cb => cb([]));
        window._programsFetchQueue = [];
      });
  }
}

// ---- Program Progress Bar Functions (stubs) ----
function renderProgramProgressBar(s) {
  // Enhanced: Render program progress bar using stageStartTimes, actualStageStartTimes, elapsed
  const elem = document.getElementById('programProgressBar');
  if (!elem) return;
  elem.innerHTML = '';
  if (!s || !s.program || !window.cachedPrograms) return;
  let prog = null;
  if (Array.isArray(window.cachedPrograms)) {
    prog = window.cachedPrograms.find(p => p.name === s.program);
  } else if (typeof window.cachedPrograms === 'object') {
    prog = window.cachedPrograms[s.program];
  }
  if (!prog || !prog.customStages) return;
  const stages = prog.customStages;
  const actualStarts = Array.isArray(s.actualStageStartTimes) ? s.actualStageStartTimes : [];
  const estStarts = Array.isArray(s.stageStartTimes) ? s.stageStartTimes : [];
  // Calculate total program duration using firmware predictions if available
  let totalSec = 0;
  let elapsed = 0;
  
  if (typeof s.predictedProgramEnd === 'number' && actualStarts[0]) {
    // Use firmware-calculated total duration (includes fermentation adjustments)
    totalSec = s.predictedProgramEnd - actualStarts[0];
    // Prefer server-calculated elapsed time, fallback to browser calculation
    elapsed = (typeof s.elapsedTime === 'number') ? s.elapsedTime : Math.floor((Date.now()/1000) - actualStarts[0]);
  } else {
    // Fallback to raw stage durations (without fermentation adjustments)
    let totalMin = stages.reduce((sum, st) => sum + (st.min || 0), 0);
    totalSec = totalMin * 60;
    // Prefer server elapsed time, then try s.elapsed, then calculate from start times
    if (typeof s.elapsedTime === 'number') {
      elapsed = s.elapsedTime;
    } else {
      elapsed = typeof s.elapsed === 'number' ? s.elapsed : 0;
      if (!elapsed && actualStarts[0]) {
        elapsed = Math.floor((Date.now()/1000) - actualStarts[0]);
      }
    }
  }
  // Render progress bar
  let html = '<div class="program-progress-bar" style="display:flex;height:18px;width:100%;border-radius:8px;overflow:hidden;box-shadow:0 1px 2px #0003;background:#eee;">';
  let accSec = 0;
  for (let i = 0; i < stages.length; ++i) {
    let stSec = (stages[i].min || 0) * 60;
    let done = false, active = false;
    if (actualStarts[i] && (i === stages.length-1 || actualStarts[i+1])) {
      // Stage completed
      done = true;
    } else if (actualStarts[i]) {
      // Current stage
      active = true;
    }
    let color = done ? '#4caf50' : (active ? '#ffd600' : '#b0b0b0');
    let width = (stSec / totalSec * 100).toFixed(2) + '%';
    let label = stages[i].label;
    html += `<div style="width:${width};background:${color};height:100%;position:relative;" title="${label}">`;
    if (active) {
      // Show elapsed for current stage
      let stElapsed = Math.floor((Date.now()/1000) - actualStarts[i]);
      html += `<span style='position:absolute;left:4px;top:1px;font-size:0.9em;color:#232323;'>${label} (${formatDuration(stElapsed)})</span>`;
    } else {
      html += `<span style='position:absolute;left:4px;top:1px;font-size:0.9em;color:#232323;'>${label}</span>`;
    }
    html += '</div>';
    accSec += stSec;
  }
  html += '</div>';
  // Show total elapsed/remaining using firmware-provided data when available
  let remain = 0;
  
  // Priority 1: Use firmware-provided timing data (most accurate)
  if (s.running && typeof s.totalProgramDuration === 'number' && typeof s.elapsedTime === 'number' && typeof s.remainingTime === 'number') {
    totalSec = s.totalProgramDuration;
    // Use the server-calculated elapsed time directly (handles timezone/NTP correctly)
    elapsed = s.elapsedTime;
    remain = s.remainingTime;
  }
  // Priority 2: Use firmware predictedProgramEnd (fermentation-adjusted)
  else if (typeof s.predictedProgramEnd === 'number' && s.predictedProgramEnd > 0 && actualStarts[0]) {
    totalSec = s.predictedProgramEnd - actualStarts[0];
    elapsed = Math.floor((Date.now()/1000) - actualStarts[0]);
    remain = Math.max(0, s.predictedProgramEnd - Math.floor(Date.now()/1000));
  }
  // Priority 3: Fallback to calculated remaining time (raw durations)
  else {
    remain = Math.max(0, totalSec - elapsed);
  }
  html += `<div style='margin-top:2px;font-size:0.95em;color:#ffd600;text-shadow:0 1px 2px #000;'>Elapsed: ${formatDuration(elapsed)} / ${formatDuration(totalSec)} &nbsp; Remaining: ${formatDuration(remain)}</div>`;
  elem.innerHTML = html;
}

function renderProgressBarKey(s) {
  // TODO: Implement progress bar key rendering if needed  
  const elem = document.getElementById('progressBarKey');
  if (elem) {
    elem.innerHTML = ''; // Clear for now
  }
}

// ---- Manual Mode UI Updates ----
function updateManualModeToggle(s) {
  const toggle = document.getElementById('manualModeToggle');
  const status = document.getElementById('manualModeStatus');
  const tempRow = document.getElementById('manualTempRow');
  
  if (toggle && s && typeof s.manualMode === 'boolean') {
    toggle.checked = s.manualMode;
  }
  
  if (status) {
    if (s && s.manualMode) {
      status.textContent = 'ON - Direct device control enabled';
      status.style.color = '#4caf50';
    } else {
      status.textContent = 'OFF - Automatic program control';
      status.style.color = '#ffb';
    }
  }

  // Show/hide manual temperature controls based on manual mode
  if (tempRow) {
    if (s && s.manualMode) {
      tempRow.style.display = 'flex';
    } else {
      tempRow.style.display = 'none';
    }
  }
  
  // Disable/enable program buttons based on manual mode
  const programButtons = ['btnStart', 'btnStop', 'btnBack', 'btnAdvance'];
  programButtons.forEach(btnId => {
    const btn = document.getElementById(btnId);
    if (btn && s && s.manualMode) {
      btn.disabled = true;
      btn.style.opacity = '0.5';
      btn.title = 'Disabled in manual mode';
    } else if (btn) {
      btn.disabled = false;
      btn.style.opacity = '1';
      btn.title = '';
    }
  });
}

// ---- Manual Output Control ----
window.handleManualOutput = function(type) {
  // Only allow manual output control when in manual mode
  if (!lastStatus || !lastStatus.manualMode) {
    console.log('Manual output control only available in manual mode');
    return;
  }
  
  // Toggle the current state of the output
  let currentState = false;
  switch(type) {
    case 'motor': currentState = lastStatus.motor; break;
    case 'heater': currentState = lastStatus.heater; break;
    case 'light': currentState = lastStatus.light; break;
    case 'buzzer': currentState = lastStatus.buzzer; break;
    default: 
      console.error('Unknown output type:', type);
      return;
  }
  
  // Toggle the state
  const newState = !currentState;
  
  // Call the API endpoint
  fetch(`/api/${type}?on=${newState ? 1 : 0}`)
    .then(r => r.json())
    .then(s => {
      window.updateStatus(s);
      // Provide visual feedback
      console.log(`${type} turned ${newState ? 'ON' : 'OFF'}`);
    })
    .catch(err => console.error(`Failed to control ${type}:`, err));
};

// ---- Global Status Update Handler ----
// This ensures program dropdown is populated regardless of how status is updated
window.updateStatus = function(s) {
  updateStatusInternal(s);
  // Also ensure program dropdown is populated
  populateProgramDropdown(s);
};

// ---- Event Listeners ----
window.addEventListener('DOMContentLoaded', () => {
  // Fetch programs first, then initialize UI
  fetchProgramsOnce(() => {
    // Populate program selector after programs are loaded
    populateProgramSelect();
    // Immediately update status after programs are loaded
    window.updateStatus();
    // Set up periodic status updates to keep everything in sync
    setInterval(() => {
      fetch('/status')
        .then(r => r.json())
        .then(s => {
          window.updateStatus(s);
        })
        .catch(err => console.error('Status poll failed:', err));
    }, 3000);
  });

  // Manual Mode toggle logic
  const manualToggle = document.getElementById('manualModeToggle');
  if (manualToggle) {
    manualToggle.onchange = function() {
      fetch(`/api/manual_mode?on=${manualToggle.checked ? 1 : 0}`)
        .then(r => r.json())
        .then(s => {
          updateManualModeToggle(s);
          window.updateStatus(s);
        });
    };
  }

  // Manual Temperature Control
  const btnSetTemp = document.getElementById('btnSetTemp');
  const btnTempOff = document.getElementById('btnTempOff');
  const manualTempInput = document.getElementById('manualTempInput');

  if (btnSetTemp && manualTempInput) {
    btnSetTemp.onclick = function() {
      const temp = parseFloat(manualTempInput.value);
      if (isNaN(temp) || temp < 0 || temp > 250) {
        alert('Please enter a valid temperature between 0 and 250\xB0C');
        return;
      }
      fetch(`/api/temperature?setpoint=${temp}`)
        .then(r => r.json())
        .then(s => {
          window.updateStatus(s);
          console.log(`Manual temperature set to ${temp}\xB0C`);
        })
        .catch(err => console.error('Failed to set temperature:', err));
    };
  }

  if (btnTempOff) {
    btnTempOff.onclick = function() {
      fetch('/api/temperature?setpoint=0')
        .then(r => r.json())
        .then(s => {
          window.updateStatus(s);
          console.log('Manual temperature control turned off');
          if (manualTempInput) manualTempInput.value = '';
        })
        .catch(err => console.error('Failed to turn off temperature control:', err));
    };
  }

  // Start at Stage functionality - Updated to work with new combined UI
  const btnStartAtStage = document.getElementById('btnStartAtStage');
  if (btnStartAtStage) {
    btnStartAtStage.onclick = () => {
      const stageSelect = document.getElementById('stageSelect');
      if (stageSelect && stageSelect.value !== '') {
        const stageIdx = parseInt(stageSelect.value);
        const selectedProgram = document.getElementById('programSelect')?.value || 'Unknown';
        const isRunning = window.status?.state === 'Running' || window.status?.state === 'Paused';
        
        let confirmMessage = `🚀 Start Program at Specific Stage?\n\nProgram: ${selectedProgram}\nStarting at Stage: ${stageIdx + 1}\n\nThis will skip all previous stages.`;
        
        if (isRunning) {
          confirmMessage += `\n\n⚠️ WARNING: A program is currently running!\nThis will stop the current program.`;
        }
        
        confirmMessage += `\n\nAre you sure?`;
        
        if (confirm(confirmMessage)) {
          fetch(`/start_at_stage?stage=${stageIdx}`)
            .then(r => r.json())
            .then(response => {
              if (response.error) {
                alert(`Error: ${response.error}\n${response.message || ''}`);
              } else {
                console.log(`Started at stage ${stageIdx}: ${response.stageName || ''}`);
                showStartAtStatus(`Started at stage ${stageIdx + 1}`);
                window.updateStatus();
              }
            })
            .catch(err => {
              console.error('Failed to start at stage:', err);
              alert('Failed to start at stage. Please try again.');
            });
        }
      } else {
        alert('Please select a stage to start at');
      }
    };
  }

  // Stage Dropdown Population
  const startAtStageRow = document.getElementById('startAtStageRow');
  // stageSelect is already declared elsewhere, do not redeclare here

  // Allow Enter key to set temperature
  if (manualTempInput && btnSetTemp) {
    manualTempInput.onkeypress = function(e) {
      if (e.key === 'Enter') {
        btnSetTemp.onclick();
      }
    };
  }

  // ---- Button Event Handlers ----
  const btnStart = document.getElementById('btnStart');
  if (btnStart) {
    btnStart.addEventListener('click', function() {
      const isRunning = window.status?.state === 'Running' || window.status?.state === 'Paused';
      const currentProgram = window.status?.program || 'Unknown';
      const selectedProgram = document.getElementById('programSelect')?.value || 'Unknown';
      
      if (isRunning) {
        if (confirm(`🔄 Restart Program?\n\nCurrent Program: ${currentProgram}\nSelected Program: ${selectedProgram}\n\nThis will stop the current program and start a new one.\nAll progress will be lost!\n\nAre you sure?`)) {
          fetch('/start')
            .then(r => r.json())
            .then(window.updateStatus)
            .catch(err => console.error('Start failed:', err));
        }
      } else {
        // No program running, start normally
        fetch('/start')
          .then(r => r.json())
          .then(window.updateStatus)
          .catch(err => console.error('Start failed:', err));
      }
    });
  }

  const btnStop = document.getElementById('btnStop');
  if (btnStop) {
    btnStop.addEventListener('click', function() {
      const currentStage = window.status?.currentStage || 'Unknown';
      const currentProgram = window.status?.program || 'Unknown';
      
      if (confirm(`⚠️ Stop Program?\n\nProgram: ${currentProgram}\nCurrent Stage: ${currentStage}\n\nThis will stop the current program and turn off all outputs.\nAre you sure?`)) {
        // Add visual feedback animation
        btnStop.classList.add('critical-pressed');
        setTimeout(() => btnStop.classList.remove('critical-pressed'), 300);
        
        fetch('/stop')
          .then(r => r.json())
          .then(window.updateStatus)
          .catch(err => console.error('Stop failed:', err));
      }
    });
  }

  const btnBack = document.getElementById('btnBack');
  if (btnBack) {
    btnBack.addEventListener('click', function() {
      const currentStage = window.status?.currentStage || 'Unknown';
      const currentProgram = window.status?.program || 'Unknown';
      
      if (confirm(`⏮️ Go to Previous Stage?\n\nProgram: ${currentProgram}\nCurrent Stage: ${currentStage}\n\nThis will move back to the previous stage and may affect timing.\nAre you sure?`)) {
        // Add visual feedback animation
        btnBack.classList.add('critical-pressed');
        setTimeout(() => btnBack.classList.remove('critical-pressed'), 300);
        
        fetch('/back')
          .then(r => r.json())
          .then(window.updateStatus)
          .catch(err => console.error('Back failed:', err));
      }
    });
  }

  const btnAdvance = document.getElementById('btnAdvance');
  if (btnAdvance) {
    btnAdvance.addEventListener('click', function() {
      const currentStage = window.status?.currentStage || 'Unknown';
      const currentProgram = window.status?.program || 'Unknown';
      const elapsedTime = window.status?.elapsedTime || 'Unknown';
      
      if (confirm(`⏭️ Advance to Next Stage?\n\nProgram: ${currentProgram}\nCurrent Stage: ${currentStage}\nElapsed: ${elapsedTime}\n\nThis will immediately advance to the next stage.\nThis action cannot be undone!\n\nAre you sure?`)) {
        // Add visual feedback animation
        btnAdvance.classList.add('critical-pressed');
        setTimeout(() => btnAdvance.classList.remove('critical-pressed'), 300);
        
        fetch('/advance')
          .then(r => r.json())
          .then(window.updateStatus)
          .catch(err => console.error('Advance failed:', err));
      }
    });
  }

  const btnSetStartAt = document.getElementById('btnSetStartAt');
  if (btnSetStartAt) {
    btnSetStartAt.onclick = () => {
      const startAtInput = document.getElementById('startAt');
      if (startAtInput && startAtInput.value) {
        fetch(`/setStartAt?time=${encodeURIComponent(startAtInput.value)}`)
          .then(r => r.text())
          .then(text => {
            showStartAtStatus(text);
            window.updateStatus();
            updateCombinedStartButton(); // Update combined button visibility
          })
          .catch(err => {
            console.error('Set start time failed:', err);
            showStartAtStatus('Failed to set start time');
          });
      } else {
        showStartAtStatus('Please select a time');
      }
    };
  }

  // Combined Timed + Stage Start button
  const btnSetTimedStageStart = document.getElementById('btnSetTimedStageStart');
  if (btnSetTimedStageStart) {
    btnSetTimedStageStart.onclick = () => {
      const startAtInput = document.getElementById('startAt');
      const stageSelect = document.getElementById('stageSelect');
      
      if (!startAtInput || !startAtInput.value) {
        alert('Please select a start time');
        return;
      }
      
      const time = startAtInput.value;
      const stageIdx = stageSelect && stageSelect.value !== '' ? parseInt(stageSelect.value) : '';
      
      let confirmMsg;
      let url;
      
      if (stageIdx === '') {
        // Time only
        confirmMsg = `Schedule the program to start at ${time} from the beginning?`;
        url = `/setStartAt?time=${encodeURIComponent(time)}`;
      } else {
        // Time + Stage
        confirmMsg = `Schedule the program to start at ${time} at stage ${stageIdx + 1}?`;
        url = `/setStartAtStage?time=${encodeURIComponent(time)}&stage=${stageIdx}`;
      }
      
      if (confirm(confirmMsg)) {
        fetch(url)
          .then(r => r.text())
          .then(text => {
            showStartAtStatus(text);
            window.updateStatus();
          })
          .catch(err => {
            console.error('Failed to set timed start:', err);
            showStartAtStatus('Failed to set timed start');
          });
      }
    };
  }

  // Update combined button when time input changes
  const startAtInput = document.getElementById('startAt');
  if (startAtInput) {
    startAtInput.addEventListener('change', updateCombinedStartButton);
    startAtInput.addEventListener('input', updateCombinedStartButton);
  }

  // Update combined button when stage selection changes
  const stageSelect = document.getElementById('stageSelect');
  if (stageSelect) {
    stageSelect.addEventListener('change', updateCombinedStartButton);
  }

  // Cancel scheduled start button
  const btnCancelScheduledStart = document.getElementById('btnCancelScheduledStart');
  if (btnCancelScheduledStart) {
    btnCancelScheduledStart.onclick = () => {
      if (confirm('Cancel the scheduled start?')) {
        fetch('/cancelScheduledStart')
          .then(r => r.text())
          .then(text => {
            showStartAtStatus(text);
            window.updateStatus();
          })
          .catch(err => {
            console.error('Cancel scheduled start failed:', err);
            showStartAtStatus('Failed to cancel scheduled start');
          });
      }
    };
  }
});

// ---- Instruction Modal ----
function showInstructionModal(title, instructions) {
  // Create or reuse a modal for showing stage instructions
  let modal = document.getElementById('instructionModal');
  if (!modal) {
    modal = document.createElement('div');
    modal.id = 'instructionModal';
    modal.style.cssText = 'display:none;position:fixed;z-index:1000;left:0;top:0;width:100vw;height:100vh;background:rgba(0,0,0,0.6);align-items:center;justify-content:center;';
    modal.innerHTML = `
      <div style="background:#232323;color:#ffe;padding:1.5em 2em;border-radius:12px;max-width:90vw;max-height:80vh;overflow:auto;box-shadow:0 4px 32px #000b;">
        <h2 id="instructionTitle">Stage Instructions</h2>
        <div id="instructionText" style="white-space:pre-wrap;font-size:1.1em;margin:1em 0;"></div>
        <button onclick="document.getElementById('instructionModal').style.display='none'">Close</button>
      </div>
    `;
    document.body.appendChild(modal);
  }
  
  document.getElementById('instructionTitle').textContent = title || 'Stage Instructions';
  document.getElementById('instructionText').textContent = instructions || 'No instructions available.';
  modal.style.display = 'flex';
}