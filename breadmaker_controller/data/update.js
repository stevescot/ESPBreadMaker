// update.js - fetch and display firmware build info and upload status
window.addEventListener('DOMContentLoaded', function() {
  fetch('/api/firmware_info').then(r => r.json()).then(info => {
    const el = document.getElementById('fwBuildInfo');
    if (el && info.build) {
      el.textContent = 'Current firmware build: ' + info.build;
    }
  });

  // Intercept form submit to show upload/rebooting message
  var form = document.getElementById('updateForm');
  var msg = document.getElementById('fwUploadMsg');
  var uploadBtn = document.getElementById('uploadBtn');
  var fileInput = document.getElementById('firmwareFile');
  
  if (form) {
    form.addEventListener('submit', function(e) {
      e.preventDefault();
      
      var file = fileInput.files[0];
      if (!file) {
        msg.textContent = 'Please select a firmware file';
        msg.style.color = '#f44336';
        return;
      }
      
      if (!file.name.endsWith('.bin')) {
        msg.textContent = 'Please select a .bin file';
        msg.style.color = '#f44336';
        return;
      }
      
      if (file.size > 1024 * 1024) {
        msg.textContent = 'File too large (max 1MB)';
        msg.style.color = '#f44336';
        return;
      }
      
      uploadBtn.disabled = true;
      uploadBtn.textContent = 'Uploading...';
      msg.textContent = 'Uploading firmware... Please wait and do not refresh the page.';
      msg.style.color = '#1976d2';
      
      var formData = new FormData();
      formData.append('firmware', file);
      
      fetch('/update', {
        method: 'POST',
        body: formData
      })
      .then(response => {
        if (response.ok) {
          msg.textContent = 'Upload successful! Device is rebooting... Please wait 30 seconds.';
          msg.style.color = '#4CAF50';
          setTimeout(() => {
            window.location.href = '/';
          }, 30000);
        } else {
          throw new Error('Upload failed with status: ' + response.status);
        }
      })
      .catch(error => {
        msg.textContent = 'Upload failed: ' + error.message + '. Check troubleshooting tips below.';
        msg.style.color = '#f44336';
        uploadBtn.disabled = false;
        uploadBtn.textContent = 'Upload & Flash';
      });
    });
  }
});
