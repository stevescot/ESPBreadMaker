function loadProgramsEditor() {
  fetch('/api/programs')
    .then(r => r.json())
    .then(data => {
      // Expect array format
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
    const p = progs[selectedProgramIdx];
    const idx = selectedProgramIdx;
    const card = document.createElement('div');
    card.className = 'prog-card';
    let html = `<h2>${p.name || 'Program ' + (idx+1)} <button data-delete="${idx}">Delete</button></h2>`;
    // Only show custom stages UI, remove all classic stage fields
    html += `<label>Name: <input type="text" data-idx="${idx}" data-field="name" value="${p.name||''}"></label><br>`;
    html += `<label>Notes/Recipe:<br><textarea data-idx="${idx}" data-field="notes" rows="7" style="width:99%;min-height:7em;resize:vertical;">${p.notes||''}</textarea></label>`;
    // Custom stages UI
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
          `Duration (min): <input type='number' data-idx='${idx}' data-cidx='${cidx}' data-cfield='min' value='${st.min||0}' style='width:4em;'> ` +
          `Temp (°C): <input type='number' data-idx='${idx}' data-cidx='${cidx}' data-cfield='temp' value='${st.temp||0}' style='width:4em;'><br>` +
          `Heater: <select data-idx='${idx}' data-cidx='${cidx}' data-cfield='heater'><option value='on'${st.heater==='on'?' selected':''}>On</option><option value='off'${st.heater==='off'?' selected':''}>Off</option></select> ` +
          `Light: <select data-idx='${idx}' data-cidx='${cidx}' data-cfield='light'><option value='on'${st.light==='on'?' selected':''}>On</option><option value='off'${st.light==='off'?' selected':''}>Off</option></select> ` +
          `Buzzer: <select data-idx='${idx}' data-cidx='${cidx}' data-cfield='buzzer'><option value='on'${st.buzzer==='on'?' selected':''}>On</option><option value='off'${st.buzzer==='off'?' selected':''}>Off</option></select><br>` +
          `Instructions:<br><textarea data-idx='${idx}' data-cidx='${cidx}' data-cfield='instructions' rows='2' style='width:99%;'>${st.instructions||''}</textarea><br>` +
          `<b>Mix Pattern:</b> <button type='button' data-addmix='${idx},${cidx}'>+ Add Step</button><br>`;
        if (Array.isArray(st.mixPattern)) {
          st.mixPattern.forEach((mp, midx) => {
            html += `&nbsp; <select data-idx='${idx}' data-cidx='${cidx}' data-midx='${midx}' data-mfield='action'><option value='mix'${mp.action==='mix'?' selected':''}>Mix</option><option value='off'${mp.action==='off'?' selected':''}>Off</option></select> ` +
              `Duration (sec): <input type='number' data-idx='${idx}' data-cidx='${cidx}' data-midx='${midx}' data-mfield='durationSec' value='${mp.durationSec||0}' style='width:5em;'> ` +
              `<button type='button' data-delmix='${idx},${cidx},${midx}'>Delete</button><br>`;
          });
        }
        html += `</div>`;
      });
    } else {
      html += `<div><button type='button' data-addcustom='${idx}'>+ Add Custom Stage</button></div>`;
    }
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
        progs[idx].customStages.push({label:'',min:1,temp:25,heater:'off',light:'off',buzzer:'off',instructions:'',mixPattern:[]});
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
    card.querySelectorAll('input[data-cfield],select[data-cfield],textarea[data-cfield]').forEach(inp => {
      if(inp.getAttribute('data-cfield')==='label') inp.maxLength = 24;
      if(inp.getAttribute('data-cfield')==='instructions') inp.maxLength = 200;
      inp.onchange = () => {
        const i = parseInt(inp.getAttribute('data-idx'));
        const c = parseInt(inp.getAttribute('data-cidx'));
        const fld = inp.getAttribute('data-cfield');
        let val = inp.value;
        if (inp.type === 'number') val = inp.valueAsNumber;
        if(fld==='label' && val.length>24) {
          alert('Stage label too long (max 24 chars)');
          inp.value = val.slice(0,24);
          val = inp.value;
        }
        if(fld==='instructions' && val.length>200) {
          alert('Instructions too long (max 200 chars)');
          inp.value = val.slice(0,200);
          val = inp.value;
        }
        progs[i].customStages[c][fld] = val;
      };
    });
    card.querySelectorAll('[data-addmix]').forEach(btn => {
      const [pidx, cidx] = btn.getAttribute('data-addmix').split(',').map(Number);
      btn.onclick = () => {
        progs[pidx].customStages[cidx].mixPattern = progs[pidx].customStages[cidx].mixPattern || [];
        progs[pidx].customStages[cidx].mixPattern.push({action:'mix',durationSec:10});
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
    card.querySelectorAll('input[data-mfield],select[data-mfield]').forEach(inp => {
      inp.onchange = () => {
        const i = parseInt(inp.getAttribute('data-idx'));
        const c = parseInt(inp.getAttribute('data-cidx'));
        const m = parseInt(inp.getAttribute('data-midx'));
        const fld = inp.getAttribute('data-mfield');
        let val = inp.value;
        if (inp.type === 'number') val = inp.valueAsNumber;
        progs[i].customStages[c].mixPattern[m][fld] = val;
      };
    });
    // Move up/down logic
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
  if(document.getElementById('addProg'))
    document.getElementById('addProg').onclick = () => {
      const name = prompt('New program name:');
      if (name && !progs.some(p => p.name === name)) {
        progs.push({
          name,
          autolyseTime: 0, mixTime: 5, bulkTemp: 25, bulkTime: 60, knockDownTime: 2,
          riseTemp: 30, riseTime: 40, bake1Temp: 170, bake1Time: 15,
          bake2Temp: 210, bake2Time: 25, coolTime: 10
        });
        selectedProgramIdx = progs.length - 1;
        renderPrograms(progs);
      }
    };
  // Save all
  if(document.getElementById('saveAll'))
    document.getElementById('saveAll').onclick = () => {
      const errors = validateProgramLengths(progs);
      if(errors.length) {
        alert('Cannot save. Fix these errors:\n'+errors.join('\n'));
        return;
      }
      fetch('/api/programs', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(progs)
      }).then(r => r.json()).then(resp => {
        if (resp.status === 'ok') alert('Programs saved');
        else alert('Error saving');
      });
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
