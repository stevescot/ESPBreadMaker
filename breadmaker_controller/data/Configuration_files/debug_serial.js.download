// Debug serial API for config.html
self.addEventListener('DOMContentLoaded', () => {
  const debugSerialCheckbox = document.getElementById('debugSerialCheckbox');
  const debugSerialStatus = document.getElementById('debugSerialStatus');
  function fetchDebugSerial() {
    fetch('/api/settings')
      .then(r => r.json())
      .then(d => {
        debugSerialCheckbox.checked = !!d.debugSerial;
        debugSerialStatus.textContent = d.debugSerial ? 'Enabled' : 'Disabled';
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
  fetchDebugSerial();
});
