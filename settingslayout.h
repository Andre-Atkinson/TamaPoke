#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Bottom half of the settings screen (the clock screen, opened by swiping down): two
// rows of pills and the OK button. renderClock() draws these rects and clockTap()
// hit-tests the very same rects, so what the player sees and what a tap reaches cannot
// drift apart. Pure (no hardware), tested on the PC (test/test_settingslayout.cpp).
//
//   y 282..311  sound | brightness | language
//   y 320..355  touch lock | screen off | battery   (the power pills, #11)
//   y 366..401  OK
//
// Both rows are centred on the panel's x = 233, and every corner stays at least
// SET_EDGE_MARGIN px inside the round panel (radius 233 around (233,233)). The first
// row sat at y 296 until #11; it moved up 14 px so the power pills could grow from
// 26 px to 36 px (the OK button's height) without moving OK or the text under it.

constexpr int SET_PANEL_C = 233;     // centre of the round panel, x and y
constexpr int SET_PANEL_R = 233;     // its radius
constexpr int SET_EDGE_MARGIN = 12;  // minimum gap from any corner to the panel edge

struct SetRect {
  int16_t x, y, w, h;
  // exactly the pixels fillRoundRect(x, y, w, h, ...) covers (its bounding box)
  constexpr bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

// first row: sound (cycles high, medium, low, off), brightness, language
#define LANG_PILL_Y 282
#define LANG_PILL_H 30
#define LANG_PILL_X 336  // language pill (cycles LANG_COUNT on a tap)
#define LANG_PILL_W 96
#define SND_PILL_X 34    // sound pill
#define SND_PILL_W 96
#define BRI_PILL_X 183   // brightness pill, centred
#define BRI_PILL_W 100
constexpr SetRect SND_PILL = {SND_PILL_X, LANG_PILL_Y, SND_PILL_W, LANG_PILL_H};
constexpr SetRect BRI_PILL = {BRI_PILL_X, LANG_PILL_Y, BRI_PILL_W, LANG_PILL_H};
constexpr SetRect LANG_PILL = {LANG_PILL_X, LANG_PILL_Y, LANG_PILL_W, LANG_PILL_H};

// second row, the power pills (#11): an icon plus a number, like the sound and
// brightness pills above them. Left to right:
enum PowerPill {
  PILL_NONE = -1,
  PILL_TOUCH_LOCK = 0,  // padlock + seconds after the screen goes off, or "off"
  PILL_SCREEN_OFF = 1,  // screen + seconds without a touch, or "off" (never)
  PILL_BATTERY = 2,     // the header's battery glyph, or its "87%"
  POWER_PILL_COUNT = 3,
};
constexpr int16_t POWER_PILL_Y = 320;
constexpr int16_t POWER_PILL_H = 36;  // was 26; now the OK button's height
constexpr int16_t POWER_PILL_W = 100;
constexpr int16_t POWER_PILL_GAP = 16;
constexpr int16_t POWER_ROW_W = POWER_PILL_COUNT * POWER_PILL_W + (POWER_PILL_COUNT - 1) * POWER_PILL_GAP;

// i in 0..POWER_PILL_COUNT-1 (PowerPill), left to right
constexpr SetRect powerPillRect(int i) {
  return {(int16_t)(SET_PANEL_C - POWER_ROW_W / 2 + i * (POWER_PILL_W + POWER_PILL_GAP)), POWER_PILL_Y,
          POWER_PILL_W, POWER_PILL_H};
}

// the power pill under (x, y), or PILL_NONE (the gaps between pills reach nothing)
inline PowerPill powerPillAt(int x, int y) {
  for (int i = 0; i < POWER_PILL_COUNT; i++)
    if (powerPillRect(i).contains(x, y)) return (PowerPill)i;
  return PILL_NONE;
}

constexpr SetRect OK_BTN = {133, 366, 200, 36};

// The number beside the padlock or the screen icon: "60s" for 60000 ms. False (and an
// empty string) when `ms` is that setting's "off" value; the caller then prints
// T(S_TLOCK_OFF), the one translated label, for both pills. The touch lock's 0 ms is
// not "off": it locks the moment the screen goes off, so it reads "0s" beside a closed
// padlock, while "off" sits beside an open one.
inline bool secondsLabel(char *buf, size_t n, uint32_t ms, uint32_t offValue) {
  if (!n) return false;
  if (ms == offValue) {
    buf[0] = '\0';
    return false;
  }
  snprintf(buf, n, "%lus", (unsigned long)(ms / 1000));
  return true;
}

// The header's battery percent ("87%", "87%+" while charging), which the battery pill
// shows as its preview too. With no reading (pc < 0) the header shows nothing at all,
// and the pill shows a bare "%".
inline void batteryPctLabel(char *buf, size_t n, int pc, bool charging) {
  if (!n) return;
  if (pc < 0) snprintf(buf, n, "%%");
  else snprintf(buf, n, charging ? "%d%%+" : "%d%%", pc);
}
