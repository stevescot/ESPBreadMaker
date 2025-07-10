#include "outputs_manager.h"
#include <Arduino.h>

// Output pins (define here for linker visibility)
const int PIN_HEATER = D1;     // Heater (PWM ~1V ON, 0V OFF)
const int PIN_MOTOR  = D2;     // Mixer motor (PWM ~1V ON, 0V OFF)
const int PIN_LIGHT  = D5;     // Light (PWM ~1V ON, 0V OFF)
const int PIN_BUZZER = D6;     // Buzzer (PWM ~1V ON, 0V OFF)

// Output pins (should be defined in main or here if needed)
extern bool debugSerial;

OutputMode outputMode = OUTPUT_ANALOG;
bool heaterState = true;
bool motorState = true;
bool lightState = true;
bool buzzerState = true;

void setHeater(bool on) {
  if (heaterState == on) return;
  heaterState = on;
  if (debugSerial) Serial.printf("[setHeater] Setting heater to %s\n", on ? "ON" : "OFF");
  if (outputMode == OUTPUT_DIGITAL) digitalWrite(PIN_HEATER, on ? HIGH : LOW);
  else analogWrite(PIN_HEATER, on ? 77 : 0);
}
void setMotor(bool on) {
  if (motorState == on) return;
  motorState = on;
  if (debugSerial) Serial.printf("[setMotor] Setting motor to %s\n", on ? "ON" : "OFF");
  if (outputMode == OUTPUT_DIGITAL) digitalWrite(PIN_MOTOR, on ? HIGH : LOW);
  else analogWrite(PIN_MOTOR, on ? 77 : 0);
}
void setLight(bool on) {
  if (lightState == on) return;
  lightState = on;
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
  if (debugSerial) Serial.printf("[setBuzzer] Setting buzzer to %s\n", on ? "ON" : "OFF");
  if (outputMode == OUTPUT_DIGITAL) digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
  else analogWrite(PIN_BUZZER, on ? 77 : 0);
  extern bool buzzActive;
  extern unsigned long buzzStart;
  buzzActive = on; if (on) buzzStart = millis();
}
void outputsManagerInit() {
  // Set pin modes for all outputs
  pinMode(PIN_HEATER, OUTPUT);
  pinMode(PIN_MOTOR, OUTPUT);
  pinMode(PIN_LIGHT, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  setHeater(false);
  setMotor(false);
  setLight(false);
  setBuzzer(false);
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
  
  // Generate sine wave tone (simplified)
  // For ESP8266, we'll use a simple on/off pattern based on frequency
  float period = 1000.0 / buzzerFrequency; // Period in milliseconds
  unsigned long cycleTime = elapsed % (unsigned long)period;
  float duty = 0.5 + (buzzerAmplitude * 0.5); // Convert amplitude to duty cycle
  
  if (cycleTime < (period * duty)) {
    if (outputMode == OUTPUT_DIGITAL) {
      digitalWrite(PIN_BUZZER, HIGH);
    } else {
      analogWrite(PIN_BUZZER, (int)(77 * buzzerAmplitude));
    }
  } else {
    if (outputMode == OUTPUT_DIGITAL) {
      digitalWrite(PIN_BUZZER, LOW);
    } else {
      analogWrite(PIN_BUZZER, 0);
    }
  }
}
