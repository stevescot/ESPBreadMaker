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
    return h + ':' + (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;
  } else {
    return m + ':' + (s < 10 ? '0' : '') + s;
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
    program = window.cachedPrograms.find(p => p.name === statusData.program);
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
  
  if (!isIdle) {
    // Find current stage index by matching stage label
    activeIdx = customStages.findIndex(stage => stage.label === currentStage);
  }
  
  for (let i = 0; i < customStages.length; ++i) {
    const stage = customStages[i];
    const div = document.createElement('div');
    div.className = 'stage-step';
    div.textContent = stage.label;
    
    if (isIdle) {
      // All grey/inactive if idle
    } else if (i < activeIdx) {
      div.classList.add('done');
    } else if (i === activeIdx) {
      div.classList.add('active');
    }
    cont.appendChild(div);
  }
}

// ---- Program Dropdown ----
let lastProgramList = [];
function populateProgramDropdown(s) {
  let select = document.getElementById('progSelect');
  // Robustly handle both array and object program lists
  let programNames = [];
  
  // First try to get from status object
  if (s && s.programList && Array.isArray(s.programList)) {
    programNames = s.programList;
  } else if (s && s.programs && typeof s.programs === 'object') {
    programNames = Object.keys(s.programs);
  } else if (s && Array.isArray(s.programs)) {
    programNames = s.programs.map(p => p.name);
  }
  
  // If no programs in status, try to get from cached programs
  if (programNames.length === 0 && window.cachedPrograms) {
    if (Array.isArray(window.cachedPrograms)) {
      if (window.cachedPrograms.length > 0 && typeof window.cachedPrograms[0] === 'object' && window.cachedPrograms[0].name) {
        programNames = window.cachedPrograms.map(p => p.name);
      } else {
        programNames = window.cachedPrograms;
      }
    } else if (typeof window.cachedPrograms === 'object') {
      programNames = Object.keys(window.cachedPrograms);
    }
  }
  
  if (select && programNames.length) {
    if (!arraysEqual(lastProgramList, programNames)) {
      lastProgramList = [...programNames];
      const prevValue = select.value;
      select.innerHTML = '';
      programNames.forEach((name, idx) => {
        let opt = document.createElement('option');
        opt.value = idx;
        opt.textContent = name;
        select.appendChild(opt);
      });
      if (!select.hasChangeListener) {
        select.onchange = function () {
          fetch('/select?idx=' + this.value)
            .then(r => {
              // The /select endpoint returns plain text "Selected", not JSON
              if (r.ok) {
                // Fetch fresh status after selection
                return fetch('/status').then(r => r.json());
              }
              throw new Error('Selection failed');
            })
            .then(window.updateStatus)
            .catch(err => console.error('Program selection failed:', err));
        };
        select.hasChangeListener = true;
      }
    }
    // Update selected index
    const correctIdx = programNames.indexOf(s && s.program);
    if (correctIdx >= 0 && select.selectedIndex !== correctIdx) {
      select.selectedIndex = correctIdx;
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
  updateStageProgress(s.stage, s);
  
  // ---- Plan Summary (stages table) ----
  showPlanSummary(s);
  
  // ---- Manual Mode UI ----
  updateManualModeToggle(s);

  // Update manual temperature input field
  const manualTempInput = document.getElementById('manualTempInput');
  if (manualTempInput && s && s.manualMode && typeof s.setTemp === 'number' && s.setTemp > 0) {
    if (!manualTempInput.value || parseFloat(manualTempInput.value) !== s.setTemp) {
      manualTempInput.value = s.setTemp.toFixed(0);
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
    if (typeof s.setTemp === 'number' && s.setTemp > 0) {
      setTempText = 'Set: ' + s.setTemp.toFixed(1) + '°C';
      if (s.manualMode) {
        setTempText += ' (Manual)';
      }
    }
    document.getElementById('setTemp').innerHTML = setTempText;
  }
  if (document.getElementById('heater')) document.getElementById('heater').textContent = s.heater > 0 ? 'ON' : 'OFF';
  if (document.getElementById('motor')) document.getElementById('motor').textContent = s.motor ? 'ON' : 'OFF';
  if (document.getElementById('light')) document.getElementById('light').textContent = s.light ? 'ON' : 'OFF';
  if (document.getElementById('buzzer')) document.getElementById('buzzer').textContent = s.buzzer ? 'ON' : 'OFF';

  // ---- Track temperature history for chart ----
  const now = Date.now();
  if (typeof s.temp === 'number' && typeof s.setTemp === 'number') {
    tempHistory.push({ time: now, temp: s.temp, setTemp: s.setTemp });
    // Keep only last 5 minutes
    const cutoff = now - 5 * 60 * 1000;
    tempHistory = tempHistory.filter(pt => pt.time >= cutoff);
  }

  // ---- Icon states/animation ----
  renderIcons(s);

  // ---- Startup Delay Handling ----
  updateStartupDelayUI(s);
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

// ---- Program Ready At (Custom Stages Only) ----
function updateProgramReadyAt(s) {
  const el = document.getElementById('programReadyAt');
  if (!el) return;
  
  // Clear display if program is idle or finished
  if (!s || !s.running || s.stage === "Idle") {
    el.textContent = '';
    return;
  }
  
  // Use firmware's calculated programReadyAt if available
  if (s && typeof s.programReadyAt === 'number' && s.programReadyAt > 0) {
    const finishTs = s.programReadyAt * 1000; // Convert firmware's seconds to milliseconds
    const now = Date.now();
    
    // If the program ready time is in the past, clear the display
    if (finishTs <= now) {
      el.textContent = '';
      return;
    }
    
    el.textContent = 'Program ready at: ' + formatDateTime(finishTs);
    return;
  }
  
  // Fallback calculation if firmware doesn't provide programReadyAt
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
  let totalMin = prog.customStages.reduce((sum, st) => sum + (st.min || 0), 0);
  let finishTs = 0;
  if (s.scheduledStart && s.scheduledStart > Math.floor(Date.now() / 1000)) {
    finishTs = (s.scheduledStart * 1000) + (totalMin * 60000);
  } else if (s.stage && s.stage !== "Idle" && typeof s.stageIdx === "number" && typeof s.timeLeft === "number" && s.timeLeft > 0) {
    let remainMin = 0;
    for (let i = s.stageIdx + 1; i < prog.customStages.length; ++i) {
      remainMin += prog.customStages[i].min || 0;
    }
    finishTs = Date.now() + (s.timeLeft * 1000) + (remainMin * 60000);
  } else {
    finishTs = Date.now() + (totalMin * 60000);
  }
  
  // If the program ready time is in the past, clear the display
  if (finishTs <= Date.now()) {
    el.textContent = '';
    return;
  }
  
  el.textContent = 'Program ready at: ' + formatDateTime(finishTs);
}

function updateCountdownDisplay() {
  if (!lastStatus) return;
  let s = lastStatus;
  let now = Date.now();
  let stageReadyAt = (typeof s.stageReadyAt === 'number') ? s.stageReadyAt : 0;
  let timeLeft = 0;
  if (stageReadyAt > 0) {
    timeLeft = Math.max(0, Math.round(stageReadyAt - (now / 1000))); // Fix: convert now to seconds to match stageReadyAt
  } else if (typeof s.timeLeft === 'number') {
    // fallback if stageReadyAt missing
    let elapsed = Math.floor((now - lastStatusTime) / 1000);
    timeLeft = Math.max(0, s.timeLeft - elapsed);
  }
  if (s.stage === "Idle" || !s.running || timeLeft <= 0) {
    document.getElementById('timeLeft').textContent = "--";
    document.getElementById('stageReadyAt').textContent = "";
  } else {
    document.getElementById('timeLeft').textContent = formatDuration(timeLeft);
    const stageReadyTs = stageReadyAt * 1000; // Convert firmware's seconds to milliseconds
    document.getElementById('stageReadyAt').textContent = "Stage ready at: " + formatDateTime(stageReadyTs);
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
function showPlanSummary(s) {
  const planSummary = document.getElementById('planSummary');
  if (!planSummary || !s || !s.program) {
    if (planSummary) planSummary.innerHTML = '<i>No bread program selected.</i>';
    return;
  }
  
  // Try to get program from status or cached programs
  let prog = null;
  if (s.programs && s.programs[s.program]) {
    prog = s.programs[s.program];
  } else if (window.cachedPrograms) {
    // If cached programs is an array of program objects
    if (Array.isArray(window.cachedPrograms)) {
      prog = window.cachedPrograms.find(p => p.name === s.program);
    } else if (typeof window.cachedPrograms === 'object') {
      prog = window.cachedPrograms[s.program];
    }
  }
  
  if (!prog) {
    planSummary.innerHTML = '<i>Program details not available.</i>';
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
      }
      let stEnd = stStart ? new Date(stStart.getTime() + (st.min * 60000)) : null;
      // Only show calendar if running and this stage requires user interaction
      if (s.running && stStart && stEnd && st.instructions && st.instructions !== 'No action needed.') {
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
      // Row status: done/active/inactive
      let rowClass = '';
      if (currentStageIdx >= 0) {
        if (i < currentStageIdx) rowClass = 'done';
        else if (i === currentStageIdx) rowClass = 'active';
        else rowClass = 'inactive';
      }
      summary += `<tr class='${rowClass}' style="background:${color};color:#232323;">`
        + `<td>${st.label}</td>`
        + `<td>${typeof st.temp === 'number' ? st.temp + '&deg;C' : '--'}</td>`
        + `<td>${renderMixPatternGraph(st.mixPattern) || '--'}</td>`
        + `<td>${st.min} min</td>`
        + `<td>${infoCell}</td>`
        + `<td>${calCell}</td>`
        + '</tr>';
    });
    summary += '</table>';
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
    fetch('/api/programs')
      .then(r => r.json())
      .then(data => {
        window.cachedPrograms = data;
        window._programsFetchInProgress = false;
        // Call all queued callbacks
        (window._programsFetchQueue || []).forEach(cb => cb(data));
        window._programsFetchQueue = [];
        
        // Update stage progress if we have a current status
        if (window.lastStatusData) {
          updateStageProgress(window.lastStatusData.stage, window.lastStatusData);
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
  // TODO: Implement program progress bar rendering if needed
  const elem = document.getElementById('programProgressBar');
  if (elem) {
    elem.innerHTML = ''; // Clear for now
  }
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
        alert('Please enter a valid temperature between 0 and 250°C');
        return;
      }
      fetch(`/api/temperature?setpoint=${temp}`)
        .then(r => r.json())
        .then(s => {
          window.updateStatus(s);
          console.log(`Manual temperature set to ${temp}°C`);
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
    btnStart.onclick = () => {
      fetch('/start')
        .then(r => r.json())
        .then(window.updateStatus)
        .catch(err => console.error('Start failed:', err));
    };
  }

  const btnStop = document.getElementById('btnStop');
  if (btnStop) {
    btnStop.onclick = () => {
      fetch('/stop')
        .then(r => r.json())
        .then(window.updateStatus)
        .catch(err => console.error('Stop failed:', err));
    };
  }

  const btnBack = document.getElementById('btnBack');
  if (btnBack) {
    btnBack.onclick = () => {
      fetch('/back')
        .then(r => r.json())
        .then(window.updateStatus)
        .catch(err => console.error('Back failed:', err));
    };
  }

  const btnAdvance = document.getElementById('btnAdvance');
  if (btnAdvance) {
    btnAdvance.onclick = () => {
      fetch('/advance')
        .then(r => r.json())
        .then(window.updateStatus)
        .catch(err => console.error('Advance failed:', err));
    };
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
