#include "display_manager.h"
#include "globals.h"
#include "programs_manager.h"
#include "calibration.h"
#include "outputs_manager.h"
#include <WiFi.h>

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__
#endif

// External variables
extern bool debugSerial;

LGFX display;

// Display state variables
static DisplayState currentState = DISPLAY_STATUS;
static DisplayState lastState = DISPLAY_STATUS;
static unsigned long lastDisplayUpdate = 0;
static const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // Update every 1000ms (reduced frequency)

// Screensaver variables
static unsigned long lastActivityTime = 0;
static bool screensaverActive = false;
static const unsigned long SCREENSAVER_TIMEOUT = 3600000; // 1 hour in milliseconds (3600 * 1000)

// Cached values to detect changes
static float lastTemperature = -999.0;
static bool lastHeaterState = false;
static bool lastMotorState = false;
static bool lastLightState = false;
static bool lastBuzzerState = false;
static bool lastRunningState = false;
static String lastProgramName = "";
static String lastStageName = "";
static bool forceFullRedraw = true;

// Button pins for TTGO T-Display
const int BUTTON_1 = 0;
const int BUTTON_2 = 35;

// Button state tracking
static bool button1Pressed = false;
static bool button2Pressed = false;
static unsigned long lastButtonPress = 0;

void displayManagerInit() {
  // Initialize LovyanGFX display
  display.init();
  display.setRotation(3);  // Landscape orientation inverted (240x135, flipped top-bottom)
  display.fillScreen(COLOR_BLACK);
  
  // Initialize buttons
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  
  // Initialize screensaver
  lastActivityTime = millis();
  screensaverActive = false;
  
  // Show boot screen
  displayBootScreen();
  
  if (debugSerial) Serial.println("[Display] Display manager initialized");
}

void updateDisplay() {
  unsigned long now = millis();
  
  // Check screensaver status
  checkScreensaver();
  
  // If screensaver is active, don't update display content
  if (screensaverActive) {
    handleButtons(); // Still handle buttons to wake up
    return;
  }
  
  // Update display at regular intervals
  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = now;
    
    // Check if we need a full redraw (state change)
    if (currentState != lastState) {
      forceFullRedraw = true;
      lastState = currentState;
    }
    
    switch (currentState) {
      case DISPLAY_STATUS:
        displayStatus();
        break;
      case DISPLAY_MENU:
        displayMenu();
        break;
      case DISPLAY_PROGRAMS:
        displayPrograms();
        break;
      case DISPLAY_SETTINGS:
        displaySettings();
        break;
      case DISPLAY_WIFI_SETUP:
        displayWiFiSetup();
        break;
    }
    
    forceFullRedraw = false; // Reset after update
  }
  
  // Handle button presses
  handleButtons();
}

// Helper function to calculate total time remaining in program
unsigned long calculateTotalTimeLeft() {
  const Program* activeProgram = getActiveProgram();
  if (!activeProgram || !programState.isRunning) return 0;
  
  // Calculate current stage time remaining
  unsigned long currentStageTimeLeft = 0;
  if (programState.customStageIdx < activeProgram->customStages.size()) {
    const CustomStage& currentStage = activeProgram->customStages[programState.customStageIdx];
    unsigned long stageDurationMs = currentStage.min * 60 * 1000;
    unsigned long elapsed = millis() - programState.customStageStart;
    currentStageTimeLeft = (elapsed < stageDurationMs) ? (stageDurationMs - elapsed) / 1000 : 0;
  }
  
  unsigned long totalRemaining = currentStageTimeLeft;
  
  // Add time for remaining stages
  for (size_t i = programState.customStageIdx + 1; i < activeProgram->customStages.size(); i++) {
    const CustomStage& stage = activeProgram->customStages[i];
    totalRemaining += stage.min * 60;
  }
  
  return totalRemaining;
}

