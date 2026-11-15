// The sky's layout on the round panel (skylayout.h, #70): the sun and the moon clear of the
// header however long its text, inside the panel, clear of the sleeping "Zz" and the clock
// badge; the stars below the header; the longest title drawn small enough to fit. Pure numbers.
#include "framework.h"
#include "../clockprompt.h"
#include "../skylayout.h"
#include <string>

// the gap between a circle and a rectangle (negative: they overlap)
static int circleRectGap(int cx, int cy, int r, int x0, int y0, int x1, int y1) {
  int dx = cx < x0 ? x0 - cx : cx > x1 ? cx - x1 : 0;
  int dy = cy < y0 ? y0 - cy : cy > y1 ? cy - y1 : 0;
  int d = 0;
  while ((d + 1) * (d + 1) <= dx * dx + dy * dy) d++;
  return d - r;
}

// the circle lies wholly inside the round panel
static bool insidePanel(int cx, int cy, int r) {
  int dx = cx - SKY_PANEL_C, dy = cy - SKY_PANEL_C;
  return (dx * dx + dy * dy) <= (SKY_PANEL_R - r) * (SKY_PANEL_R - r);
}

TEST(SkyLayout, the_round_panels_chord) {
  CHECK_EQ(roundChordW(SKY_PANEL_C), 2 * SKY_PANEL_R);  // the widest row
  CHECK_EQ(roundChordW(52), 292);                       // 2 * floor(sqrt(233^2 - 181^2))
  CHECK_EQ(roundChordW(0), 0);
  CHECK_EQ(roundChordW(466), 0);
  CHECK_EQ(roundChordW(-5), 0);
  CHECK_EQ(roundChordW(52), roundChordW(2 * SKY_PANEL_C - 52));  // symmetric
}

// The sun and the moon sit below the header: the title and the status line can each run
// nearly the panel's width, so anything behind them above SKY_HEADER_BOTTOM would be covered.
TEST(SkyLayout, the_sun_and_the_moon_sit_below_the_header_inside_the_panel) {
  CHECK(SKY_HEADER_BOTTOM >= HEADER_STATUS_Y + 2 * 8);  // the status line is size 2: 16 px
  CHECK(SKY_SUN_Y - SKY_SUN_R >= SKY_HEADER_BOTTOM);
  CHECK(SKY_MOON_Y - SKY_MOON_R >= SKY_HEADER_BOTTOM);
  CHECK(insidePanel(SKY_SUN_X, SKY_SUN_Y, SKY_SUN_R));
  CHECK(insidePanel(SKY_MOON_X, SKY_MOON_Y, SKY_MOON_R));
  CHECK(SKY_SUN_Y + SKY_SUN_R < 232);  // above the horizon (HORIZON, scene.cpp)
  // clear of the sleeping "Zz" (size 3: 2 characters of 18 px, 24 px tall)
  CHECK(circleRectGap(SKY_MOON_X, SKY_MOON_Y, SKY_MOON_R, SLEEP_ZZ_X, SLEEP_ZZ_Y, SLEEP_ZZ_X + 36, SLEEP_ZZ_Y + 24) > 4);
  CHECK(circleRectGap(SKY_SUN_X, SKY_SUN_Y, SKY_SUN_R, SLEEP_ZZ_X, SLEEP_ZZ_Y, SLEEP_ZZ_X + 36, SLEEP_ZZ_Y + 24) > 4);
  // and of the clock badge on the other side (clockprompt.h)
  CHECK(SKY_MOON_X - SKY_MOON_R > CLOCK_BADGE_X + CLOCK_BADGE_R);
}

TEST(SkyLayout, the_stars_sit_below_the_header_and_off_the_moon) {
  for (auto &st : STARS) {
    std::string at = std::to_string(st[0]) + "," + std::to_string(st[1]);
    CHECK_MSG(st[1] >= SKY_HEADER_BOTTOM, "star " + at + " behind the header");
    CHECK_MSG(insidePanel(st[0] + SKY_STAR_SIZE / 2, st[1] + SKY_STAR_SIZE / 2, SKY_STAR_SIZE), "star " + at);
    CHECK_MSG(circleRectGap(SKY_MOON_X, SKY_MOON_Y, SKY_MOON_R, st[0], st[1], st[0] + SKY_STAR_SIZE, st[1] + SKY_STAR_SIZE) > 0,
              "star " + at + " on the moon");
  }
}

// Up to 16 characters fit the round edge at size 3 on the title's top row; longer titles
// (a 10-letter name at level 100, any shiny nickname) are drawn at size 2, and the longest,
// "*" + an 11-letter nickname + " Niv.100" (20 characters), fits there.
TEST(SkyLayout, a_title_too_wide_for_the_round_edge_is_drawn_smaller) {
  const int W3 = 18, W2 = 12;  // the classic font's 6 px a character, at sizes 3 and 2
  CHECK_EQ(headerTitleSize(16 * W3), (uint8_t)3);  // "BUTTERFREE Lv.99"
  CHECK_EQ(headerTitleSize(17 * W3), (uint8_t)2);  // "BUTTERFREE Lv.100"
  CHECK_EQ(headerTitleSize(18 * W3), (uint8_t)2);  // "*KANGASKHAN Lv.100"
  CHECK_EQ(headerTitleSize(0), (uint8_t)3);
  for (int n = 1; n <= 20; n++) {
    uint8_t s = headerTitleSize(n * W3);
    int w = n * (s == 3 ? W3 : W2), y = headerTitleY(s);
    CHECK_MSG(w + 2 * HEADER_TITLE_MARGIN <= roundChordW(y), std::to_string(n) + " characters at size " + std::to_string(s));
  }
  CHECK_EQ(headerTitleY(3), HEADER_TITLE_Y);
  CHECK_EQ(headerTitleY(2), HEADER_TITLE_Y + 4);  // centred in the size-3 band (24 px vs 16)
  CHECK(headerTitleY(2) + 2 * 8 <= HEADER_STATUS_Y);
}
