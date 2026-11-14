// Asking for the time after a reseed (clockprompt.h, #10). Pure logic.
#include "framework.h"
#include "../clockprompt.h"

static ClockPromptHold nothingUp() { return ClockPromptHold(); }

// ---------------------------------------------------------------- automatic opening
TEST(ClockPrompt, a_reseeded_clock_opens_the_clock_screen_once) {
  ClockPrompt p;
  p.boot(true, true);
  CHECK(p.due(true, nothingUp()));
  CHECK(!p.due(true, nothingUp()));  // closed without setting the time: not again
  CHECK(!p.due(true, nothingUp()));
}

TEST(ClockPrompt, a_clock_with_its_time_never_asks) {
  ClockPrompt p;
  p.boot(false, true);
  CHECK(!p.due(false, nothingUp()));
  CHECK(!p.asking(false));
}

TEST(ClockPrompt, a_clock_that_cannot_be_set_never_asks) {
  // original 1.75 with no PCF85063: the clock keeps reading 0, setting it cannot help
  ClockPrompt p;
  p.boot(true, false);
  CHECK(!p.due(true, nothingUp()));
  CHECK(!p.asking(true));
}

TEST(ClockPrompt, each_boot_state_holds_it_back_until_it_ends) {
  const char *names[] = { "choosing", "ceremony", "evolving", "dialog", "otherScreen" };
  for (int i = 0; i < 5; i++) {
    ClockPrompt p;
    p.boot(true, true);
    ClockPromptHold h;
    bool *f[] = { &h.choosing, &h.ceremony, &h.evolving, &h.dialog, &h.otherScreen };
    *f[i] = true;
    CHECK_MSG(!p.due(true, h), std::string("opened over ") + names[i]);
    CHECK_MSG(!p.due(true, h), std::string("opened over ") + names[i] + " (second pass)");
    *f[i] = false;
    CHECK_MSG(p.due(true, h), std::string("never opened after ") + names[i]);
  }
}

TEST(ClockPrompt, setting_the_clock_first_cancels_the_opening) {
  // held behind the egg choice, then the owner sends TIME: nothing left to ask
  ClockPrompt p;
  p.boot(true, true);
  ClockPromptHold h;
  h.choosing = true;
  CHECK(!p.due(true, h));
  CHECK(!p.due(false, nothingUp()));
  CHECK(!p.asking(false));
}

TEST(ClockPrompt, opening_it_by_hand_uses_up_the_automatic_opening) {
  // swiped down to the clock screen while the prompt was held, then swiped up
  ClockPrompt p;
  p.boot(true, true);
  p.opened();
  CHECK(!p.due(true, nothingUp()));
  CHECK(p.asking(true));
}

TEST(ClockPrompt, skipping_keeps_the_badge_until_the_clock_is_set) {
  ClockPrompt p;
  p.boot(true, true);
  CHECK(p.due(true, nothingUp()));
  CHECK(p.asking(true));   // skipped: the badge stays
  CHECK(!p.asking(false)); // set by any route: gone
}

// ---------------------------------------------------------------- across soft resets
TEST(ClockStaleMark, power_on_garbage_reads_as_not_stale) {
  const ClockStaleMark garbage[] = {
    { 0, 0 },
    { 0xFFFFFFFFUL, 0xFFFFFFFFUL },
    { CLOCK_STALE_MAGIC, 0 },                    // magic alone
    { CLOCK_STALE_MAGIC, CLOCK_STALE_MAGIC },
    { 0, (uint32_t)~CLOCK_STALE_MAGIC },         // check alone
    { (uint32_t)~CLOCK_STALE_MAGIC, CLOCK_STALE_MAGIC },  // swapped
  };
  for (const ClockStaleMark &m : garbage) {
    CHECK(!clockMarkedStale(m));
    CHECK(!clockStaleAtBoot(false, m));
  }
}

TEST(ClockStaleMark, marking_survives_and_clearing_forgets) {
  ClockStaleMark m = { 0x12345678UL, 0x9ABCDEF0UL };
  clockMarkStale(m, true);
  CHECK(clockMarkedStale(m));
  CHECK(clockStaleAtBoot(false, m));  // soft reset with the clock still not set
  clockMarkStale(m, false);
  CHECK(!clockMarkedStale(m));
  CHECK(!clockStaleAtBoot(false, m));  // set before the reset: trusted
}

TEST(ClockStaleMark, a_reseed_is_stale_whatever_the_mark_says) {
  ClockStaleMark m;
  clockMarkStale(m, false);
  CHECK(clockStaleAtBoot(true, m));
  m = { 0xDEADBEEFUL, 0x01234567UL };
  CHECK(clockStaleAtBoot(true, m));
}

// ---------------------------------------------------------------- did the set take
TEST(ClockSet, a_set_counts_only_if_the_clock_reads_it_back) {
  const uint32_t e = 1794564000UL;  // 2026-11-13 10:00
  CHECK(clockSetTook(e, e));
  CHECK(clockSetTook(e, e + 5));   // a slow serial line, a second ticking over
  CHECK(clockSetTook(e + 5, e));
  CHECK(!clockSetTook(e, e + 6));
  CHECK(!clockSetTook(e + 6, e));
  CHECK(!clockSetTook(e, 0));      // missing PCF85063: still no time
  CHECK(!clockSetTook(0, 0));      // TIME 0 with no clock: reading 0 back is not a set
  CHECK(!clockSetTook(1000, 0));   // 1.75C: TIME before 2025 is ignored
  CHECK(!clockSetTook(1000, e));   // ... and the clock keeps its old time
}

// ---------------------------------------------------------------- the badge
TEST(ClockBadge, finger_sized_inside_the_panel_and_clear_of_the_pet_and_buttons) {
  CHECK(CLOCK_BADGE_HIT >= 30);  // at least 60 px across
  CHECK(CLOCK_BADGE_HIT > CLOCK_BADGE_R);
  // the whole hit circle on the round 466 px panel (centre 233,233), with 8 px to spare
  int dx = CLOCK_BADGE_X - 233, dy = CLOCK_BADGE_Y - 233;
  int reach = CLOCK_BADGE_HIT + 8;
  CHECK(dx * dx + dy * dy <= (233 - reach) * (233 - reach));
  CHECK(CLOCK_BADGE_X + CLOCK_BADGE_HIT <= 110);  // inPetZone starts at x > 110
  CHECK(CLOCK_BADGE_Y + CLOCK_BADGE_HIT < 172);   // EVO_BTN_Y, FAR_BTN_Y (176)
  CHECK(CLOCK_BADGE_Y - CLOCK_BADGE_R >= 100);    // below the battery and the name
}

TEST(ClockBadge, a_tap_hits_inside_the_touch_circle_only) {
  CHECK(clockBadgeHit(CLOCK_BADGE_X, CLOCK_BADGE_Y));
  CHECK(clockBadgeHit(CLOCK_BADGE_X + CLOCK_BADGE_HIT, CLOCK_BADGE_Y));
  CHECK(clockBadgeHit(CLOCK_BADGE_X, CLOCK_BADGE_Y - CLOCK_BADGE_HIT));
  CHECK(!clockBadgeHit(CLOCK_BADGE_X + CLOCK_BADGE_HIT + 1, CLOCK_BADGE_Y));
  CHECK(!clockBadgeHit(CLOCK_BADGE_X + 25, CLOCK_BADGE_Y + 25));  // corner of the box
  CHECK(!clockBadgeHit(233, 202));  // the pet
}
