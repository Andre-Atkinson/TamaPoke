#include "framework.h"
#include "../trainingtouch.h"
#include "../exercise.h"
using T = TrainingTouch;
TEST(TrainingTouch, OneHundredRapidTapsNoRepeatWhileHeld) {
  T t;
  for (uint32_t n = 0; n < 100; ++n) {
    CHECK_EQ(t.update(true, 233, 120, n * 30), T::HIT);
    CHECK_EQ(t.update(true, 234, 120, n * 30 + 5), T::NONE);
    CHECK_EQ(t.update(false, 0, 0, n * 30 + 15), T::NONE);
  }
}
TEST(TrainingTouch, TopAndBroadScreenAreHits) {
  for (auto y : {30, 80, 160, 240, 320, 370}) {
    T t; CHECK_EQ(t.update(true, 233, y, 0), T::HIT);
  }
  CHECK(!T::onPanel(0, 0)); CHECK(!T::onPanel(467, 233));
  CHECK(T::onPanel(T::EXIT_X, T::EXIT_Y + T::EXIT_H));
}
TEST(TrainingTouch, ExitRequiresAContinuousOneSecondHold) {
  T t;
  for (uint32_t n = 0; n < 1000; n += 25)
    CHECK_EQ(t.update(true, 233, 405, n), T::NONE);
  CHECK_EQ(t.update(true, 233, 405, 999), T::NONE);
  CHECK_EQ(t.update(true, 233, 405, 1000), T::EXIT);
  CHECK(t.waitingRelease());
  CHECK_EQ(t.update(true, 233, 405, 2000), T::NONE);
  CHECK_EQ(t.update(false, 0, 0, 2100), T::NONE);
  CHECK(!t.waitingRelease());
}
TEST(TrainingTouch, QuickTapsOnExitDoNotExitOrScore) {
  T t;
  for (uint32_t n = 0; n < 100; ++n) {
    CHECK_EQ(t.update(true, 233, 405, n * 50), T::NONE);
    CHECK_EQ(t.update(false, 0, 0, n * 50 + 20), T::NONE);
  }
}
TEST(TrainingTouch, MovingOrMissingReportsCancelsExit) {
  T t; t.update(true, 233, 405, 0); t.update(true, 233, 370, 25);
  for (uint32_t n = 50; n <= 1500; n += 25)
    CHECK_EQ(t.update(true, 233, 405, n), T::NONE);
  t.reset(); t.update(true, 233, 405, 0);
  CHECK_EQ(t.update(true, 233, 405, 1000), T::NONE);
  t.reset(); CHECK_EQ(t.update(true, 233, 233, 0), T::HIT);
  for (uint32_t n = 25; n <= 1500; n += 25)
    CHECK_EQ(t.update(true, 233, 405, n), T::NONE);
}
TEST(TrainingTouch, LockAndExitSwallowHeldContact) {
  T t; t.update(true, 233, 405, 0); t.reset(true);
  for (uint32_t n = 0; n < 2000; n += 25)
    CHECK_EQ(t.update(true, 233, 405, n), T::NONE);
  CHECK(t.waitingRelease());
  t.update(false, 0, 0, 2000); CHECK(!t.waitingRelease());
  CHECK_EQ(t.update(true, 233, 233, 2050), T::HIT);
}
TEST(TrainingTouch, DecoderRestoresLegacyEmptyRearming) {
  const uint8_t down[] = {0x06, 0x0E, 0x1B, 0x94, 0, 1, 0xAB};
  const uint8_t ack[] = {0xAB, 0, 0, 0, 0, 0, 0xAB};
  const uint8_t up[] = {0, 0, 0, 0, 0, 0, 0xAB};
  CHECK(decodeTrainingReport(ack, 7).valid);
  CHECK(!decodeTrainingReport(ack, 7).pressed);
  CHECK_EQ(decodeTrainingReport(ack, 7).kind, TrainingReport::EMPTY);
  CHECK(!decodeTrainingReport(down, 6).valid);
  CHECK(decodeTrainingReport(down, 7).pressed);
  CHECK_EQ(decodeTrainingReport(down, 7).x, 233);
  CHECK_EQ(decodeTrainingReport(down, 7).y, 30);
  CHECK(decodeTrainingReport(up, 7).valid);
  CHECK(!decodeTrainingReport(up, 7).pressed);
}
TEST(TrainingTouch, ExitHoldSurvivesRollover) {
  T t; uint32_t start = 0xFFFFFF00u;
  for (uint32_t n = 0; n < 1000; n += 25)
    CHECK_EQ(t.update(true, 233, 405, start + n), T::NONE);
  CHECK_EQ(t.update(true, 233, 405, start + 1000), T::EXIT);
}

