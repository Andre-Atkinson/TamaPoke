#include "framework.h"
#include "../touchlock.h"

TEST(TouchLock, default_waits_10s_then_locks_and_a_wake_clears_it) {
  TouchLock t;
  CHECK_EQ(t.update(true, 1000), 0);
  CHECK(!t.active);
  CHECK_EQ(t.update(true, 1000 + 9999), 0);
  CHECK_EQ(t.update(true, 1000 + 10000), 1);
  CHECK(t.active);
  CHECK_EQ(t.update(true, 50000), 0);
  CHECK_EQ(t.update(false, 50001), -1);
  CHECK(!t.active);
  CHECK_EQ(t.update(false, 50002), 0);
}

TEST(TouchLock, zero_locks_as_the_screen_goes_off) {
  TouchLock t;
  t.delayMs = 0;
  CHECK_EQ(t.update(true, 10), 1);
  CHECK(t.active);
}

TEST(TouchLock, never_does_not_lock) {
  TouchLock t;
  t.delayMs = TOUCH_LOCK_NEVER;
  CHECK_EQ(t.update(true, 10), 0);
  CHECK_EQ(t.update(true, 100000), 0);
  CHECK(!t.active);
}

TEST(TouchLock, touch_before_the_delay_cancels_the_timer) {
  TouchLock t;
  CHECK_EQ(t.update(true, 0), 0);
  CHECK_EQ(t.update(false, 5000), 0);
  CHECK(!t.active);
  CHECK_EQ(t.update(true, 5001), 0);
  CHECK_EQ(t.update(true, 5001 + 9999), 0);
  CHECK_EQ(t.update(true, 5001 + 10000), 1);
}

TEST(TouchLock, millis_wrap_and_the_saved_selector) {
  TouchLock t;
  t.delayMs = 32;
  CHECK_EQ(t.update(true, 0xFFFFFFF0u), 0);
  CHECK_EQ(t.update(true, 0xFFFFFFF0u + 31), 0);
  CHECK_EQ(t.update(true, 0xFFFFFFF0u + 32), 1);
  CHECK_EQ((int)touchLockSel(10000), 0);
  CHECK_EQ((int)touchLockSel(30000), 1);
  CHECK_EQ((int)touchLockSel(60000), 2);
  CHECK_EQ((int)touchLockSel(0), 3);
  CHECK_EQ((int)touchLockSel(TOUCH_LOCK_NEVER), 4);
  CHECK_EQ(touchLockFromSel(0), 10000u);
  CHECK_EQ(touchLockFromSel(4), TOUCH_LOCK_NEVER);
  CHECK_EQ(touchLockNext(10000), 30000u);
  CHECK_EQ(touchLockNext(60000), TOUCH_LOCK_NEVER);
  CHECK_EQ(touchLockNext(TOUCH_LOCK_NEVER), 0u);
  CHECK_EQ(touchLockNext(0), 10000u);
}

TEST(TouchLock, tp_int_is_only_used_while_the_chip_is_awake) {
  // touch.sleep() makes TP_INT open drain and it reads LOW: polling or arming it as a
  // light-sleep wake source under the auto lock would keep the loop awake forever.
  CHECK(touchWakeEnabled(false, false, true));
  CHECK(!touchWakeEnabled(true, false, true));
  CHECK(!touchWakeEnabled(false, true, true));
  CHECK(!touchWakeEnabled(true, true, true));
  TouchLock t;  // the auto lock going active is what disables it
  CHECK(touchWakeEnabled(false, t.active, true));
  t.update(true, 0);
  t.update(true, 10000);
  CHECK(!touchWakeEnabled(false, t.active, true));
  // A failed touch.begin() leaves TP_INT floating: it stays disabled whatever the locks say.
  CHECK(!touchWakeEnabled(false, false, false));
  CHECK(!touchWakeEnabled(true, false, false));
  CHECK(!touchWakeEnabled(false, true, false));
  CHECK(!touchWakeEnabled(true, true, false));
}

TEST(TouchLock, selector_three_is_zero_seconds) {
  CHECK_EQ(touchLockFromSel(3), 0u);
  CHECK_EQ(touchLockFromSel(2), 60000u);
  CHECK_EQ(touchLockFromSel(1), 30000u);
  CHECK_EQ(touchLockFromSel(9), 10000u);
}

