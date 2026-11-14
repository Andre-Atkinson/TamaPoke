// Attention calls (attention.h, #9): threshold, re-arm, quiet hours, what a call does and
// where the bubble goes. Pure logic.
#include "framework.h"
#include "../attention.h"

static uint16_t hm(int h, int m) { return (uint16_t)(h * 60 + m); }

static AttnLevels all(uint8_t v) { return attnLevels(v, v, v, v); }

static const uint8_t FOOD = 1u << ATTN_FOOD, HYG = 1u << ATTN_HYGIENE, ENE = 1u << ATTN_ENERGY,
                     JOY = 1u << ATTN_JOY;

// ---------------------------------------------------------------- levels and the bubble's need
TEST(Attention, levels_map_the_pet_fields_to_status_line_order) {
  AttnLevels l = attnLevels(11, 22, 33, 44);  // fullness, joy, energy, hygiene
  CHECK_EQ(l.v[ATTN_FOOD], (uint8_t)11);
  CHECK_EQ(l.v[ATTN_JOY], (uint8_t)22);
  CHECK_EQ(l.v[ATTN_ENERGY], (uint8_t)33);
  CHECK_EQ(l.v[ATTN_HYGIENE], (uint8_t)44);
}

TEST(Attention, bubble_shows_the_first_low_need_in_status_line_order) {
  CHECK_EQ(attnShown(all(80)), (uint8_t)ATTN_NONE);
  CHECK_EQ(attnShown(all(25)), (uint8_t)ATTN_NONE);  // 25 is not low (red bar is < 25)
  CHECK_EQ(attnShown(attnLevels(24, 80, 80, 80)), (uint8_t)ATTN_FOOD);
  CHECK_EQ(attnShown(attnLevels(80, 24, 80, 80)), (uint8_t)ATTN_JOY);
  CHECK_EQ(attnShown(attnLevels(80, 80, 24, 80)), (uint8_t)ATTN_ENERGY);
  CHECK_EQ(attnShown(attnLevels(80, 80, 80, 24)), (uint8_t)ATTN_HYGIENE);
  // several low: food, then hygiene, then energy, then joy, whatever the values
  CHECK_EQ(attnShown(attnLevels(20, 0, 0, 0)), (uint8_t)ATTN_FOOD);
  CHECK_EQ(attnShown(attnLevels(80, 0, 10, 20)), (uint8_t)ATTN_HYGIENE);
  CHECK_EQ(attnShown(attnLevels(80, 0, 20, 80)), (uint8_t)ATTN_ENERGY);
}

// ---------------------------------------------------------------- threshold and re-arm
TEST(Attention, a_need_calls_once_when_it_drops_below_25) {
  AttnArm a;
  CHECK_EQ(a.update(all(80), false), (uint8_t)0);
  CHECK_EQ(a.update(attnLevels(25, 80, 80, 80), false), (uint8_t)0);  // 25: not yet
  CHECK_EQ(a.update(attnLevels(24, 80, 80, 80), false), FOOD);         // the drop
  a.done();
  CHECK_EQ(a.update(attnLevels(22, 80, 80, 80), false), (uint8_t)0);  // still low: no nagging
  CHECK_EQ(a.update(attnLevels(0, 80, 80, 80), false), (uint8_t)0);
}

TEST(Attention, each_need_calls_on_its_own) {
  AttnArm a;
  a.update(all(80), false);
  CHECK_EQ(a.update(attnLevels(80, 24, 80, 80), false), JOY);
  a.done();
  CHECK_EQ(a.update(attnLevels(80, 24, 24, 80), false), ENE);
  a.done();
  CHECK_EQ(a.update(attnLevels(80, 24, 24, 24), false), HYG);
  a.done();
  CHECK_EQ(a.update(attnLevels(24, 24, 24, 24), false), FOOD);
}

TEST(Attention, two_needs_dropping_together_wait_as_one_call) {
  AttnArm a;
  a.update(all(80), false);
  CHECK_EQ(a.update(attnLevels(20, 80, 20, 80), false), (uint8_t)(FOOD | ENE));
  a.done();
  CHECK_EQ(a.waiting(), (uint8_t)0);
}

