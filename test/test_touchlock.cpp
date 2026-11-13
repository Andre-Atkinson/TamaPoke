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
  CHECK(touchWakeEnabled(false, false));
  CHECK(!touchWakeEnabled(true, false));
  CHECK(!touchWakeEnabled(false, true));
  CHECK(!touchWakeEnabled(true, true));
  TouchLock t;  // the auto lock going active is what disables it
  CHECK(touchWakeEnabled(false, t.active));
  t.update(true, 0);
  t.update(true, 10000);
  CHECK(!touchWakeEnabled(false, t.active));
}

TEST(TouchLock, selector_three_is_zero_seconds) {
  CHECK_EQ(touchLockFromSel(3), 0u);
  CHECK_EQ(touchLockFromSel(2), 60000u);
  CHECK_EQ(touchLockFromSel(1), 30000u);
  CHECK_EQ(touchLockFromSel(9), 10000u);
}
