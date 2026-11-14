#include "framework.h"
#include "../settingslayout.h"
#include "../power.h"
#include "../touchlock.h"

#include <string>

// #11: the settings screen's power pills. renderClock() draws powerPillRect() and
// clockTap() hit-tests powerPillAt(), so these tests pin the geometry both share.

static const SetRect kRow1[] = {SND_PILL, BRI_PILL, LANG_PILL};

static bool overlaps(const SetRect &a, const SetRect &b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// every corner pixel of `r` at least SET_EDGE_MARGIN px inside the round panel
static bool cornersInside(const SetRect &r) {
  const int lim = SET_PANEL_R - SET_EDGE_MARGIN;
  for (int cx : {(int)r.x, r.x + r.w - 1})
    for (int cy : {(int)r.y, r.y + r.h - 1}) {
      int dx = cx - SET_PANEL_C, dy = cy - SET_PANEL_C;
      if (dx * dx + dy * dy > lim * lim) return false;
    }
  return true;
}

TEST(SettingsLayout, power_pills_are_centred_on_the_panel) {
  SetRect l = powerPillRect(PILL_TOUCH_LOCK), m = powerPillRect(PILL_SCREEN_OFF), r = powerPillRect(PILL_BATTERY);
  CHECK_EQ(m.x * 2 + m.w, 2 * SET_PANEL_C);        // the middle pill's centre is x = 233
  CHECK_EQ(l.x + (r.x + r.w), 2 * SET_PANEL_C);    // the outer two mirror each other
  CHECK_EQ(l.x, 67);
  CHECK_EQ(r.x + r.w, 399);
  CHECK(l.y == m.y && m.y == r.y);
}

TEST(SettingsLayout, power_pills_are_finger_sized) {
  for (int i = 0; i < POWER_PILL_COUNT; i++) {
    SetRect p = powerPillRect(i);
    CHECK(p.h >= 26 + 8);          // clearly bigger than the old 26 px
    CHECK(p.h >= LANG_PILL_H);     // no smaller than the row above
    CHECK(p.h >= OK_BTN.h);        // as tall as OK
    CHECK(p.w >= SND_PILL_W);      // as wide as the row above's narrowest
  }
}

TEST(SettingsLayout, every_corner_stays_inside_the_round_panel) {
  for (int i = 0; i < POWER_PILL_COUNT; i++) CHECK_MSG(cornersInside(powerPillRect(i)), "power pill " + std::to_string(i));
  for (const SetRect &r : kRow1) CHECK_MSG(cornersInside(r), "row 1 pill at x " + std::to_string(r.x));
  CHECK(cornersInside(OK_BTN));
}

TEST(SettingsLayout, power_pills_clear_the_row_above_ok_and_each_other) {
  for (int i = 0; i < POWER_PILL_COUNT; i++) {
    SetRect p = powerPillRect(i);
    for (const SetRect &r : kRow1) {
      CHECK(!overlaps(p, r));
      CHECK(p.y - (r.y + r.h) >= 6);  // a visible gap, not just no overlap
    }
    CHECK(!overlaps(p, OK_BTN));
    CHECK(OK_BTN.y - (p.y + p.h) >= 6);
    if (i + 1 < POWER_PILL_COUNT) {
      SetRect q = powerPillRect(i + 1);
      CHECK(!overlaps(p, q));
      CHECK(q.x - (p.x + p.w) >= 8);
    }
  }
}

// One finger tap reaches one thing: no screen pixel is in two of the bottom-half targets.
TEST(SettingsLayout, no_pixel_reaches_two_targets) {
  for (int y = 0; y < 2 * SET_PANEL_C; y++)
    for (int x = 0; x < 2 * SET_PANEL_C; x++) {
      int n = powerPillAt(x, y) != PILL_NONE;
      for (const SetRect &r : kRow1) n += r.contains(x, y);
      n += OK_BTN.contains(x, y);
      if (n > 1) {
        CHECK_MSG(false, "two targets at " + std::to_string(x) + "," + std::to_string(y));
        return;
      }
    }
}

// The hit area is exactly the drawn pill: its first and last pixel reach it, the next
// one out (the gap, the rows above and below) reaches nothing.
TEST(SettingsLayout, a_tap_reaches_exactly_the_drawn_pill) {
  CHECK_EQ(powerPillAt(117, 338), PILL_TOUCH_LOCK);
  CHECK_EQ(powerPillAt(233, 338), PILL_SCREEN_OFF);
  CHECK_EQ(powerPillAt(349, 338), PILL_BATTERY);
  for (int i = 0; i < POWER_PILL_COUNT; i++) {
    SetRect p = powerPillRect(i);
    int my = p.y + p.h / 2, mx = p.x + p.w / 2;
    CHECK_EQ(powerPillAt(p.x, my), (PowerPill)i);
    CHECK_EQ(powerPillAt(p.x + p.w - 1, my), (PowerPill)i);
    CHECK_EQ(powerPillAt(mx, p.y), (PowerPill)i);
    CHECK_EQ(powerPillAt(mx, p.y + p.h - 1), (PowerPill)i);
    CHECK_EQ(powerPillAt(p.x - 1, my), PILL_NONE);
    CHECK_EQ(powerPillAt(p.x + p.w, my), PILL_NONE);
    CHECK_EQ(powerPillAt(mx, p.y - 1), PILL_NONE);
    CHECK_EQ(powerPillAt(mx, p.y + p.h), PILL_NONE);
  }
  CHECK(OK_BTN.contains(OK_BTN.x + OK_BTN.w - 1, OK_BTN.y + OK_BTN.h - 1));
  CHECK(!OK_BTN.contains(OK_BTN.x + OK_BTN.w, OK_BTN.y));
  CHECK(!OK_BTN.contains(OK_BTN.x, OK_BTN.y + OK_BTN.h));
}

// The touch lock's 0 ms locks as the screen goes off: it is a delay of 0 s, not "off".
TEST(SettingsLayout, touch_lock_labels_say_seconds_and_only_never_is_off) {
  char b[12];
  CHECK(secondsLabel(b, sizeof(b), 0, TOUCH_LOCK_NEVER));
  CHECK_STREQ(b, "0s");
  CHECK(secondsLabel(b, sizeof(b), 10000, TOUCH_LOCK_NEVER));
  CHECK_STREQ(b, "10s");
  CHECK(secondsLabel(b, sizeof(b), 60000, TOUCH_LOCK_NEVER));
  CHECK_STREQ(b, "60s");
  CHECK(!secondsLabel(b, sizeof(b), TOUCH_LOCK_NEVER, TOUCH_LOCK_NEVER));
  CHECK_STREQ(b, "");
  // one full cycle: five settings, five different labels, one of them "off"
  std::string seen[5];
  int offs = 0;
  uint32_t ms = 10000;
  for (int i = 0; i < 5; i++, ms = touchLockNext(ms)) {
    offs += !secondsLabel(b, sizeof(b), ms, TOUCH_LOCK_NEVER);
    seen[i] = b;
    for (int j = 0; j < i; j++) CHECK(seen[i] != seen[j]);
  }
  CHECK_EQ(ms, 10000u);
  CHECK_EQ(offs, 1);
}

// Screen off's "never" (0 ms) shows "off", like the touch lock's.
TEST(SettingsLayout, screen_off_labels_say_seconds_and_never_is_off) {
  char b[12];
  CHECK(secondsLabel(b, sizeof(b), 60000, 0));
  CHECK_STREQ(b, "60s");
  CHECK(secondsLabel(b, sizeof(b), 15000, 0));
  CHECK_STREQ(b, "15s");
  CHECK(!secondsLabel(b, sizeof(b), 0, 0));
  CHECK_STREQ(b, "");
  std::string seen[4];
  int offs = 0;
  uint32_t ms = 60000;
  for (int i = 0; i < 4; i++, ms = powerOffNext(ms)) {
    offs += !secondsLabel(b, sizeof(b), ms, 0);
    seen[i] = b;
    for (int j = 0; j < i; j++) CHECK(seen[i] != seen[j]);
  }
  CHECK_EQ(ms, 60000u);
  CHECK_EQ(offs, 1);
}

TEST(SettingsLayout, battery_percent_label_matches_the_header) {
  char b[8];  // drawBattery()'s buffer size
  batteryPctLabel(b, sizeof(b), 87, false);
  CHECK_STREQ(b, "87%");
  batteryPctLabel(b, sizeof(b), 12, true);
  CHECK_STREQ(b, "12%+");
  batteryPctLabel(b, sizeof(b), 100, true);
  CHECK_STREQ(b, "100%+");
  batteryPctLabel(b, sizeof(b), 0, false);
  CHECK_STREQ(b, "0%");
  batteryPctLabel(b, sizeof(b), -1, false);  // no reading
  CHECK_STREQ(b, "%");
  batteryPctLabel(b, sizeof(b), -1, true);
  CHECK_STREQ(b, "%");
}
