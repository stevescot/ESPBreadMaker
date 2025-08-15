#include "capacitive_buttons.h"
#include "globals.h"
#include "display_manager.h"
#include "missing_stubs.h"

// External variables
extern bool debugSerial;

// Button state tracking
static unsigned long lastButtonPress[6] = {0};
static bool buttonStates[6] = {false};

// Touch pin mapping array for easy iteration
static const int touchPins[6] = {
  TOUCH_START_PAUSE,
  TOUCH_STOP, 
  TOUCH_SELECT,
  TOUCH_ADVANCE,
  TOUCH_BACK,
  TOUCH_LIGHT
};

// Button action mapping
static const ButtonAction buttonActions[6] = {
  ButtonAction::START_PAUSE,
  ButtonAction::STOP,
  ButtonAction::SELECT, 
  ButtonAction::ADVANCE,
  ButtonAction::BACK,
  ButtonAction::LIGHT
};

void capacitiveButtonsInit() {
  if (debugSerial) Serial.println("Initializing buttons...");
  
  // Set up digital input pins with pull-up resistors
  pinMode(TOUCH_LIGHT, INPUT_PULLUP);  // GPIO22 - digital input
  
  // Capacitive touch pins (2, 12, 13, 15, 27) don't need pinMode setup
  // They are automatically configured when touchRead() is called
  
  // Initialize button states
  for (int i = 0; i < 6; i++) {
    lastButtonPress[i] = 0;
    buttonStates[i] = false;
  }
  
  if (debugSerial) {
    Serial.println("Buttons initialized:");
    Serial.printf("  Start/Pause: GPIO%d (T2 - capacitive)\n", TOUCH_START_PAUSE);
    Serial.printf("  Stop: GPIO%d (T5 - capacitive)\n", TOUCH_STOP);
    Serial.printf("  Select: GPIO%d (T4 - capacitive)\n", TOUCH_SELECT);
    Serial.printf("  Advance: GPIO%d (T3 - capacitive)\n", TOUCH_ADVANCE);
    Serial.printf("  Back: GPIO%d (T7 - capacitive)\n", TOUCH_BACK);
    Serial.printf("  Light: GPIO%d (digital input)\n", TOUCH_LIGHT);
  }
  Serial.printf("  Touch threshold: %d\n", TOUCH_THRESHOLD);
}

void capacitiveButtonsUpdate() {
  unsigned long currentTime = millis();
  
  // Check each button for presses
  for (int i = 0; i < 6; i++) {
    bool currentlyPressed = isButtonPressed(touchPins[i]);
    
    // Detect rising edge (button just pressed)
    if (currentlyPressed && !buttonStates[i]) {
      // Check debounce timing
      if (currentTime - lastButtonPress[i] > DEBOUNCE_MS) {
        lastButtonPress[i] = currentTime;
        buttonStates[i] = true;
        
        // Handle the button action
        handleButtonAction(buttonActions[i]);
        
        // Debug output
        Serial.printf("Button pressed: %s (GPIO%d)\n", 
                     getButtonName(buttonActions[i]).c_str(), 
                     touchPins[i]);
      }
    }
    // Detect falling edge (button released)  
    else if (!currentlyPressed && buttonStates[i]) {
      buttonStates[i] = false;
    }
  }
}

ButtonAction checkButtonPress() {
  unsigned long currentTime = millis();
  
  for (int i = 0; i < 6; i++) {
    if (isButtonPressed(touchPins[i])) {
      if (currentTime - lastButtonPress[i] > DEBOUNCE_MS) {
        lastButtonPress[i] = currentTime;
        updateActivityTime(); // Update display activity time
        return buttonActions[i];
      }
    }
  }
  
  return ButtonAction::NONE;
}

void handleButtonAction(ButtonAction action) {
  switch (action) {
    case ButtonAction::START_PAUSE:
      if (debugSerial) Serial.println("Action: Start/Pause breadmaker");
      // Trigger start/pause endpoint logic
      if (programState.isRunning) {
        // Pause logic - this would normally be handled by the pause endpoint
        if (debugSerial) Serial.println("  -> Pausing breadmaker (would call /api/pause)");
        displayMessage("Paused");
      } else {
        // Start logic - this would normally be handled by the start endpoint  
        if (debugSerial) Serial.println("  -> Starting breadmaker (would call /api/start)");
        displayMessage("Starting...");
      }
      break;
      
    case ButtonAction::STOP:
      if (debugSerial) Serial.println("Action: Stop breadmaker");
      // Call the actual stop function
      if (debugSerial) Serial.println("  -> Calling stopBreadmaker() function");
      stopBreadmaker();
      displayMessage("Stopped");
      break;
      
    case ButtonAction::SELECT:
      if (debugSerial) Serial.println("Action: Select/Enter");
      // Navigate into menu or confirm selection
      displayMessage("Select");
      break;
      
    case ButtonAction::ADVANCE:
      if (debugSerial) Serial.println("Action: Advance/Next");
      // Navigate to next option or advance program
      displayMessage("Next");
      break;
      
    case ButtonAction::BACK:
      if (debugSerial) Serial.println("Action: Back/Previous");
      // Navigate back or previous option
      displayMessage("Back");
      break;
      
    case ButtonAction::LIGHT:
      if (debugSerial) Serial.println("Action: Toggle light");
      // Toggle breadmaker light state (same as /api/light endpoint)
      outputStates.light = !outputStates.light;
      if (debugSerial) Serial.printf("  -> Light is now %s\n", outputStates.light ? "ON" : "OFF");
      displayMessage(outputStates.light ? "Light ON" : "Light OFF");
      break;
      
    case ButtonAction::NONE:
    default:
      // No action
      break;
  }
}

bool isButtonPressed(int pin) {
  // Handle different input types based on pin
  if (pin == TOUCH_LIGHT) {
    // Digital input (GPIO22) - active LOW with pull-up
    return digitalRead(pin) == LOW;
  } else {
    // Capacitive touch inputs (GPIO2, GPIO12, GPIO13, GPIO15, GPIO27)
    uint16_t touchValue = touchRead(pin);
    return touchValue < TOUCH_THRESHOLD;
  }
}

String getButtonName(ButtonAction action) {
  switch (action) {
    case ButtonAction::START_PAUSE: return "Start/Pause";
    case ButtonAction::STOP: return "Stop";
    case ButtonAction::SELECT: return "Select";
    case ButtonAction::ADVANCE: return "Advance";
    case ButtonAction::BACK: return "Back";
    case ButtonAction::LIGHT: return "Light";
    default: return "Unknown";
  }
}
