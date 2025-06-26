#pragma once
#include <Arduino.h>
#include <vector>

struct CalibPoint { int raw; float temp; };
extern std::vector<CalibPoint> rtdCalibTable;
extern const char* CALIB_FILE;
extern float calibrationSlope, calibrationOffset;

void saveCalibration();
void loadCalibration();
float tempFromRaw(int raw);
float readTemperature();
