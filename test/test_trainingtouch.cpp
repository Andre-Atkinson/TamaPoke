#include "framework.h"
#include "../trainingtouch.h"
#include "../exercise.h"
#include "../punchlunge.h"
#include "../sdmon.h"  // pmdScaleFor, PMD_*
#include <dirent.h>
#include <cstdio>
#include <string>
#include <vector>
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
  T o;  // a touch-down off the round panel (inside 0..466, outside the circle) never holds
  CHECK_EQ(o.update(true, 5, 5, 0), T::NONE);
  CHECK(!o.holding(0));
  CHECK_EQ(o.update(true, 5, 5, 20), T::NONE);
  CHECK(!o.holding(20));
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
  // frames that don't divide 1.5 s: the time past a rep goes into the next one
  SH k; k.begin(0);
  for (uint32_t t = 0; t <= 1600; t += 200) k.step(t, true);
  CHECK_EQ(k.reps, (uint8_t)1);
  CHECK_EQ(k.into, (uint32_t)100);
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

// ---------------------------------------------------------------- the punch's lunge (punchlunge.h, #90)
using PL = PunchLunge;
// A made-up ATTACK: frames of W x H palette indices (0xFF transparent), each one block.
struct LungeFrames {
  uint8_t W, H;
  std::vector<uint8_t> px;
  uint8_t n = 0;
  explicit LungeFrames(uint8_t w = 8, uint8_t h = 10) : W(w), H(h) {}
  // a frame whose opaque block covers rows [top, feet) and columns [lo, hi)
  LungeFrames &add(uint8_t top, uint8_t feet, uint8_t lo = 2, uint8_t hi = 6) {
    size_t at = px.size();
    px.resize(at + (size_t)W * H, 0xFF);
    for (int r = top; r < feet; r++)
      for (int c = lo; c < hi; c++) px[at + (size_t)r * W + c] = 1;
    n++;
    return *this;
  }
  LungeFrames &addEmpty() {
    px.resize(px.size() + (size_t)W * H, 0xFF);
    n++;
    return *this;
  }
  PL plan(uint8_t s = 4) const {
    PL l;
    l.plan(px.data(), W, H, n, s);
    return l;
  }
};
// a punch landed at `at`
static RP landedAt(uint32_t at) {
  RP p;
  p.begin(at - RP::CONTACT_MS);
  p.tap(at);
  return p;
}

