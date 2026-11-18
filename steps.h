#pragma once
#include <stdint.h>
// Walks (#117): the 1.75C's step counter, the step engine inside its QMI8658 motion
// sensor. The chip counts by itself, so the ESP32 can stay in light sleep and read the
// count when it next wakes. The original 1.75 has no step counter here: stepsBegin()
// returns false and the sketch powers the sensor down as before.
//
// Hardware only (steps.cpp, through SensorLib); the simulator swaps it for
// sim/steps_sim.cpp.
//
// The accelerometer's rate: the step engine needs it running. STEPS_ACC_LP 1 runs it in
// the chip's low-power mode at 21 Hz (CTRL2's rate code 0x0D, with the gyroscope off),
// which SensorLib 0.5.2 only sets for wake-on-motion; 0 runs it at the lowest normal rate
// SensorLib sets, about 28 Hz (code 0x08). Which of the two still counts steps, and what
// each costs in battery, is what the first test build on the board measures (#117 step 1).
#ifndef STEPS_ACC_LP
#define STEPS_ACC_LP 1
#endif

// setup(), after Wire.begin(): finds the QMI8658, turns its accelerometer and step engine
// on and starts the count at 0. False when there is no sensor or it doesn't take the
// settings (the sketch then powers the sensor down, as it did before #117).
bool stepsBegin();
// The step engine is running (stepsBegin() succeeded).
bool stepsOn();
// The chip's count since the engine started: 24 bits, so it wraps after 16,777,215.
// 0 when the engine is off or the read fails.
uint32_t stepsChipCount();
// STEPS? (read-only): one line, "STEPS on=<0|1> chip=<n> ctrl2=0x.. ctrl7=0x.. ctrl8=0x..",
// the count and the registers that say the accelerometer's rate, which sensors are on and
// whether the step engine is.
void stepsPrintStatus();
