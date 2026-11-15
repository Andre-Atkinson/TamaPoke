// The standby clock (standby.h, power.h, #60): when it comes up, what keeps it, what it does
// to the power policy, and its burn-in steps. Pure logic.
#include "framework.h"
#include "../i18n.h"
#include "../power.h"
#include "../skylayout.h"  // roundChordW()
#include "../standby.h"
#include "../weather.h"    // civilDate()
#include <string>
#include <set>
#include <utility>

static StandbyIn plugged(uint32_t idle, uint32_t offMs) {
  StandbyIn s;
  s.usb = true;
  s.clockKnown = true;
  s.petAwake = true;
  s.screenOn = true;
  s.gameOpen = false;
  s.idleMs = idle;
  s.offMs = offMs;
  return s;
}

TEST(Standby, it_comes_up_when_the_screen_would_turn_off_or_at_90_s_with_never) {
  CHECK(!standbyStarts(plugged(59999, 60000)));
  CHECK(standbyStarts(plugged(60000, 60000)));
  CHECK(standbyStarts(plugged(15000, 15000)));  // the 15 s setting
  CHECK(!standbyStarts(plugged(89999, 0)));     // Never: at the first dim
  CHECK(standbyStarts(plugged(POWER_DIM1_MS, 0)));
  CHECK_EQ(standbyAfterMs(30000), 30000u);
  CHECK_EQ(standbyAfterMs(0), (uint32_t)POWER_DIM1_MS);
}

// Only plugged in, with a trusted clock, an awake pet, the screen on and no game; any of
// them going ends it (and the screen's usual rules apply again).
TEST(Standby, it_needs_power_a_clock_an_awake_pet_and_the_screen_on) {
  CHECK(standbyAllowed(plugged(70000, 60000)));
  StandbyIn s = plugged(70000, 60000);
  s.usb = false;  // on battery: nothing changes
  CHECK(!standbyAllowed(s));
  CHECK(!standbyStarts(s));
  s = plugged(70000, 60000);
  s.clockKnown = false;  // a reseeded clock would show a wrong time
  CHECK(!standbyAllowed(s));
  s = plugged(70000, 60000);
  s.petAwake = false;  // asleep: the screen goes dark as before
  CHECK(!standbyAllowed(s));
  s = plugged(70000, 60000);
  s.screenOn = false;  // PWR, or the pocket lock
  CHECK(!standbyAllowed(s));
  s = plugged(70000, 60000);
  s.gameOpen = true;
  CHECK(!standbyAllowed(s));
}

// While it is up: no timed screen-off, its own dim brightness, the nearly-off stage's 1 s
// frames and 80 MHz, no light sleep (on USB anyway), no amplifier.
TEST(Standby, the_power_policy_while_it_is_up) {
  PowerCtx c = {};
  c.usb = true;
  c.batPct = 90;
  c.offMs = 60000;
  c.idleMs = 70000;
  CHECK(autoScreenOff(c));  // without it the screen would go dark now
  c.standby = true;
  CHECK(!autoScreenOff(c));
  CHECK_EQ((int)dimStageNow(c), (int)DIM_OFF);
  CHECK_EQ((int)brightnessFor(c), STANDBY_BRIGHT);
  CHECK_EQ(frameIntervalMs(c), 1000u);
  CHECK_EQ(cpuMhzFor(c), 80u);
  CHECK(!amplifierWanted(c));
  CHECK(!lightSleepAllowed(c));
  c.userDim = 2;  // the settings' low level scales it like everything else
  CHECK_EQ((int)brightnessFor(c), STANDBY_BRIGHT * userBrightPct(2) / 100);
  c.userDim = 0;
  c.screenOff = true;  // PWR off wins
  CHECK_EQ((int)brightnessFor(c), 0);
  CHECK_EQ(frameIntervalMs(c), 0u);
  // with screen-off set to Never it comes at the first dim (90 s): it still reads as the
  // nearly-off stage (1 s frames, 80 MHz), not the first dim's 250 ms and 160 MHz
  c.offMs = 0;
  c.screenOff = false;
  c.idleMs = POWER_DIM1_MS + 10000;
  c.standby = false;
  CHECK_EQ((int)dimStageNow(c), (int)DIM_LOW);
  c.standby = true;
  CHECK_EQ((int)dimStageNow(c), (int)DIM_OFF);
  CHECK_EQ(frameIntervalMs(c), 1000u);
  CHECK_EQ(cpuMhzFor(c), 80u);
  // a dim the player can read, but well under the lit screen's
  PowerCtx lit = {};
  lit.usb = true;
  lit.batPct = 90;
  CHECK(STANDBY_BRIGHT > 8 && STANDBY_BRIGHT < brightnessFor(lit) / 2);
}

