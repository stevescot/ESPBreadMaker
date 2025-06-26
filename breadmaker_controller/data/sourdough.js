// Sourdough Starter Management Logic
// Controls breadmaker hardware for starter prep: mixing, heating, alerts, and reminders

document.addEventListener('DOMContentLoaded', () => {
  const form = document.getElementById('sdForm');
  const feedback = document.getElementById('sd-feedback');
  const timerBlock = document.getElementById('sd-timer');
  const planSummary = document.getElementById('planSummary');
  const iconRow = document.getElementById('iconRow');
  let starterActive = false;
  let timerInterval = null;

  // Load saved plan
  function getPlan() {
    return JSON.parse(localStorage.getItem('sourdoughPlan') || '{}');
  }
  const plan = getPlan();
  for (const k in plan) {
    if (form.elements[k]) form.elements[k].value = plan[k];
  }

  // Show plan summary
  function showPlanSummary() {
    const p = getPlan();
    if (!p.name && !p.flour && !p.water && !p.keep && !p.interval && !p.temp) {
      planSummary.textContent = 'No starter plan saved.';
      return;
    }
    // Calculate next feed time (if running, else now + interval)
    let now = new Date();
    let intervalH = parseInt(p.interval) || 24;
    let start = new Date(now.getTime() + intervalH * 3600 * 1000);
    let end = new Date(start.getTime() + 30 * 60000); // 30min event
    function toGCalDate(dt) {
      return dt.toISOString().replace(/[-:]/g, '').replace(/\.\d+Z$/, 'Z');
    }
    let gcalUrl = `https://calendar.google.com/calendar/render?action=TEMPLATE&text=${encodeURIComponent('Feed Starter: ' + (p.name||''))}` +
      `&dates=${toGCalDate(start)}/${toGCalDate(end)}` +
      `&details=${encodeURIComponent('Flour: ' + (p.flour||'-') + 'g, Water: ' + (p.water||'-') + 'g, Keep: ' + (p.keep||'-') + 'g, Temp: ' + (p.temp||'-') + '°C')}`;
    planSummary.innerHTML = String.raw`<b>Plan:</b><br>
      Starter: <b>${p.name||'-'}</b><br>
      Flour: <b>${p.flour||'-'}</b> g, Water: <b>${p.water||'-'}</b> g<br>
      Keep: <b>${p.keep||'-'}</b> g, Feed every <b>${p.interval||'-'}</b> h<br>
      Fermentation Temp: <b>${p.temp||'-'}</b> °C<br>
      <a href="${gcalUrl}" target="_blank" style="display:inline-block;margin-top:0.7em;background:#4285F4;color:#fff;padding:0.5em 1em;border-radius:4px;text-decoration:none;font-weight:bold;">Add Next Feed to Google Calendar</a>`;
  }
  showPlanSummary();

  // Helper: AJAX call to backend
  function controlDevice(path, params = {}) {
    const url = path + (Object.keys(params).length ? '?' + new URLSearchParams(params) : '');
    return fetch(url).then(r => r.text());
  }

  // Start/stop starter management
  document.getElementById('sd-start').onclick = async () => {
    if (starterActive) {
      // Stop all outputs
      await controlDevice('/motor', {on: 0});
      await controlDevice('/heater', {on: 0});
      await controlDevice('/light', {on: 0});
      await controlDevice('/buzz');
      starterActive = false;
      feedback.textContent = 'Starter management stopped.';
      document.getElementById('sd-start').textContent = 'Start Starter Prep';
      clearInterval(timerInterval);
      timerBlock.textContent = '';
      return;
    }
    // Start outputs
    await controlDevice('/light', {on: 1});
    feedback.textContent = 'Starter management started!';
    document.getElementById('sd-start').textContent = 'Stop Starter Prep';
    starterActive = true;
    // Begin mixing/heating cycles
    let mixCycle = 0;
    let mixOn = false;
    let tempTarget = parseFloat(form.elements['temp'].value) || 25;
    let interval = parseInt(form.elements['interval'].value) || 24;
    let nextFeed = Date.now() + interval * 3600 * 1000;
    timerBlock.textContent = '';
    // Mixing: burst every 30min for 10s
    timerInterval = setInterval(async () => {
      if (!starterActive) return;
      // Mixing burst
      if (mixCycle % 1800 === 0) {
        await controlDevice('/motor', {on: 1});
        setTimeout(() => controlDevice('/motor', {on: 0}), 10000);
      }
      // Heating: maintain temp
      fetch('/status').then(r => r.json()).then(status => {
        if (status.temp !== undefined && tempTarget) {
          if (status.temp < tempTarget - 0.5) controlDevice('/heater', {on: 1});
          else if (status.temp > tempTarget + 0.5) controlDevice('/heater', {on: 0});
        }
        // Use shared renderIcons from common.js
        renderIcons(status);
      });
      // Timer/alert
      let msLeft = nextFeed - Date.now();
      if (msLeft <= 0) {
        feedback.textContent = 'Time to feed your starter!';
        await controlDevice('/buzz');
        nextFeed = Date.now() + interval * 3600 * 1000;
      }
      // Show countdown
      let h = Math.floor(msLeft / 3600000);
      let m = Math.floor((msLeft % 3600000) / 60000);
      let s = Math.floor((msLeft % 60000) / 1000);
      timerBlock.textContent = `Next feed in: ${h}h ${m}m ${s}s`;
      mixCycle++;
    }, 1000);
  };

  // Save plan
  form.onsubmit = function(e) {
    e.preventDefault();
    const data = Object.fromEntries(new FormData(form).entries());
    localStorage.setItem('sourdoughPlan', JSON.stringify(data));
    feedback.textContent = 'Starter plan saved locally!';
    showPlanSummary();
  };

  // Initial status icon render
  // Poll status every 2s to update icons
  setInterval(() => {
    fetch('/status').then(r => r.json()).then(renderIcons);
  }, 2000);
  // Initial icon render
  fetch('/status').then(r => r.json()).then(renderIcons);
});
