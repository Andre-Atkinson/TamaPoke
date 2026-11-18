// Walks (#117): the QMI8658's step engine on the 1.75C (steps.h). The simulator uses
// sim/steps_sim.cpp instead.
#include "steps.h"
#include "board.h"
#include <Arduino.h>

#if BOARD_AMOLED_175C
#include <Wire.h>
#include <string.h>
// SensorLib's current QMI8658 driver (the one ImuDrv.hpp includes). <SensorQMI8658.hpp>
// is the library's older driver, without the step engine's calls used here.
#include <sensor/imu/qmi8658/SensorQMI8658.hpp>

static SensorQMI8658 imu;
static bool engineOn = false;
static uint8_t imuAddr = 0;

// The registers the status line shows (SensorLib's SensorQMI8658_Reg.hpp).
static const uint8_t QMI_CTRL1 = 0x02;  // bit 0 sensor disable (oscillator off), bit 1 reserved
static const uint8_t QMI_CTRL2 = 0x03;  // accelerometer range (bits 6-4) and rate (bits 3-0)
static const uint8_t QMI_CTRL7 = 0x08;  // bit 0 accelerometer on, bit 1 gyroscope on
static const uint8_t QMI_CTRL8 = 0x09;  // bit 4 step engine on
static const uint8_t QMI_STATUSINT = 0x2D;
static const uint8_t QMI_STATUS0 = 0x2E;  // bit 0 new accelerometer data
static const uint8_t QMI_STATUS1 = 0x2F;  // the motion events, the step engine's among them
static const uint8_t QMI_AX_L = 0x35;     // AX, AY, AZ: six bytes, little-endian
static const uint8_t QMI_STEP_L = 0x5A;   // the step count: three bytes, little-endian

// The accelerometer's rates with the gyroscope off (QMI8658A datasheet Rev A, CTRL2):
// SensorLib names CTRL2's codes by the rates they give with the gyroscope on (28, 56, 112 Hz),
// so `cfgHz` is what SensorLib's configAccel() takes to pick the code, and `hz` the real
// rate, which the step profile's sample counts are worked out from.
struct StepRate {
  const char *name;
  float hz;
  float cfgHz;
  uint8_t lpCode;  // CTRL2's low-power rate code, 0 for the normal mode
};
static const StepRate RATES[] = {
  { "lp21", 21.0f, 28.0f, 0x0D }, { "lp128", 128.0f, 28.0f, 0x0C },
  { "31", 31.25f, 28.0f, 0 },     { "62", 62.5f, 56.0f, 0 },        { "125", 125.0f, 112.0f, 0 },
};
static const uint8_t RATE_COUNT = sizeof(RATES) / sizeof(RATES[0]);
static const char *const PROFILES[] = { "ds", "lib", "easy" };
enum : uint8_t { PROF_DS = 0, PROF_LIB = 1, PROF_EASY = 2, PROF_COUNT = 3 };
static const uint8_t BOOT_RATE = 3;  // 62.5 Hz
static uint8_t curRate = BOOT_RATE;
static uint8_t curProf = PROF_DS;

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

