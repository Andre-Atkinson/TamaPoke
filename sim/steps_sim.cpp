// Simulator: stands in for steps.cpp (the QMI8658's step engine through SensorLib, #117).
// The simulator has no motion sensor: the engine is on for --board 175c (the board that has
// one) and off for --board 175, and the count only moves when a script says so (`steps N`,
// sim/script.h, through simStepsAdd).
#include "../steps.h"
#include "../board.h"
#include <Arduino.h>
#include "simhw.h"

static uint32_t gChip = 0;
static bool gOn = false;

bool stepsBegin() {
  gChip = 0;
  gOn = BOARD_AMOLED_175C;
  return gOn;
}

bool stepsOn() { return gOn; }

bool stepsRead(uint32_t *count) {
  if (!gOn) return false;
  *count = gChip & 0xFFFFFFu;  // 24 bits, as the chip's
  return true;
}

void simStepsAdd(uint32_t n) { gChip += n; }
