function loadProgramsEditor() {
  fetch('/api/programs')
    .then(r => r.json())
    .then(data => {
      window._programs = data.programs || {};
      window._calibration = data.calibration || {};
      renderPrograms(window._programs);
    });
}

function renderPrograms(progs) {
  const container = document.getElementById('progList');
  container.innerHTML = '';
  for (const name in progs) {
    const p = progs[name];
    const card = document.createElement('div');
    card.className = 'prog-card';
    card.innerHTML = `
      <h2>${name} <button data-delete="${name}">Delete</button></h2>
      <label>Autolyse (min): <input type="number" data-prog="${name}" data-field="autolyseTime" value="${p.autolyseTime||0}"></label><br>
      <label>Mix Time (min): <input type="number" data-prog="${name}" data-field="mixTime" value="${p.mixTime||0}"></label><br>
      <label>Bulk Temp (°C): <input type="number" step="0.1" data-prog="${name}" data-field="bulkTemp" value="${p.bulkTemp||0}"></label><br>
      <label>Bulk Time (min): <input type="number" data-prog="${name}" data-field="bulkTime" value="${p.bulkTime||0}"></label><br>
      <label>Knock-Down (min): <input type="number" data-prog="${name}" data-field="knockDownTime" value="${p.knockDownTime||0}"></label><br>
      <label>Rise Temp (°C): <input type="number" step="0.1" data-prog="${name}" data-field="riseTemp" value="${p.riseTemp||0}"></label><br>
      <label>Rise Time (min): <input type="number" data-prog="${name}" data-field="riseTime" value="${p.riseTime||0}"></label><br>
      <label>Bake 1 Temp (°C): <input type="number" step="0.1" data-prog="${name}" data-field="bake1Temp" value="${p.bake1Temp||0}"></label><br>
      <label>Bake 1 Time (min): <input type="number" data-prog="${name}" data-field="bake1Time" value="${p.bake1Time||0}"></label><br>
      <label>Bake 2 Temp (°C): <input type="number" step="0.1" data-prog="${name}" data-field="bake2Temp" value="${p.bake2Temp||0}"></label><br>
      <label>Bake 2 Time (min): <input type="number" data-prog="${name}" data-field="bake2Time" value="${p.bake2Time||0}"></label><br>
      <label>Cool Time (min): <input type="number" data-prog="${name}" data-field="coolTime" value="${p.coolTime||0}"></label>
    `;
    container.appendChild(card);

    // delete
    card.querySelector('[data-delete]').onclick = e => {
      delete window._programs[e.target.getAttribute('data-delete')];
      renderPrograms(window._programs);
    };
    // update fields
    card.querySelectorAll('input').forEach(inp => {
      inp.onchange = () => {
        const nm = inp.getAttribute('data-prog');
        const fld = inp.getAttribute('data-field');
        let val = inp.value;
        if (inp.type === 'number' && inp.step && inp.step !== 'any')
          val = inp.valueAsNumber;
        window._programs[nm][fld] = val;
      };
    });
  }
  // Add program
  if(document.getElementById('addProg'))
    document.getElementById('addProg').onclick = () => {
      const name = prompt('New program name:');
      if (name && !window._programs[name]) {
        window._programs[name] = {
          autolyseTime: 0, mixTime: 5, bulkTemp: 25, bulkTime: 60, knockDownTime: 2,
          riseTemp: 30, riseTime: 40, bake1Temp: 170, bake1Time: 15,
          bake2Temp: 210, bake2Time: 25, coolTime: 10
        };
        renderPrograms(window._programs);
      }
    };
  // Save all
  if(document.getElementById('saveAll'))
    document.getElementById('saveAll').onclick = () => {
      const payload = {
        programs: window._programs,
        calibration: window._calibration || { slope: 1.0, offset: 0.0 }
      };
      fetch('/api/programs', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      }).then(r => r.json()).then(resp => {
        if (resp.status === 'ok') alert('Programs saved');
        else alert('Error saving');
      });
    };
}

if(document.getElementById('progList')) loadProgramsEditor();
