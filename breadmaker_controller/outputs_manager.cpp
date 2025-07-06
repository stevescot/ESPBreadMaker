#include "outputs_manager.h"
#include <Arduino.h>

// Output pins (define here for linker visibility)
const int PIN_HEATER = D1;     // Heater (PWM ~1V ON, 0V OFF)
const int PIN_MOTOR  = D2;     // Mixer motor (PWM ~1V ON, 0V OFF)
const int PIN_LIGHT  = D5;     // Light (PWM ~1V ON, 0V OFF)
const int PIN_BUZZER = D6;     // Buzzer (PWM ~1V ON, 0V OFF)

// Output pins (should be defined in main or here if needed)
extern bool debugSerial;
extern void broadcastStatusWSIfChanged();

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
  broadcastStatusWSIfChanged();
}
void setMotor(bool on) {
  if (motorState == on) return;
  motorState = on;
  if (debugSerial) Serial.printf("[setMotor] Setting motor to %s\n", on ? "ON" : "OFF");
  if (outputMode == OUTPUT_DIGITAL) digitalWrite(PIN_MOTOR, on ? HIGH : LOW);
  else analogWrite(PIN_MOTOR, on ? 77 : 0);
  broadcastStatusWSIfChanged();
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
  broadcastStatusWSIfChanged();
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
  broadcastStatusWSIfChanged();
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
