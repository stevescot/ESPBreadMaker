// update.js - fetch and display firmware build info and upload status
window.addEventListener('DOMContentLoaded', function() {
  fetch('/api/firmware_info').then(r => r.json()).then(info => {
    const el = document.getElementById('fwBuildInfo');
    if (el && info.build) {
      el.textContent = 'Current firmware build: ' + info.build;
    }
  });

  // Intercept form submit to show upload/rebooting message
  var form = document.querySelector('form[action="/update"]');
  if (form) {
    form.addEventListener('submit', function(e) {
      var msg = document.getElementById('fwUploadMsg');
      if (!msg) {
        msg = document.createElement('div');
        msg.id = 'fwUploadMsg';
        msg.style = 'margin:10px 0;color:#1976d2;font-weight:bold;';
        form.parentNode.insertBefore(msg, form.nextSibling);
      }
      msg.textContent = 'Uploading firmware... Please wait.';
      setTimeout(function() {
        msg.textContent = 'Upload complete! Device is rebooting. If the page does not reload, wait 10–30 seconds and refresh.';
      }, 2000);
    });
  }
});