TEST(Attention, a_small_top_up_does_not_re_arm_only_40_does) {
  AttnArm a;
  a.update(all(80), false);
  CHECK_EQ(a.update(attnLevels(80, 24, 80, 80), false), JOY);
  a.done();
  CHECK_EQ(a.update(attnLevels(80, 29, 80, 80), false), (uint8_t)0);  // +5 cuddle
  CHECK_EQ(a.update(attnLevels(80, 24, 80, 80), false), (uint8_t)0);  // drains back: no second call
  CHECK_EQ(a.update(attnLevels(80, 39, 80, 80), false), (uint8_t)0);  // 39: still not re-armed
  CHECK_EQ(a.update(attnLevels(80, 24, 80, 80), false), (uint8_t)0);
  CHECK_EQ(a.update(attnLevels(80, 40, 80, 80), false), (uint8_t)0);  // 40 re-arms (no call)
  CHECK_EQ(a.update(attnLevels(80, 25, 80, 80), false), (uint8_t)0);
  CHECK_EQ(a.update(attnLevels(80, 24, 80, 80), false), JOY);         // calls again
}

TEST(Attention, needs_already_low_on_the_first_sample_do_not_call) {
  AttnArm a;
  CHECK_EQ(a.update(attnLevels(15, 80, 15, 30), false), (uint8_t)0);  // boot after a power-off
  CHECK_EQ(a.update(attnLevels(14, 80, 14, 30), false), (uint8_t)0);
  CHECK_EQ(a.update(attnLevels(14, 80, 14, 24), false), HYG);  // 30 was armed: it calls
  a.done();
  CHECK_EQ(a.update(attnLevels(40, 80, 14, 24), false), (uint8_t)0);  // food re-arms at 40
  CHECK_EQ(a.update(attnLevels(24, 80, 14, 24), false), FOOD);
}

TEST(Attention, a_waiting_call_is_dropped_if_the_need_recovers_first) {
  AttnArm a;
  a.update(all(80), false);
  CHECK_EQ(a.update(attnLevels(24, 80, 80, 80), false), FOOD);  // can't go out yet (no done())
  CHECK_EQ(a.update(attnLevels(23, 80, 80, 80), false), FOOD);  // still waiting
  CHECK_EQ(a.update(attnLevels(25, 80, 80, 80), false), (uint8_t)0);  // fed: nothing to say
  CHECK_EQ(a.update(attnLevels(24, 80, 80, 80), false), (uint8_t)0);  // and not re-armed by 25
}

TEST(Attention, away_drops_the_call_and_disarms_the_need) {
  AttnArm a;
  a.update(all(80), false);
  CHECK_EQ(a.update(attnLevels(24, 80, 80, 80), true), (uint8_t)0);  // asleep, egg, ceremony...
  CHECK_EQ(a.update(attnLevels(24, 80, 80, 80), false), (uint8_t)0);  // back: already spent
  // a call that was waiting when the pet went away is dropped too
  CHECK_EQ(a.update(attnLevels(40, 80, 80, 80), false), (uint8_t)0);
  CHECK_EQ(a.update(attnLevels(24, 80, 80, 80), false), FOOD);
  CHECK_EQ(a.update(attnLevels(24, 80, 80, 80), true), (uint8_t)0);
  CHECK_EQ(a.waiting(), (uint8_t)0);
  CHECK_EQ(a.update(attnLevels(24, 80, 80, 80), false), (uint8_t)0);
}

TEST(Attention, away_still_re_arms_a_need_that_recovers) {
  AttnArm a;
  a.update(all(80), false);
  CHECK_EQ(a.update(attnLevels(80, 80, 24, 80), false), ENE);
  a.done();
  CHECK_EQ(a.update(attnLevels(80, 80, 60, 80), true), (uint8_t)0);  // energy back while asleep
  CHECK_EQ(a.update(attnLevels(80, 80, 24, 80), false), ENE);
}