// The engine (re)started at RATES[r] with profile p, its count at 0.
static bool configure(uint8_t r, uint8_t p) {
  const StepRate &rt = RATES[r];
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
  // The normal mode at the rate (the low-power one starts from SensorLib's lowest), the
  // gyroscope off (reset() leaves it off), which the low-power mode needs. The low-pass
  // filter OFF: SensorLib's default is its narrowest mode, 2.66% of the rate (the
  // datasheet's Table 10), 1.7 Hz at 62.5 Hz, which flattens the 1.5-2.5 Hz peaks of a
  // walk; off, the bandwidth is 27.5% of the rate. With it on, a 100-step walk counted 26
  // at 62.5 Hz (2026-11-18), and every rate counted more the faster it ran.
  if (!imu.configAccel(AccelFullScaleRange::FS_4G, rt.cfgHz, SensorQMI8658::LpfMode::OFF)) {
    Serial.println("steps: configAccel failed");
    return false;
  }
  if (rt.lpCode) {  // the rate bits of CTRL2, with the accelerometer still off
    int c2 = readReg(QMI_CTRL2);
    if (c2 < 0 || !writeReg(QMI_CTRL2, (uint8_t)((c2 & 0xF0) | rt.lpCode))) {
      Serial.println("steps: low-power rate failed");
      return false;
    }
  }
  if (!imu.enableAccel()) {
    Serial.println("steps: enableAccel failed");
    return false;
  }
  bool ok;
  if (p == PROF_LIB) {
    ok = imu.configPedometerDefault(rt.hz);
  } else if (p == PROF_EASY) {
    // SensorLib's example's "debug" thresholds (80 and 60 mg), counting from the first step,
    // over the datasheet's 1 s window and time limits
    ok = imu.configPedometer((uint16_t)(rt.hz + 0.5f), 80, 60, (uint16_t)(rt.hz * 4.0f + 0.5f),
                             (uint8_t)(rt.hz * 0.4f + 0.5f), 1, 0, 1);
  } else {
    // The datasheet's example profile (QMI8658A Rev A, 11.1, given at 50 Hz), in seconds: a
    // 1 s window, a step's peak-to-peak 200 mg (0x00CC, u6.10 g) and peak 100 mg (0x0066)
    // over the average, a step at most 4 s and at least 0.4 s long, 10 steps in a row before
    // it counts. The count goes up every step (ped_sig_count 1; the datasheet's 4 makes it go
    // up in fours).
    ok = imu.configPedometer((uint16_t)(rt.hz + 0.5f), 0x00CC, 0x0066, (uint16_t)(rt.hz * 4.0f + 0.5f),
                             (uint8_t)(rt.hz * 0.4f + 0.5f), 10, 0, 1);
  }
  if (!ok) {
    Serial.println("steps: configPedometer failed");
    return false;
  }
  imu.resetStepCount();
  // Turning the engine on restarts its count at 0 (the datasheet, via SensorLib).
  if (!imu.enablePedometer()) {
    Serial.println("steps: enablePedometer failed");
    return false;
  }
  curRate = r;
  curProf = p;
  engineOn = true;
  Serial.printf("steps: step engine on, accel %s, profile %s\n", rt.name, PROFILES[p]);
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
  return configure(BOOT_RATE, PROF_DS);
}

bool stepsOn() { return engineOn; }

uint32_t stepsChipCount() {
  return engineOn ? imu.getStepCount() : 0;
}

bool stepsSetRate(const char *rate, const char *profile) {
  if (!imuAddr) return false;
  uint8_t r = RATE_COUNT, p = PROF_DS;
  for (uint8_t i = 0; i < RATE_COUNT; i++)
    if (!strcmp(rate, RATES[i].name)) r = i;
  if (r == RATE_COUNT) return false;
  if (profile && *profile) {
    p = PROF_COUNT;
    for (uint8_t i = 0; i < PROF_COUNT; i++)
      if (!strcmp(profile, PROFILES[i])) p = i;
    if (p == PROF_COUNT) return false;
  }
  return configure(r, p);
}

void stepsPrintStatus() {
  if (!imuAddr) {
    Serial.println("STEPS on=0 chip=0 (no QMI8658)");
    return;
  }
  uint8_t st[3] = { 0, 0, 0 }, ax[6] = { 0, 0, 0, 0, 0, 0 }, sc[3] = { 0, 0, 0 };
  bool stOk = readRegs(QMI_STATUSINT, st, 3);
  bool axOk = readRegs(QMI_AX_L, ax, 6);
  bool scOk = readRegs(QMI_STEP_L, sc, 3);
  // mg at the +-4 g range: 8192 counts a g
  int x = (int16_t)(ax[0] | ax[1] << 8) * 1000 / 8192;
  int y = (int16_t)(ax[2] | ax[3] << 8) * 1000 / 8192;
  int z = (int16_t)(ax[4] | ax[5] << 8) * 1000 / 8192;
  Serial.printf("STEPS on=%d chip=%lu rate=%s profile=%s raw=%02X%02X%02X%s ctrl1=0x%02X ctrl2=0x%02X "
                "ctrl7=0x%02X ctrl8=0x%02X stint=0x%02X st0=0x%02X st1=0x%02X%s acc_mg=%d,%d,%d%s "
                "acc_raw=%02X%02X.%02X%02X.%02X%02X\n",
                engineOn ? 1 : 0, (unsigned long)stepsChipCount(), RATES[curRate].name,
                PROFILES[curProf], sc[2], sc[1], sc[0], scOk ? "" : "(failed)",
                readReg(QMI_CTRL1) & 0xFF, readReg(QMI_CTRL2) & 0xFF, readReg(QMI_CTRL7) & 0xFF,
                readReg(QMI_CTRL8) & 0xFF, st[0], st[1], st[2], stOk ? "" : "(failed)", x, y, z,
                axOk ? "" : "(failed)", ax[0], ax[1], ax[2], ax[3], ax[4], ax[5]);
}

#else  // the original 1.75: no step counter

bool stepsBegin() { return false; }
bool stepsOn() { return false; }
uint32_t stepsChipCount() { return 0; }
bool stepsSetRate(const char *, const char *) { return false; }
void stepsPrintStatus() { Serial.println("STEPS on=0 chip=0 (no step counter on this board)"); }

#endif