TEST(TrainingTouch, ContactEmptyCyclesRecoverEveryTap) {
  const uint8_t down[] = {0x06, 0x0E, 0x0E, 0x99, 0, 1, 0xAB};
  const uint8_t empty[] = {0xAB, 0, 0, 0, 0, 0, 0xAB};
  T t;
  for (uint32_t n = 0; n < 100; ++n) {
    auto r = decodeTrainingReport(down, 7);
    CHECK_EQ(t.update(r.pressed, r.x, r.y, n * 200), T::HIT);
    r = decodeTrainingReport(empty, 7);
    CHECK_EQ(t.update(r.pressed, r.x, r.y, n * 200 + 100), T::NONE);
  }
}
TEST(TrainingTouch, ContactFramesAndTrackIdsAreNotTapCounters) {
  T t;
  CHECK_EQ(t.update(true, 233, 233, 0), T::HIT);
  for (uint32_t n = 20; n <= 10000; n += 20)
    CHECK_EQ(t.update(true, 233, 233, n), T::NONE);
  CHECK_EQ(T::POLL_MS, 20u);
}
TEST(TrainingTouch, SecondRecordCanBeFirstActiveFinger) {
  const uint8_t b[] = {0x10, 0, 0, 0, 0, 2, 0xAB, 0x06, 0x0E, 0x0E, 0x99, 0};
  auto r = decodeTrainingReport(b, sizeof(b));
  CHECK(r.valid); CHECK(r.pressed); CHECK_EQ(r.x, 233); CHECK_EQ(r.y, 233);
}
TEST(TrainingTouch, EmptyClearsHeldExitAndZeroPacketDoesNotLatch) {
  const uint8_t b[] = {0, 0, 0, 0, 0, 0, 0};
  auto r = decodeTrainingReport(b, sizeof(b));
  CHECK(r.valid); CHECK(!r.pressed);
  T t; t.reset(true); t.update(r.pressed, 0, 0, 0);
  CHECK(!t.waitingRelease()); CHECK_EQ(t.update(true, 233, 233, 100), T::HIT);
}

// ---------------------------------------------------------------- the stamina hold's touch (#78)
// A HIT's finger holds while its reports keep coming; the release, a stalled stream, the
// exit chip and a contact swallowed after a lock or an exit never hold.
TEST(TrainingTouch, AHitFingerHoldsUntilItLifts) {
  T t;
  CHECK(!t.holding(0));
  CHECK_EQ(t.update(true, 233, 233, 0), T::HIT);
  for (uint32_t n = 20; n <= 20000; n += 20) {
    CHECK_EQ(t.update(true, 233, 233, n), T::NONE);
    CHECK(t.holding(n));
  }
  CHECK_EQ(t.update(false, 0, 0, 20020), T::NONE);  // the release is never a tap
  CHECK(!t.holding(20020));
  CHECK_EQ(t.update(true, 233, 233, 20100), T::HIT);  // a new touch holds again
  CHECK(t.holding(20100));
}
TEST(TrainingTouch, AStalledReportStreamPausesTheHold) {
  T t;
  t.update(true, 233, 233, 0);
  CHECK(t.holding(T::REPORT_GAP_MS));
  CHECK(!t.holding(T::REPORT_GAP_MS + 1));
  t.update(true, 233, 233, 500);  // the stream comes back: the same finger, no new tap
  CHECK(t.holding(500));
}
TEST(TrainingTouch, TheExitChipNeverHoldsAndAHoldNeverExits) {
  T t;
  for (uint32_t n = 0; n < 1000; n += 20) {
    CHECK_EQ(t.update(true, 233, 405, n), T::NONE);
    CHECK(!t.holding(n));
  }
  CHECK_EQ(t.update(true, 233, 405, 1000), T::EXIT);
  CHECK(!t.holding(1000));
  T u;  // a hold on the pet that slides onto the chip keeps holding and never exits
  CHECK_EQ(u.update(true, 233, 300, 0), T::HIT);
  for (uint32_t n = 20; n <= 3000; n += 20) {
    CHECK_EQ(u.update(true, 233, 405, n), T::NONE);
    CHECK(u.holding(n));
  }
}
TEST(TrainingTouch, AContactSwallowedAfterALockNeverHolds) {
  T t;
  t.update(true, 233, 233, 0);
  CHECK(t.holding(0));
  t.reset(true);  // a lock while the finger is down
  for (uint32_t n = 20; n <= 2000; n += 20) {
    CHECK_EQ(t.update(true, 233, 233, n), T::NONE);
    CHECK(!t.holding(n));
  }
}