// Helper function to calculate current stage time remaining
unsigned long calculateStageTimeLeft() {
  const Program* activeProgram = getActiveProgram();
  if (!activeProgram || !programState.isRunning) return 0;
  
  if (programState.customStageIdx < activeProgram->customStages.size()) {
    const CustomStage& currentStage = activeProgram->customStages[programState.customStageIdx];
    unsigned long stageDurationMs = currentStage.min * 60 * 1000;
    unsigned long elapsed = millis() - programState.customStageStart;
    return (elapsed < stageDurationMs) ? (stageDurationMs - elapsed) / 1000 : 0;
  }
  
  return 0;
}

// TTGO T-Display Layout Implementation (240×135 px)
void drawTTGOProgramLayout() {
  display.fillScreen(COLOR_BLACK);
  
  const Program* activeProgram = getActiveProgram();
  if (!activeProgram || !programState.isRunning) return;
  
  // Get current stage info
  String stageName = "Unknown";
  if (programState.customStageIdx < activeProgram->customStages.size()) {
    const CustomStage& stage = activeProgram->customStages[programState.customStageIdx];
    stageName = String(stage.label);
  }
  
  // Y: 0–30 (Top area) - Stage name (large font, centered)
  display.setTextColor(COLOR_WHITE);
  display.setTextSize(2);
  int stageWidth = stageName.length() * 12; // Approximate width
  int stageX = (240 - stageWidth) / 2; // Center horizontally
  display.setCursor(stageX, 8);
  display.println(stageName);
  
  // Y: 30–60 (Progress area)
  // Timer (MM:SS left) centered at top of this band
  unsigned long timeLeft = calculateStageTimeLeft();
  int minutes = timeLeft / 60;
  int seconds = timeLeft % 60;
  
  display.setTextColor(COLOR_YELLOW);
  display.setTextSize(2);
  String timeStr = String(minutes) + ":" + (seconds < 10 ? "0" : "") + String(seconds);
  int timeWidth = timeStr.length() * 12;
  int timeX = (240 - timeWidth) / 2;
  display.setCursor(timeX, 33);
  display.println(timeStr);
  
  // Progress bar (200×15 px), horizontally centered at Y: 45
  int progressBarX = (240 - 200) / 2; // Center 200px bar in 240px width
  int progressBarY = 45;
  int progressBarWidth = 200;
  int progressBarHeight = 15;
  
  // Calculate progress percentage
  float progress = 0.0;
  if (activeProgram->customStages.size() > 0) {
    progress = (float)programState.customStageIdx / activeProgram->customStages.size();
  }
  int filledWidth = (int)(progress * progressBarWidth);
  
  // Draw progress bar background
  display.drawRect(progressBarX, progressBarY, progressBarWidth, progressBarHeight, COLOR_WHITE);
  // Draw filled portion
  if (filledWidth > 0) {
    display.fillRect(progressBarX + 1, progressBarY + 1, filledWidth - 2, progressBarHeight - 2, COLOR_GREEN);
  }
  
  // Y: 60–90 (Middle area) - Alerts/messages (for now, show program name)
  display.setTextColor(COLOR_CYAN);
  display.setTextSize(1);
  String programName = String(activeProgram->name);
  int progWidth = programName.length() * 6;
  int progX = (240 - progWidth) / 2;
  display.setCursor(progX, 72);
  display.println(programName);
  
  // Y: 90–120 (Bottom info area) - Overall process time
  unsigned long totalTimeLeft = calculateTotalTimeLeft();
  int totalHours = totalTimeLeft / 3600;
  int totalMinutes = (totalTimeLeft % 3600) / 60;
  
  // Calculate ready time
  time_t now = time(nullptr);
  time_t readyTime = now + totalTimeLeft;
  struct tm* readyTm = localtime(&readyTime);
  
  display.setTextColor(COLOR_WHITE);
  display.setTextSize(1);
  String readyStr = "Ready at " + String(readyTm->tm_hour) + ":" + 
                   (readyTm->tm_min < 10 ? "0" : "") + String(readyTm->tm_min) +
                   " (" + String(totalHours) + "h " + String(totalMinutes) + "m left)";
  int readyWidth = readyStr.length() * 6;
  int readyX = (240 - readyWidth) / 2;
  display.setCursor(readyX, 100);
  display.println(readyStr);
  
  // Y: 120–135 (Bottom row) - Metrics will be handled in displayStatus()
}

