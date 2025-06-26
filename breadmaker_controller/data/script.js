// ---- Utility Functions ----
function showStartAtStatus(msg) {
  let el = document.getElementById('startAtStatus');
  if (el) el.textContent = msg || '';
}

// ---- Utility: Format Duration ----
function formatDuration(seconds) {
  if (typeof seconds !== 'number' || isNaN(seconds)) return '--';
  let m = Math.floor(seconds / 60);
  let s = Math.floor(seconds % 60);
  return m + ':' + (s < 10 ? '0' : '') + s;
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

// ---- Utility: Render Icons (stub) ----
function renderIcons(s) {
  // Use the shared renderIcons from common.js if available
  if (window.renderIcons && typeof window.renderIcons === 'function' && window.renderIcons !== renderIcons) {
    const iconRow = document.querySelector('.icon-row-devices');
    window.renderIcons(s, iconRow);
    return;
  }
  // fallback: color icons directly if needed
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
function updateStageProgress(currentStage) {
  const cont = document.getElementById('stageProgress');
  if (!cont) return;
  cont.innerHTML = '';
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
}

// ---- Program Dropdown ----
function populateProgramDropdown(s) {
  let select = document.getElementById('progSelect');
  if (select && s.programList && s.programList.length) {
    select.innerHTML = '';
    s.programList.forEach((name, idx) => {
      let opt = document.createElement('option');
      opt.value = idx;
      opt.textContent = name;
      if (s.program == name) opt.selected = true;
      select.appendChild(opt);
    });
    select.onchange = function () {
      fetch('/select?idx=' + this.value).then(updateStatus);
    };
  }
  let curProg = document.getElementById('currentProg');
  if (curProg) curProg.textContent = s.program || "";
}

// ---- Update Status ----
let tempHistory = [];
function updateStatus(s) {
  // ---- Time left in stage & stage ready at ----
  if (s.stage === "Idle" || typeof s.timeLeft !== "number" || s.timeLeft <= 0) {
    document.getElementById('timeLeft').textContent = "--";
    document.getElementById('stageReadyAt').textContent = "";
  } else {
    document.getElementById('timeLeft').textContent = formatDuration(s.timeLeft);
    const stageReadyTs = Date.now() + (s.timeLeft * 1000);
    document.getElementById('stageReadyAt').textContent = "Stage ready at: " + formatDateTime(stageReadyTs);
  }

  // ---- Scheduled Start Time (delayed program start) ----
  if (s.scheduledStart && s.scheduledStart > Math.floor(Date.now() / 1000)) {
    const startDt = new Date(s.scheduledStart * 1000);
    showStartAtStatus("Program scheduled for: " + formatDateTime(startDt));
  } else {
    showStartAtStatus("");
  }

  // ---- Whole program "ready at" time ----
  if (typeof s.program === "string" && s.program.length > 0) {
    fetchProgramsOnce(data => {
      const progArr = Array.isArray(data) ? data : (data.programs || []);
      let prog = null;
      if (Array.isArray(progArr)) {
        prog = progArr.find(p => p.name === s.program);
      } else if (progArr && typeof progArr === 'object') {
        prog = progArr[s.program];
      }
      if (!prog) {
        document.getElementById('programReadyAt').textContent = "";
        return;
      }
      // Stage order
      let stageNames = [
        "delayStart", "mixTime", "bulkTime", "knockDownTime",
        "riseTime", "bake1Time", "bake2Time", "coolTime"
      ];
      // Find which stage we're in
      let idx = 0;
      if (s.stage && s.stage !== "Idle" && typeof s.timeLeft === "number" && s.timeLeft > 0) {
        idx = stageNames.findIndex(n =>
          n.toLowerCase().replace("time", "") === s.stage.toLowerCase().replace("start", "")
        );
        if (idx < 0) idx = 0;
      }
      let total = 0;
      if (s.scheduledStart && s.scheduledStart > Math.floor(Date.now() / 1000)) {
        // Scheduled: sum all
        for (let i = 0; i < stageNames.length; ++i)
          total += (prog[stageNames[i]] || 0) * 60;
        const finishTs = (s.scheduledStart * 1000) + (total * 1000);
        document.getElementById('programReadyAt').textContent = "Program ready at: " + formatDateTime(finishTs);
      } else if (s.stage && s.stage !== "Idle" && typeof s.timeLeft === "number" && s.timeLeft > 0) {
        // Running: current stage left plus all after
        total = s.timeLeft;
        for (let i = idx + 1; i < stageNames.length; ++i)
          total += (prog[stageNames[i]] || 0) * 60;
        const finishTs = Date.now() + total * 1000;
        document.getElementById('programReadyAt').textContent = "Program ready at: " + formatDateTime(finishTs);
      } else {
        // Idle: sum all
        for (let i = 0; i < stageNames.length; ++i)
          total += (prog[stageNames[i]] || 0) * 60;
        const finishTs = Date.now() + total * 1000;
        document.getElementById('programReadyAt').textContent = "Program ready at: " + formatDateTime(finishTs);
      }
    });
  } else {
    document.getElementById('programReadyAt').textContent = "";
  }

  // ---- Program Dropdown & Progress ----
  populateProgramDropdown(s);
  updateStageProgress(s.stage);

  // ---- Program Progress Bar ----
  renderProgramProgressBar(s);
  renderProgressBarKey(s);

  let btnPauseResume = document.getElementById('btnPauseResume');
  if (s.stage !== 'Idle' && s.running) {
    btnPauseResume.style.display = '';
    btnPauseResume.textContent = 'Pause';
    btnPauseResume.onclick = () => fetch('/pause').then(updateStatus);
  } else if (s.stage !== 'Idle' && !s.running) {
    btnPauseResume.style.display = '';
    btnPauseResume.textContent = 'Resume';
    btnPauseResume.onclick = () => fetch('/resume').then(updateStatus);
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
  if (document.getElementById('setTemp')) document.getElementById('setTemp').innerHTML = (typeof s.setTemp === 'number' ? 'Set: ' + s.setTemp.toFixed(1) + '&deg;C' : '');
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
  showStageSvg(s.stage);
}

// ---- Render Mix Pattern Graph ----
function renderMixPatternGraph(mixPattern) {
  if (!Array.isArray(mixPattern) || !mixPattern.length) return '';
  // Calculate total duration
  const total = mixPattern.reduce((sum, mp) => sum + (mp.durationSec || 0), 0);
  if (!total) return '';
  let html = '<div class="mix-pattern-bar" style="display:flex;height:14px;width:80px;border-radius:6px;overflow:hidden;box-shadow:0 1px 2px #0003;">';
  mixPattern.forEach(mp => {
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
  if (!planSummary || !s || !s.program || !s.programList || !s.programs) {
    planSummary.innerHTML = '<i>No bread program selected.</i>';
    return;
  }
  const prog = s.programs[s.program];
  if (!prog) {
    planSummary.innerHTML = '<i>No bread program selected.</i>';
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
      let stStart = (Array.isArray(s.stageStartTimes) && s.stageStartTimes[i]) ? new Date(s.stageStartTimes[i] * 1000) : null;
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

// Patch updateStatus to also update plan summary
const origUpdateStatus = updateStatus;
updateStatus = function() {
  fetch('/status')
    .then(r => r.json())
    .then(s => {
      origUpdateStatus && origUpdateStatus(s);
      // Fetch all programs only once if not present
      if (!s.programs && !cachedPrograms) {
        fetchProgramsOnce(data => {
          s.programs = Array.isArray(data) ? data.reduce((acc, p) => { acc[p.name] = p; return acc; }, {}) : (data.programs || {});
          showPlanSummary(s);
        });
      } else {
        if (!s.programs && cachedPrograms) {
          s.programs = Array.isArray(cachedPrograms) ? cachedPrograms.reduce((acc, p) => { acc[p.name] = p; return acc; }, {}) : (cachedPrograms.programs || {});
        }
        showPlanSummary(s);
      }
    });
};

// Only fetch /api/programs once and cache it
let cachedPrograms = null;

// ---- Whole program "ready at" time ----
function fetchProgramsOnce(callback) {
  if (cachedPrograms) {
    callback(cachedPrograms);
  } else {
    fetch('/api/programs')
      .then(r => r.json())
      .then(data => {
        cachedPrograms = data;
        callback(cachedPrograms);
      });
  }
}

// ---- Event Listeners ----
window.addEventListener('DOMContentLoaded', () => {
  // Manual output toggles:
  document.getElementById('mixIcon').onclick = () => {
    fetch('/motor?on=' + (window._motorState ? '0' : '1')).then(updateStatus);
  };
  document.getElementById('heaterIcon').onclick = () => {
    fetch('/heater?on=' + (window._heaterState ? '0' : '1')).then(updateStatus);
  };
  document.getElementById('lightIcon').onclick = () => {
    fetch('/light?on=' + (window._lightState ? '0' : '1')).then(updateStatus);
  };
  document.getElementById('buzzerIcon').onclick = () => {
    fetch('/buzz?on=1').then(updateStatus);
  };

  // Program control buttons:
  if (document.getElementById('btnStart'))
    document.getElementById('btnStart').onclick = () => fetch('/start').then(updateStatus);
  if (document.getElementById('btnPause'))
    document.getElementById('btnPause').onclick = () => fetch('/pause').then(updateStatus);
  if (document.getElementById('btnResume'))
    document.getElementById('btnResume').onclick = () => fetch('/resume').then(updateStatus);
  if (document.getElementById('btnStop'))
    document.getElementById('btnStop').onclick = () => fetch('/stop').then(updateStatus);
  if (document.getElementById('btnAdvance'))
    document.getElementById('btnAdvance').onclick = () => fetch('/advance').then(updateStatus);
  if (document.getElementById('btnBack'))
    document.getElementById('btnBack').onclick = () => fetch('/back').then(updateStatus);
  if (document.getElementById('btnPrograms'))
    document.getElementById('btnPrograms').onclick = () => location.href = '/programs.html';
  if (document.getElementById('btnCalibrate'))
    document.getElementById('btnCalibrate').onclick = () => location.href = '/calibration.html';
  if (document.getElementById('btnUpdate'))
    document.getElementById('btnUpdate').onclick = () => location.href = '/update.html';

  // Set "start at" logic:
  if (document.getElementById('btnSetStartAt')) {
    document.getElementById('btnSetStartAt').onclick = function() {
      const timeVal = document.getElementById('startAt').value; // format: "HH:MM"
      if (!timeVal) return alert("Pick a time!");
      fetch('/setStartAt?time=' + encodeURIComponent(timeVal)).then(updateStatus);
    };
  }

  // Show Recipe Button Logic
  if (document.getElementById('btnShowRecipe')) {
    document.getElementById('btnShowRecipe').onclick = function() {
      // Find current program
      let progName = '';
      const progSelect = document.getElementById('progSelect');
      if (progSelect) progName = progSelect.value;
      // Try to get from window._programs, but fallback to cachedPrograms if needed
      let progs = window._programs || [];
      if ((!progs || !progs.length) && window.cachedPrograms) {
        progs = Array.isArray(window.cachedPrograms) ? window.cachedPrograms : (window.cachedPrograms.programs || []);
      }
      let prog = progs.find(p => p.name === progName);
      if (!prog && progs.length > 0) prog = progs[0];
      let notes = (prog && prog.notes) ? prog.notes : 'No recipe/notes for this program.';
      document.getElementById('recipeTitle').textContent = prog ? (prog.name + ' Recipe') : 'Recipe';
      document.getElementById('recipeText').textContent = notes;
      document.getElementById('recipeModal').style.display = 'flex';
    };
  }

  // Temperature chart modal logic
  const tempEl = document.getElementById('temp');
  if (tempEl) {
    tempEl.style.cursor = 'pointer';
    tempEl.onclick = function() {
      document.getElementById('tempChartModal').style.display = 'flex';
      drawTempChart();
    };
  }

  updateStatus();
  setInterval(updateStatus, 1000);
});

function showStageSvg(stage) {
  let st = (stage||'').toLowerCase();
  let map = {
    'autolyse':  'delay', // Use delay SVG for autolyse unless you provide a new one
    'delaystart': 'delay',
    'delay':      'delay',
    'mix':        'mix',
    'bulk':       'bulk',
    'rise':       'bulk',
    'knockdown':  'knockdown',
    'bake':       'bake',
    'bake1':      'bake',
    'bake2':      'bake',
    'cool':       'cool'
  };
  let which = map[st] || 'delay';
  document.querySelectorAll('.bm-stage-svg').forEach(d => d.style.display = 'none');
  let active = document.getElementById('svg-' + which);
  if (active) active.style.display = '';
}

// ---- Segmented Program Progress Bar ----
function renderProgramProgressBar(s) {
  const bar = document.getElementById('programProgressBar');
  if (!bar || !s || !s.program) { bar.innerHTML = ''; return; }
  fetchProgramsOnce(data => {
    const progArr = Array.isArray(data) ? data : (data.programs || []);
    let prog = null;
    if (Array.isArray(progArr)) {
      prog = progArr.find(p => p.name === s.program);
    } else if (progArr && typeof progArr === 'object') {
      prog = progArr[s.program];
    }
    if (!prog) { bar.innerHTML = ''; return; }
    // --- Custom Stages Support ---
    if (prog.customStages && Array.isArray(prog.customStages) && prog.customStages.length) {
      // Assign colors (palette or from stage)
      const palette = [
        '#b0b0b0', '#ff9933', '#4caf50', '#2196f3', '#ffd600', '#bb8640', '#e57373', '#90caf9', '#ce93d8', '#80cbc4'
      ];
      let stages = prog.customStages.map((st, i) => ({
        label: st.label || `Stage ${i+1}`,
        min: st.min || 0,
        color: st.color || palette[i % palette.length]
      })).filter(st => st.min > 0);
      const totalSec = stages.reduce((a, b) => a + b.min * 60, 0);
      if (!totalSec) { bar.innerHTML = ''; return; }
      // Find current stage index
      let curIdx = -1;
      if (s.stage && s.stage !== 'Idle') {
        curIdx = stages.findIndex(st => (st.label||'').toLowerCase() === (s.stage||'').toLowerCase());
        if (curIdx === -1 && typeof s.stageIdx === 'number') curIdx = s.stageIdx;
      }
      // Calculate elapsed seconds
      let elapsedSec = 0;
      if (Array.isArray(s.stageStartTimes) && curIdx >= 0) {
        elapsedSec = s.stageStartTimes[curIdx] - s.stageStartTimes[0];
        if (typeof s.timeLeft === 'number' && s.timeLeft > 0) {
          elapsedSec += (stages[curIdx].min * 60 - s.timeLeft);
        }
      } else if (curIdx > 0) {
        elapsedSec = stages.slice(0, curIdx).reduce((a, b) => a + b.min * 60, 0);
        if (typeof s.timeLeft === 'number' && s.timeLeft > 0) {
          elapsedSec += (stages[curIdx].min * 60 - s.timeLeft);
        }
      } else if (curIdx === 0 && typeof s.timeLeft === 'number' && s.timeLeft > 0) {
        elapsedSec = stages[0].min * 60 - s.timeLeft;
      }
      // Build bar segments
      let html = '<div class="progress-bar-outer" style="position:relative;">';
      let accSec = 0;
      for (let i = 0; i < stages.length; ++i) {
        const st = stages[i];
        const stSec = st.min * 60;
        let segWidth = (stSec / totalSec * 100).toFixed(2) + '%';
        let segClass = 'progress-segment';
        let segStyle = `background:${st.color};width:${segWidth};`;
        if (i < curIdx) {
          segStyle = `background:linear-gradient(90deg, #888 80%, ${st.color} 100%);opacity:0.5;width:${segWidth};`;
          segClass += ' done';
        } else if (i === curIdx) {
          // Show elapsed in grey, rest in color
          let doneWidth = Math.max(0, Math.min(1, elapsedSec / totalSec - accSec / totalSec)) * 100;
          let remainWidth = 100 - doneWidth;
          html += `<div class="progress-segment done" style="background:#888;width:${(doneWidth * stSec / totalSec).toFixed(2)}%;opacity:0.7;"></div>`;
          segStyle += 'border:2px solid #fff;box-shadow:0 0 6px #fff8;';
        }
        html += `<div class="${segClass}" style="${segStyle}" title="${st.label}: ${st.min} min"></div>`;
        accSec += stSec;
      }
      // Add vertical cursor for current time
      let cursorLeft = Math.max(0, Math.min(1, elapsedSec / totalSec)) * 100;
      html += `<div class='progress-bar-cursor' style='left:${cursorLeft}%;'></div>`;
      html += '</div>';
      bar.innerHTML = html;
      return;
    }
    // Classic program stages
    const stageDefs = [
      { key: 'autolyseTime', color: '#b0b0b0' },
      { key: 'mixTime', color: '#ff9933' },
      { key: 'bulkTime', color: '#4caf50' },
      { key: 'knockDownTime', color: '#2196f3' },
      { key: 'riseTime', color: '#ffd600' },
      { key: 'bake1Time', color: '#bb8640' },
      { key: 'bake2Time', color: '#e57373' },
      { key: 'coolTime', color: '#90caf9' }
    ];
    // Build stage segments
    let stages = stageDefs.map(st => ({
      key: st.key,
      min: prog[st.key] || 0,
      color: st.color
    })).filter(st => st.min > 0);
    const totalSec = stages.reduce((a, b) => a + b.min * 60, 0);
    if (!totalSec) { bar.innerHTML = ''; return; }
    // Find current stage index
    let curIdx = -1;
    if (s.stage && s.stage !== 'Idle') {
      curIdx = stages.findIndex(st => st.key.toLowerCase().replace('time','') === s.stage.toLowerCase().replace('start',''));
    }
    // Calculate elapsed seconds
    let elapsedSec = 0;
    if (Array.isArray(s.stageStartTimes) && curIdx >= 0) {
      elapsedSec = s.stageStartTimes[curIdx] - s.stageStartTimes[0];
      if (typeof s.timeLeft === 'number' && s.timeLeft > 0) {
        elapsedSec += (stages[curIdx].min * 60 - s.timeLeft);
      }
    } else if (curIdx > 0) {
      elapsedSec = stages.slice(0, curIdx).reduce((a, b) => a + b.min * 60, 0);
      if (typeof s.timeLeft === 'number' && s.timeLeft > 0) {
        elapsedSec += (stages[curIdx].min * 60 - s.timeLeft);
      }
    } else if (curIdx === 0 && typeof s.timeLeft === 'number' && s.timeLeft > 0) {
      elapsedSec = stages[0].min * 60 - s.timeLeft;
    }
    // Build bar segments
    let html = '<div class="progress-bar-outer" style="position:relative;">';
    let accSec = 0;
    for (let i = 0; i < stages.length; ++i) {
      const st = stages[i];
      const stSec = st.min * 60;
      let segWidth = (stSec / totalSec * 100).toFixed(2) + '%';
      let segClass = 'progress-segment';
      let segStyle = `background:${st.color};width:${segWidth};`;
      if (i < curIdx) {
        segStyle = `background:linear-gradient(90deg, #888 80%, ${st.color} 100%);opacity:0.5;width:${segWidth};`;
        segClass += ' done';
      } else if (i === curIdx) {
        // Show elapsed in grey, rest in color
        let doneWidth = Math.max(0, Math.min(1, elapsedSec / totalSec - accSec / totalSec)) * 100;
        let remainWidth = 100 - doneWidth;
        html += `<div class="progress-segment done" style="background:#888;width:${(doneWidth * stSec / totalSec).toFixed(2)}%;opacity:0.7;"></div>`;
        segStyle += 'border:2px solid #fff;box-shadow:0 0 6px #fff8;';
      }
      html += `<div class="${segClass}" style="${segStyle}" title="${st.key.replace('Time','')}: ${st.min} min"></div>`;
      accSec += stSec;
    }
    // Add vertical cursor for current time
    let cursorLeft = Math.max(0, Math.min(1, elapsedSec / totalSec)) * 100;
    html += `<div class='progress-bar-cursor' style='left:${cursorLeft}%;'></div>`;
    html += '</div>';
    bar.innerHTML = html;
  });
}

// ---- Progress Bar Color Key ----
function renderProgressBarKey(s) {
  const keyDiv = document.getElementById('progressBarKey');
  if (!keyDiv || !s || !s.program) { keyDiv.innerHTML = ''; return; }
  fetchProgramsOnce(data => {
    const progArr = Array.isArray(data) ? data : (data.programs || []);
    let prog = null;
    if (Array.isArray(progArr)) {
      prog = progArr.find(p => p.name === s.program);
    } else if (progArr && typeof progArr === 'object') {
      prog = progArr[s.program];
    }
    if (!prog) { keyDiv.innerHTML = ''; return; }
    // --- Custom Stages Support ---
    if (prog.customStages && Array.isArray(prog.customStages) && prog.customStages.length) {
      const palette = [
        '#b0b0b0', '#ff9933', '#4caf50', '#2196f3', '#ffd600', '#bb8640', '#e57373', '#90caf9', '#ce93d8', '#80cbc4'
      ];
      let stages = prog.customStages.map((st, i) => ({
        label: st.label || `Stage ${i+1}`,
        color: st.color || palette[i % palette.length]
      }));
      if (!stages.length) { keyDiv.innerHTML = ''; return; }
      let html = '<div class="progress-key-row">';
      for (let st of stages) {
        html += `<span class="progress-key-item" style="background:${st.color};">${st.label}</span>`;
      }
      html += '</div>';
      keyDiv.innerHTML = html;
      return;
    }
    // Classic program stages
    const stageDefs = [
      { key: 'autolyseTime', label: 'Autolyse', color: '#b0b0b0' },
      { key: 'mixTime', label: 'Mix', color: '#ff9933' },
      { key: 'bulkTime', label: 'Bulk', color: '#4caf50' },
      { key: 'knockDownTime', label: 'Knock-Down', color: '#2196f3' },
      { key: 'riseTime', label: 'Rise', color: '#ffd600' },
      { key: 'bake1Time', label: 'Bake 1', color: '#bb8640' },
      { key: 'bake2Time', label: 'Bake 2', color: '#e57373' },
      { key: 'coolTime', label: 'Cool', color: '#90caf9' }
    ];
    let stages = stageDefs.map(st => ({
      key: st.key,
      label: st.label,
      min: prog[st.key] || 0,
      color: st.color
    })).filter(st => st.min > 0);
    if (!stages.length) { keyDiv.innerHTML = ''; return; }
    let html = '<div class="progress-key-row">';
    for (let st of stages) {
      html += `<span class="progress-key-item" style="background:${st.color};">${st.label}</span>`;
    }
    html += '</div>';
    keyDiv.innerHTML = html;
  });
}

// ---- Temperature Chart Modal ----
function drawTempChart() {
  const canvas = document.getElementById('tempChart');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  if (tempHistory.length < 2) {
    ctx.fillStyle = '#ffe';
    ctx.font = '18px sans-serif';
    ctx.fillText('Not enough data', 30, 110);
    return;
  }
  // Chart area
  const W = canvas.width, H = canvas.height;
  const PAD = 40;
  // X: time, Y: temp
  const minT = Math.min(...tempHistory.map(pt => pt.temp), ...tempHistory.map(pt => pt.setTemp()));
  const maxT = Math.max(...tempHistory.map(pt => pt.temp), ...tempHistory.map(pt => pt.setTemp()));
  const t0 = tempHistory[0].time;
  const t1 = tempHistory[tempHistory.length-1].time;
  // Axes
  ctx.strokeStyle = '#888';
  ctx.lineWidth = 1.2;
  ctx.beginPath();
  ctx.moveTo(PAD, PAD);
  ctx.lineTo(PAD, H-PAD);
  ctx.lineTo(W-PAD, H-PAD);
  ctx.stroke();
  // Y labels
  ctx.fillStyle = '#ffe';
  ctx.font = '13px sans-serif';
  for (let i=0; i<=4; ++i) {
    const v = minT + (maxT-minT)*i/4;
    const y = H-PAD - ((H-2*PAD)*(i/4));
    ctx.fillText(v.toFixed(1)+'°C', 2, y+4);
    ctx.strokeStyle = '#333';
    ctx.beginPath();
    ctx.moveTo(PAD-3, y);
    ctx.lineTo(W-PAD, y);
    ctx.stroke();
  }
  // X labels (time)
  for (let i=0; i<=5; ++i) {
    const t = t0 + (t1-t0)*i/5;
    const x = PAD + ((W-2*PAD)*(i/5));
    const dt = new Date(t);
    ctx.fillText(dt.getMinutes()+":"+('0'+dt.getSeconds()).slice(-2), x-12, H-PAD+18);
  }
  // Plot temp
  ctx.strokeStyle = '#ffd600';
  ctx.lineWidth = 2.2;
  ctx.beginPath();
  tempHistory.forEach((pt, i) => {
    const x = PAD + ((W-2*PAD)*(pt.time-t0)/(t1-t0||1));
    const y = H-PAD - ((H-2*PAD)*((pt.temp-minT)/(maxT-minT||1)));
    if (i===0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();
  // Plot setTemp
  ctx.strokeStyle = '#4fc3f7';
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  tempHistory.forEach((pt, i) => {
    const x = PAD + ((W-2*PAD)*(pt.time-t0)/(t1-t0||1));
    const y = H-PAD - ((H-2*PAD)*((pt.setTemp-minT)/(maxT-minT||1)));
    if (i===0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();
}

// Add modal for instructions if not present
if (!document.getElementById('instructionModal')) {
  const modal = document.createElement('div');
  modal.id = 'instructionModal';
  modal.style = 'display:none;position:fixed;z-index:1000;left:0;top:0;width:100vw;height:100vh;background:rgba(0,0,0,0.6);align-items:center;justify-content:center;';
  modal.innerHTML = `<div style="background:#232323;color:#ffe;padding:1.5em 2em;border-radius:12px;max-width:90vw;max-height:80vh;overflow:auto;box-shadow:0 4px 32px #000b;">
    <h2 id='instructionModalTitle'>Instructions</h2>
    <pre id='instructionModalText' style='white-space:pre-wrap;font-size:1.1em;'></pre>
    <button onclick="document.getElementById('instructionModal').style.display='none'">Close</button>
  </div>`;
  document.body.appendChild(modal);
}
function showInstructionModal(title, text) {
  document.getElementById('instructionModalTitle').textContent = title || 'Instructions';
  document.getElementById('instructionModalText').textContent = text || '';
  document.getElementById('instructionModal').style.display = 'flex';
}
