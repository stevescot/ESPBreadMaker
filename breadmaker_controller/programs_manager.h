#pragma once
#include <Arduino.h>
#include <map>
#include <vector>

struct MixStep {
  String action; // "mix" or "wait"
  int durationSec;
};

struct CustomStage {
  String label;
  int min;
  int temp;
  String heater, light, buzzer;
  String instructions;
  std::vector<MixStep> mixPattern;
};

struct Program {
  String name;
  int autolyseTime; // was delayStart
  int mixTime;
  float bulkTemp;
  int bulkTime;
  int knockDownTime;
  float riseTemp;
  int riseTime;
  float bake1Temp;
  int bake1Time;
  float bake2Temp;
  int bake2Time;
  int coolTime;
  std::vector<CustomStage> customStages;
};

extern std::map<String, Program> programs;
extern float calibrationSlope, calibrationOffset;

void loadPrograms();
void savePrograms();
