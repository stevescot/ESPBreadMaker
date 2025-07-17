#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <TFT_eSPI.h>
#include <Arduino.h>

// Display dimensions for TTGO T-Display
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 135

// Color definitions
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_ORANGE    0xFD20
#define COLOR_GRAY      0x7BEF
#define COLOR_DARKGRAY  0x4208

// Display states
enum DisplayState {
  DISPLAY_STATUS,
  DISPLAY_MENU,
  DISPLAY_PROGRAMS,
  DISPLAY_SETTINGS,
  DISPLAY_WIFI_SETUP
};

// Function declarations
void displayManagerInit();
void updateDisplay();
void displayStatus();
void displayMenu();
void displayPrograms();
void displaySettings();
void displayWiFiSetup();
void displayError(const String& message);
void displayBootScreen();
void handleButtons();

// Display state management
void setDisplayState(DisplayState state);
DisplayState getDisplayState();

// Display utilities
void drawProgressBar(int x, int y, int width, int height, float progress);
void drawTemperature(int x, int y, float temp);
void drawStageInfo(int x, int y, const String& stage, unsigned long timeLeft);
void drawOutputStates(int x, int y, bool heater, bool motor, bool light, bool buzzer);

extern TFT_eSPI tft;

#endif // DISPLAY_MANAGER_H
