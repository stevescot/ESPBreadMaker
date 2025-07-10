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
