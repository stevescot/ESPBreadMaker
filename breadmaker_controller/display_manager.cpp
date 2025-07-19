#include "display_manager.h"
#include "globals.h"
#include "programs_manager.h"
#include "calibration.h"
#include "outputs_manager.h"
#include <WiFi.h>

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__
#endif

LGFX display;

// Display state variables
static DisplayState currentState = DISPLAY_STATUS;
static unsigned long lastDisplayUpdate = 0;
static const unsigned long DISPLAY_UPDATE_INTERVAL = 500; // Update every 500ms

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
  display.setRotation(1);  // Landscape orientation (240x135)
  display.fillScreen(COLOR_BLACK);
  
  // Initialize buttons
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  
  // Show boot screen
  displayBootScreen();
  
  Serial.println("[Display] Display manager initialized");
}

void updateDisplay() {
  unsigned long now = millis();
  
  // Update display at regular intervals
  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = now;
    
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
  }
  
  // Handle button presses
  handleButtons();
}

void displayStatus() {
  display.fillScreen(COLOR_BLACK);
  
  // Title
  display.setTextColor(COLOR_WHITE);
  display.setTextSize(2);
  display.setCursor(5, 5);
  display.println("Breadmaker");
  
  // Temperature on the right side
  float temp = readTemperature();
  drawTemperature(140, 5, temp);
  
  // Current program info
  const Program* activeProgram = getActiveProgram();
  if (activeProgram && programState.isRunning) {
    // Show current stage
    if (programState.customStageIdx < activeProgram->customStages.size()) {
      const CustomStage& stage = activeProgram->customStages[programState.customStageIdx];
      drawStageInfo(5, 35, stage.label, 0); // TODO: Calculate time left
    }
    
    // Show progress bar - wider for landscape
    float progress = 0.5; // TODO: Calculate actual progress
    drawProgressBar(5, 65, 220, 15, progress);
  } else {
    display.setTextColor(COLOR_GRAY);
    display.setTextSize(1);
    display.setCursor(5, 35);
    display.println("No program running");
  }
  
  // Output states at bottom
  drawOutputStates(5, 95, outputStates.heater, outputStates.motor, 
                   outputStates.light, outputStates.buzzer);
}

void displayMenu() {
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

void displayPrograms() {
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

void displaySettings() {
  display.fillScreen(COLOR_BLACK);
  
  display.setTextColor(COLOR_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 10);
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

void displayWiFiSetup() {
  display.fillScreen(COLOR_BLACK);
  
  display.setTextColor(COLOR_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.println("WiFi Setup");
  
  display.setTextSize(1);
  display.setCursor(10, 40);
  if (WiFi.status() == WL_CONNECTED) {
    display.setTextColor(COLOR_GREEN);
    display.println("Connected");
    display.setCursor(10, 55);
    display.printf("IP: %s", WiFi.localIP().toString().c_str());
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

void displayError(const String& message) {
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
