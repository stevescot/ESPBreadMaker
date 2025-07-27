#pragma once
#include <Arduino.h>

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
