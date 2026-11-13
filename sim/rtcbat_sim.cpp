// Simulador: sustituye a rtcbat.cpp (AXP2101 + PCF85063 por XPowersLib/SensorLib).
// Misma API (rtcbat.h); el estado sale de gSimPower y del reloj falso de millis().
// El reloj se comporta como el de software de la 1.75C: por debajo de 2025-01-01 no
// hay hora, y TIME con un valor asi no lo cambia.
#include "../rtcbat.h"
#include "simhw.h"

static const uint32_t kSoftMinEpoch = 1735689600UL;  // 2025-01-01, como rtcbat.cpp
static bool gClockSet = false;
static uint64_t gEpochMsAtSet = 0;  // hora (ms) cuando se puso
static uint32_t gMillisAtSet = 0;   // millis() en ese momento
static bool gPwrLatched = false;    // bit PKEY short de INTSTS2

void simClockSet(uint32_t epoch) {
  if (epoch < kSoftMinEpoch) { gClockSet = false; return; }
  gClockSet = true;
  gEpochMsAtSet = (uint64_t)epoch * 1000ULL;
  gMillisAtSet = millis();
}

bool rtcBegin() { return false; }

uint32_t rtcEpoch() {
  if (!gClockSet) return 0;
  return (uint32_t)((gEpochMsAtSet + (uint32_t)(millis() - gMillisAtSet)) / 1000ULL);
}

void rtcSetEpoch(uint32_t e) {
  if (e < kSoftMinEpoch) return;
  simClockSet(e);
}

bool batBegin() { return true; }
void pmuEnablePanel() {}
void batSetPollMs(uint32_t) {}
int batPercent() { return gSimPower.batPct; }
bool batCharging() { return gSimPower.charging && gSimPower.usb; }
int batMillivolts() {
  if (gSimPower.batPct < 0) return 0;
  return 3300 + gSimPower.batPct * 9;  // aproximado: 3,3 V vacia, 4,2 V llena
}
bool batShouldShutdown() { return gSimPower.batPct == 0 && !gSimPower.usb; }
void batPowerOff() { gSimPower.poweredOff = true; }
bool usbPresent() { return gSimPower.usb; }
bool usbPresentNow() { return gSimPower.usb; }

void pwrSetup() { gPwrLatched = false; }
bool pwrShortPressed() {
  bool was = gPwrLatched;
  gPwrLatched = false;
  return was;
}
void simPwrPress() { gPwrLatched = true; }
