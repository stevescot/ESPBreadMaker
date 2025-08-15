// Simple patch to fix elapsed/remaining time display
// This will be injected into the page to override the progress bar display

function fixElapsedRemainingDisplay() {
  if (!lastStatus) return;
  
  const s = lastStatus;
  if (s.running && s.totalProgramDuration && s.elapsedTime && s.remainingTime) {
    // Find the elapsed/remaining display in the progress bar
    const progressDiv = document.getElementById('programProgressBar');
    if (progressDiv) {
      const elapsedDiv = progressDiv.querySelector('div[style*="color:#ffd600"]');
      if (elapsedDiv) {
        const elapsedHours = Math.floor(s.elapsedTime / 3600);
        const elapsedMins = Math.floor((s.elapsedTime % 3600) / 60);
        const totalHours = Math.floor(s.totalProgramDuration / 3600);
        const totalMins = Math.floor((s.totalProgramDuration % 3600) / 60);
        const remainingHours = Math.floor(s.remainingTime / 3600);
        const remainingMins = Math.floor((s.remainingTime % 3600) / 60);
        
        let elapsedStr = '';
        if (elapsedHours > 0) elapsedStr += elapsedHours + 'h ';
        elapsedStr += elapsedMins + 'm';
        
        let totalStr = '';
        if (totalHours > 0) totalStr += totalHours + 'h ';
        totalStr += totalMins + 'm';
        
        let remainingStr = '';
        if (remainingHours > 0) remainingStr += remainingHours + 'h ';
        remainingStr += remainingMins + 'm';
        
        elapsedDiv.innerHTML = `Elapsed: ${elapsedStr} / ${totalStr} &nbsp; Remaining: ${remainingStr}`;
      }
    }
  }
}

// Override the original function
if (typeof window.originalRenderProgramProgressBar === 'undefined') {
  window.originalRenderProgramProgressBar = window.renderProgramProgressBar;
}

window.renderProgramProgressBar = function(s) {
  // Call original function first
  if (window.originalRenderProgramProgressBar) {
    window.originalRenderProgramProgressBar(s);
  }
  // Then apply our fix
  setTimeout(fixElapsedRemainingDisplay, 10);
};

// Apply fix on page load
document.addEventListener('DOMContentLoaded', function() {
  setTimeout(fixElapsedRemainingDisplay, 1000);
});

console.log('Elapsed/Remaining time display fix loaded');