TEST(Standby, it_steps_a_few_px_each_minute_against_burn_in) {
  std::set<std::pair<int, int>> spots;
  std::set<int> xs, ys;  // it moves across and up and down, not along one line
  for (uint32_t m = 0; m < 2 * STANDBY_SHIFTS; m++) {
    int dx, dy;
    standbyShift(m, dx, dy);
    CHECK(dx >= -STANDBY_SHIFT_MAX && dx <= STANDBY_SHIFT_MAX && dy >= -STANDBY_SHIFT_MAX && dy <= STANDBY_SHIFT_MAX);
    int px, py;
    standbyShift(m + 1, px, py);
    CHECK(px != dx || py != dy);  // it moves every minute
    spots.insert({ dx, dy });
    xs.insert(dx);
    ys.insert(dy);
  }
  CHECK_EQ((int)spots.size(), STANDBY_SHIFTS);
  CHECK((int)xs.size() >= 5);
  CHECK((int)ys.size() >= 5);
}

// The date in both languages: "Sat 15 Nov", "s\240b, 15 nov" (European Portuguese, in lower
// case, \240 is CP437's a-acute: one byte a letter, never UTF-8).
TEST(Standby, the_date_in_english_and_portuguese) {
  char b[STANDBY_DATE_MAX];
  Lang was = gLang;
  gLang = LANG_EN;
  formatDate(b, sizeof b, 6, 15, 11);
  CHECK_STREQ(b, "Sat 15 Nov");
  formatDate(b, sizeof b, 0, 1, 1);
  CHECK_STREQ(b, "Sun 1 Jan");
  gLang = LANG_PT;
  formatDate(b, sizeof b, 6, 15, 11);
  CHECK_STREQ(b, "s\240b, 15 nov");
  formatDate(b, sizeof b, 3, 30, 9);
  CHECK_STREQ(b, "qua, 30 set");
  for (uint8_t bad : { 0, 13 }) {  // a month out of range
    formatDate(b, sizeof b, 1, 1, bad);
    CHECK_STREQ(b, "?");
  }
  formatDate(b, sizeof b, 7, 1, 1);  // a weekday out of range
  CHECK_STREQ(b, "?");
  formatDate(b, sizeof b, 1, 0, 1);  // a day out of range
  CHECK_STREQ(b, "?");
  formatDate(b, 0, 1, 1, 1);  // no room: nothing written
  gLang = was;
}

// Every date of two years, in both languages, and every time fit the round screen at their
// rows, wherever the burn-in step puts them.
TEST(Standby, every_date_and_time_fits_the_round_screen) {
  auto fits = [](int w, int y, int h) {
    for (int dy = -STANDBY_SHIFT_MAX; dy <= STANDBY_SHIFT_MAX; dy++)
      for (int row : { y + dy, y + dy + h - 1 })
        if (w + 2 * STANDBY_SHIFT_MAX + 4 > roundChordW(row)) return false;
    return true;
  };
  CHECK(fits(5 * 6 * 9, STANDBY_TIME_Y, 8 * 9));  // "23:59" at size 9: every time is 5 characters
  Lang was = gLang;
  size_t longest = 0;
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (uint32_t e = 1767225600UL; e < 1767225600UL + 731 * 86400UL; e += 86400UL) {  // 2026-2027
      CivilDate d = civilDate(e);
      char b[STANDBY_DATE_MAX];
      formatDate(b, sizeof b, d.weekday, d.day, d.month);
      size_t n = strlen(b);
      longest = n > longest ? n : longest;
      CHECK_MSG(n > 5 && fits((int)n * 6 * 3, STANDBY_DATE_Y, 8 * 3), std::string("'") + b + "'");
      for (const char *c = b; *c; c++)  // one byte a letter: ASCII, or CP437's a-acute
        CHECK((uint8_t)*c < 0x80 || (uint8_t)*c == 0240);
    }
  }
  CHECK_EQ((int)longest, 11);  // "qua, 30 set"
  gLang = was;
  // the lines don't run into each other: time, date, weather, battery, top to bottom
  CHECK(STANDBY_TIME_Y + 8 * 9 + 2 * STANDBY_SHIFT_MAX < STANDBY_DATE_Y);
  CHECK(STANDBY_DATE_Y + 8 * 3 < STANDBY_WX_Y - 32);
  CHECK(STANDBY_WX_Y + 34 < STANDBY_BAT_Y);
  CHECK(fits(32 + 8 + 5 * 12, STANDBY_BAT_Y, 16));  // the glyph and "100%+"
}