TEST(PunchLunge, EachFrameStandsOnItsOwnFeetAndTheStrikeIsTheFirstLowest) {
  // the wind-up sits high on the canvas, the strike lowest (toward the viewer), then back up
  LungeFrames f;
  f.add(0, 5).add(1, 6).add(4, 9).add(4, 9).add(2, 7);
  PL l = f.plan();
  CHECK(l.ok);
  CHECK_EQ(l.feet[0], (uint8_t)5);
  CHECK_EQ(l.feet[1], (uint8_t)6);
  CHECK_EQ(l.feet[2], (uint8_t)9);
  CHECK_EQ(l.feet[4], (uint8_t)7);
  CHECK_MSG(l.strike == 2, "the first of the two lowest frames: " + testStr(l.strike));
  CHECK_EQ(l.s, (uint8_t)4);
}
TEST(PunchLunge, PlaysFromTheStrikeAtTheSpritesPace) {
  LungeFrames f;
  f.add(0, 5).add(1, 6).add(4, 9).add(3, 8).add(2, 7);
  PL l = f.plan();
  const uint16_t ms[] = {100, 100, 80, 90, 120};
  RP p = landedAt(10000);
  CHECK_EQ(l.frameAt(p, 10000, ms), 2);  // the strike at once: the pose never trails the hit
  CHECK_EQ(l.frameAt(p, 10079, ms), 2);
  CHECK_EQ(l.frameAt(p, 10080, ms), 3);
  CHECK_EQ(l.frameAt(p, 10169, ms), 3);
  CHECK_EQ(l.frameAt(p, 10170, ms), 4);
  CHECK_EQ(l.frameAt(p, 10289, ms), 4);
  CHECK_MSG(l.frameAt(p, 10290, ms) == -1, "back on IDLE once its last frame is done");
}
TEST(PunchLunge, BackOnIdleBeforeTheNextWindowCanOpen) {
  // a slow ATTACK: 1.2 s from the strike to its end
  LungeFrames f;
  f.add(0, 5).add(4, 9).add(2, 7);
  PL l = f.plan();
  const uint16_t ms[] = {600, 600, 600};
  RP p = landedAt(10000);
  CHECK_EQ(l.frameAt(p, 10000 + PL::MAX_MS - 1, ms), 2);
  CHECK_MSG(l.frameAt(p, 10000 + PL::MAX_MS, ms) == -1, "cut back to IDLE at MAX_MS");
  // the latest a punch can land, and the moment the next window opens
  RP q; q.begin(0);
  CHECK(q.tap(RP::CONTACT_MS + RP::WINDOW_MS - 1));
  uint32_t next = RP::SWING_MS + RP::CONTACT_MS - RP::WINDOW_MS;
  CHECK(q.open(next));
  CHECK_MSG(l.frameAt(q, next - 100, ms) == -1, "idle at least 100 ms before the next \"now!\"");
}
TEST(PunchLunge, OnlyALandedPunchLunges) {
  LungeFrames f;
  f.add(0, 5).add(4, 9).add(2, 7);
  PL l = f.plan();
  const uint16_t ms[] = {100, 100, 100};
  RP p; p.begin(0);
  CHECK_MSG(l.frameAt(p, 100, ms) == -1, "no punch yet: the pet stands on IDLE");
  CHECK(!p.tap(RP::CONTACT_MS - RP::WINDOW_MS - 1));  // too early: lands nothing
  CHECK_MSG(l.frameAt(p, RP::CONTACT_MS - RP::WINDOW_MS, ms) == -1, "a tap that doesn't land shows nothing");
  CHECK(p.tap(RP::CONTACT_MS));
  CHECK_EQ(l.frameAt(p, RP::CONTACT_MS, ms), 1);
  CHECK(!p.tap(RP::CONTACT_MS + 150));  // a second tap in the same swing: no new lunge
  CHECK_MSG(l.frameAt(p, RP::CONTACT_MS + 150, ms) == 2, "the first punch's lunge runs on");
}
TEST(PunchLunge, AFrameThatDoesNotFitTheRoomKeepsTodaysLook) {
  // 64 x 64 at scale 4: a 63-row frame is 252 px tall (the room), 64 rows is 256
  LungeFrames tall(64, 64);
  tall.add(0, 63, 20, 40).add(1, 64, 20, 40);
  CHECK_MSG(tall.plan().ok, "exactly ROOM_UP tall fits");
  LungeFrames taller(64, 64);
  taller.add(0, 63, 20, 40).add(0, 64, 20, 40);
  CHECK_MSG(!taller.plan().ok, "taller than the room: IDLE as today");
  // the canvas centre is 128 px in: columns from 2 (120 px left of it) to 62 (120 right) fit
  LungeFrames wide(64, 64);
  wide.add(40, 63, 2, 62).add(41, 64, 2, 62);
  CHECK(wide.plan().ok);
  LungeFrames left(64, 64);
  left.add(40, 63, 2, 62).add(41, 64, 1, 62);
  CHECK_MSG(!left.plan().ok, "wider than the room on the left");
  LungeFrames right(64, 64);
  right.add(40, 63, 2, 62).add(41, 64, 2, 63);
  CHECK_MSG(!right.plan().ok, "wider than the room on the right");
  // the strike itself is checked; a wind-up frame before it is never drawn, so it isn't
  LungeFrames strike(64, 64);
  strike.add(0, 64, 20, 40).add(1, 64, 20, 40);
  CHECK_MSG(!strike.plan().ok, "a strike taller than the room");
  LungeFrames high(80, 80);
  high.add(0, 70, 30, 50).add(70, 80, 30, 50);
  CHECK_MSG(high.plan().ok, "a frame before the strike isn't drawn, so it isn't checked");
}
TEST(PunchLunge, NothingToPlanFromKeepsTodaysLook) {
  LungeFrames f;
  f.add(0, 5).add(4, 9).add(2, 7);
  PL l;
  l.plan(nullptr, f.W, f.H, f.n, 4);
  CHECK_MSG(!l.ok, "no ATTACK (or no PMD sprite)");
  l.plan(f.px.data(), f.W, f.H, 0, 4);
  CHECK(!l.ok);
  l.plan(f.px.data(), 0, f.H, f.n, 4);
  CHECK(!l.ok);
  l.plan(f.px.data(), f.W, 0, f.n, 4);
  CHECK(!l.ok);
  l.plan(f.px.data(), f.W, f.H, f.n, 0);
  CHECK(!l.ok);
  std::vector<uint8_t> many((size_t)f.W * f.H * (PL::MAX_FRAMES + 1), 1);
  l.plan(many.data(), f.W, f.H, PL::MAX_FRAMES + 1, 4);
  CHECK_MSG(!l.ok, "more frames than a TPK2 action holds");
  l.plan(many.data(), f.W, f.H, PL::MAX_FRAMES, 4);
  CHECK(l.ok);
  LungeFrames gap;
  gap.add(0, 5).add(4, 9).addEmpty();
  CHECK_MSG(!gap.plan().ok, "an empty frame after the strike: the pet would blink out");
  LungeFrames before;
  before.addEmpty().add(4, 9).add(2, 7);
  CHECK_MSG(before.plan().ok, "an empty frame before the strike is never drawn");
  LungeFrames none;
  none.addEmpty().addEmpty();
  CHECK(!none.plan().ok);
  const uint16_t ms[] = {100, 100, 100};
  RP p = landedAt(5000);
  CHECK_MSG(none.plan().frameAt(p, 5000, ms) == -1, "a plan that isn't ok never lunges");
}
TEST(PunchLunge, IsDrawnOnlyForTheSpriteItWasPlannedFrom) {
  LungeFrames f;
  f.add(0, 5).add(4, 9).add(2, 7);
  PL l = f.plan();
  CHECK(l.isFor(f.px.data(), f.W, f.H, f.n));
  LungeFrames g;
  g.add(0, 5).add(4, 9).add(2, 7);
  CHECK_MSG(!l.isFor(g.px.data(), f.W, f.H, f.n), "another sprite's frames");
  CHECK(!l.isFor(f.px.data(), f.W + 1, f.H, f.n));
  CHECK(!l.isFor(f.px.data(), f.W, f.H + 1, f.n));
  CHECK(!l.isFor(f.px.data(), f.W, f.H, f.n - 1));
}

