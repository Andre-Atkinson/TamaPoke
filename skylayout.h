// Where things sit in the main scene's sky on the round panel (#70): below the header's name
// and status line, the sun, the moon and the stars, and the sleeping "Zz". Pure numbers and a
// helper, host-tested in test/test_skylayout.cpp; scene.cpp and render() (TamaPoke.ino) draw
// them. The name's own size and place are uitext.h's (headerSize(), HEADER_Y3/HEADER_Y2).
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

// The header (drawHeader(), scene.cpp): the name at size 3 from y 52 (or size 2 from y 56,
// uitext.h) and the status line at size 2 from y 90. Each can run nearly the panel's width,
// so the sky below SKY_HEADER_BOTTOM is clear of both, however long.
#define HEADER_STATUS_Y 90
#define SKY_HEADER_BOTTOM 112

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
