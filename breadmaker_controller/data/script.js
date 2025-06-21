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
function showStartAtStatus(msg) {
  let el = document.getElementById('startAtStatus');
  if (el) el.textContent = msg || '';
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
    s.programList.forEach(name => {
      let opt = document.createElement('option');
      opt.value = name;
      opt.textContent = name;
      if (s.program == name) opt.selected = true;
      select.appendChild(opt);
    });
    select.onchange = function () {
      fetch('/select?name=' + encodeURIComponent(this.value)).then(updateStatus);
    };
  }
  let curProg = document.getElementById('currentProg');
  if (curProg) curProg.textContent = s.program || "";
}

// ---- Update Status ----
function updateStatus() {
  fetch('/status')
    .then(r => r.json())
    .then(s => {
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
        fetch('/api/programs')
          .then(r => r.json())
          .then(data => {
            const prog = data.programs && data.programs[s.program];
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
      if (document.getElementById('stage')) document.getElementById('stage').textContent = s.stage;
      if (document.getElementById('temp')) document.getElementById('temp').innerHTML = (typeof s.temp === 'number' ? s.temp.toFixed(1) : '--') + '&deg;C';
      if (document.getElementById('heater')) document.getElementById('heater').textContent = s.heater > 0 ? 'ON' : 'OFF';
      if (document.getElementById('motor')) document.getElementById('motor').textContent = s.motor ? 'ON' : 'OFF';
      if (document.getElementById('light')) document.getElementById('light').textContent = s.light ? 'ON' : 'OFF';
      if (document.getElementById('buzzer')) document.getElementById('buzzer').textContent = s.buzzer ? 'ON' : 'OFF';

      // ---- Icon states/animation ----
      let mixIcon = document.getElementById('mixIcon');
      let heaterIcon = document.getElementById('heaterIcon');
      let lightIcon = document.getElementById('lightIcon');
      let buzzerIcon = document.getElementById('buzzerIcon');
      if (s.motor) mixIcon.classList.add('spin'); else mixIcon.classList.remove('spin');
      heaterIcon.className = 'icon ' + (s.heater > 0 ? 'heat-on' : 'heat-off');
      lightIcon.className = 'icon ' + (s.light ? 'light-on' : 'light-off');
      buzzerIcon.className = 'icon ' + (s.buzzer ? 'buzz-on' : 'buzz-off');
      showStageSvg(s.stage);
    })
    .catch(() => {
      // Show as disconnected/offline if needed
      document.getElementById('programReadyAt').textContent = "";
      document.getElementById('stageReadyAt').textContent = "";
      showStartAtStatus("");
      
    });
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
    document.getElementById('btnPrograms').onclick = () => location.href = '/programs';
  if (document.getElementById('btnCalibrate'))
    document.getElementById('btnCalibrate').onclick = () => location.href = '/calibrate';
  if (document.getElementById('btnUpdate'))
    document.getElementById('btnUpdate').onclick = () => location.href = '/update';

  // Set "start at" logic:
  if (document.getElementById('btnSetStartAt')) {
    document.getElementById('btnSetStartAt').onclick = function() {
      const timeVal = document.getElementById('startAt').value; // format: "HH:MM"
      if (!timeVal) return alert("Pick a time!");
      fetch('/setStartAt?time=' + encodeURIComponent(timeVal)).then(updateStatus);
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
