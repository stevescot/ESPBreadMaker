#pragma once
#include <Arduino.h>

// Output mode setting
enum OutputMode { OUTPUT_DIGITAL, OUTPUT_ANALOG };
extern OutputMode outputMode;

extern bool heaterState;
extern bool motorState;
extern bool lightState;
extern bool buzzerState;

void setHeater(bool on);
void setMotor(bool on);
void setLight(bool on);
void setBuzzer(bool on);
void outputsManagerInit();

// Buzzer tone functions
void startBuzzerTone(float frequency, float amplitude, unsigned long duration);
void shortBeep();
void updateBuzzerTone();
