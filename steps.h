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
// count steps is what the test builds on the board find out (#117 step 1). Battery: the
// QMI8658A datasheet (Rev A, Table 15) gives the accelerometer alone about 133 uA in the
// normal mode at any rate from 31.25 to 125 Hz, 42-55 uA in the low-power mode, against 6 uA
// powered down; the board draws about 22 mA on battery (the diary, #14), so under 1%.
// The test builds so far, on the board (2026-11-18): SensorLib turned the chip's register
// auto-increment off (fixed in configure()) and its low-pass filter on at 2.66% of the rate,
// which flattened a walk's peaks (now off). This one starts at 62.5 Hz with the datasheet's
// own example profile, and STEPRATE switches either, in RAM, without a reflash:
//   rates: lp21, lp128 (the low-power mode, gyroscope off), 31, 62, 125 (normal mode, Hz)
//   profiles: ds     the datasheet's example (11.1): a 1 s window, 200 mg peak-to-peak,
//                    100 mg peak, steps 0.4 to 4 s long, 10 in a row before it counts
//             lib    SensorLib's configPedometerDefault(): a 50-sample window at any rate,
//                    100/116 mg, steps 0.3 to 2 s, 8 in a row
//             easy   ds's timing with SensorLib's example's lower thresholds (80/60 mg),
//                    counting from the first step

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
// profile (the names above; ds when left out), its count back at 0. False for a
// name it doesn't know or settings the sensor doesn't take.
bool stepsSetRate(const char *rate, const char *profile);
// STEPS? (read-only): one line, "STEPS on=<0|1> chip=<n> rate=<name> profile=<name>"
// followed by the raw step bytes, CTRL1/2/7/8, STATUSINT/0/1 and the accelerometer's
// reading in mg, which say whether the sensor is sampling and the engine is on.
void stepsPrintStatus();
