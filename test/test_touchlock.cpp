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
  CHECK_MSG(!touchOnlyWakes(0, true, true), "lit by a call: the touch acts");
  CHECK(touchOnlyWakes(0, true, false));   // dark: only wakes
  CHECK(touchOnlyWakes(1, false, false));  // dimmed: only wakes
  CHECK(touchOnlyWakes(2, false, false));  // almost off: only wakes
  CHECK(!touchOnlyWakes(0, false, false));  // awake: acts
}