// ---------------------------------------------------------------- quiet hours
TEST(Attention, quiet_hours_follow_the_bedtime_window_when_one_is_set) {
  CHECK(!attnQuiet(1, true, hm(20, 59)));
  CHECK(attnQuiet(1, true, hm(21, 0)));
  CHECK(attnQuiet(1, true, hm(6, 59)));
  CHECK(!attnQuiet(1, true, hm(7, 0)));
  CHECK(!attnQuiet(3, true, hm(22, 30)));
  CHECK(attnQuiet(3, true, hm(23, 0)));
  CHECK(attnQuiet(3, true, hm(7, 59)));
  CHECK(!attnQuiet(3, true, hm(8, 0)));
}

TEST(Attention, with_bedtime_off_quiet_hours_are_22_to_07) {
  CHECK(!attnQuiet(0, true, hm(21, 59)));
  CHECK(attnQuiet(0, true, hm(22, 0)));
  CHECK(attnQuiet(0, true, hm(0, 0)));
  CHECK(attnQuiet(0, true, hm(6, 59)));
  CHECK(!attnQuiet(0, true, hm(7, 0)));
  CHECK(!attnQuiet(0, true, hm(13, 0)));
  // an unknown preset (the sketch range-checks it, this is the second line) is the same
  CHECK(attnQuiet(BED_PRESETS, true, hm(23, 0)));
  CHECK(!attnQuiet(255, true, hm(12, 0)));
}

TEST(Attention, an_unknown_or_stale_clock_is_always_quiet) {
  for (uint8_t p = 0; p < BED_PRESETS; p++)
    for (int m = 0; m < 1440; m += 7) CHECK(attnQuiet(p, false, (uint16_t)m));
}

// ---------------------------------------------------------------- what a call does
static AttnScene scene(uint8_t screen, bool quiet = false) {
  AttnScene s = {};
  s.screen = screen;
  s.quiet = quiet;
  return s;
}

TEST(Attention, lit_screen_chirps_and_brightens) {
  AttnAction a = attnDecide(scene(ATTN_SCR_LIT));
  CHECK(!a.wait);
  CHECK(a.chirp);
  CHECK(a.brighten);
  CHECK(!a.peek);
  CHECK(!a.closeViews);
}

TEST(Attention, timed_out_screen_peeks_and_chirps) {
  AttnAction a = attnDecide(scene(ATTN_SCR_TIMED_OUT));
  CHECK(!a.wait);
  CHECK(a.chirp);
  CHECK(a.peek);
  CHECK(!a.brighten);
  CHECK(!a.closeViews);
}

TEST(Attention, pwr_off_screen_stays_dark_but_chirps) {
  AttnAction a = attnDecide(scene(ATTN_SCR_PWR_OFF));
  CHECK(!a.wait);
  CHECK(a.chirp);
  CHECK(!a.peek);
  CHECK(!a.brighten);
}

TEST(Attention, pocket_lock_gets_nothing_and_the_call_is_spent) {
  AttnAction a = attnDecide(scene(ATTN_SCR_POCKET));
  CHECK(!a.wait);
  CHECK(!a.chirp);
  CHECK(!a.peek);
  CHECK(!a.brighten);
}

TEST(Attention, quiet_hours_mean_no_chirp_and_no_light_on_any_screen) {
  for (uint8_t scr = ATTN_SCR_LIT; scr <= ATTN_SCR_POCKET; scr++) {
    AttnAction a = attnDecide(scene(scr, true));
    CHECK(!a.wait);
    CHECK(!a.chirp);
    CHECK(!a.peek);
    CHECK(!a.brighten);
    CHECK(!a.closeViews);
  }
}

TEST(Attention, busy_waits_whatever_the_screen_or_the_hour) {
  for (uint8_t scr = ATTN_SCR_LIT; scr <= ATTN_SCR_POCKET; scr++) {
    for (int q = 0; q < 2; q++) {
      AttnScene s = scene(scr, q);
      s.busy = true;
      AttnAction a = attnDecide(s);
      CHECK(a.wait);
      CHECK(!a.chirp);
      CHECK(!a.peek);
    }
  }
}

