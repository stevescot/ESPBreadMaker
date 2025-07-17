#include "display_manager.h"
#include "globals.h"
#include "programs_manager.h"
#include "calibration.h"
#include "outputs_manager.h"
#include <WiFi.h>

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__
#endif

extern TFT_eSPI tft;

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
  // Initialize TFT display
  tft.init();
  tft.setRotation(0);  // Portrait orientation (135x240)
  tft.fillScreen(COLOR_BLACK);
  
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
  tft.fillScreen(COLOR_BLACK);
  
  // Title
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.println("Breadmaker");
  
  // Temperature
  float temp = readTemperature();
  drawTemperature(5, 30, temp);
  
  // Current program info
  const Program* activeProgram = getActiveProgram();
  if (activeProgram && programState.isRunning) {
    // Show current stage
    if (programState.customStageIdx < activeProgram->customStages.size()) {
      const CustomStage& stage = activeProgram->customStages[programState.customStageIdx];
      drawStageInfo(5, 60, stage.label, 0); // TODO: Calculate time left
    }
    
    // Show progress bar
    float progress = 0.5; // TODO: Calculate actual progress
    drawProgressBar(5, 90, 125, 15, progress);
  } else {
    tft.setTextColor(COLOR_GRAY);
    tft.setTextSize(1);
    tft.setCursor(5, 60);
    tft.println("No program running");
  }
  
  // Output states
  drawOutputStates(5, 120, outputStates.heater, outputStates.motor, 
                   outputStates.light, outputStates.buzzer);
}

void displayMenu() {
  tft.fillScreen(COLOR_BLACK);
  
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.println("Menu");
  
  tft.setTextSize(1);
  tft.setCursor(5, 30);
  tft.println("1. Status");
  tft.setCursor(5, 45);
  tft.println("2. Programs");
  tft.setCursor(5, 60);
  tft.println("3. Settings");
  tft.setCursor(5, 75);
  tft.println("4. WiFi Setup");
  
  tft.setTextColor(COLOR_GRAY);
  tft.setCursor(5, 210);
  tft.println("BTN1: Select");
  tft.setCursor(5, 225);
  tft.println("BTN2: Back");
}

void displayPrograms() {
  tft.fillScreen(COLOR_BLACK);
  
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.println("Programs");
  
  // Show available programs
  const auto& metadata = getProgramMetadata();
  int y = 30;
  for (size_t i = 0; i < metadata.size() && i < 10; i++) {
    tft.setTextSize(1);
    tft.setCursor(5, y);
    tft.printf("%d. %s", metadata[i].id, metadata[i].name.c_str());
    y += 15;
  }
  
  tft.setTextColor(COLOR_GRAY);
  tft.setCursor(5, 210);
  tft.println("BTN1: Select");
  tft.setCursor(5, 225);
  tft.println("BTN2: Back");
}

void displaySettings() {
  tft.fillScreen(COLOR_BLACK);
  
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Settings");
  
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.printf("Heap: %d bytes", ESP.getFreeHeap());
  tft.setCursor(10, 55);
  tft.printf("Build: %s", FIRMWARE_BUILD_DATE);
  
  tft.setTextColor(COLOR_GRAY);
  tft.setCursor(10, 115);
  tft.println("BTN1: Select  BTN2: Back");
}

void displayWiFiSetup() {
  tft.fillScreen(COLOR_BLACK);
  
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("WiFi Setup");
  
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(COLOR_GREEN);
    tft.println("Connected");
    tft.setCursor(10, 55);
    tft.printf("IP: %s", WiFi.localIP().toString().c_str());
  } else {
    tft.setTextColor(COLOR_RED);
    tft.println("Disconnected");
    tft.setCursor(10, 55);
    tft.println("Starting AP mode...");
  }
  
  tft.setTextColor(COLOR_GRAY);
  tft.setCursor(10, 115);
  tft.println("BTN1: Select  BTN2: Back");
}

void displayError(const String& message) {
  tft.fillScreen(COLOR_RED);
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("ERROR");
  
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.println(message);
}

void displayBootScreen() {
  tft.fillScreen(COLOR_BLACK);
  
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.println("ESP32 Breadmaker");
  
  tft.setTextSize(1);
  tft.setCursor(10, 50);
  tft.println("TTGO T-Display");
  tft.setCursor(10, 65);
  tft.println("Starting up...");
  
  tft.setTextColor(COLOR_GRAY);
  tft.setCursor(10, 90);
  tft.printf("Build: %s", FIRMWARE_BUILD_DATE);
  
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
  tft.drawRect(x, y, width, height, COLOR_WHITE);
  
  // Draw filled portion
  int fillWidth = (int)(width * progress);
  if (fillWidth > 0) {
    tft.fillRect(x + 1, y + 1, fillWidth - 2, height - 2, COLOR_GREEN);
  }
  
  // Draw percentage text
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(1);
  tft.setCursor(x + width + 5, y + height/2 - 4);
  tft.printf("%.1f%%", progress * 100);
}

void drawTemperature(int x, int y, float temp) {
  tft.setTextColor(COLOR_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.printf("Temp: %.1f°C", temp);
}

void drawStageInfo(int x, int y, const String& stage, unsigned long timeLeft) {
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.printf("Stage: %s", stage.c_str());
  
  // TODO: Display time left
  if (timeLeft > 0) {
    tft.setCursor(x, y + 12);
    tft.printf("Time: %lu min", timeLeft / 60000);
  }
}

void drawOutputStates(int x, int y, bool heater, bool motor, bool light, bool buzzer) {
  tft.setTextSize(1);
  
  // Heater
  tft.setTextColor(heater ? COLOR_RED : COLOR_GRAY);
  tft.setCursor(x, y);
  tft.print("H");
  
  // Motor
  tft.setTextColor(motor ? COLOR_BLUE : COLOR_GRAY);
  tft.setCursor(x + 20, y);
  tft.print("M");
  
  // Light
  tft.setTextColor(light ? COLOR_YELLOW : COLOR_GRAY);
  tft.setCursor(x + 40, y);
  tft.print("L");
  
  // Buzzer
  tft.setTextColor(buzzer ? COLOR_GREEN : COLOR_GRAY);
  tft.setCursor(x + 60, y);
  tft.print("B");
}
