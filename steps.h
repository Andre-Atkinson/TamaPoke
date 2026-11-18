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
// The step engine needs the accelerometer running, and which rate and which step profile
// still count steps, at what cost in battery, is what the test builds on the board measure
// (#117 step 1). The first one, the accelerometer's low-power 21 Hz with SensorLib's default
// profile, counted nothing on a walk (2026-11-18). This one starts at the lowest normal rate
// SensorLib sets (about 28 Hz) with the profile scaled to that rate, and STEPRATE switches
// either, in RAM, without a reflash:
//   rates: lp21, lp128 (the low-power mode, gyroscope off), 28, 56, 112 (normal mode)
//   profiles: lib    SensorLib's configPedometerDefault(): a 50-sample window whatever the
//                    rate (its default rate is 125 Hz, where that is 0.4 s; at 21 Hz, 2.4 s)
//             scaled the same thresholds with the window scaled to 0.4 s at any rate
//             easy   scaled, with SensorLib's example's lower thresholds and no run of
//                    steps needed before it starts counting

// setup(), after Wire.begin(): finds the QMI8658, turns its accelerometer and step engine
// on and starts the count at 0. False when there is no sensor or it doesn't take the
// settings (the sketch then powers the sensor down, as it did before #117).
bool stepsBegin();
// The step engine is running (stepsBegin() or the last STEPRATE succeeded).
bool stepsOn();
// The chip's count since the engine last started: 24 bits, so it wraps after 16,777,215.
// 0 when the engine is off or the read fails.
uint32_t stepsChipCount();
// STEPRATE <rate> [profile] (test builds, RAM only): the engine restarted at that rate and
// profile (the names above; profile scaled when left out), its count back at 0. False for a
// name it doesn't know or settings the sensor doesn't take.
bool stepsSetRate(const char *rate, const char *profile);
// STEPS? (read-only): one line, "STEPS on=<0|1> chip=<n> rate=<name> profile=<name>"
// followed by the raw step bytes, CTRL1/2/7/8, STATUSINT/0/1 and the accelerometer's
// reading in mg, which say whether the sensor is sampling and the engine is on.
void stepsPrintStatus();
