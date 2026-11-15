// The sky's layout on the round panel (skylayout.h, #70): the sun and the moon clear of the
// header however long its text, inside the panel, clear of the sleeping "Zz" and the clock
// badge; the stars below the header. Pure numbers.
#include "framework.h"
#include "../clockprompt.h"
#include "../skylayout.h"
#include "../uitext.h"  // the name's sizes and places (HEADER_Y3, HEADER_Y2)
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
  CHECK(HEADER_STATUS_Y >= HEADER_Y3 + 3 * 8);           // under the name at size 3 (24 px)...
  CHECK(HEADER_STATUS_Y >= HEADER_Y2 + 2 * 8);           // ...or at size 2
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
