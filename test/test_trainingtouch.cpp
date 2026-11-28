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
// Training taps are at least RhythmPunch::SWING_MIN_MS (350 ms) apart; each one's lift is
// confirmed (a RELEASE, which the punch ignores) before the next goes down.
TEST(TrainingTouch, OneHundredRapidTapsNoRepeatWhileHeld) {
  T t;
  for (uint32_t n = 0; n < 100; ++n) {
    uint32_t at = n * RhythmPunch::SWING_MIN_MS;
    CHECK_EQ(t.update(true, 233, 120, at), T::HIT);
    CHECK_EQ(t.update(true, 234, 120, at + 5), T::NONE);
    CHECK_EQ(t.update(false, 0, 0, at + 20), T::NONE);
    CHECK_EQ(t.update(false, 0, 0, at + 40), T::NONE);
    CHECK_EQ(t.update(false, 0, 0, at + 60), T::RELEASE);
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

// Since #126 a lift keeps the chip read every 20 ms until it is confirmed, so a tap's lift is a
// run of empty reports (a RELEASE, which the punch ignores), and the next contact is a new HIT.
TEST(TrainingTouch, ContactEmptyCyclesRecoverEveryTap) {
  const uint8_t down[] = {0x06, 0x0E, 0x0E, 0x99, 0, 1, 0xAB};
  const uint8_t empty[] = {0xAB, 0, 0, 0, 0, 0, 0xAB};
  T t;
  for (uint32_t n = 0; n < 100; ++n) {
    auto r = decodeTrainingReport(down, 7);
    CHECK_EQ(t.update(r.pressed, r.x, r.y, n * 200), T::HIT);
    r = decodeTrainingReport(empty, 7);
    CHECK_EQ(t.update(r.pressed, r.x, r.y, n * 200 + 100), T::NONE);
    CHECK_EQ(t.update(r.pressed, r.x, r.y, n * 200 + 120), T::NONE);
    CHECK_EQ(t.update(r.pressed, r.x, r.y, n * 200 + 140), T::RELEASE);
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

// ---------------------------------------------------------------- the push-ups' let-go (#126)
// A HIT's finger lifting is a RELEASE once reports with no finger have gone on for
// LiftConfirm::CONFIRM_MS, timed at the first of them. One report with no finger between
// contacts is the same finger. The exit chip, a touch off the round panel, and a contact
// swallowed after a lock or an exit never let go.
TEST(TrainingTouch, ALetGoIsConfirmedAndTimedWhenTheFingerLeft) {
  T t;
  CHECK_EQ(t.update(true, 233, 233, 0), T::HIT);
  for (uint32_t n = 20; n <= 3000; n += 20) CHECK_EQ(t.update(true, 233, 233, n), T::NONE);
  CHECK_EQ(t.update(false, 0, 0, 3020), T::NONE);
  CHECK_EQ(t.update(false, 0, 0, 3040), T::NONE);
  CHECK_EQ(t.update(false, 0, 0, 3060), T::RELEASE);
  CHECK_EQ(t.liftAt(), (uint32_t)3020);
  for (uint32_t n = 3080; n <= 4000; n += 20) CHECK_EQ(t.update(false, 0, 0, n), T::NONE);  // once
  CHECK_EQ(t.update(true, 233, 233, 4020), T::HIT);  // a new touch goes down again
}
TEST(TrainingTouch, AnEmptyReportBetweenContactsIsNotALetGo) {
  const uint8_t down[] = {0x06, 0x0E, 0x0E, 0x99, 0, 1, 0xAB};
  const uint8_t empty[] = {0xAB, 0, 0, 0, 0, 0, 0xAB};
  auto c = decodeTrainingReport(down, 7), e = decodeTrainingReport(empty, 7);
  T t;
  CHECK_EQ(t.update(c.pressed, c.x, c.y, 0), T::HIT);
  for (uint32_t n = 20; n < 2900; n += 40) {
    CHECK_EQ(t.update(e.pressed, e.x, e.y, n), T::NONE);
    // the contact again is the same finger (R1-1): neither a let-go nor a new HIT
    CHECK_EQ(t.update(c.pressed, c.x, c.y, n + 20), T::NONE);
  }
  // reports with no finger for CONFIRM_MS: the let-go, timed at the first of them
  CHECK_EQ(t.update(e.pressed, e.x, e.y, 3000), T::NONE);
  CHECK_EQ(t.update(e.pressed, e.x, e.y, 3020), T::NONE);
  CHECK_EQ(t.update(e.pressed, e.x, e.y, 3040), T::RELEASE);
  CHECK_EQ(t.liftAt(), (uint32_t)3000);
}
// R1-1, R2-1: a contact back before the lift is confirmed is the same finger: not a let-go, and
// not a new HIT (a punch's tap would count twice), however late it is read.
TEST(TrainingTouch, AContactBackBeforeTheLiftIsConfirmedIsTheSameFinger) {
  T t;
  CHECK_EQ(t.update(true, 233, 233, 0), T::HIT);
  CHECK_EQ(t.update(false, 0, 0, 20), T::NONE);
  CHECK_EQ(t.update(true, 233, 233, 20 + LiftConfirm::CONFIRM_MS - 1), T::NONE);
  // still a finger down that lets go later, timed at its own first report with no finger
  CHECK_EQ(t.update(false, 0, 0, 100), T::NONE);
  CHECK_EQ(t.update(false, 0, 0, 120), T::NONE);
  CHECK_EQ(t.update(false, 0, 0, 140), T::RELEASE);
  CHECK_EQ(t.liftAt(), (uint32_t)100);
  // the original 1.75's poll: one report with no finger just before a frame, the contact read
  // 65 ms later, past CONFIRM_MS: still the same finger (R2-1)
  T u;
  CHECK_EQ(u.update(true, 233, 233, 0), T::HIT);
  CHECK_EQ(u.update(false, 0, 0, 20), T::NONE);
  CHECK_EQ(u.update(true, 233, 233, 20 + 65), T::NONE);
  // once the lift is confirmed, a contact is a new touch, a HIT
  T v;
  CHECK_EQ(v.update(true, 233, 233, 0), T::HIT);
  CHECK_EQ(v.update(false, 0, 0, 20), T::NONE);
  CHECK_EQ(v.update(false, 0, 0, 20 + LiftConfirm::CONFIRM_MS), T::RELEASE);
  CHECK_EQ(v.update(true, 233, 233, 80), T::HIT);
  // across the millis() wrap
  T w;
  CHECK_EQ(w.update(true, 233, 233, 0xFFFFFFF0u), T::HIT);
  CHECK_EQ(w.update(false, 0, 0, 0xFFFFFFFFu), T::NONE);
  CHECK_EQ(w.update(true, 233, 233, 10), T::NONE);
}
TEST(TrainingTouch, TheExitChipOffPanelAndSwallowedContactsNeverLetGo) {
  T t;  // held on the exit chip, then lifted
  for (uint32_t n = 0; n < 500; n += 20) CHECK_EQ(t.update(true, 233, 405, n), T::NONE);
  for (uint32_t n = 500; n < 1500; n += 20) CHECK_EQ(t.update(false, 0, 0, n), T::NONE);
  T o;  // a touch-down off the round panel (inside 0..466, outside the circle)
  CHECK_EQ(o.update(true, 5, 5, 0), T::NONE);
  for (uint32_t n = 20; n < 500; n += 20) CHECK_EQ(o.update(false, 0, 0, n), T::NONE);
  T u;  // a lock while a push-up is down: the rest of the contact and its lift are swallowed
  CHECK_EQ(u.update(true, 233, 233, 0), T::HIT);
  u.reset(true);
  for (uint32_t n = 20; n <= 1000; n += 20) CHECK_EQ(u.update(true, 233, 233, n), T::NONE);
  for (uint32_t n = 1020; n <= 2000; n += 20) CHECK_EQ(u.update(false, 0, 0, n), T::NONE);
  T v;  // a push-up that slides onto the chip still lets go, and never exits
  CHECK_EQ(v.update(true, 233, 300, 0), T::HIT);
  for (uint32_t n = 20; n <= 3000; n += 20) CHECK_EQ(v.update(true, 233, 405, n), T::NONE);
  v.update(false, 0, 0, 3020);
  v.update(false, 0, 0, 3040);
  CHECK_EQ(v.update(false, 0, 0, 3060), T::RELEASE);
}
// R2-2: a contact after LET_GO_REPORTS reports with no finger, before the lift is confirmed,
// is a new press: the finger let go (at the first of them). After one it is the same finger.
TEST(LiftConfirm, TwoReportsWithNoFingerMakeAContactANewPress) {
  LiftConfirm l;
  CHECK(!l.pressedAgain());  // nothing lost
  l.lost(100);
  CHECK(l.sameFinger());
  CHECK(!l.pressedAgain());
  l.lost(120);
  CHECK(!l.sameFinger());
  CHECK(l.pressedAgain());
  CHECK_EQ(l.at, (uint32_t)100);
  l.lost(130);  // more reports keep it a new press, timed at the first
  CHECK(l.pressedAgain());
  CHECK_EQ(l.at, (uint32_t)100);
  l.back();
  CHECK(!l.pressedAgain());  // back: a later lift counts its own reports
  l.lost(300);
  CHECK(l.sameFinger());
  CHECK(!l.pressedAgain());
  CHECK_EQ(l.at, (uint32_t)300);
  // the original 1.75's poll: reports 20 ms apart, then a frame; a contact after the frame
  LiftConfirm f;
  f.lost(85);
  f.lost(105);
  CHECK(!f.confirm(105));
  CHECK(f.pressedAgain());
  // ...or no finger after the frame: the lift is confirmed, and a contact is a plain new touch
  CHECK(f.confirm(170));
  CHECK(!f.pressedAgain());
  CHECK(!f.sameFinger());
  l.reset();
  CHECK(!l.pending);
  CHECK_EQ(l.reports, (uint8_t)0);
}
// R2-2, as the 1.75C's chip reported a push-up let go and pressed again 45 ms later on the
// hardware try (2026-11-28, TRAINTRACE; trainingtouch.cpp checks the same reads at compile time)
TEST(TrainingTouch, AQuickRePressAsTheChipReportedItIsALetGoAndANewPress) {
  const uint8_t down[] = {0x06, 0x0C, 0x08, 0x3B, 0x00, 0x01, 0xAB, 0, 0, 0, 0x3C, 0x4A};
  const uint8_t lifted[] = {0x00, 0x0C, 0x08, 0x3B, 0x00, 0x01, 0xAB, 0, 0, 0, 0x3C, 0x4A};
  const uint8_t empty[] = {0x0C, 0x08, 0x3B, 0x00, 0x01, 0xAB, 0, 0, 0, 0x3C, 0x4A, 0x09};
  const uint8_t back[] = {0x06, 0x0C, 0x08, 0x9D, 0x00, 0x01, 0xAB, 0, 0, 0, 0x3C, 0x4A};
  auto d = decodeTrainingReport(down, 12), l = decodeTrainingReport(lifted, 12);
  auto e = decodeTrainingReport(empty, 12), b = decodeTrainingReport(back, 12);
  CHECK(d.pressed && b.pressed && !l.pressed && !e.pressed);
  CHECK_EQ((int)l.kind, (int)TrainingReport::RELEASE);
  CHECK_EQ((int)e.kind, (int)TrainingReport::EMPTY);
  T t;
  CHECK_EQ(t.update(d.pressed, d.x, d.y, 1157839), T::HIT);
  for (uint32_t n = 1157859; n <= 1158499; n += 20) CHECK_EQ(t.update(d.pressed, d.x, d.y, n), T::NONE);
  CHECK_EQ(t.update(l.pressed, l.x, l.y, 1158519), T::NONE);
  CHECK_EQ(t.update(e.pressed, e.x, e.y, 1158539), T::NONE);
  CHECK_EQ(t.update(b.pressed, b.x, b.y, 1158564), T::RELEASE_HIT);
  CHECK_EQ(t.liftAt(), (uint32_t)1158519);
  CHECK_EQ(t.update(b.pressed, b.x, b.y, 1158584), T::NONE);
  // one report with no finger before the contact: the same finger still (R1-1, R2-1)
  CHECK_EQ(t.update(l.pressed, l.x, l.y, 1159000), T::NONE);
  CHECK_EQ(t.update(b.pressed, b.x, b.y, 1159020), T::NONE);
  // the new finger lets go in its turn, timed at its own first report with no finger
  CHECK_EQ(t.update(l.pressed, l.x, l.y, 1159135), T::NONE);
  CHECK_EQ(t.update(e.pressed, e.x, e.y, 1159155), T::NONE);
  CHECK_EQ(t.update(e.pressed, e.x, e.y, 1159175), T::RELEASE);
  CHECK_EQ(t.liftAt(), (uint32_t)1159135);
  // pressed again on the exit chip: only the let-go
  T x;
  CHECK_EQ(x.update(true, 233, 233, 0), T::HIT);
  CHECK_EQ(x.update(false, 0, 0, 20), T::NONE);
  CHECK_EQ(x.update(false, 0, 0, 40), T::NONE);
  CHECK_EQ(x.update(true, 233, 405, 50), T::RELEASE);
  CHECK_EQ(x.liftAt(), (uint32_t)20);
}
TEST(LiftConfirm, TheOriginal175sPollConfirmsTheSameWay) {
  LiftConfirm l;
  CHECK(!l.confirm(100));
  l.lost(100);
  l.lost(120);  // timed at the first report with no finger
  CHECK(!l.confirm(139));
  CHECK(l.confirm(140));
  CHECK_EQ(l.at, (uint32_t)100);
  CHECK(!l.confirm(200));  // once
  l.lost(300);
  l.back();
  CHECK(!l.confirm(400));  // the finger came back: no let-go
  // the same finger: a contact while the lift waits to be confirmed (R1-1, R2-1)
  LiftConfirm s;
  CHECK(!s.sameFinger());  // nothing lost: a new touch
  s.lost(100);
  CHECK(s.sameFinger());
  s.back();
  CHECK(!s.sameFinger());  // once back, a later contact is new
  // the original 1.75's poll around a frame (R2-1): a report with no finger at 85, the next
  // poll 65 ms on. A contact then is the same finger; no finger then confirms the lift.
  LiftConfirm f;
  f.lost(85);
  CHECK(f.sameFinger());
  CHECK(!f.confirm(85));
  f.lost(150);
  CHECK(f.confirm(150));
  CHECK_EQ(f.at, (uint32_t)85);
  CHECK(!f.sameFinger());  // confirmed: a contact now is a new touch
  l.lost(0xFFFFFFF0u);
  CHECK(!l.confirm(0xFFFFFFF0u + 39));
  CHECK(l.confirm(0xFFFFFFF0u + 40));  // across the millis() wrap
}

// ---------------------------------------------------------------- the rhythm punch (exercise.h)
using RP = RhythmPunch;
// A run that lands each of its first `k` swings at the swing's contact, from `t0`.
static RP perfectPunches(uint8_t k, uint32_t t0 = 0) {
  RP r;
  r.begin(t0);
  uint32_t start = t0;
  for (uint8_t i = 0; i < k; i++) {
    uint32_t len = RP::swingMs(i);
    CHECK_EQ(r.tap(start + len / 2), RP::TAP_LANDED);
    start += len;
  }
  return r;
}
TEST(RhythmPunch, TheWindowIsAroundTheMomentTheBagReachesThePet) {
  uint32_t L = RP::swingMs(0), W = RP::windowMs(0);
  CHECK_EQ(L, 1500u);
  CHECK_EQ(W, 350u);
  CHECK(!RP::inWindow(L / 2 - W - 1, L, W));
  CHECK(RP::inWindow(L / 2 - W, L, W));
  CHECK(RP::inWindow(L / 2, L, W));
  CHECK(RP::inWindow(L / 2 + W - 1, L, W));
  CHECK(!RP::inWindow(L / 2 + W, L, W));
  RP r;
  r.begin(1000);
  CHECK(r.reach(1000) < 0.01f);                // out at the start of a swing
  CHECK(r.reach(1000 + L / 2) > 0.99f);        // at the pet in the window's middle
  CHECK(r.reach(1000 + L - 1) < 0.01f);        // and out again
  CHECK(r.reach(1000 + L / 2 - W) > 0.5f);     // on its way in when it opens
}
TEST(RhythmPunch, EachPunchMakesTheSwingsQuickerAndTheWindowsNarrowerDownToAFloor) {
  for (uint8_t n = 1; n <= EX_MAX_COUNT; n++) {
    CHECK(RP::swingMs(n) <= RP::swingMs(n - 1));
    CHECK(RP::windowMs(n) <= RP::windowMs(n - 1));
    CHECK_MSG(RP::windowMs(n) < RP::swingMs(n) / 2, "a window never covers its swing: " + testStr(n));
  }
  CHECK_EQ(RP::swingMs(10), 1150u);
  CHECK_EQ(RP::windowMs(10), 230u);
  CHECK_EQ(RP::swingMs(20), 800u);
  CHECK_EQ(RP::windowMs(20), 110u);
  CHECK_MSG(RP::swingMs(30) > RP::SWING_MIN_MS, "still getting quicker at 30");
  CHECK_EQ(RP::swingMs(EX_MAX_COUNT), RP::SWING_MIN_MS);
  CHECK_EQ(RP::windowMs(EX_MAX_COUNT), RP::WINDOW_MIN_MS);
  RP r = perfectPunches(40);
  CHECK_EQ(r.punches, (uint8_t)40);
  CHECK(!r.over());
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 40; i++) sum += RP::swingMs(i);
  CHECK_EQ(r.nextStart, sum);
}
TEST(RhythmPunch, TheWarmUpForgivesEverythingUntilTheFirstPunch) {
  RP r;
  r.begin(0);
  for (uint32_t t = 0; t < 400; t += 30) CHECK_EQ(r.tap(t), RP::TAP_NOTHING);  // too early
  r.update(10000);  // swings pass with no punch
  CHECK(!r.over());
  CHECK_EQ(r.tap(10150), RP::TAP_NOTHING);  // 10150 % 1500 = 1150: past the window
  CHECK_EQ(r.tap(10500 + 750), RP::TAP_LANDED);  // the 8th swing's contact
  CHECK_EQ(r.punches, (uint8_t)1);
  CHECK_EQ(r.nextStart, (uint32_t)12000);
  CHECK_MSG(r.swingMs(1) == 1465, "the run is on: the next swing is quicker");
}
TEST(RhythmPunch, AWarmUpWithNoPunchEndsAfterFifteenSeconds) {
  RP r;
  r.begin(500);
  r.update(500 + RP::WARMUP_MS - 1);
  CHECK(!r.over());
  r.update(500 + RP::WARMUP_MS);
  CHECK(r.over());
  CHECK_MSG(!r.missed, "the warm-up running out isn't a miss");
  CHECK_EQ(r.tap(500 + RP::WARMUP_MS + 750), RP::TAP_NOTHING);
}
TEST(RhythmPunch, AfterTheFirstPunchAnOffBeatTapIsTheMiss) {
  RP r = perfectPunches(3);
  uint32_t s = r.nextStart, L = RP::swingMs(3), W = RP::windowMs(3);
  CHECK(!r.open(s + L / 2 - W - 1));
  CHECK_EQ(r.tap(s + L / 2 - W - 1), RP::TAP_MISSED);  // too early
  CHECK(r.over());
  CHECK(r.missed);
  CHECK_EQ(r.endAt, s + L / 2 - W - 1);
  CHECK_EQ(r.punches, (uint8_t)3);
  CHECK_EQ(r.tap(s + L / 2), RP::TAP_NOTHING);  // over: nothing lands after the miss
  RP q = perfectPunches(3);
  CHECK_EQ(q.tap(s + L / 2 - W), RP::TAP_LANDED);  // the window's first ms
  RP u = perfectPunches(3);
  CHECK_EQ(u.tap(s + L / 2 + W), RP::TAP_MISSED);  // just past its last
}
TEST(RhythmPunch, ASecondTapInALandedSwingIsTheMissButABounceIsNot) {
  RP r = perfectPunches(1);  // landed at 750
  CHECK_EQ(r.tap(750 + RP::BOUNCE_MS - 1), RP::TAP_NOTHING);
  CHECK(!r.over());
  CHECK_EQ(r.tap(750 + RP::BOUNCE_MS), RP::TAP_MISSED);
  CHECK(r.missed);
}
// BOUNCE_MS must never swallow a real tap: from a punch landed at the last ms of a window to the
// next window's first, the gap is longer than the bounce, at every swing.
TEST(RhythmPunch, TheBounceIsShorterThanTheGapFromTheLatestPunchToTheNextWindow) {
  for (uint8_t n = 0; n < EX_MAX_COUNT; n++) {
    uint32_t latest = RP::swingMs(n) / 2 + RP::windowMs(n);  // into swing n
    uint32_t nextOpen = RP::swingMs(n) + RP::swingMs(n + 1) / 2 - RP::windowMs(n + 1);
    CHECK_MSG(nextOpen - latest > RP::BOUNCE_MS, "the next window opens after the bounce");
  }
  CHECK_EQ(RP::BOUNCE_MS, 150u);
}
TEST(RhythmPunch, ASwingThatPassesIsTheMissOnceItSettlesAndTapsAreJudgedWhenCaptured) {
  RP r = perfectPunches(5);
  uint32_t close = r.nextStart + RP::swingMs(5) / 2 + RP::windowMs(5);
  r.update(close + RP::SETTLE_MS - 1);
  CHECK(!r.over());
  // captured just before the window closed, read after: it lands
  CHECK_EQ(r.tap(close - 1), RP::TAP_LANDED);
  CHECK_EQ(r.punches, (uint8_t)6);
  RP q = perfectPunches(5);
  q.update(close + RP::SETTLE_MS);
  CHECK(q.over());
  CHECK(q.missed);
  CHECK_EQ(q.endAt, close);
}
// The touch policy into the punch, as gameui.cpp wires them: a HIT is the punch's tap (a
// RELEASE is the push-ups' and the punch ignores it).
struct PunchTouch {
  T touch;
  RP punch;
  int taps = 0;
  void report(bool pressed, uint32_t now) {
    if (touch.update(pressed, 233, 240, now) == T::HIT) {
      taps++;
      punch.tap(now);
    }
  }
  // a finger down from `from` to `to` (reports every 20 ms), with the reports at `acks` (ms)
  // having no finger in them (an ACK or an EMPTY from the chip); then reports with no finger
  // until `until`
  void finger(uint32_t from, uint32_t to, uint32_t until, std::vector<uint32_t> acks = {}) {
    for (uint32_t n = from; n <= to; n += 20) {
      bool ack = false;
      for (uint32_t a : acks) ack = ack || a == n;
      report(!ack, n);
    }
    for (uint32_t n = to + 20; n <= until; n += 20) report(false, n);
  }
};
// R1-1: a tap held about 200 ms with one ACK report in it is one tap, not a second one that
// is past BOUNCE_MS of the landed punch and ends the run.
TEST(RhythmPunch, ATapHeldWithAnAckReportInsideItIsOneTapAndNeverTheMiss) {
  PunchTouch p;
  p.punch.begin(0);
  p.finger(740, 940, 1100, {900});
  CHECK_EQ(p.taps, 1);
  CHECK_EQ(p.punch.punches, (uint8_t)1);
  CHECK(!p.punch.over());
  // the next swing's punch lands as well
  uint32_t second = p.punch.nextStart + RP::swingMs(1) / 2;
  p.finger(second - 10, second + 190, second + 300, {second + 150});
  CHECK_EQ(p.taps, 2);
  CHECK_EQ(p.punch.punches, (uint8_t)2);
  CHECK(!p.punch.over());
  // with the ACK on the first report, and on the last one down
  PunchTouch q;
  q.punch.begin(0);
  q.finger(740, 940, 1100, {740 + 20, 940});
  CHECK_EQ(q.punch.punches, (uint8_t)1);
  CHECK(!q.punch.over());
  // a real second tap more than CONFIRM_MS after the lift, in the same swing, is still the miss
  PunchTouch r;
  r.punch.begin(0);
  r.finger(740, 760, 900);
  r.finger(920, 940, 1000);
  CHECK_EQ(r.taps, 2);
  CHECK(r.punch.over());
  CHECK(r.punch.missed);
}
TEST(RhythmPunch, ARunEndsByItselfAtTheMostPunches) {
  RP r = perfectPunches(EX_MAX_COUNT);
  CHECK(r.over());
  CHECK(!r.missed);
  CHECK_EQ(r.punches, (uint8_t)EX_MAX_COUNT);
}
TEST(RhythmPunch, SurvivesMillisRollover) {
  uint32_t start = 0xFFFFF000u;  // 4 s before the wrap: the run crosses it
  RP r = perfectPunches(5, start);
  CHECK_EQ(r.punches, (uint8_t)5);
  CHECK(!r.over());
  r.update(r.nextStart + 10);
  CHECK(!r.over());
  CHECK_EQ(r.tap(start - 10), RP::TAP_NOTHING);  // before the start
  RP w;
  w.begin(start);
  w.update(start + RP::WARMUP_MS - 1);  // past the wrap, not yet 15 s
  CHECK(!w.over());
}

// ---------------------------------------------------------------- the push-ups (exercise.h, #126)
using PU = PushUps;
// the middle of the band of the rep planned now, for a press at `at`
static uint32_t bandMid(const PU &p, uint32_t at) { return at + (p.bandLo + p.bandHi) / 2; }
// `k` reps, each pressed 200 ms after the last let-go and let go in its band's middle
static uint32_t goodReps(PU &p, uint8_t k, uint32_t at) {
  for (uint8_t i = 0; i < k; i++) {
    at += 200;
    p.press(at);
    at = bandMid(p, at);
    CHECK_EQ(p.release(at), PU::PU_REP);
  }
  return at;
}
TEST(PushUps, EveryRepsBandIsInsideItsFillAndNarrowsDownToAFloor) {
  for (uint32_t seed = 1; seed <= 40; seed++) {
    PU p;
    p.begin(0, seed);
    for (uint8_t n = 0; n < EX_MAX_COUNT; n++) {
      p.reps = n;
      p.plan();
      uint32_t base = PU::fillBaseMs(n);
      CHECK(p.fillMs >= base * 80 / 100 && p.fillMs <= base * 120 / 100);
      CHECK_MSG(p.bandLo >= p.fillMs * 35 / 100, "the band starts 35% into the fill");
      CHECK_MSG(p.bandHi <= p.fillMs * 95 / 100, "the bar is never full before it's too late");
      CHECK_EQ(p.bandHi - p.bandLo, PU::bandMs(n));
    }
  }
  for (uint8_t n = 1; n <= EX_MAX_COUNT; n++) {
    CHECK(PU::fillBaseMs(n) <= PU::fillBaseMs(n - 1));
    CHECK(PU::bandMs(n) <= PU::bandMs(n - 1));
  }
  CHECK_EQ(PU::bandMs(10), 340u);
  CHECK_EQ(PU::fillBaseMs(10), 1300u);
  CHECK_EQ(PU::bandMs(EX_MAX_COUNT), PU::BAND_MIN_MS);
  CHECK_EQ(PU::fillBaseMs(EX_MAX_COUNT), PU::FILL_MIN_MS);
  // the band changes from rep to rep: watched, not counted
  PU a;
  a.begin(0, 12345);
  uint32_t lo0 = a.bandLo, f0 = a.fillMs;
  goodReps(a, 1, 0);
  CHECK(a.bandLo != lo0 || a.fillMs != f0);
}
TEST(PushUps, LettingGoInTheBandIsARep) {
  PU p;
  p.begin(0, 7);
  CHECK(!p.down());
  p.press(100);
  CHECK(p.down());
  CHECK(!p.inBand(100 + p.bandLo - 1));
  CHECK(p.inBand(100 + p.bandLo));
  CHECK(p.fill(100) < 0.01f);
  uint32_t lo = p.bandLo;
  CHECK_EQ(p.release(100 + lo), PU::PU_REP);  // the band's first ms
  CHECK_EQ(p.reps, (uint8_t)1);
  CHECK_EQ(p.lastRepAt, 100 + lo);
  CHECK(!p.down());
  p.press(5000);
  uint32_t hi = p.bandHi;
  CHECK_EQ(p.release(5000 + hi), PU::PU_REP);  // ...and its last
  CHECK_EQ(p.reps, (uint8_t)2);
  CHECK_EQ(p.lastRepAt, 5000 + hi);
}
TEST(PushUps, AfterTheFirstRepTooEarlyOrTooLateIsTheMiss) {
  PU p;
  p.begin(0, 9);
  uint32_t t = goodReps(p, 2, 0) + 300;
  p.press(t);
  CHECK_EQ(p.release(t + p.bandLo - 1), PU::PU_MISSED);
  CHECK(p.over());
  CHECK_EQ(p.miss, (uint8_t)PU::MISS_EARLY);
  CHECK_EQ(p.reps, (uint8_t)2);
  PU q;
  q.begin(0, 9);
  t = goodReps(q, 2, 0) + 300;
  q.press(t);
  CHECK_EQ(q.release(t + q.bandHi + 1), PU::PU_MISSED);
  CHECK_EQ(q.miss, (uint8_t)PU::MISS_LATE);
  PU u;  // still holding: too late once LATE_MS past the band
  u.begin(0, 9);
  t = goodReps(u, 2, 0) + 300;
  u.press(t);
  CHECK_EQ(u.update(t + u.bandHi + PU::LATE_MS - 1), PU::PU_NOTHING);
  CHECK(!u.over());
  CHECK_EQ(u.update(t + u.bandHi + PU::LATE_MS), PU::PU_MISSED);
  CHECK(u.over());
  CHECK_EQ(u.miss, (uint8_t)PU::MISS_LATE);
}
TEST(PushUps, TheFirstRepIsAWarmUp) {
  PU p;
  p.begin(0, 3);
  p.press(100);
  uint32_t lo = p.bandLo;
  CHECK_EQ(p.release(100 + lo - 1), PU::PU_RETRY);
  CHECK(!p.over());
  CHECK(!p.down());
  CHECK_EQ(p.retry, (uint8_t)PU::MISS_EARLY);
  CHECK_EQ(p.retryAt, 100 + lo - 1);
  p.press(3000);
  CHECK_EQ(p.update(3000 + p.bandHi + PU::LATE_MS), PU::PU_RETRY);  // held too long
  CHECK_EQ(p.retry, (uint8_t)PU::MISS_LATE);
  CHECK(!p.over());
  p.press(6000);
  CHECK_EQ(p.release(bandMid(p, 6000)), PU::PU_REP);
  CHECK_EQ(p.retry, (uint8_t)PU::MISS_NONE);
  CHECK_EQ(p.reps, (uint8_t)1);
}
// A try at the warm-up that failed (shown and tried again) is somebody there: the 15 s with
// no finger start again from it, not from the start of the session.
TEST(PushUps, AFailedWarmUpTryStartsTheIdleTimerAgain) {
  PU p;
  p.begin(0, 3);
  p.press(10000);
  uint32_t at = 10000 + p.bandLo - 1;  // let go too early
  CHECK_EQ(p.release(at), PU::PU_RETRY);
  CHECK_EQ(p.readyAt, at);
  p.update(at + PU::IDLE_END_MS - 1);
  CHECK(!p.over());
  p.update(at + PU::IDLE_END_MS);
  CHECK(p.over());
  PU q;  // held too long, found by the frame's update
  q.begin(0, 3);
  q.press(10000);
  at = 10000 + q.bandHi + PU::LATE_MS;
  CHECK_EQ(q.update(at), PU::PU_RETRY);
  CHECK_EQ(q.readyAt, at);
  q.update(at + PU::IDLE_END_MS - 1);
  CHECK(!q.over());
  q.update(at + PU::IDLE_END_MS);
  CHECK(q.over());
}
TEST(PushUps, ATouchDownWhileDownIsTheSameFingerAndALiftWithNoPressIsNothing) {
  PU p;
  p.begin(0, 5);
  CHECK_EQ(p.release(50), PU::PU_NOTHING);
  p.press(100);
  p.press(300);  // its reports paused and came back
  CHECK_EQ(p.pressAt, (uint32_t)100);
  CHECK_EQ(p.release(bandMid(p, 100)), PU::PU_REP);
}
TEST(PushUps, NobodyThereForFifteenSecondsBetweenRepsEndsIt) {
  PU p;
  p.begin(1000, 11);
  p.update(1000 + PU::IDLE_END_MS - 1);
  CHECK(!p.over());
  p.update(1000 + PU::IDLE_END_MS);
  CHECK(p.over());
  CHECK_EQ(p.miss, (uint8_t)PU::MISS_NONE);
  PU q;
  q.begin(0, 11);
  uint32_t t = goodReps(q, 3, 0);
  q.update(t + PU::IDLE_END_MS - 1);
  CHECK(!q.over());
  q.update(t + PU::IDLE_END_MS);
  CHECK(q.over());
  CHECK_EQ(q.miss, (uint8_t)PU::MISS_NONE);
  CHECK_EQ(q.reps, (uint8_t)3);
}
TEST(PushUps, ARunEndsByItselfAtTheMostReps) {
  PU p;
  p.begin(0, 1);
  goodReps(p, EX_MAX_COUNT, 0);
  CHECK(p.over());
  CHECK_EQ(p.miss, (uint8_t)PU::MISS_NONE);
  CHECK_EQ(p.reps, (uint8_t)EX_MAX_COUNT);
}
TEST(PushUps, SurvivesMillisRollover) {
  PU p;
  p.begin(0xFFFFF000u, 2);
  uint32_t press = 0xFFFFFF00u;  // the band is past the wrap
  p.press(press);
  CHECK(p.fill(press + 10) > 0.0f);
  CHECK_EQ(p.update(press + p.bandLo), PU::PU_NOTHING);
  CHECK(p.inBand(press + p.bandLo));
  CHECK_EQ(p.release(bandMid(p, press)), PU::PU_REP);
}

// The touch policy into the push-ups, as gameui.cpp wires them (R2-3): a HIT presses, a RELEASE
// lets go at its lift time, and the frame's update() may run before any report is read. A
// finger lifted on the band's last ms is a rep, not late: LATE_MS outlasts the lift's
// confirmation and a poll.
static_assert(PushUps::LATE_MS >= LiftConfirm::CONFIRM_MS + TrainingTouch::POLL_MS + 20,
              "a let-go at the band's end is confirmed before the frame calls it late");
// R2-2: let go in the band and pressed again before the lift is confirmed (after two reports
// with no finger): a rep, then the next push-up down from the new press, as the 1.75C's
// capture queues it (TrainingCapture::poll: the RELEASE at liftAt(), then the HIT).
TEST(PushUps, AQuickRePressIsARepAndTheNextPushUpThroughTheTouchPolicy) {
  for (uint32_t seed = 1; seed <= 20; seed++) {
    PU p;
    p.begin(0, seed);
    uint32_t press = goodReps(p, 1, 0) + 500;
    uint32_t lift = bandMid(p, press);
    T t;
    int result = PU::PU_NOTHING;
    auto report = [&](bool pressed, uint32_t now) {
      int u = p.update(now);
      if (u != PU::PU_NOTHING) result = u;
      T::Action a = t.update(pressed, 233, 270, now);
      if ((a == T::RELEASE || a == T::RELEASE_HIT) && result == PU::PU_NOTHING) result = p.release(t.liftAt());
      if (a == T::HIT || a == T::RELEASE_HIT) p.press(now);
    };
    report(true, press);
    for (uint32_t n = press + 20; n < lift; n += 20) report(true, n);
    report(false, lift);
    report(false, lift + 20);
    report(true, lift + 40);  // back before the lift is confirmed
    CHECK_MSG(result == PU::PU_REP, "seed " + testStr(seed) + ": result " + testStr(result));
    CHECK_EQ(p.reps, (uint8_t)2);
    CHECK(p.down());  // the next push-up, from the new press
    CHECK_EQ(p.pressAt, lift + 40);
  }
}
TEST(PushUps, ALetGoOnTheBandsLastMsIsARepThroughTheTouchPolicy) {
  for (uint32_t seed = 1; seed <= 20; seed++) {
    PU p;
    p.begin(0, seed);
    uint32_t press = goodReps(p, 1, 0) + 500;  // one rep done: the next one is judged
    uint32_t end = press + p.bandHi;           // the band's last ms
    T t;
    int result = PU::PU_NOTHING;
    auto report = [&](bool pressed, uint32_t now) {
      int u = p.update(now);  // the frame, just before this report is read
      if (u != PU::PU_NOTHING) result = u;
      T::Action a = t.update(pressed, 233, 270, now);
      if (a == T::HIT) p.press(now);
      if (a == T::RELEASE && result == PU::PU_NOTHING) result = p.release(t.liftAt());
    };
    for (uint32_t n = press; n < end; n += 20) report(true, n);
    for (uint32_t n = end; n <= end + 200; n += 20) report(false, n);
    CHECK_MSG(result == PU::PU_REP, "seed " + testStr(seed) + ": result " + testStr(result));
    CHECK_EQ(p.reps, (uint8_t)2);
  }
}

// ---------------------------------------------------------------- the pay (exercise.h, #126)
TEST(Exercise, EachHitPaysMoreTheLongerTheRun) {
  CHECK_EQ(exerciseGain(0), (uint8_t)0);
  CHECK_EQ(exerciseGain(1), (uint8_t)1);   // half a point, rounded up: a hit never shows +0
  CHECK_EQ(exerciseGain(2), (uint8_t)1);
  CHECK_EQ(exerciseGain(5), (uint8_t)3);   // 2.5
  CHECK_EQ(exerciseGain(10), (uint8_t)8);  // 7.5
  CHECK_EQ(exerciseGain(12), (uint8_t)11); // 10.5 (a perfect 12 paid 18 before #126)
  CHECK_EQ(exerciseGain(15), (uint8_t)15);
  CHECK_EQ(exerciseGain(20), (uint8_t)25);
  CHECK_EQ(exerciseGain(22), (uint8_t)30);  // the strength ribbon in one run
  CHECK_EQ(exerciseGain(25), (uint8_t)38);
  CHECK_EQ(exerciseGain(30), (uint8_t)53);
  CHECK_EQ(exerciseGain(42), (uint8_t)99);
  CHECK_EQ(exerciseGain(43), (uint8_t)EX_GAIN_MAX);  // a stat's top
  CHECK_EQ(exerciseGain(EX_MAX_COUNT), (uint8_t)EX_GAIN_MAX);
  CHECK_EQ(exerciseGain(255), (uint8_t)EX_GAIN_MAX);
  for (int n = 1; n <= 255; n++) CHECK(exerciseGain((uint8_t)n) >= exerciseGain((uint8_t)(n - 1)));
  // each hit of a step pays more than each hit of the step before
  for (uint8_t n = 0; n + 2 * EX_TIER_HITS <= 40; n += EX_TIER_HITS)
    CHECK(exerciseGain(n + 2 * EX_TIER_HITS) - exerciseGain(n + EX_TIER_HITS) >
          exerciseGain(n + EX_TIER_HITS) - exerciseGain(n));
  CHECK_EQ(exerciseTier(0), (uint8_t)1);
  CHECK_EQ(exerciseTier(4), (uint8_t)1);
  CHECK_EQ(exerciseTier(5), (uint8_t)2);
  CHECK_EQ(exerciseTier(10), (uint8_t)3);
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
// a run's first punch, landed at `at` (the first swing's contact)
static RP landedAt(uint32_t at) {
  RP p;
  p.begin(at - RP::swingMs(0) / 2);
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
  // At every pace (#126: the swings get quicker), a punch landed the latest it can, and the
  // moment the next window opens: idle CLEAR_MS before it, the strike still shown a while.
  uint32_t shortest = PL::MAX_MS;
  for (uint8_t n = 0; n + 1 < EX_MAX_COUNT; n++) {
    RP q = perfectPunches(n);
    uint32_t start = n ? q.nextStart : 0;
    uint32_t len = RP::swingMs(n), w = RP::windowMs(n);
    CHECK_EQ(q.tap(start + len / 2 + w - 1), RP::TAP_LANDED);
    uint32_t next = q.nextOpen();
    CHECK(q.open(next));
    CHECK(!q.open(next - 1));
    CHECK_MSG(l.frameAt(q, next - PL::CLEAR_MS, ms) == -1,
              "idle CLEAR_MS before the next \"now!\" after " + testStr(n) + " punches");
    uint32_t len2 = PL::lengthFor(q);
    if (len2 < shortest) shortest = len2;
  }
  CHECK_MSG(shortest >= 120, "at the quickest the strike still shows: " + testStr(shortest));
}
TEST(PunchLunge, OnlyALandedPunchLunges) {
  LungeFrames f;
  f.add(0, 5).add(4, 9).add(2, 7);
  PL l = f.plan();
  const uint16_t ms[] = {100, 100, 100};
  RP p; p.begin(0);
  const uint32_t C = RP::swingMs(0) / 2, W = RP::windowMs(0);
  CHECK_MSG(l.frameAt(p, 100, ms) == -1, "no punch yet: the pet stands on IDLE");
  CHECK_EQ(p.tap(C - W - 1), RP::TAP_NOTHING);  // too early in the warm-up: lands nothing
  CHECK_MSG(l.frameAt(p, C - W, ms) == -1, "a tap that doesn't land shows nothing");
  CHECK_EQ(p.tap(C), RP::TAP_LANDED);
  CHECK_EQ(l.frameAt(p, C, ms), 1);
  CHECK_EQ(p.tap(C + 100), RP::TAP_NOTHING);  // a bounce of the same tap: no new lunge
  CHECK_MSG(l.frameAt(p, C + 150, ms) == 2, "the first punch's lunge runs on");
}
TEST(PunchLunge, AFrameThatDoesNotFitTheRoomKeepsTodaysLook) {
  // 64 x 64 at scale 4: ROOM_UP is 228 (gameui.cpp keeps it clear of the pay step), so a
  // 57-row frame is 228 px tall (the room), 58 rows is 232
  CHECK_EQ(PL::ROOM_UP, 57 * 4);
  CHECK_EQ(PL::ROOM_SIDE, 30 * 4);
  LungeFrames tall(64, 64);
  tall.add(0, 57, 20, 40).add(1, 58, 20, 40);
  CHECK_MSG(tall.plan().ok, "exactly ROOM_UP tall fits");
  LungeFrames taller(64, 64);
  taller.add(0, 57, 20, 40).add(0, 58, 20, 40);
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
  // a plan that isn't ok still records the sprite it looked at (gameui.cpp plans a sprite
  // once, and drawPunch asks isFor): scale 0 is "no PMD sprite", whose pointers may be null
  CHECK_MSG(l.isFor(f.px.data(), f.W, f.H, f.n), "a plan with no scale knows its sprite");
  l.plan(nullptr, 0, 0, 0, 0);
  CHECK(!l.ok);
  CHECK_MSG(l.isFor(nullptr, 0, 0, 0), "an unloaded sprite is planned once, not every frame");
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
TEST(PunchLunge, PlanningAgainStartsFromScratch) {
  // the firmware plans into one static, again when the sprite changes: nothing of the last
  // plan may survive (every other test plans into a fresh object)
  PL l;
  LungeFrames a;  // fits; the strike is its third frame
  a.add(0, 5).add(1, 6).add(4, 9).add(3, 8).add(2, 7);
  l.plan(a.px.data(), a.W, a.H, a.n, 4);
  CHECK(l.ok);
  CHECK_EQ(l.strike, (uint8_t)2);
  LungeFrames big(64, 64);  // doesn't fit (64 rows at scale 4); its strike is frame 1
  big.add(0, 60, 20, 40).add(0, 64, 20, 40);
  l.plan(big.px.data(), big.W, big.H, big.n, 4);
  CHECK_MSG(!l.ok, "a plan that doesn't fit is not ok, whatever the one before was");
  CHECK_EQ(l.strike, (uint8_t)1);
  LungeFrames c;  // the lowest frame is frame 0
  c.add(5, 9).add(2, 6).add(1, 5);
  l.plan(c.px.data(), c.W, c.H, c.n, 4);
  CHECK(l.ok);
  CHECK_MSG(l.strike == 0, "the strike starts from frame 0 again: " + testStr(l.strike));
  LungeFrames d;  // frame 0 is empty, before the strike
  d.addEmpty().add(4, 9).add(2, 7);
  l.plan(d.px.data(), d.W, d.H, d.n, 4);
  CHECK(l.ok);
  CHECK_MSG(l.feet[0] == 0, "an empty frame has no feet: " + testStr(l.feet[0]));
  CHECK_EQ(l.strike, (uint8_t)1);
}
TEST(PunchLunge, ScaleRuleIsTheRenderers) {
  // pmdScaleFor(idleH, actH, maxS): IDLE fills about 170 px (never under 2, never over the
  // cap); a taller action steps down until it is at most 250 px, but not under 2
  CHECK_EQ(pmdScaleFor(0, 0, 5), (uint8_t)5);     // no IDLE height: the cap
  CHECK_EQ(pmdScaleFor(0, 0, 4), (uint8_t)4);
  CHECK_EQ(pmdScaleFor(34, 34, 5), (uint8_t)5);   // 170 / 34
  CHECK_EQ(pmdScaleFor(36, 36, 5), (uint8_t)4);   // 170 / 36 (180 / 36 would be 5)
  CHECK_EQ(pmdScaleFor(42, 42, 4), (uint8_t)4);
  CHECK_EQ(pmdScaleFor(100, 100, 5), (uint8_t)2); // 170 / 100 = 1: the floor
  CHECK_EQ(pmdScaleFor(34, 50, 5), (uint8_t)5);   // 250 px: not over
  CHECK_EQ(pmdScaleFor(34, 51, 5), (uint8_t)4);   // 255 px: one step down
  CHECK_EQ(pmdScaleFor(42, 64, 4), (uint8_t)3);   // 256 px
  CHECK_EQ(pmdScaleFor(42, 80, 4), (uint8_t)3);   // 320 px, 240 at scale 3
  CHECK_EQ(pmdScaleFor(42, 88, 4), (uint8_t)2);   // 352 and 264 px: down to 2
  CHECK_EQ(pmdScaleFor(42, 200, 4), (uint8_t)2);  // still over at 2: the floor
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
