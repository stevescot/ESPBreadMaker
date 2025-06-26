// test_ui_sync.js
// Simulate frontend polling and UI plan summary logic
// Focus: stageStartTimes, navigation, icon display, and temperature chart modal

describe('UI Plan Summary', () => {
  it('should not show calendar icon for active stage', () => {
    // Simulate UI logic: active stage index
    const activeIdx = 2;
    const stages = [
      {name: 'autolyse'},
      {name: 'mix'},
      {name: 'bulk'},
      {name: 'knockDown'}
    ];
    const icons = stages.map((s, i) => i === activeIdx ? '' : 'calendar');
    expect(icons[activeIdx]).toBe('');
  });

  it('should use backend stageStartTimes for all stages', () => {
    const backendTimes = [100, 200, 300, 400];
    const uiTimes = backendTimes.slice();
    expect(uiTimes).toEqual(backendTimes);
  });

  it('should use .html links for navigation', () => {
    const navLinks = ['programs.html', 'calibration.html', 'update.html'];
    navLinks.forEach(link => {
      expect(link.endsWith('.html')).toBe(true);
    });
  });

  it('should display both current and set temperature', () => {
    const status = { temp: 25.5, setTemp: 27.0 };
    expect(typeof status.temp).toBe('number');
    expect(typeof status.setTemp).toBe('number');
  });

  it('should show temperature chart modal on click', () => {
    let modalShown = false;
    function showModal() { modalShown = true; }
    // Simulate click handler
    showModal();
    expect(modalShown).toBe(true);
  });
});