// The shipped sprite pack (tools/sdcard/mons, TPK2: PmdMon::load): every species and every
// shiny lunges, at every scale cap up to 5 (gameui.cpp's TRAIN_PET_S is 4).
struct TpkSprite {
  std::vector<uint8_t> blob;
  struct Act { uint8_t w = 0, h = 0, n = 0; uint16_t ms[24] = {}; const uint8_t *data = nullptr; } acts[PMD_NACTS];
};
static bool readTpk(const std::string &path, TpkSprite &t) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  t.blob.resize(size > 0 ? (size_t)size : 0);
  bool read = size > 7 && fread(t.blob.data(), 1, t.blob.size(), f) == t.blob.size();
  fclose(f);
  if (!read || memcmp(t.blob.data(), "TPK2", 4) != 0) return false;
  const uint8_t *b = t.blob.data();
  size_t p = 7 + (size_t)(b[5] | b[6] << 8) * 2, end = t.blob.size();
  for (uint8_t i = 0; i < b[4]; i++) {
    if (p + 4 > end || b[p] >= PMD_NACTS || b[p + 3] > 24) return false;
    TpkSprite::Act &a = t.acts[b[p]];
    a.w = b[p + 1];
    a.h = b[p + 2];
    a.n = b[p + 3];
    p += 4;
    if (p + 2 * a.n > end) return false;
    for (uint8_t k = 0; k < a.n; k++, p += 2) a.ms[k] = (uint16_t)(b[p] | b[p + 1] << 8) ? (uint16_t)(b[p] | b[p + 1] << 8) : 100;
    a.data = b + p;
    p += (size_t)a.w * a.h * a.n;
    if (p > end) return false;
  }
  return true;
}
TEST(PunchLunge, EveryShippedSpriteLungesAtIdlesSize) {
  const std::string dir = "../tools/sdcard/mons/";
  DIR *d = opendir(dir.c_str());
  CHECK_MSG(d != nullptr, "the sprite pack is in the repo: " + dir);
  if (!d) return;
  std::vector<std::string> names;
  while (dirent *e = readdir(d)) {
    std::string n = e->d_name;
    if (n.size() > 4 && n[0] == 'p' && n.compare(n.size() - 4, 4, ".bin") == 0) names.push_back(n);
  }
  closedir(d);
  CHECK_MSG(names.size() >= 302, "151 species and their shinies: " + testStr(names.size()));
  int shrinks = 0, species = 0;
  for (const std::string &n : names) {
    TpkSprite t;
    CHECK_MSG(readTpk(dir + n, t), n);
    const TpkSprite::Act &idle = t.acts[PMD_IDLE], &atk = t.acts[PMD_ATTACK];
    CHECK_MSG(idle.n && atk.n, n + " has IDLE and ATTACK");
    if (!idle.n || !atk.n) continue;
    if (n[1] != 's') {  // the renderer's own scale for ATTACK, at the punch's cap: why #90 draws it itself
      species++;
      if (pmdScaleFor(idle.h, atk.h, 4) < pmdScaleFor(idle.h, idle.h, 4)) shrinks++;
    }
    for (uint8_t maxS = 2; maxS <= 5; maxS++) {
      PL l;
      l.plan(atk.data, atk.w, atk.h, atk.n, pmdScaleFor(idle.h, idle.h, maxS));
      CHECK_MSG(l.ok, n + " lunges at scale cap " + testStr(maxS));
      RP p = landedAt(20000);
      CHECK_MSG(l.frameAt(p, 20000, atk.ms) == l.strike, n + " strikes as the punch lands");
    }
  }
  CHECK_EQ(species, 151);
  CHECK_MSG(shrinks == 86, "drawPmdActM() would draw ATTACK smaller than IDLE for 86 of 151: " + testStr(shrinks));
}
