#pragma once
#include <stdint.h>

// CO5300 power sequence. Brightness 0 (reg 0x51) leaves the panel scanning.
// Display off (0x28) then sleep-in (0x10) stops the OLED. Wake is sleep-out
// (0x11), 120 ms, display on (0x29), then brightness. The GFX displayOn()
// sends 0x29 before 0x11; do not use it for this.

enum PanelTok : uint8_t { PANEL_REG = 1, PANEL_CMD = 2, PANEL_WAIT = 3 };

struct PanelStep {
  uint8_t kind;
  uint8_t a;
  uint8_t b;
  uint16_t waitMs;
};

constexpr uint16_t PANEL_BUS_WAIT_MS = 120;

inline int panelCopy(PanelStep *out, int cap, const PanelStep *src, int n) {
  if (!out || cap < n) return 0;
  for (int i = 0; i < n; i++) out[i] = src[i];
  return n;
}

inline int panelSleepProgram(PanelStep *out, int cap) {
  const PanelStep s[] = {
      {PANEL_REG, 0x51, 0, 0},
      {PANEL_CMD, 0x28, 0, 0},
      {PANEL_WAIT, 0, 0, PANEL_BUS_WAIT_MS},
      {PANEL_CMD, 0x10, 0, 0},
      {PANEL_WAIT, 0, 0, PANEL_BUS_WAIT_MS},
  };
  return panelCopy(out, cap, s, 5);
}

inline int panelWakeProgram(PanelStep *out, int cap, uint8_t brightness) {
  const PanelStep s[] = {
      {PANEL_CMD, 0x11, 0, 0},
      {PANEL_WAIT, 0, 0, PANEL_BUS_WAIT_MS},
      {PANEL_CMD, 0x29, 0, 0},
      {PANEL_REG, 0x51, brightness, 0},
  };
  return panelCopy(out, cap, s, 4);
}

inline bool panelSleepEdge(bool &asleep, bool wantSleep) {
  if (wantSleep == asleep) return false;
  asleep = wantSleep;
  return true;
}