// ---------------------------------------------------------------- the rhythm punch (exercise.h)
using RP = RhythmPunch;
TEST(RhythmPunch, TheWindowIsAroundTheMomentTheBagReachesThePet) {
  CHECK_EQ(RP::CONTACT_MS, RP::SWING_MS / 2);
  CHECK(!RP::inWindow(RP::CONTACT_MS - RP::WINDOW_MS - 1));
  CHECK(RP::inWindow(RP::CONTACT_MS - RP::WINDOW_MS));
  CHECK(RP::inWindow(RP::CONTACT_MS));
  CHECK(RP::inWindow(RP::CONTACT_MS + RP::WINDOW_MS - 1));
  CHECK(!RP::inWindow(RP::CONTACT_MS + RP::WINDOW_MS));
  CHECK_MSG(2 * RP::WINDOW_MS * 100 / RP::SWING_MS >= 45, "forgiving: almost half of every swing");
  RP r; r.begin(1000);
  CHECK(r.reach(1000) < 0.01f);                       // out at the start of a swing
  CHECK(r.reach(1000 + RP::CONTACT_MS) > 0.99f);      // at the pet in the window's middle
  CHECK(r.reach(1000 + RP::SWING_MS - 1) < 0.01f);    // and out again
  CHECK(r.reach(1000 + RP::CONTACT_MS - RP::WINDOW_MS) > 0.5f);  // on its way in when it opens
}
TEST(RhythmPunch, ATapInTheWindowLandsOnePunchPerSwing) {
  RP r; r.begin(0);
  CHECK(!r.tap(RP::CONTACT_MS - RP::WINDOW_MS - 1));
  CHECK(r.tap(RP::CONTACT_MS - RP::WINDOW_MS));
  CHECK_MSG(!r.tap(RP::CONTACT_MS), "a second tap in the same swing does nothing");
  CHECK(!r.open(RP::CONTACT_MS));
  CHECK(r.tap(RP::SWING_MS + RP::CONTACT_MS + RP::WINDOW_MS - 1));  // the next swing, late
  CHECK_EQ(r.punches, (uint8_t)2);
  CHECK_EQ(r.lastPunchAt, RP::SWING_MS + RP::CONTACT_MS + RP::WINDOW_MS - 1);
}
TEST(RhythmPunch, AnOffBeatTapCostsNothing) {
  RP r; r.begin(0);
  for (uint32_t t = 0; t < RP::CONTACT_MS - RP::WINDOW_MS; t += 30) CHECK(!r.tap(t));
  CHECK_MSG(r.tap(RP::CONTACT_MS), "the swing's punch is still there after taps too early");
  CHECK_EQ(r.punches, (uint8_t)1);
}
TEST(RhythmPunch, ASessionIsTwelveSwingsAndTapsAreJudgedWhenCaptured) {
  RP r; r.begin(5000);
  for (uint8_t i = 0; i < EX_COUNT; i++) CHECK(r.tap(5000 + i * RP::SWING_MS + RP::CONTACT_MS));
  CHECK_EQ(r.punches, (uint8_t)EX_COUNT);
  CHECK(!r.over(5000 + RP::TOTAL_MS - 1));
  CHECK(r.over(5000 + RP::TOTAL_MS));
  RP s; s.begin(5000);
  // the last swing's window, captured just before the end and read after it: it counts
  uint32_t last = 5000 + (EX_COUNT - 1) * RP::SWING_MS + RP::CONTACT_MS;
  CHECK(s.over(last + RP::SWING_MS));
  CHECK(s.tap(last));
  CHECK(!s.tap(5000 + RP::TOTAL_MS + RP::CONTACT_MS));  // after the end
  CHECK(!s.tap(4999));                                  // before the start
  CHECK_EQ(s.punches, (uint8_t)1);
  CHECK_EQ(s.swing(5000 + RP::TOTAL_MS), (uint8_t)EX_COUNT);
}
TEST(RhythmPunch, SurvivesMillisRollover) {
  RP r; uint32_t start = 0xFFFFFF00u;
  r.begin(start);
  CHECK(r.tap(start + RP::CONTACT_MS));  // past the wrap
  CHECK(!r.over(start + RP::TOTAL_MS - 1));
  CHECK(r.over(start + RP::TOTAL_MS));
  CHECK(!r.tap(start - 10));
}

