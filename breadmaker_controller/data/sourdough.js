// Sourdough Starter Management Logic
// Controls breadmaker hardware for starter prep: mixing, heating, alerts, and reminders

document.addEventListener('DOMContentLoaded', () => {
  const form = document.getElementById('sdForm');
  const feedback = document.getElementById('sd-feedback');
  const timerBlock = document.getElementById('sd-timer');
  let starterActive = false;
  let timerInterval = null;

  // Load saved plan
  const plan = JSON.parse(localStorage.getItem('sourdoughPlan') || '{}');
  for (const k in plan) {
    if (form.elements[k]) form.elements[k].value = plan[k];
  }

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
      if (mixCycle % 1800 === 0) { // every 30min
        await controlDevice('/motor', {on: 1});
        setTimeout(() => controlDevice('/motor', {on: 0}), 10000);
        feedback.textContent = 'Mixing starter...';
      }
      // Heating: maintain temp
      fetch('/status').then(r => r.json()).then(status => {
        if (status.temp < tempTarget - 0.5) controlDevice('/heater', {on: 1});
        else if (status.temp > tempTarget + 0.5) controlDevice('/heater', {on: 0});
      });
      // Timer/alert
      let msLeft = nextFeed - Date.now();
      if (msLeft <= 0) {
        await controlDevice('/buzz');
        feedback.textContent = 'Time to feed your starter!';
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
  };
});
