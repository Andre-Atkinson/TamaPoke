// Where things sit in the main scene's sky on the round panel (#70): the header's title and
// status line, the sun, the moon, the stars and the sleeping "Zz". Pure numbers and helpers,
// host-tested in test/test_skylayout.cpp; scene.cpp and render() (TamaPoke.ino) draw them.
#pragma once
#include <stdint.h>

#define SKY_PANEL_C 233  // the round panel's centre, x and y
#define SKY_PANEL_R 233  // and its radius

// The width of the round panel's chord on row y, in px (0 outside the panel).
inline int roundChordW(int y) {
  int dy = y - SKY_PANEL_C, v = SKY_PANEL_R * SKY_PANEL_R - dy * dy;
  if (v <= 0) return 0;
  int r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return 2 * r;
}

// The header (drawHeader(), scene.cpp): the title at size 3 from y 52 and the status line
// at size 2 from y 90; the sky below SKY_HEADER_BOTTOM is clear of both, however long.
#define HEADER_TITLE_Y 52
#define HEADER_STATUS_Y 90
#define SKY_HEADER_BOTTOM 112
#define HEADER_TITLE_MARGIN 2  // px kept inside the panel's edge at each end

// A title too wide for size 3 on its top row (the longest: a shiny nickname at level 100,
// "*" + 11 letters + " Lv.100", 19 characters) is drawn at size 2, so the round edge never
// cuts its ends off. `w3` is its width at size 3.
inline uint8_t headerTitleSize(int w3) {
  return w3 <= roundChordW(HEADER_TITLE_Y) - 2 * HEADER_TITLE_MARGIN ? 3 : 2;
}
// a size-2 title sits in the middle of the size-3 title's band (8 px a row at size 1)
inline int headerTitleY(uint8_t size) { return HEADER_TITLE_Y + (3 - size) * 8 / 2; }

// The sun and the moon, right of the sky below the header, mirroring the clock badge on
// the left (clockprompt.h, x 76) and clear of the sleeping "Zz".
#define SKY_SUN_X 392
#define SKY_SUN_Y 146
#define SKY_SUN_R 26
#define SKY_MOON_X 392
#define SKY_MOON_Y 142
#define SKY_MOON_R 24

// the big "Zz" over a sleeping pet (render()): size 3, two characters
#define SLEEP_ZZ_X 320
#define SLEEP_ZZ_Y 130

// estrellas del modo noche, all below the header
inline const uint16_t STARS[][2] = { {120,140},{330,120},{370,210},{95,230},{250,128},{165,118} };
#define SKY_STAR_SIZE 4
