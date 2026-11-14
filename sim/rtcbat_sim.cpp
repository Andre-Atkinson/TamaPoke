// Simulador: sustituye a rtcbat.cpp (AXP2101 + PCF85063 por XPowersLib/SensorLib).
// Misma API (rtcbat.h); el estado sale de gSimPower y del reloj falso de millis().
// El reloj cuenta con mockMillis64(): el millis() de 32 bits da la vuelta a los 49,7 dias
// y el reloj de software de la placa (time()) no.
// El reloj se comporta como el de software de la 1.75C: por debajo de 2025-01-01 no
// hay hora, y TIME con un valor asi no lo cambia.
#include "../rtcbat.h"
#include "simhw.h"

static const uint32_t kSoftMinEpoch = 1735689600UL;  // 2025-01-01, como rtcbat.cpp
static bool gClockSet = false;
static uint64_t gEpochMsAtSet = 0;  // hora (ms) cuando se puso
static uint64_t gMs64AtSet = 0;     // mockMillis64() en ese momento (64 bits: no da la vuelta)
static bool gPwrLatched = false;    // bit PKEY short de INTSTS2

SimPower gSimPower;  // aqui y no en simhw.cpp: asi los tests enlazan este fichero sin Arduino_GFX

void simClockSet(uint32_t epoch) {
  if (epoch < kSoftMinEpoch) { gClockSet = false; return; }
  gClockSet = true;
  gEpochMsAtSet = (uint64_t)epoch * 1000ULL;
  gMs64AtSet = mockMillis64();
}

bool rtcBegin() { return false; }

uint32_t rtcEpoch() {
  if (!gClockSet) return 0;
  return (uint32_t)((gEpochMsAtSet + (mockMillis64() - gMs64AtSet)) / 1000ULL);
}

void rtcSetEpoch(uint32_t e) {
  if (e < kSoftMinEpoch) return;
  simClockSet(e);
}

// pmuOk false ("pmu none" in a script) gives what rtcbat.cpp gives without the AXP2101:
// no battery reading, not charging, never a low-battery shutdown, and USB reads as present
bool batBegin() { return gSimPower.pmuOk; }
void pmuEnablePanel() {}
void batSetPollMs(uint32_t) {}
int batPercent() { return gSimPower.pmuOk ? gSimPower.batPct : -1; }
bool batCharging() { return gSimPower.pmuOk && gSimPower.charging && gSimPower.usb; }
int batMillivolts() {
  if (!gSimPower.pmuOk || gSimPower.batPct < 0) return 0;
  return 3300 + gSimPower.batPct * 9;  // aproximado: 3,3 V vacia, 4,2 V llena
}
bool batShouldShutdown() { return gSimPower.pmuOk && gSimPower.batPct == 0 && !gSimPower.usb; }
void batPowerOff() { if (gSimPower.pmuOk) gSimPower.poweredOff = true; }
bool usbPresent() { return gSimPower.pmuOk ? gSimPower.usb : true; }
bool usbPresentNow() { return gSimPower.pmuOk ? gSimPower.usb : true; }

void pwrSetup() { gPwrLatched = false; }
bool pwrShortPressed() {
  if (!gSimPower.pmuOk) return false;  // the PKEY is read from the AXP2101
  bool was = gPwrLatched;
  gPwrLatched = false;
  return was;
}
void simPwrPress() { gPwrLatched = true; }
