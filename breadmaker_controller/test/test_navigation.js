// test_navigation.js
// Tests for programmatic next/back button logic in the breadmaker UI, including program selection by index

describe('Program Navigation', () => {
  let currentStage;
  const STAGES = ['autolyse', 'mix', 'bulk', 'knockDown', 'rise', 'bake1', 'bake2', 'cool'];

  function nextStage(stage) {
    const idx = STAGES.indexOf(stage);
    if (idx === -1 || idx === STAGES.length - 1) return STAGES[STAGES.length - 1];
    return STAGES[idx + 1];
  }

  function prevStage(stage) {
    const idx = STAGES.indexOf(stage);
    if (idx <= 0) return STAGES[0];
    return STAGES[idx - 1];
  }

  beforeEach(() => {
    currentStage = 'autolyse';
  });

  it('should advance to the next stage', () => {
    currentStage = nextStage(currentStage);
    expect(currentStage).toBe('mix');
    currentStage = nextStage(currentStage);
    expect(currentStage).toBe('bulk');
  });

  it('should not advance past the last stage', () => {
    currentStage = 'cool';
    currentStage = nextStage(currentStage);
    expect(currentStage).toBe('cool');
  });

  it('should go back to the previous stage', () => {
    currentStage = 'bulk';
    currentStage = prevStage(currentStage);
    expect(currentStage).toBe('mix');
    currentStage = prevStage(currentStage);
    expect(currentStage).toBe('autolyse');
  });

  it('should not go back before the first stage', () => {
    currentStage = 'autolyse';
    currentStage = prevStage(currentStage);
    expect(currentStage).toBe('autolyse');
  });

  it('should remain consistent after multiple next and back operations', () => {
    currentStage = 'autolyse';
    // Go forward through all stages
    for (let i = 1; i < STAGES.length; ++i) {
      currentStage = nextStage(currentStage);
      expect(currentStage).toBe(STAGES[i]);
    }
    // Try to go past the last stage
    for (let i = 0; i < 3; ++i) {
      currentStage = nextStage(currentStage);
      expect(currentStage).toBe('cool');
    }
    // Go back through all stages
    for (let i = STAGES.length - 2; i >= 0; --i) {
      currentStage = prevStage(currentStage);
      expect(currentStage).toBe(STAGES[i]);
    }
    // Try to go before the first stage
    for (let i = 0; i < 3; ++i) {
      currentStage = prevStage(currentStage);
      expect(currentStage).toBe('autolyse');
    }
  });

  it('should select program robustly by index', () => {
    const programs = ['A', 'B', 'C', 'D'];
    function selectByIndex(idx) {
      return programs[idx];
    }
    expect(selectByIndex(2)).toBe('C');
    expect(selectByIndex(0)).toBe('A');
  });
});
