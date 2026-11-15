// The standby clock (standby.h, power.h, #60): when it comes up, what keeps it, what it does
// to the power policy, and its burn-in steps. Pure logic.
#include "framework.h"
#include "../power.h"
#include "../standby.h"
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
  for (uint32_t m = 0; m < 2 * STANDBY_SHIFTS; m++) {
    int dx, dy;
    standbyShift(m, dx, dy);
    CHECK(dx >= -STANDBY_SHIFT_MAX && dx <= STANDBY_SHIFT_MAX && dy >= -STANDBY_SHIFT_MAX && dy <= STANDBY_SHIFT_MAX);
    int px, py;
    standbyShift(m + 1, px, py);
    CHECK(px != dx || py != dy);  // it moves every minute
    spots.insert({ dx, dy });
  }
  CHECK_EQ((int)spots.size(), STANDBY_SHIFTS);
}