// An attention call's window lights a timed-out screen (#9): the touch lock lets go while it
// shows, so a touch on the lit pet works, and comes back after its delay once the window ends
// untouched (the screen dark again). It used to read screenOff alone: locked through the window.
TEST(TouchLock, a_call_window_lets_go_and_the_lock_starts_over_after_it) {
  TouchLock t;
  CHECK_EQ(t.update(touchScreenDark(true, false), 0), 0);
  CHECK_EQ(t.update(touchScreenDark(true, false), 10000), 1);  // locked, 10 s after screen off
  CHECK_MSG(t.update(touchScreenDark(true, true), 800000) == -1, "the call's window unlocks");
  CHECK(!t.active);
  CHECK_EQ(t.update(touchScreenDark(true, true), 814999), 0);  // still lit: no lock
  CHECK(!t.active);
  CHECK_EQ(t.update(touchScreenDark(true, false), 815000), 0);  // window over: the delay again
  CHECK_EQ(t.update(touchScreenDark(true, false), 815000 + 9999), 0);
  CHECK_EQ(t.update(touchScreenDark(true, false), 815000 + 10000), 1);
}

TEST(TouchLock, screen_dark_only_when_off_and_no_call_lights_it) {
  CHECK(touchScreenDark(true, false));
  CHECK(!touchScreenDark(true, true));
  CHECK(!touchScreenDark(false, false));
  CHECK(!touchScreenDark(false, true));
}

// The first touch on a dimmed or dark screen only wakes it; on a screen a call lights at full
// brightness it acts, so one tap answers the call (it used to be swallowed there too).
TEST(TouchLock, first_touch_acts_on_a_screen_a_call_lights) {
  CHECK_MSG(!touchOnlyWakes(0, true, true, false), "lit by a call: the touch acts");
  CHECK(touchOnlyWakes(0, true, false, false));   // dark: only wakes
  CHECK(touchOnlyWakes(1, false, false, false));  // dimmed: only wakes
  CHECK(touchOnlyWakes(2, false, false, false));  // almost off: only wakes
  CHECK(!touchOnlyWakes(0, false, false, false));  // awake: acts
  CHECK(!touchOnlyWakes(0, false, true, false));   // a dimmed screen a call brightened: acts
}

// The run-away button is one tap with no dialog, across the pet, so a call that comes while it
// is up (a sickness call) must not let the first tap hit it: it only wakes, whether the call
// peeked on a timed-out screen or brightened a dimmed one. With no call an awake screen acts as
// before, and the other buttons need nothing.
TEST(TouchLock, first_touch_only_wakes_a_call_window_while_the_runaway_button_is_up) {
  CHECK_MSG(touchOnlyWakes(0, true, true, true), "peek + run-away button: only wakes");
  CHECK_MSG(touchOnlyWakes(0, false, true, true), "brighten + run-away button: only wakes");
  CHECK(touchOnlyWakes(0, true, false, true));    // dark, no call: only wakes anyway
  CHECK(!touchOnlyWakes(0, false, false, true));  // awake: acts (the button is tapped on purpose)
  CHECK(touchOnlyWakes(1, false, true, true));    // dimmed: only wakes
}

// touchBringUp() runs from loop() when it is pending and the pocket lock is off. The first
// attempt goes at once, even on a dark screen; a retry after a failure waits the period and
// needs a screen that is on or lit by a call (a dead chip blocks the loop ~0.5 s per try).
TEST(TouchLock, bring_up_first_attempt_goes_at_once_even_on_a_dark_screen) {
  CHECK(touchBringUpDue(true, false, false, true, 0, 2000));
  CHECK(touchBringUpDue(true, false, false, false, 0, 2000));
  CHECK(!touchBringUpDue(false, false, false, false, 99999, 2000));  // nothing pending
}

TEST(TouchLock, bring_up_retry_waits_the_period_and_needs_a_lit_screen) {
  CHECK_MSG(!touchBringUpDue(true, false, true, false, 1999, 2000), "retry waits TOUCH_RETRY_MS");
  CHECK(touchBringUpDue(true, false, true, false, 2000, 2000));
  CHECK_MSG(!touchBringUpDue(true, false, true, true, 99999, 2000), "dark screen: no retry");
  // a call's window lights the screen: touchScreenDark is false, so the retry may go
  CHECK(touchBringUpDue(true, false, true, touchScreenDark(true, true), 2000, 2000));
  CHECK(!touchBringUpDue(true, false, true, touchScreenDark(true, false), 2000, 2000));
}

TEST(TouchLock, bring_up_never_runs_under_the_pocket_lock) {
  CHECK(!touchBringUpDue(true, true, false, false, 0, 2000));
  CHECK(!touchBringUpDue(true, true, true, false, 99999, 2000));
}