// ---------------------------------------------------------------- the stamina hold (exercise.h)
using SH = StaminaHold;
// steps every `every` ms from `from` to `to`, all with the same `on`
static uint8_t holdSteps(SH &h, uint32_t from, uint32_t to, bool on, uint32_t every = 85) {
  uint8_t done = 0;
  for (uint32_t t = from; t <= to; t += every) done += h.step(t, on);
  return done;
}
TEST(StaminaHold, EachRepIsOneAndAHalfSecondsHeld) {
  SH h; h.begin(0);
  h.step(0, true);
  holdSteps(h, 100, 1400, true, 100);
  CHECK_EQ(h.reps, (uint8_t)0);
  CHECK(h.step(1500, true));
  CHECK_EQ(h.reps, (uint8_t)1);
  CHECK_EQ(h.into, (uint32_t)0);
}
TEST(StaminaHold, LiftingPausesTheRepAndHoldingAgainGoesOn) {
  SH h; h.begin(0);
  h.step(0, true);
  h.step(1000, true);  // a stalled frame counts STEP_MAX_MS only
  CHECK_EQ(h.into, SH::STEP_MAX_MS);
  holdSteps(h, 1100, 1700, true, 100);
  CHECK_EQ(h.into, SH::STEP_MAX_MS + 700);
  holdSteps(h, 1800, 9000, false);  // lifted for 7 s: paused, not reset, not failed
  CHECK_EQ(h.into, SH::STEP_MAX_MS + 700);
  CHECK(!h.over(9000));
  h.step(9100, true);  // the first step that sees the finger again doesn't count the gap
  CHECK_EQ(h.into, SH::STEP_MAX_MS + 700);
  h.step(9200, true);
  CHECK_EQ(h.into, SH::STEP_MAX_MS + 800);
}
TEST(StaminaHold, AFingerSeenByOneStepOnlyNeverCounts) {
  SH h; h.begin(0);
  for (uint32_t t = 0; t < 20000; t += 200) h.step(t, (t / 200) % 2 == 0);
  CHECK_EQ(h.into, (uint32_t)0);
  CHECK_EQ(h.reps, (uint8_t)0);
}
TEST(StaminaHold, TwelveRepsEndTheSession) {
  SH h; h.begin(0);
  uint8_t done = holdSteps(h, 0, 60000, true, 50);
  CHECK_EQ(done, (uint8_t)EX_COUNT);
  CHECK_EQ(h.reps, (uint8_t)EX_COUNT);
  CHECK(h.done());
  CHECK(h.over(60000));
  CHECK_EQ(exerciseGain(h.reps), (uint8_t)EX_GAIN_CAP);
}
TEST(StaminaHold, FifteenSecondsWithNoFingerEndTheSession) {
  SH h; h.begin(1000);
  holdSteps(h, 1000, 1000 + SH::IDLE_END_MS - 100, false, 100);
  CHECK(!h.over(1000 + SH::IDLE_END_MS - 1));
  CHECK(h.over(1000 + SH::IDLE_END_MS));  // untouched from the start
  SH k; k.begin(0);
  holdSteps(k, 0, 40000, true, 100);  // a long hold isn't idle, whatever its length
  CHECK(!k.idleOver(40000));
  holdSteps(k, 40100, 40100 + SH::IDLE_END_MS - 200, false, 100);
  CHECK(!k.idleOver(40100 + SH::IDLE_END_MS - 200));
  CHECK(k.idleOver(40000 + SH::IDLE_END_MS));
}
TEST(Exercise, EachPunchOrRepIsWorthOneAndAHalfPointsUpTo18) {
  CHECK_EQ(exerciseGain(0), (uint8_t)0);
  CHECK_EQ(exerciseGain(1), (uint8_t)1);
  CHECK_EQ(exerciseGain(2), (uint8_t)3);
  CHECK_EQ(exerciseGain(11), (uint8_t)16);
  CHECK_EQ(exerciseGain(EX_COUNT), (uint8_t)EX_GAIN_CAP);
  CHECK_EQ(exerciseGain(255), (uint8_t)EX_GAIN_CAP);
  CHECK_EQ(EX_GAIN_CAP, 18);
}