void displayStatus() {
  // Only clear screen if we need a full redraw
  if (forceFullRedraw) {
    display.fillScreen(COLOR_BLACK);
  }
  
  const Program* activeProgram = getActiveProgram();
  bool currentRunning = (activeProgram && programState.isRunning);
  String currentProgramName = activeProgram ? String(activeProgram->name) : "";
  String currentStageName = "";
  
  if (currentRunning && programState.customStageIdx < activeProgram->customStages.size()) {
    const CustomStage& stage = activeProgram->customStages[programState.customStageIdx];
    currentStageName = String(stage.label);
  }
  
  // Only update if something changed
  if (forceFullRedraw || currentRunning != lastRunningState || 
      currentProgramName != lastProgramName || currentStageName != lastStageName) {
    
    if (currentRunning) {
      // Use the new TTGO T-Display layout specification
      drawTTGOProgramLayout();
    } else {
      // Clear screen and show idle state
      display.fillScreen(COLOR_BLACK);
      
      // Y: 0-30 (Top area) - Show "Breadmaker" when idle
      display.setTextColor(COLOR_WHITE);
      display.setTextSize(2);
      display.setCursor(50, 8);
      display.println("Breadmaker");
      
      // Y: 60-90 (Middle area) - Show idle message
      display.setTextColor(COLOR_GRAY);
      display.setTextSize(1);
      display.setCursor(85, 75);
      display.println("Idle");
      
      // Y: 120-135 (Bottom row) - Show temperature only
      float temp = readTemperature();
      display.setTextColor(COLOR_CYAN);
      display.setTextSize(1);
      display.setCursor(5, 123);
      display.printf("Temp: %.1f°C", temp);
    }
    
    lastRunningState = currentRunning;
    lastProgramName = currentProgramName;
    lastStageName = currentStageName;
  }
  
  // Always update temperature in bottom row if running
  if (currentRunning) {
    float temp = readTemperature();
    if (forceFullRedraw || abs(temp - lastTemperature) > 0.5) {
      // Clear and update temperature in bottom row
      display.fillRect(5, 120, 80, 15, COLOR_BLACK);
      display.setTextColor(COLOR_CYAN);
      display.setTextSize(1);
      display.setCursor(5, 123);
      display.printf("Temp: %.0f°C", temp);
      lastTemperature = temp;
    }
  }
  
  // Update output states in bottom row if running
  if (currentRunning) {
    bool heaterOn = outputStates.heater;
    bool motorOn = outputStates.motor;
    bool lightOn = outputStates.light;
    
    if (forceFullRedraw || motorOn != lastMotorState) {
      // Clear and update motor status (center)
      display.fillRect(85, 120, 70, 15, COLOR_BLACK);
      display.setTextColor(motorOn ? COLOR_GREEN : COLOR_GRAY);
      display.setTextSize(1);
      display.setCursor(85, 123);
      display.printf("Motor: %s", motorOn ? "On" : "Off");
      lastMotorState = motorOn;
    }
    
    if (forceFullRedraw || heaterOn != lastHeaterState) {
      // Clear and update power status (right) - using heater as power indicator
      display.fillRect(160, 120, 75, 15, COLOR_BLACK);
      display.setTextColor(heaterOn ? COLOR_RED : COLOR_GRAY);
      display.setTextSize(1);
      display.setCursor(160, 123);
      // Estimate power based on heater state (simplified)
      int power = heaterOn ? 45 : 0;
      display.printf("Power: %dW", power);
      lastHeaterState = heaterOn;
    }
  }
  
  forceFullRedraw = false;
}

