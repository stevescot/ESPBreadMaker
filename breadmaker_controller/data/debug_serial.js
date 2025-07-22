// Debug serial API for config.html
self.addEventListener('DOMContentLoaded', () => {
  const debugSerialCheckbox = document.getElementById('debugSerialCheckbox');
  const debugSerialStatus = document.getElementById('debugSerialStatus');
  const safetyEnabledCheckbox = document.getElementById('safetyEnabledCheckbox');
  const safetyEnabledStatus = document.getElementById('safetyEnabledStatus');
  
  function fetchDebugSerial() {
    fetch('/api/settings')
      .then(r => r.json())
      .then(d => {
        debugSerialCheckbox.checked = !!d.debugSerial;
        debugSerialStatus.textContent = d.debugSerial ? 'Enabled' : 'Disabled';
        
        safetyEnabledCheckbox.checked = !!d.safetyEnabled;
        safetyEnabledStatus.textContent = d.safetyEnabled ? 'Enabled' : 'DISABLED (Test Mode)';
      });
  }
  
  debugSerialCheckbox.onchange = function() {
    fetch('/api/settings', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({debugSerial: debugSerialCheckbox.checked})
    })
    .then(r => r.json())
    .then(d => {
      debugSerialStatus.textContent = d.debugSerial ? 'Enabled' : 'Disabled';
    });
  };
  
  safetyEnabledCheckbox.onchange = function() {
    console.log('Safety checkbox changed to:', safetyEnabledCheckbox.checked);
    
    // Use simple toggle endpoint instead of complex settings POST
    fetch('/api/safety/toggle', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'}
    })
    .then(r => {
      console.log('Response status:', r.status);
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.json();
    })
    .then(d => {
      console.log('Response data:', d);
      safetyEnabledCheckbox.checked = d.safetyEnabled;
      safetyEnabledStatus.textContent = d.safetyEnabled ? 'Enabled' : 'DISABLED (Test Mode)';
    })
    .catch(e => {
      console.error('Safety toggle failed:', e);
      safetyEnabledStatus.textContent = 'Error: ' + e.message;
      // Revert checkbox to previous state
      safetyEnabledCheckbox.checked = !safetyEnabledCheckbox.checked;
    });
  };
  
  fetchDebugSerial();
});
