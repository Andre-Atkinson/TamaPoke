#pragma once
#include <stdint.h>
// Walks (#117): the 1.75C's step counter, the step engine inside its QMI8658 motion
// sensor. The chip counts by itself, so the ESP32 can stay in light sleep and read the
// count when it next wakes (Pet::walkMinute, once a minute). The original 1.75 has no step
// counter here: stepsBegin() returns false and the sketch powers the sensor down as before.
//
// Hardware only (steps.cpp, through SensorLib); the simulator swaps it for
// sim/steps_sim.cpp.
//
// What it runs, found on the owner's dev board (#117 step 1, 2026-11-18): the accelerometer
// in the normal mode at 62.5 Hz, its gyroscope off, its low-pass filter OFF, and the
// datasheet's own example step profile (QMI8658A Rev A, 11.1: a 1 s window, 200 mg
// peak-to-peak, 100 mg peak, steps 0.4 to 4 s long, 10 in a row before it counts). A
// 100-step walk counted 102. SensorLib's defaults flatten a walk (its filter is 2.66% of the
// rate, and its profile counts little at any rate), and its reset leaves the chip's register
// auto-increment and CTRL9 handshake bits clear (steps.cpp, configure()).
// Battery: the datasheet (Rev A, Table 15) gives the accelerometer alone about 133 uA in the
// normal mode at any rate from 31.25 to 125 Hz, against 6 uA powered down; the board draws
// about 22 mA on battery (the diary, #14), so under 1%.

// setup(), after Wire.begin(): finds the QMI8658, turns its accelerometer and step engine
// on and starts the count at 0. False when there is no sensor or it doesn't take the
// settings (the sketch then powers the sensor down, as it did before #117).
bool stepsBegin();
// The step engine is running (stepsBegin() succeeded).
bool stepsOn();
// The chip's count since the engine started: 24 bits, so it wraps after 16,777,215. False
// (and *count untouched) when the engine is off or the read fails, so a failed read is never
// mistaken for a count of 0. Matches Pet::stepSource.
bool stepsRead(uint32_t *count);
