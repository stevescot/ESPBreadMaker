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
    // Use GET-based endpoint to avoid POST connection reset issues
    const enabled = debugSerialCheckbox.checked ? 'true' : 'false';
    fetch(`/api/settings/debug?enabled=${enabled}`)
    .then(r => r.json())
    .then(d => {
      debugSerialStatus.textContent = d.debugSerial ? 'Enabled' : 'Disabled';
      console.log('Debug serial updated:', d);
    })
    .catch(e => {
      console.error('Debug serial update failed:', e);
      debugSerialStatus.textContent = 'Error: ' + e.message;
      // Revert checkbox to previous state
      debugSerialCheckbox.checked = !debugSerialCheckbox.checked;
    });
  };
  
  safetyEnabledCheckbox.onchange = function() {
    console.log('Safety checkbox changed to:', safetyEnabledCheckbox.checked);
    
    // Use GET-based toggle endpoint to avoid POST crash
    fetch('/api/safety/toggle-get', {
      method: 'GET'
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
