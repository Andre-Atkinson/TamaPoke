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

// The registers the status line shows (SensorLib's SensorQMI8658_Reg.hpp).
static const uint8_t QMI_CTRL2 = 0x03;  // accelerometer range (bits 6-4) and rate (bits 3-0)
static const uint8_t QMI_CTRL7 = 0x08;  // bit 0 accelerometer on, bit 1 gyroscope on
static const uint8_t QMI_CTRL8 = 0x09;  // bit 4 step engine on
static const uint8_t QMI_ODR_LP_21HZ = 0x0D;
static const float STEPS_ODR_HZ = STEPS_ACC_LP ? 21.0f : 28.0f;

// A raw register read, for the status line; -1 when the read fails.
static int readReg(uint8_t reg) {
  Wire.beginTransmission(imuAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(imuAddr, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

static bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(imuAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
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
  // The accelerometer's normal mode at the rate below 28 Hz SensorLib rounds to; the
  // gyroscope stays off (reset() leaves it off), which the low-power mode needs.
  if (!imu.configAccel(AccelFullScaleRange::FS_4G, 28.0f)) {
    Serial.println("steps: configAccel failed");
    return false;
  }
#if STEPS_ACC_LP
  // Low-power 21 Hz: the rate bits of CTRL2, with the accelerometer still off.
  int c2 = readReg(QMI_CTRL2);
  if (c2 < 0 || !writeReg(QMI_CTRL2, (uint8_t)((c2 & 0xF0) | QMI_ODR_LP_21HZ))) {
    Serial.println("steps: low-power rate failed");
    return false;
  }
#endif
  if (!imu.enableAccel()) {
    Serial.println("steps: enableAccel failed");
    return false;
  }
  // SensorLib's default walking profile, its time limits worked out from the rate.
  if (!imu.configPedometerDefault(STEPS_ODR_HZ)) {
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
  Serial.printf("steps: QMI8658 0x%02X step engine on, accel %s %.0f Hz\n", imuAddr,
                STEPS_ACC_LP ? "low-power" : "normal", STEPS_ODR_HZ);
  return true;
}

bool stepsOn() { return engineOn; }

uint32_t stepsChipCount() {
  return engineOn ? imu.getStepCount() : 0;
}

void stepsPrintStatus() {
  if (!imuAddr) {
    Serial.println("STEPS on=0 chip=0 (no QMI8658)");
    return;
  }
  Serial.printf("STEPS on=%d chip=%lu ctrl2=0x%02X ctrl7=0x%02X ctrl8=0x%02X\n", engineOn ? 1 : 0,
                (unsigned long)stepsChipCount(), readReg(QMI_CTRL2) & 0xFF,
                readReg(QMI_CTRL7) & 0xFF, readReg(QMI_CTRL8) & 0xFF);
}

#else  // the original 1.75: no step counter

bool stepsBegin() { return false; }
bool stepsOn() { return false; }
uint32_t stepsChipCount() { return 0; }
void stepsPrintStatus() { Serial.println("STEPS on=0 chip=0 (no step counter on this board)"); }

#endif
