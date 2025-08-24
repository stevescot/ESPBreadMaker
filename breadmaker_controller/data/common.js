// common.js - Shared UI logic for breadmaker controller
// Provides: device status icons, utility functions, and status polling

// ---- Device Status Icons ----
const ICONS = {
  motor: String.raw`<svg id="mixIcon" class="icon" viewBox="0 0 40 40"><circle id="mixGroup" cx="20" cy="20" r="16" fill="#888"/><text x="20" y="25" text-anchor="middle" font-size="16" fill="#fff">M</text></svg>`,
  heater: String.raw`<svg id="heaterIcon" class="icon" viewBox="0 0 40 40"><rect x="10" y="10" width="20" height="20" rx="6" fill="#888"/><text x="20" y="25" text-anchor="middle" font-size="16" fill="#fff">H</text></svg>`,
  light: String.raw`<svg id="lightIcon" class="icon" viewBox="0 0 40 40"><circle id="bulb" cx="20" cy="20" r="12" fill="#888"/><text x="20" y="25" text-anchor="middle" font-size="16" fill="#fff">L</text></svg>`,
  buzzer: String.raw`<svg id="buzzerIcon" class="icon" viewBox="0 0 40 40"><ellipse id="buzzBody" cx="20" cy="20" rx="12" ry="8" fill="#888"/><text x="20" y="25" text-anchor="middle" font-size="16" fill="#fff">B</text></svg>`
};

function renderIcons(status, iconRow) {
  if (!iconRow) return;
  iconRow.innerHTML = '';
  [
    {key: 'motor', label: 'Motor', on: status && status.motor},
    {key: 'heater', label: 'Heater', on: status && status.heater},
    {key: 'light', label: 'Light', on: status && status.light},
    {key: 'buzzer', label: 'Buzzer', on: status && status.buzzer}
  ].forEach(dev => {
    const div = document.createElement('div');
    div.className = 'dev-icon-box ' + (dev.on ? 'on' : 'off');
    div.innerHTML = ICONS[dev.key] + `<div class="dev-label">${dev.label}</div>`;
    if (dev.on) div.querySelector('.icon').style.filter = 'brightness(1.5)';
    iconRow.appendChild(div);
  });
}

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

// ---- Non-Stacking Status Polling Manager ----
// Prevents multiple concurrent status requests from stacking up
class StatusPollingManager {
  constructor() {
    this.isPolling = false;
    this.pollInterval = null;
    this.callbacks = new Set();
    this.lastStatus = null;
    this.pollIntervalMs = 3000; // Default interval
  }

  // Add a callback function that will receive status updates
  addCallback(callback) {
    this.callbacks.add(callback);
  }

  // Remove a callback function
  removeCallback(callback) {
    this.callbacks.delete(callback);
  }

  // Start polling with the specified interval
  startPolling(intervalMs = 3000) {
    this.pollIntervalMs = intervalMs;
    
    // Clear any existing interval
    if (this.pollInterval) {
      clearInterval(this.pollInterval);
    }
    
    // Start immediate poll
    this.pollOnce();
    
    // Set up interval for future polls
    this.pollInterval = setInterval(() => {
      this.pollOnce();
    }, this.pollIntervalMs);
    
    console.log(`[STATUS-POLL] Started polling every ${intervalMs}ms`);
  }

  // Stop polling
  stopPolling() {
    if (this.pollInterval) {
      clearInterval(this.pollInterval);
      this.pollInterval = null;
    }
    console.log('[STATUS-POLL] Stopped polling');
  }

  // Perform a single poll (only if not already polling)
  async pollOnce() {
    if (this.isPolling) {
      console.log('[STATUS-POLL] Skipping poll - previous request still in progress');
      return this.lastStatus; // Return cached status
    }

    this.isPolling = true;
    
    try {
      const response = await fetch('/status');
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }
      
      const status = await response.json();
      this.lastStatus = status;
      
      // Call all registered callbacks with the new status
      this.callbacks.forEach(callback => {
        try {
          callback(status);
        } catch (error) {
          console.error('[STATUS-POLL] Callback error:', error);
        }
      });
      
      return status;
    } catch (error) {
      console.error('[STATUS-POLL] Failed to fetch status:', error);
      // Still call callbacks with null status to let them handle the error
      this.callbacks.forEach(callback => {
        try {
          callback(null, error);
        } catch (callbackError) {
          console.error('[STATUS-POLL] Callback error during error handling:', callbackError);
        }
      });
      return null;
    } finally {
      this.isPolling = false;
    }
  }

  // Get the last cached status without making a new request
  getLastStatus() {
    return this.lastStatus;
  }
}

// Global status polling manager instance
window.statusPollingManager = new StatusPollingManager();

// ---- Status Polling ----
function pollStatusAndRenderIcons(iconRow, intervalMs=2000) {
  function renderIconsCallback(status) {
    if (status) {
      renderIcons(status, iconRow);
    }
  }
  
  // Add callback to global manager
  window.statusPollingManager.addCallback(renderIconsCallback);
  
  // Start polling if not already started, or update interval if needed
  if (!window.statusPollingManager.pollInterval || window.statusPollingManager.pollIntervalMs !== intervalMs) {
    window.statusPollingManager.startPolling(intervalMs);
  }
  
  // Return a cleanup function
  return () => {
    window.statusPollingManager.removeCallback(renderIconsCallback);
  };
}

// ---- Shared Device Icons ----
// Returns HTML string for a device icon (motor, heater, light, buzzer)
function getDeviceIconHtml(type) {
  switch (type) {
    case 'motor':
      return `<svg viewBox="0 0 32 32" width="32" height="32"><g id="mixGroup" style="transform-origin: 16px 16px;"><circle cx="16" cy="16" r="13" stroke="#888" stroke-width="3" fill="none"/><rect x="15" y="4" width="2" height="10" rx="1" fill="#888"/></g></svg>`;
    case 'heater':
      return `<svg viewBox="0 0 32 32" width="32" height="32"><path id="flamePath" d="M16 29C8 22 12 14 16 3C20 14 24 22 16 29Z" fill="#888"/></svg>`;
    case 'light':
      return `<svg viewBox="0 0 32 32" width="32" height="32"><ellipse id="bulb" cx="16" cy="17" rx="8" ry="10" fill="#888" /><rect x="13" y="26" width="6" height="4" rx="2" fill="#444"/></svg>`;
    case 'buzzer':
      return `<svg viewBox="0 0 32 32" width="32" height="32"><polygon id="buzzBody" points="8,20 16,20 22,27 22,5 16,12 8,12" fill="#888"/><path id="buzzWave" d="M25 12 Q29 16 25 20" stroke="#888" stroke-width="2" fill="none"/></svg>`;
    default:
      return '';
  }
}
