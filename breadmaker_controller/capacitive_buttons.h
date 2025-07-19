#pragma once
#include <Arduino.h>

// Button pin assignments for TTGO T-Display
// Using all available capacitive touch pins
constexpr int TOUCH_START_PAUSE = 2;   // T2 - Start/Pause (capacitive touch)
constexpr int TOUCH_STOP = 12;         // T5 - Stop (capacitive touch) 
constexpr int TOUCH_SELECT = 13;       // T4 - Select/Enter (capacitive touch)
constexpr int TOUCH_ADVANCE = 15;      // T3 - Advance/Next (capacitive touch)
constexpr int TOUCH_BACK = 27;         // T7 - Back/Previous (capacitive touch)
constexpr int TOUCH_LIGHT = 22;        // GPIO22 - Light toggle (digital input)

// Note: GPIO 32, 33 are used for heater/motor outputs
// Additional available pins for future use: 17, 21, 35, 36, 37

// Touch sensitivity and debouncing settings
constexpr int TOUCH_THRESHOLD = 70;    // Touch threshold value (lower = more sensitive)
constexpr unsigned long DEBOUNCE_MS = 500; // Debounce delay in milliseconds

// Button states and actions
enum class ButtonAction {
  START_PAUSE,
  STOP,
  SELECT,
  ADVANCE,
  BACK,
  LIGHT,
  NONE
};

// Function declarations
void capacitiveButtonsInit();
void capacitiveButtonsUpdate();
ButtonAction checkButtonPress();
void handleButtonAction(ButtonAction action);

// Touch detection functions
bool isButtonPressed(int pin);
String getButtonName(ButtonAction action);