void displayMenu() {
  // Only clear screen on state change
  if (forceFullRedraw) {
    display.fillScreen(COLOR_BLACK);
    
    display.setTextColor(COLOR_WHITE);
    display.setTextSize(2);
    display.setCursor(5, 5);
    display.println("Menu");
    
    display.setTextSize(1);
    display.setCursor(5, 30);
    display.println("1. Status");
    display.setCursor(5, 45);
    display.println("2. Programs");
    display.setCursor(5, 60);
    display.println("3. Settings");
    display.setCursor(5, 75);
    display.println("4. WiFi Setup");
    
    display.setTextColor(COLOR_GRAY);
    display.setCursor(5, 110);
    display.println("BTN1: Select  BTN2: Back");
  }
}

void displayPrograms() {
  // Only clear screen on state change
  if (forceFullRedraw) {
    display.fillScreen(COLOR_BLACK);
    
    display.setTextColor(COLOR_WHITE);
    display.setTextSize(2);
    display.setCursor(5, 5);
    display.println("Programs");
    
    // Show available programs in two columns for landscape
    const auto& metadata = getProgramMetadata();
    int y = 30;
    int col1_x = 5;
    int col2_x = 125;
    
    for (size_t i = 0; i < metadata.size() && i < 8; i++) {
      display.setTextSize(1);
      if (i < 4) {
        display.setCursor(col1_x, y + (i * 15));
      } else {
        display.setCursor(col2_x, y + ((i-4) * 15));
      }
      display.printf("%d. %s", metadata[i].id, metadata[i].name.c_str());
    }
    display.setTextColor(COLOR_GRAY);
    display.setCursor(5, 110);
    display.println("BTN1: Select  BTN2: Back");
  }
}

void displaySettings() {
  // Only clear screen on state change
  if (forceFullRedraw) {
    display.fillScreen(COLOR_BLACK);
    
    display.setTextColor(COLOR_WHITE);
    display.setTextSize(2);
    display.setCursor(10, 10);
    display.println("Settings");
    display.println("Settings");
    
    display.setTextSize(1);
    display.setCursor(10, 40);
    display.printf("Heap: %d bytes", ESP.getFreeHeap());
    display.setCursor(10, 55);
    display.printf("Build: %s", FIRMWARE_BUILD_DATE);
    
    display.setTextColor(COLOR_GRAY);
    display.setCursor(10, 115);
    display.println("BTN1: Select  BTN2: Back");
  }
}

void displayWiFiSetup() {
  // Only clear screen on state change
  if (forceFullRedraw) {
    display.fillScreen(COLOR_BLACK);
    
    display.setTextColor(COLOR_WHITE);
    display.setTextSize(2);
    display.setCursor(10, 10);
    display.println("WiFi Setup");
    
    display.setTextSize(1);
    display.setCursor(10, 40);
    if (wifiCache.isConnected()) {
      display.setTextColor(COLOR_GREEN);
      display.println("Connected");
      display.setCursor(10, 55);
      display.printf("IP: %s", wifiCache.getIPString().c_str());
    } else {
      display.setTextColor(COLOR_RED);
      display.println("Disconnected");
      display.setCursor(10, 55);
      display.println("Starting AP mode...");
    }
    
    display.setTextColor(COLOR_GRAY);
    display.setCursor(10, 115);
    display.println("BTN1: Select  BTN2: Back");
  }
}

