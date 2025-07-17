#include "outputs_manager.h"
#include <Arduino.h>
#include "globals.h"

// Output pins (define here for linker visibility)
// ESP32 TTGO T-Display Pin Assignments
// Display pins (used by TFT_eSPI): 19(MOSI), 18(SCK), 5(CS), 16(DC), 23(RST), 4(BL)
// Buttons: 0, 35 (check your board silkscreen)
// Available GPIO pins for breadmaker outputs (digital only):
const int PIN_HEATER = 32;     // Heater (digital ON/OFF)
const int PIN_MOTOR  = 33;     // Mixer motor (digital ON/OFF)  
const int PIN_LIGHT  = 25;     // Light (digital ON/OFF)
const int PIN_BUZZER = 26;     // Buzzer (digital ON/OFF)

// Output pins (should be defined in main or here if needed)
extern bool debugSerial;

OutputMode outputMode = OUTPUT_DIGITAL;  // Default to digital-only mode
bool heaterState = false;
bool motorState = false;
bool lightState = false;
bool buzzerState = false;

void setHeater(bool on) {
  if (heaterState == on) return;
  heaterState = on;
  outputStates.heater = on;  // Keep struct in sync
  if (debugSerial) Serial.printf("[setHeater] Setting heater to %s\n", on ? "ON" : "OFF");
  digitalWrite(PIN_HEATER, on ? HIGH : LOW);
}

void setMotor(bool on) {
  if (motorState == on) return;
  motorState = on;
  outputStates.motor = on;  // Keep struct in sync
  if (debugSerial) Serial.printf("[setMotor] Setting motor to %s\n", on ? "ON" : "OFF");
  digitalWrite(PIN_MOTOR, on ? HIGH : LOW);
}

void setLight(bool on) {
  if (lightState == on) return;
  lightState = on;
  outputStates.light = on;  // Keep struct in sync
  if (debugSerial) Serial.printf("[setLight] Setting light to %s\n", on ? "ON" : "OFF");
  digitalWrite(PIN_LIGHT, on ? HIGH : LOW);
  if (on) {
    extern unsigned long lightOnTime;
    lightOnTime = millis();
  }
}

void setBuzzer(bool on) {
  if (buzzerState == on) return;
  buzzerState = on;
  outputStates.buzzer = on;  // Keep struct in sync
  if (debugSerial) Serial.printf("[setBuzzer] Setting buzzer to %s\n", on ? "ON" : "OFF");
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
  extern bool buzzActive;
  extern unsigned long buzzStart;
  buzzActive = on; if (on) buzzStart = millis();
}
void outputsManagerInit() {
  // Set pin modes for all outputs (digital only)
  pinMode(PIN_HEATER, OUTPUT);
  pinMode(PIN_MOTOR, OUTPUT);
  pinMode(PIN_LIGHT, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  
  // Initialize all outputs to off state
  setHeater(false);
  setMotor(false);
  setLight(false);
  setBuzzer(false);
  // outputStates is now synced by the setter functions
}

// Buzzer tone generation variables
static float buzzerFrequency = 0;
static float buzzerAmplitude = 0;
static unsigned long buzzerStartTime = 0;
static unsigned long buzzerDuration = 0;
static bool buzzerToneActive = false;

void startBuzzerTone(float frequency, float amplitude, unsigned long duration) {
  buzzerFrequency = frequency;
  buzzerAmplitude = amplitude;
  buzzerDuration = duration * 2; // Make tones 2x as long as requested
  buzzerStartTime = millis();
  buzzerToneActive = true;
  if (debugSerial) Serial.printf("[Buzzer] Starting tone: %.1fHz, %.2f amplitude, %lums duration\n", 
                                frequency, amplitude, buzzerDuration);
}

void shortBeep() {
  startBuzzerTone(1000.0, 0.3, 400); // 400ms beep (will be 800ms due to 2x multiplier)
}

void updateBuzzerTone() {
  if (!buzzerToneActive) {
    setBuzzer(false);
    return;
  }
  
  unsigned long elapsed = millis() - buzzerStartTime;
  if (elapsed >= buzzerDuration) {
    // Tone finished
    buzzerToneActive = false;
    setBuzzer(false);
    return;
  }
  
  // Generate square wave tone with digital output
  float period = 1000.0 / buzzerFrequency; // Period in milliseconds
  unsigned long cycleTime = elapsed % (unsigned long)period;
  float duty = 0.5 + (buzzerAmplitude * 0.5); // Convert amplitude to duty cycle
  
  if (cycleTime < (period * duty)) {
    digitalWrite(PIN_BUZZER, HIGH);
  } else {
    digitalWrite(PIN_BUZZER, LOW);
  }
}
