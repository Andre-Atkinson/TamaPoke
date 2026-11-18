// Walks (#117): the QMI8658's step engine on the 1.75C (steps.h). The simulator uses
// sim/steps_sim.cpp instead.
#include "steps.h"
#include "board.h"
#include <Arduino.h>

#if BOARD_AMOLED_175C
#include <Wire.h>
// SensorLib's current QMI8658 driver (the one ImuDrv.hpp includes). <SensorQMI8658.hpp>
// is the library's older driver, without the step engine's calls used here.
#include <sensor/imu/qmi8658/SensorQMI8658.hpp>

static SensorQMI8658 imu;
static bool engineOn = false;
static uint8_t imuAddr = 0;

// The registers this touches (SensorLib's SensorQMI8658_Reg.hpp).
static const uint8_t QMI_CTRL1 = 0x02;   // bit 0 sensor disable (oscillator off), bit 6 register auto-increment
static const uint8_t QMI_CTRL8 = 0x09;   // bit 7 CTRL9 handshake, bit 4 step engine on
static const uint8_t QMI_STEP_L = 0x5A;  // the step count: three bytes, little-endian

// The accelerometer's rate: normal mode with the gyroscope off, 62.5 Hz (QMI8658A datasheet
// Rev A, CTRL2). SensorLib names CTRL2's codes by the rates they give with the gyroscope on
// (28, 56, 112 Hz), so configAccel() takes 56 to pick this one; the step profile's sample
// counts are worked out from the real rate.
static const float ACCEL_CFG_HZ = 56.0f;
static const float ACCEL_HZ = 62.5f;

// A raw register read; -1 when it fails.
static int readReg(uint8_t reg) {
  Wire.beginTransmission(imuAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(imuAddr, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

static bool readRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(imuAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(imuAddr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(imuAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// The engine started at ACCEL_HZ with the datasheet's profile, its count at 0.
static bool configure() {
  engineOn = false;
  imu.disablePedometer();
  imu.disableAccel();
  // CTRL1: the oscillator on (bit 0 clear; the power-down before #117 set bits 0 and 1, and
  // the chip keeps its power across an app-only update) and the register address
  // auto-increment on (bit 6). SensorLib's reset() sets bit 6, but its wait can end on a
  // stale "reset done" and the reset then finishes later and clears it: seen on the board
  // (2026-11-18, CTRL1 0x20, every multi-byte read repeating one register). Without it,
  // SensorLib's two-byte writes of the step profile land in one register and nothing counts.
  int c1 = readReg(QMI_CTRL1);
  uint8_t want1 = (uint8_t)((c1 & ~0x03) | 0x40);
  if (c1 < 0 || (c1 != want1 && !writeReg(QMI_CTRL1, want1)) || readReg(QMI_CTRL1) != want1) {
    Serial.println("steps: CTRL1 failed");
    return false;
  }
  // CTRL8 bit 7: CTRL9 commands hand shake through STATUSINT bit 7, which SensorLib's
  // begin() sets and its commands wait on; the late reset clears it the same way.
  int c8 = readReg(QMI_CTRL8);
  if (c8 < 0 || (!(c8 & 0x80) && !writeReg(QMI_CTRL8, (uint8_t)(c8 | 0x80)))) {
    Serial.println("steps: CTRL8 failed");
    return false;
  }
  // The normal mode at 62.5 Hz, the gyroscope off (reset() leaves it off). The low-pass
  // filter OFF: SensorLib's default is its narrowest mode, 2.66% of the rate (the
  // datasheet's Table 10), 1.7 Hz at 62.5 Hz, which flattens the 1.5-2.5 Hz peaks of a
  // walk; off, the bandwidth is 27.5% of the rate. With it on, a 100-step walk counted 26
  // at 62.5 Hz (2026-11-18); with it off and this profile, 102.
  if (!imu.configAccel(AccelFullScaleRange::FS_4G, ACCEL_CFG_HZ, SensorQMI8658::LpfMode::OFF)) {
    Serial.println("steps: configAccel failed");
    return false;
  }
  if (!imu.enableAccel()) {
    Serial.println("steps: enableAccel failed");
    return false;
  }
  // The datasheet's example profile (QMI8658A Rev A, 11.1, given at 50 Hz), in seconds at
  // ACCEL_HZ: a 1 s window, a step's peak-to-peak 200 mg (0x00CC, u6.10 g) and peak 100 mg
  // (0x0066) over the average, a step at most 4 s and at least 0.4 s long, 10 steps in a row
  // before it counts. The count goes up every step (ped_sig_count 1; the datasheet's 4 makes
  // it go up in fours).
  if (!imu.configPedometer((uint16_t)(ACCEL_HZ + 0.5f), 0x00CC, 0x0066, (uint16_t)(ACCEL_HZ * 4.0f + 0.5f),
                           (uint8_t)(ACCEL_HZ * 0.4f + 0.5f), 10, 0, 1)) {
    Serial.println("steps: configPedometer failed");
    return false;
  }
  imu.resetStepCount();
  // Turning the engine on restarts its count at 0 (the datasheet, via SensorLib).
  if (!imu.enablePedometer()) {
    Serial.println("steps: enablePedometer failed");
    return false;
  }
  engineOn = true;
  Serial.println("steps: step engine on, 62.5 Hz");
  return true;
}

bool stepsBegin() {
  engineOn = false;
  // The two addresses the QMI8658 can answer on (SDO high or low), found by its WHO_AM_I
  // (register 0x00, 0x05), as the power-down in setup() finds it, so SensorLib's begin()
  // runs once.
  const uint8_t addrs[] = { 0x6B, 0x6A };
  for (uint8_t a : addrs) {
    imuAddr = a;
    if (readReg(0x00) == 0x05) break;
    imuAddr = 0;
  }
  // -1 for the pins: the sketch has already started Wire on the board's I2C pins, which
  // the touch, PMU and clock share.
  if (!imuAddr || !imu.begin(Wire, imuAddr, -1, -1)) {
    Serial.println("steps: no QMI8658");
    imuAddr = 0;
    return false;
  }
  Serial.printf("steps: QMI8658 at 0x%02X\n", imuAddr);
  // Time for a soft reset whose wait in begin() ended early to finish before configure()
  // sets the registers it would clear (above, CTRL1).
  delay(100);
  return configure();
}

bool stepsOn() { return engineOn; }

bool stepsRead(uint32_t *count) {
  uint8_t b[3];
  if (!engineOn || !readRegs(QMI_STEP_L, b, 3)) return false;
  *count = (uint32_t)b[2] << 16 | (uint32_t)b[1] << 8 | b[0];
  return true;
}

#else  // the original 1.75: no step counter

bool stepsBegin() { return false; }
bool stepsOn() { return false; }
bool stepsRead(uint32_t *) { return false; }

#endif