void displayError(const String& message) {
  // Error display should always update immediately
  display.fillScreen(COLOR_RED);
  display.setTextColor(COLOR_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.println("ERROR");
  
  display.setTextSize(1);
  display.setCursor(10, 40);
  display.println(message);
}

void displayBootScreen() {
  display.fillScreen(COLOR_BLACK);
  
  display.setTextColor(COLOR_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 20);
  display.println("ESP32 Breadmaker");
  
  display.setTextSize(1);
  display.setCursor(10, 50);
  display.println("TTGO T-Display");
  display.setCursor(10, 65);
  display.println("Starting up...");
  
  display.setTextColor(COLOR_GRAY);
  display.setCursor(10, 90);
  display.printf("Build: %s", FIRMWARE_BUILD_DATE);
  
  delay(2000); // Show boot screen for 2 seconds
}

void handleButtons() {
  unsigned long now = millis();
  
  // Debounce buttons
  if (now - lastButtonPress < 200) return;
  
  bool btn1 = !digitalRead(BUTTON_1);
  bool btn2 = !digitalRead(BUTTON_2);
  
  // Button 1 pressed (select/next)
  if (btn1 && !button1Pressed) {
    button1Pressed = true;
    lastButtonPress = now;
    updateActivityTime(); // Update activity time on button press
    
    // If screensaver is active, just wake up and don't process the button action
    if (screensaverActive) {
      disableScreensaver();
      return;
    }
    
    switch (currentState) {
      case DISPLAY_STATUS:
        setDisplayState(DISPLAY_MENU);
        break;
      case DISPLAY_MENU:
        setDisplayState(DISPLAY_PROGRAMS);
        break;
      case DISPLAY_PROGRAMS:
        setDisplayState(DISPLAY_SETTINGS);
        break;
      case DISPLAY_SETTINGS:
        setDisplayState(DISPLAY_WIFI_SETUP);
        break;
      case DISPLAY_WIFI_SETUP:
        setDisplayState(DISPLAY_STATUS);
        break;
    }
  }
  
  // Button 2 pressed (back)
  if (btn2 && !button2Pressed) {
    button2Pressed = true;
    lastButtonPress = now;
    updateActivityTime(); // Update activity time on button press
    
    // If screensaver is active, just wake up and don't process the button action
    if (screensaverActive) {
      disableScreensaver();
      return;
    }
    
    if (currentState != DISPLAY_STATUS) {
      setDisplayState(DISPLAY_STATUS);
    }
  }
  
  // Update button states
  if (!btn1) button1Pressed = false;
  if (!btn2) button2Pressed = false;
}

void setDisplayState(DisplayState state) {
  currentState = state;
  forceFullRedraw = true; // Force full redraw when state changes
  lastDisplayUpdate = 0; // Force immediate update
}

DisplayState getDisplayState() {
  return currentState;
}

// Utility functions
void drawProgressBar(int x, int y, int width, int height, float progress) {
  // Draw border
  display.drawRect(x, y, width, height, COLOR_WHITE);
  
  // Draw filled portion
  int fillWidth = (int)(width * progress);
  if (fillWidth > 0) {
    display.fillRect(x + 1, y + 1, fillWidth - 2, height - 2, COLOR_GREEN);
  }
  
  // Draw percentage text
  display.setTextColor(COLOR_WHITE);
  display.setTextSize(1);
  display.setCursor(x + width + 5, y + height/2 - 4);
  display.printf("%.1f%%", progress * 100);
}

void drawProgramRunningLayout(int x, int y) {
  // Large centered temperature display at top
  float temp = readTemperature();
  display.setTextColor(COLOR_YELLOW);
  display.setTextSize(3);  // Large font
  
  // Calculate center position for temperature
  char tempStr[16];
  snprintf(tempStr, sizeof(tempStr), "%.1f°C", temp);
  int tempWidth = strlen(tempStr) * 18; // Approximate width for size 3 font
  int centerX = (235 - tempWidth) / 2;
  
  display.setCursor(centerX, y);
  display.print(tempStr);
  
  // Motor status in center area
  display.setTextColor(outputStates.motor ? COLOR_GREEN : COLOR_GRAY);
  display.setTextSize(2);  // Medium-large font
  
  String motorText = outputStates.motor ? "Motor: ON" : "Motor: OFF";
  int motorWidth = motorText.length() * 12; // Approximate width for size 2 font
  int motorCenterX = (235 - motorWidth) / 2;
  
  display.setCursor(motorCenterX, y + 35);
  display.print(motorText);
  
  // Power information on the right
  display.setTextColor(COLOR_WHITE);
  display.setTextSize(1);
  
  // Calculate approximate power based on active outputs
  int powerWatts = 0;
  if (outputStates.heater) powerWatts += 40;  // Heater power
  if (outputStates.motor) powerWatts += 5;    // Motor power
  if (outputStates.light) powerWatts += 3;    // Light power
  
  display.setCursor(170, y + 60);  // Right side
  display.printf("Power: %dW", powerWatts);
}

void drawTemperature(int x, int y, float temp) {
  display.setTextColor(COLOR_YELLOW);
  display.setTextSize(1);
  display.setCursor(x, y);
  display.printf("Temp: %.1f°C", temp);
}

void drawStageInfo(int x, int y, const String& stage, unsigned long timeLeft) {
  display.setTextColor(COLOR_WHITE);
  display.setTextSize(1);
  display.setCursor(x, y);
  display.printf("Stage: %s", stage.c_str());
  
  // TODO: Display time left
  if (timeLeft > 0) {
    display.setCursor(x, y + 12);
    display.printf("Time: %lu min", timeLeft / 60000);
  }
}

void drawOutputStates(int x, int y, bool heater, bool motor, bool light, bool buzzer) {
  display.setTextSize(1);
  
  // Heater
  display.setTextColor(heater ? COLOR_RED : COLOR_GRAY);
  display.setCursor(x, y);
  display.print("H");
  
  // Motor
  display.setTextColor(motor ? COLOR_BLUE : COLOR_GRAY);
  display.setCursor(x + 20, y);
  display.print("M");
  
  // Light
  display.setTextColor(light ? COLOR_YELLOW : COLOR_GRAY);
  display.setCursor(x + 40, y);
  display.print("L");
  
  // Buzzer
  display.setTextColor(buzzer ? COLOR_GREEN : COLOR_GRAY);
  display.setCursor(x + 60, y);
  display.print("B");
}

// Screensaver implementation
void updateActivityTime() {
  lastActivityTime = millis();
  if (screensaverActive) {
    if (debugSerial) Serial.println("[Display] Activity detected, waking up from screensaver");
  }
}

bool isScreensaverActive() {
  return screensaverActive;
}

void enableScreensaver() {
  if (!screensaverActive) {
    if (debugSerial) Serial.println("[Display] Enabling screensaver - turning off display and backlight");
    screensaverActive = true;
    
    // Turn off backlight using LovyanGFX brightness control
    display.setBrightness(0);
    
    // Clear display to save power
    display.fillScreen(COLOR_BLACK);
    
    // Optionally turn off the display completely (if supported)
    display.sleep();
  }
}

void disableScreensaver() {
  if (screensaverActive) {
    if (debugSerial) Serial.println("[Display] Disabling screensaver - turning on display and backlight");
    screensaverActive = false;
    
    // Wake up display
    display.wakeup();
    
    // Restore backlight to full brightness
    display.setBrightness(255);
    
    // Force a full redraw
    forceFullRedraw = true;
    lastDisplayUpdate = 0; // Force immediate update
    
    // Clear the screen for clean redraw
    display.fillScreen(COLOR_BLACK);
  }
}

void checkScreensaver() {
  unsigned long now = millis();
  
  // Handle millis() overflow (occurs every ~50 days)
  if (now < lastActivityTime) {
    lastActivityTime = now;
    return;
  }
  
  // Check if screensaver timeout has been reached
  if (!screensaverActive && (now - lastActivityTime) >= SCREENSAVER_TIMEOUT) {
    Serial.printf("[Display] No activity for %lu minutes, activating screensaver\n", SCREENSAVER_TIMEOUT / 60000);
    enableScreensaver();
  }
}