TEST(Attention, a_game_or_a_view_on_a_lit_screen_waits) {
  AttnScene s = scene(ATTN_SCR_LIT);
  s.playing = true;
  CHECK(attnDecide(s).wait);
  s = scene(ATTN_SCR_LIT);
  s.browsing = true;
  CHECK(attnDecide(s).wait);
}

TEST(Attention, almost_off_screen_chirps_brightens_and_closes_a_view_left_open) {
  AttnAction a = attnDecide(scene(ATTN_SCR_LIT_IDLE));
  CHECK(!a.wait);
  CHECK(a.chirp);
  CHECK(a.brighten);
  CHECK(!a.peek);
  CHECK(!a.closeViews);
  AttnScene s = scene(ATTN_SCR_LIT_IDLE);
  s.browsing = true;  // the card left open with screen-off set to never
  a = attnDecide(s);
  CHECK(!a.wait);
  CHECK(a.chirp);
  CHECK(a.brighten);
  CHECK(a.closeViews);
  s = scene(ATTN_SCR_LIT_IDLE);
  s.playing = true;  // a game on a lit screen still waits
  CHECK(attnDecide(s).wait);
}

TEST(Attention, timed_out_with_a_view_left_open_closes_it_and_peeks) {
  AttnScene s = scene(ATTN_SCR_TIMED_OUT);
  s.browsing = true;
  AttnAction a = attnDecide(s);
  CHECK(!a.wait);
  CHECK(a.chirp);
  CHECK(a.peek);
  CHECK(a.closeViews);
}

TEST(Attention, a_game_left_in_the_dark_chirps_but_is_never_lit) {
  AttnScene s = scene(ATTN_SCR_PWR_OFF);
  s.playing = true;
  AttnAction a = attnDecide(s);
  CHECK(!a.wait);
  CHECK(a.chirp);
  CHECK(!a.peek);
  CHECK(!a.closeViews);
  s.screen = ATTN_SCR_TIMED_OUT;  // can't happen (games don't time out); still stays dark
  a = attnDecide(s);
  CHECK(a.chirp);
  CHECK(!a.peek);
}

TEST(Attention, a_view_left_open_behind_pwr_off_is_not_closed) {
  AttnScene s = scene(ATTN_SCR_PWR_OFF);
  s.browsing = true;
  AttnAction a = attnDecide(s);
  CHECK(a.chirp);
  CHECK(!a.closeViews);
  CHECK(!a.peek);
}

// ---------------------------------------------------------------- where the bubble goes
// Inside the round panel (radius 233 around 233,233) with a margin, below the status
// line, and above the evolve / farewell / run-away buttons (top at y 173 at their smallest).
static bool insideRound(int x, int y, int margin) {
  long dx = x - 233, dy = y - 233, r = 233 - margin;
  return dx * dx + dy * dy <= r * r;
}

TEST(Attention, bubble_stays_on_the_round_screen_and_clear_of_the_text_and_buttons) {
  for (int petX = 0; petX <= 466; petX++) {
    AttnBubble b = attnBubbleAt(petX);
    CHECK(insideRound(b.x, b.y, 8));
    CHECK(insideRound(b.x + b.w, b.y, 8));
    CHECK(insideRound(b.x, b.y + b.h + ATTN_BUBBLE_TAIL, 8));
    CHECK(insideRound(b.x + b.w, b.y + b.h + ATTN_BUBBLE_TAIL, 8));
    CHECK(b.y >= 108);
    CHECK(b.y + b.h + ATTN_BUBBLE_TAIL < 173);
  }
}

TEST(Attention, bubble_sits_on_the_roomier_side_of_the_pet) {
  AttnBubble b = attnBubbleAt(150);
  CHECK(b.right);
  CHECK_EQ(b.x, (int16_t)(150 + ATTN_BUBBLE_GAP));
  b = attnBubbleAt(233);
  CHECK(b.right);
  b = attnBubbleAt(234);
  CHECK(!b.right);
  CHECK_EQ(b.x + b.w, 234 - ATTN_BUBBLE_GAP);
  b = attnBubbleAt(325);
  CHECK(!b.right);
  CHECK_EQ(b.x + b.w, 325 - ATTN_BUBBLE_GAP);
}
