// Simulator: stands in for steps.cpp (the QMI8658's step engine through SensorLib, #117).
// The simulator has no motion sensor, so the engine is off and the count stays at 0.
#include "../steps.h"
#include <Arduino.h>

bool stepsBegin() { return false; }
bool stepsOn() { return false; }
uint32_t stepsChipCount() { return 0; }
void stepsPrintStatus() { Serial.println("STEPS on=0 chip=0 (simulator)"); }
