// Global flag to prevent multiple simultaneous saves
let isSaving = false;

function loadProgramsEditor(forceRefresh) {
  // Use shared cache if available, unless forceRefresh is true
  if (window.cachedPrograms && !forceRefresh) {
    window._programs = Array.isArray(window.cachedPrograms) ? window.cachedPrograms : (Array.isArray(window.cachedPrograms.programs) ? window.cachedPrograms.programs : []);
    renderPrograms(window._programs);
    return;
  }
  fetch('/api/programs')
    .then(r => r.json())
    .then(data => {
      window.cachedPrograms = data;
      window._programs = Array.isArray(data) ? data : (Array.isArray(data.programs) ? data.programs : []);
      renderPrograms(window._programs);
    });
}

let selectedProgramIdx = 0;

function renderPrograms(progs) {
  const container = document.getElementById('progList');
  container.innerHTML = '';

  // Render dropdown in the correct container, always visible
  let selectContainer = document.getElementById('programSelectContainer');
  if (selectContainer) {
    selectContainer.innerHTML = '';
    let dropdown = document.createElement('select');
    dropdown.id = 'programSelect';
    dropdown.style.marginRight = '1em';
    if (progs.length === 0) {
      let opt = document.createElement('option');
      opt.value = '';
      opt.textContent = 'No programs. Add one!';
      opt.disabled = true;
      opt.selected = true;
      dropdown.appendChild(opt);
      dropdown.disabled = true;
      selectedProgramIdx = 0;
    } else {
      progs.forEach((p, idx) => {
        const opt = document.createElement('option');
        opt.value = idx;
        opt.textContent = p.name || 'Program ' + (idx+1);
        dropdown.appendChild(opt);
      });
      if (selectedProgramIdx >= progs.length) {
        selectedProgramIdx = progs.length - 1;
      }
      dropdown.value = selectedProgramIdx;
      dropdown.disabled = false;
      dropdown.onchange = () => {
        selectedProgramIdx = parseInt(dropdown.value);
        renderPrograms(progs);
      };
    }
    selectContainer.appendChild(dropdown);
  }

  // Only render the selected program
  if (progs.length > 0 && progs[selectedProgramIdx]) {
    let html = '';
    const p = progs[selectedProgramIdx];
    const idx = selectedProgramIdx;
    const card = document.createElement('div');
    card.className = 'prog-card';
    html += `<h2>${p.name || 'Program ' + (idx+1)} <button data-delete="${idx}">Delete</button></h2>`;
    html += `<label>Name: <input type="text" data-idx="${idx}" data-field="name" value="${p.name||''}"></label><br>`;
    html += `<label>Notes/Recipe:<br><textarea data-idx="${idx}" data-field="notes" rows="7" style="width:99%;min-height:7em;resize:vertical;">${p.notes||''}</textarea></label>`;
    html += `<label>Fermentation Baseline Temp (&deg;C): <input type="number" data-idx="${idx}" data-field="fermentBaselineTemp" value="${p.fermentBaselineTemp||20}" style="width:4em;"></label>\n`;
    html += `<label>Fermentation Q10 Factor: <input type="number" step="0.01" data-idx="${idx}" data-field="fermentQ10" value="${p.fermentQ10||2.0}" style="width:4em;"></label>\n`;
    if (Array.isArray(p.customStages)) {
      const palette = [
        '#b0b0b0', '#ff9933', '#4caf50', '#2196f3', '#ffd600', '#bb8640', '#e57373', '#90caf9', '#ce93d8', '#80cbc4'
      ];
      html += `<div style='margin:1em 0 0.5em 0;'><b>Custom Stages:</b> <button type='button' data-addcustom='${idx}'>+ Add Custom Stage</button></div>`;
      p.customStages.forEach((st, cidx) => {
        const color = st.color || palette[cidx % palette.length];
        html += `<div class='custom-stage-card' style='background:${color};color:#232323;border:1px solid #bbb;padding:0.7em 1em;margin-bottom:0.7em;border-radius:7px;'>` +
          `<b>Stage ${cidx+1}:</b> ` +
          `<button type='button' data-movecustom='${idx},${cidx},up' title='Move Up' style='font-size:1.1em;margin-right:0.2em;'>&uarr;</button>` +
          `<button type='button' data-movecustom='${idx},${cidx},down' title='Move Down' style='font-size:1.1em;margin-right:0.5em;'>&darr;</button>` +
          `<input type='text' data-idx='${idx}' data-cidx='${cidx}' data-cfield='label' value='${st.label||''}' placeholder='Label'> ` +
          `<button type='button' data-delcustom='${idx},${cidx}'>Delete</button><br>` +
          `Duration: <input type='number' data-idx='${idx}' data-cidx='${cidx}' data-cfield='durationHours' value='${Math.floor((st.min||0)/60)}' style='width:3em;' min='0' max='23'> h ` +
          `<input type='number' data-idx='${idx}' data-cidx='${cidx}' data-cfield='durationMinutes' value='${(st.min||0)%60}' style='width:3em;' min='0' max='59'> min ` +
          `<span style='font-size:0.9em;color:#666;'>(Total: ${st.min||0} min)</span><br>` +
          `Temp (&deg;C): <input type='number' data-idx='${idx}' data-cidx='${cidx}' data-cfield='temp' value='${st.temp||0}' style='width:4em;' min='0' max='250'> ` +
          `<span style='font-size:0.9em;color:#666;'>${(st.temp||0) === 0 ? '(No heating)' : '(Heating active)'}</span><br>` +
          `Instructions:<br><textarea data-idx='${idx}' data-cidx='${cidx}' data-cfield='instructions' rows='2' style='width:99%;'>${st.instructions||''}</textarea><br>`;
        html += `<b>Mixing:</b> `;
        html += `<label><input type='checkbox' data-idx='${idx}' data-cidx='${cidx}' data-cfield='noMix'${st.noMix?' checked':''}> No Mixing in this stage</label>`;
        if (!st.noMix) {
          html += ` <button type='button' data-addmix='${idx},${cidx}'>+ Add Step</button><br>`;
          if (Array.isArray(st.mixPattern)) {
            st.mixPattern.forEach((mp, midx) => {
              html += `&nbsp; Mix: <input type='number' data-idx='${idx}' data-cidx='${cidx}' data-midx='${midx}' data-mfield='mixSec' value='${mp.mixSec||0}' style='width:4em;'> sec, ` +
                `Wait: <input type='number' data-idx='${idx}' data-cidx='${cidx}' data-midx='${midx}' data-mfield='waitSec' value='${mp.waitSec||0}' style='width:4em;'> sec, ` +
                `<span style='white-space:nowrap;'>Duration: <input type='number' data-idx='${idx}' data-cidx='${cidx}' data-midx='${midx}' data-mfield='durationSec' value='${mp.durationSec!=null?mp.durationSec:(mp.mixSec||0)+(mp.waitSec||0)}' style='width:4em;' min='0'> sec</span> ` +
                `<button type='button' data-delmix='${idx},${cidx},${midx}'>Delete</button><br>`;
            });
          }
        } else {
          html += `<br>`;
        }
        html += `<label><input type='checkbox' data-idx='${idx}' data-cidx='${cidx}' data-cfield='isFermentation'${st.isFermentation?' checked':''}> Is Fermentation Stage (adjust time by temp)</label><br>`;
        html += `</div>`;
      });
    } else {
      html += `<div><button type='button' data-addcustom='${idx}'>+ Add Custom Stage</button></div>`;
    }
    html += `<input type="hidden" data-idx="${idx}" data-field="id" value="${typeof p.id === 'number' ? p.id : ''}" readonly>`;
    card.innerHTML = html;
    container.appendChild(card);
    // delete
    card.querySelector('[data-delete]').onclick = e => {
      progs.splice(idx, 1);
      if (selectedProgramIdx >= progs.length) selectedProgramIdx = progs.length - 1;
      renderPrograms(progs);
    };
    // update fields
    card.querySelectorAll('input,textarea').forEach(inp => {
      if(inp.getAttribute('data-field')==='name') inp.maxLength = 31;
      inp.onchange = () => {
        const i = parseInt(inp.getAttribute('data-idx'));
        const fld = inp.getAttribute('data-field');
        let val = inp.value;
        if (inp.type === 'number' && inp.step && inp.step !== 'any')
          val = inp.valueAsNumber;
        if(fld==='name' && val.length>31) {
          alert('Name too long (max 31 chars)');
          inp.value = val.slice(0,31);
          val = inp.value;
        }
        progs[i][fld] = val;
      };
    });
    // Custom stage add/delete/edit logic
    card.querySelectorAll('[data-addcustom]').forEach(btn => {
      btn.onclick = () => {
        if (!progs[idx].customStages) progs[idx].customStages = [];
        progs[idx].customStages.push({
          label: '',
          min: 30,  // 30 minutes default
          temp: 0,  // No heating by default
          instructions: '',
          mixPattern: []
        });
        renderPrograms(progs);
      };
    });
    card.querySelectorAll('[data-delcustom]').forEach(btn => {
      const [pidx, cidx] = btn.getAttribute('data-delcustom').split(',').map(Number);
      btn.onclick = () => {
        progs[pidx].customStages.splice(cidx, 1);
        renderPrograms(progs);
      };
    });
    card.querySelectorAll('input[data-cfield],textarea[data-cfield]').forEach(inp => {
      if(inp.getAttribute('data-cfield')==='label') inp.maxLength = 24;
      if(inp.getAttribute('data-cfield')==='instructions') inp.maxLength = 200;
      inp.onchange = () => {
        const i = parseInt(inp.getAttribute('data-idx'));
        const c = parseInt(inp.getAttribute('data-cidx'));
        const fld = inp.getAttribute('data-cfield');
        let val = inp.value;
        if (inp.type === 'number') val = inp.valueAsNumber;
        if (inp.type === 'checkbox') val = inp.checked;
        if(fld==='label' && typeof val === 'string' && val.length>24) {
          alert('Stage label too long (max 24 chars)');
          inp.value = val.slice(0,24);
          val = inp.value;
        }
        if(fld==='instructions' && typeof val === 'string' && val.length>200) {
          alert('Instructions too long (max 200 chars)');
          inp.value = val.slice(0,200);
          val = inp.value;
        }
        if(fld === 'durationHours' || fld === 'durationMinutes') {
          const currentStage = progs[i].customStages[c];
          let hours = 0, minutes = 0;
          if(fld === 'durationHours') {
            hours = Math.max(0, Math.min(23, val || 0));
            inp.value = hours;
            minutes = currentStage.min % 60;
          } else {
            minutes = Math.max(0, Math.min(59, val || 0));
            inp.value = minutes;
            hours = Math.floor(currentStage.min / 60);
          }
          progs[i].customStages[c].min = hours * 60 + minutes;
          renderPrograms(progs);
          return;
        }
        progs[i].customStages[c][fld] = val;
        if(fld === 'temp') {
          renderPrograms(progs);
        }
      };
    });
    card.querySelectorAll('input[data-cfield="noMix"]').forEach(inp => {
      inp.onchange = () => {
        const i = parseInt(inp.getAttribute('data-idx'));
        const c = parseInt(inp.getAttribute('data-cidx'));
        progs[i].customStages[c].noMix = inp.checked;
        if (inp.checked) progs[i].customStages[c].mixPattern = [];
        renderPrograms(progs);
      };
    });
    card.querySelectorAll('input[data-cfield="isFermentation"]').forEach(inp => {
      inp.onchange = () => {
        const i = parseInt(inp.getAttribute('data-idx'));
        const c = parseInt(inp.getAttribute('data-cidx'));
        progs[i].customStages[c].isFermentation = inp.checked;
      };
    });
    card.querySelectorAll('[data-addmix]').forEach(btn => {
      const [pidx, cidx] = btn.getAttribute('data-addmix').split(',').map(Number);
      btn.onclick = () => {
        progs[pidx].customStages[cidx].mixPattern = progs[pidx].customStages[cidx].mixPattern || [];
        progs[pidx].customStages[cidx].mixPattern.push({mixSec:10,waitSec:10});
        progs[pidx].customStages[cidx].noMix = false;
        renderPrograms(progs);
      };
    });
    card.querySelectorAll('[data-delmix]').forEach(btn => {
      const [pidx, cidx, midx] = btn.getAttribute('data-delmix').split(',').map(Number);
      btn.onclick = () => {
        progs[pidx].customStages[cidx].mixPattern.splice(midx, 1);
        renderPrograms(progs);
      };
    });
    card.querySelectorAll('input[data-mfield]').forEach(inp => {
      inp.onchange = () => {
        const i = parseInt(inp.getAttribute('data-idx'));
        const c = parseInt(inp.getAttribute('data-cidx'));
        const m = parseInt(inp.getAttribute('data-midx'));
        const fld = inp.getAttribute('data-mfield');
        let val = inp.valueAsNumber;
        progs[i].customStages[c].mixPattern[m][fld] = val;
      };
    });
    card.querySelectorAll('[data-movecustom]').forEach(btn => {
      const [pidx, cidx, dir] = btn.getAttribute('data-movecustom').split(',');
      btn.onclick = () => {
        const arr = progs[pidx].customStages;
        const i = parseInt(cidx);
        if (dir === 'up' && i > 0) {
          [arr[i-1], arr[i]] = [arr[i], arr[i-1]];
        } else if (dir === 'down' && i < arr.length - 1) {
          [arr[i], arr[i+1]] = [arr[i+1], arr[i]];
        }
        renderPrograms(progs);
      };
    });
  }
  // Add program
  if(document.getElementById('addProg')) {
    document.getElementById('addProg').onclick = () => {
      const name = prompt('New program name:');
      if (name && !progs.some(p => p.name === name)) {
        // Find the next available id (max id + 1)
        let nextId = 0;
        if (progs.length > 0) {
          nextId = Math.max(...progs.map(p => typeof p.id === 'number' ? p.id : -1)) + 1;
        }
        progs.push({
          id: nextId,
          name,
          autolyseTime: 0, mixTime: 5, bulkTemp: 25, bulkTime: 60, knockDownTime: 2,
          riseTemp: 30, riseTime: 40, bake1Temp: 170, bake1Time: 15,
          bake2Temp: 210, bake2Time: 25, coolTime: 10
        });
        selectedProgramIdx = progs.length - 1;
        renderPrograms(progs);
      }
    };
  }

  // (Removed duplicate id field rendering; already handled in main block)
  // Save all
  if(document.getElementById('saveAll'))
    document.getElementById('saveAll').onclick = async () => {
      if (isSaving) {
        alert('Save in progress. Please wait.');
        return;
      }
      isSaving = true;
      
      const errors = validateProgramLengths(progs);
      if(errors.length) {
        alert('Cannot save. Fix these errors:\n'+errors.join('\n'));
        isSaving = false;
        return;
      }
      
      // Check payload size to prevent server overload
      const jsonString = JSON.stringify(progs);
      const sizeKB = Math.round(jsonString.length / 1024);
      
      if (sizeKB > 500) { // 500KB limit
        if (!confirm(`Large program data (${sizeKB}KB). This may take a while to save. Continue?`)) {
          isSaving = false;
          return;
        }
      }
      
      const saveBtn = document.getElementById('saveAll');
      const originalText = saveBtn.textContent;
      
      try {
        // Show loading state
        saveBtn.textContent = `Saving ${progs.length} programs...`;
        saveBtn.disabled = true;
        saveBtn.style.opacity = '0.6';
        
        // Add timeout and better error handling
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 30000); // 30 second timeout
        
        console.log(`Saving ${progs.length} programs (${sizeKB}KB)...`);
        
        // Use file upload endpoint instead of /api/programs
        const formData = new FormData();
        const blob = new Blob([jsonString], { type: 'application/json' });
        formData.append('file', blob, 'programs.json');
        
        const response = await fetch('/api/upload', {
          method: 'POST',
          body: formData,
          signal: controller.signal
        });
        
        clearTimeout(timeoutId);
        
        if (!response.ok) {
          throw new Error(`Server error: ${response.status} ${response.statusText}`);
        }
        
        const responseText = await response.text();
        
        if (responseText === 'Upload complete') {
          console.log(`✅ Save successful: ${progs.length} programs (${sizeKB}KB)`);
          alert(`Programs saved successfully! (${progs.length} programs, ${sizeKB}KB)`);
          // Refresh shared cache after save
          window.cachedPrograms = JSON.parse(JSON.stringify(progs));
          // Reload programs from server to ensure consistency
          setTimeout(() => {
            window.cachedPrograms = null; // Clear cache
            loadProgramsEditor(true); // Force refresh
          }, 500);
        } else {
          throw new Error('Unexpected server response: ' + responseText);
        }
        
      } catch (error) {
        console.error('❌ Save error:', error);
        
        if (error.name === 'AbortError') {
          alert('Save operation timed out. The server may be overloaded. Please try again.');
        } else if (error.message.includes('Failed to fetch')) {
          alert('Network error: Unable to connect to the server. Check your connection and try again.');
        } else {
          alert('Error saving programs: ' + error.message);
        }
      } finally {
        // Restore button state and clear flag
        isSaving = false;
        saveBtn.textContent = originalText;
        saveBtn.disabled = false;
        saveBtn.style.opacity = '1';
      }
    };
}

window.addProgramFromJson = function(prog) {
  if (!window._programs) window._programs = [];
  window._programs.push(prog);
  renderPrograms(window._programs, window._programs.length - 1);
};

function validateProgramLengths(progs) {
  const NAME_MAX = 31;
  const INSTR_MAX = 200; // Example, adjust as needed
  const LABEL_MAX = 24;  // Example, adjust as needed
  let errors = [];
  progs.forEach((p, idx) => {
    if (p.name && p.name.length > NAME_MAX) {
      errors.push(`Program ${idx+1} name too long (max ${NAME_MAX})`);
    }
    if (Array.isArray(p.customStages)) {
      p.customStages.forEach((st, cidx) => {
        if (st.label && st.label.length > LABEL_MAX) {
          errors.push(`Program ${idx+1} stage ${cidx+1} label too long (max ${LABEL_MAX})`);
        }
        if (st.instructions && st.instructions.length > INSTR_MAX) {
          errors.push(`Program ${idx+1} stage ${cidx+1} instructions too long (max ${INSTR_MAX})`);
        }
      });
    }
  });
  return errors;
}
