#include "trainingtouch.h"
#include <initializer_list>

// Every firmware build compiles these tests, even without a host C++ compiler.
namespace {
using T = TrainingTouch;
// Training taps are 350 ms or more apart (a swing is at least RhythmPunch::SWING_MIN_MS), and
// a lift is confirmed after LiftConfirm::CONFIRM_MS of reports with no finger, so a tap's
// finger has been confirmed gone (a RELEASE, which the punch ignores) before the next goes down.
constexpr bool rapid() {
  T t;
  for (uint32_t n = 0; n < 100; ++n) {
    if (t.update(true, 233, 120, n * 350) != T::HIT) return false;
    if (t.update(true, 235, 122, n * 350 + 5) != T::NONE) return false;
    if (t.update(false, 0, 0, n * 350 + 20) != T::NONE) return false;
    if (t.update(false, 0, 0, n * 350 + 40) != T::NONE) return false;
    if (t.update(false, 0, 0, n * 350 + 60) != T::RELEASE) return false;
  }
  return true;
}
// One report with no finger inside a held tap (an ACK or EMPTY from the chip, then the contact
// again) is the same finger: one HIT, not two, or a punch's tap would count twice (R1-1).
constexpr bool ackInsideATapIsOneHit() {
  T t;
  if (t.update(true, 233, 120, 0) != T::HIT) return false;
  for (uint32_t n = 20; n <= 160; n += 20)
    if (t.update(true, 233, 120, n) != T::NONE) return false;
  if (t.update(false, 0, 0, 180) != T::NONE) return false;
  if (t.update(true, 233, 120, 200) != T::NONE) return false;  // back 20 ms later: the same
  if (t.update(false, 0, 0, 220) != T::NONE) return false;
  if (t.update(false, 0, 0, 240) != T::NONE) return false;
  if (t.update(false, 0, 0, 260) != T::RELEASE || t.liftAt() != 220) return false;
  // a new tap after the lift was confirmed
  if (t.update(false, 0, 0, 400) != T::NONE) return false;
  if (t.update(true, 233, 120, 420) != T::HIT) return false;
  // one report with no finger and the contact read a frame later (the 1.75's poll): the same
  // finger still (R2-1), however late it is read
  if (t.update(false, 0, 0, 440) != T::NONE) return false;
  if (t.update(true, 233, 120, 440 + 65) != T::NONE) return false;
  if (t.update(false, 0, 0, 525) != T::NONE) return false;
  if (t.update(false, 0, 0, 545) != T::NONE) return false;
  if (t.update(false, 0, 0, 565) != T::RELEASE) return false;
  return t.update(true, 233, 120, 600) == T::HIT;
}
constexpr bool oldQuitStrip() {
  T t;
  return t.update(true, 233, 30, 0) == T::HIT &&
         t.update(true, 233, 30, 2000) == T::NONE;
}
constexpr bool broadTarget() {
  for (auto y : {80, 160, 240, 320, 370}) {
    for (auto x : {130, 233, 336}) {
      T t;
      if (t.update(true, x, y, 0) != T::HIT) return false;
    }
  }
  return true;
}
constexpr bool fastExitTaps() {
  T t;
  for (uint32_t n = 0; n < 100; ++n) {
    if (t.update(true, 233, 405, n * 50) != T::NONE) return false;
    if (t.update(false, 0, 0, n * 50 + 20) != T::NONE) return false;
  }
  return true;
}
constexpr bool hold(uint32_t start = 100) {
  T t;
  for (uint32_t elapsed = 0; elapsed < 1000; elapsed += 25)
    if (t.update(true, 233, 405, start + elapsed) != T::NONE) return false;
  if (t.update(true, 233, 405, start + 999) != T::NONE) return false;
  return t.update(true, 233, 405, start + 1000) == T::EXIT &&
         t.update(true, 233, 405, start + 2000) == T::NONE &&
         t.update(false, 0, 0, start + 2100) == T::NONE &&
         t.update(true, 233, 233, start + 2150) == T::HIT;
}
constexpr bool shortHold() {
  T t;
  for (uint32_t n = 0; n <= 975; n += 25)
    if (t.update(true, 233, 405, n) != T::NONE) return false;
  return t.update(false, 0, 0, 999) == T::NONE && t.exitHeldMs(1000) == 0;
}
constexpr bool slideOutAndBack() {
  T t; t.update(true, 233, 405, 0);
  t.update(true, 233, 370, 25);
  for (uint32_t n = 50; n <= 1500; n += 25)
    if (t.update(true, 233, 405, n) != T::NONE) return false;
  return t.exitHeldMs(1500) == 0;
}
constexpr bool slideIntoExit() {
  T t;
  if (t.update(true, 233, 233, 0) != T::HIT) return false;
  for (uint32_t n = 25; n <= 1500; n += 25)
    if (t.update(true, 233, 405, n) != T::NONE) return false;
  return true;
}
constexpr bool missingReports() {
  T t; t.update(true, 233, 405, 0);
  return t.update(true, 233, 405, 1000) == T::NONE && t.exitHeldMs(1000) == 0;
}
constexpr bool heldThroughReset() {
  T t; t.update(true, 233, 405, 0); t.reset(true);
  for (uint32_t n = 25; n < 2000; n += 25)
    if (t.update(true, 233, 405, n) != T::NONE) return false;
  return t.update(false, 0, 0, 2000) == T::NONE &&
         t.update(true, 233, 233, 2050) == T::HIT;
}
constexpr uint8_t down[] = {0x06, 0x0E, 0x0E, 0x99, 0, 1, 0xAB};
constexpr uint8_t top[] = {0x06, 0x0E, 0x1B, 0x94, 0, 1, 0xAB};  // raw (233,436) -> UI (233,30)
constexpr uint8_t up[] = {0, 0, 0, 0, 0, 0, 0xAB};
constexpr uint8_t stale[] = {0xAB, 0, 0, 0, 0, 0, 0xAB};
constexpr uint8_t bad[] = {0x06, 0x0E, 0x0E, 0x99, 0, 1, 0};
constexpr uint8_t cover[] = {0x06, 0x0E, 0x0E, 0x99, 0x80, 1, 0xAB};
constexpr uint8_t outside[] = {0x06, 0xFF, 0x0E, 0x99, 0, 1, 0xAB};
static_assert(rapid(), "100 taps 350 ms apart count once each; no repeat while held");
static_assert(ackInsideATapIsOneHit(), "an ACK/empty report inside a held tap is the same finger, not a second HIT");
static_assert(oldQuitStrip(), "top strip hits, never quits");
static_assert(broadTarget(), "large tap zone around and below the visible sack");
static_assert(fastExitTaps(), "repeated quick taps on exit never quit or score");
static_assert(hold(), "exit at one second exactly, once, then swallow release");
static_assert(hold(0xFFFFFF00u), "exit hold survives millis rollover");
static_assert(shortHold(), "release just before one second cancels exit");
static_assert(slideOutAndBack(), "leaving exit cancels the entire hold");
static_assert(slideIntoExit(), "a strike dragged onto exit never quits");
static_assert(missingReports(), "missing reports cannot complete an exit hold");
static_assert(heldThroughReset(), "lock/unlock discards partial exit; require release");
static_assert(!T::onPanel(0, 0) && !T::onPanel(467, 233) && T::onPanel(233, 0), "round-panel bounds");
static_assert(T::inExit(153, 384) && T::inExit(312, 431) && !T::inExit(313, 432), "exit bounds match UI");
static_assert(decodeTrainingReport(down, sizeof(down)).pressed && decodeTrainingReport(down, sizeof(down)).x == 233, "native mirrored touch coordinates");
static_assert(decodeTrainingReport(top, sizeof(top)).y == 30, "top strip mapping, no old-board scaling");
static_assert(decodeTrainingReport(up, sizeof(up)).valid && !decodeTrainingReport(up, sizeof(up)).pressed, "real release packet");
static_assert(decodeTrainingReport(stale, sizeof(stale)).valid && !decodeTrainingReport(stale, sizeof(stale)).pressed,
              "regression: ACK/empty must rearm like legacy getPoint(), not latch down");
static_assert(decodeTrainingReport(bad, sizeof(bad)).valid && !decodeTrainingReport(bad, sizeof(bad)).pressed &&
              !decodeTrainingReport(down, 6).valid, "completed filtered report is empty; short read is error");
static_assert(decodeTrainingReport(cover, sizeof(cover)).valid && !decodeTrainingReport(cover, sizeof(cover)).pressed &&
              !decodeTrainingReport(outside, sizeof(outside)).valid, "cover is empty; invalid coordinates cannot score");
// Since #126 a lift keeps the chip read every 20 ms until it is confirmed (readTrainingTouch),
// so a tap's lift is a run of empty reports, and the next contact is a new HIT.
constexpr bool legacyEmptyRearms() {
  T t;
  auto contact = decodeTrainingReport(down, sizeof(down));
  auto empty = decodeTrainingReport(stale, sizeof(stale));
  for (uint32_t n = 0; n < 100; ++n) {
    if (t.update(contact.pressed, contact.x, contact.y, n * 200) != T::HIT) return false;
    if (t.update(empty.pressed, empty.x, empty.y, n * 200 + 100) != T::NONE) return false;
    if (t.update(empty.pressed, empty.x, empty.y, n * 200 + 120) != T::NONE) return false;
    if (t.update(empty.pressed, empty.x, empty.y, n * 200 + 140) != T::RELEASE) return false;
  }
  return true;
}
constexpr bool manyContactReportsAreNotManyTaps() {
  T t;
  auto contact = decodeTrainingReport(down, sizeof(down));
  if (t.update(contact.pressed, contact.x, contact.y, 0) != T::HIT) return false;
  for (uint32_t n = 20; n <= 10000; n += 20)
    if (t.update(contact.pressed, contact.x, contact.y, n) != T::NONE) return false;
  return true;
}
constexpr uint8_t secondActive[] = {0x10, 0, 0, 0, 0, 2, 0xAB, 0x06, 0x0E, 0x0E, 0x99, 0};
constexpr uint8_t zeroHeaderNoTrailer[] = {0, 0, 0, 0, 0, 0, 0};
constexpr bool emptyClearsReleaseGuard() {
  T t; t.reset(true);
  auto empty = decodeTrainingReport(stale, sizeof(stale));
  return t.update(empty.pressed, 0, 0, 0) == T::NONE && !t.waitingRelease() &&
         t.update(true, 233, 233, 100) == T::HIT;
}
static_assert(legacyEmptyRearms(), "100 contact/ACK-empty cycles yield 100 hits, not one latched contact");
static_assert(manyContactReportsAreNotManyTaps(), "0x06 is contact, NOT a unique DOWN or per-IRQ strike");
static_assert(decodeTrainingReport(secondActive, sizeof(secondActive)).pressed &&
              decodeTrainingReport(secondActive, sizeof(secondActive)).x == 233,
              "choose first ACTIVE point like SensorLib, not first raw record");
static_assert(decodeTrainingReport(zeroHeaderNoTrailer, sizeof(zeroHeaderNoTrailer)).valid &&
              !decodeTrainingReport(zeroHeaderNoTrailer, sizeof(zeroHeaderNoTrailer)).pressed,
              "legacy empty packet without trailer must not latch previous contact");
static_assert(emptyClearsReleaseGuard(), "legacy empty sample clears a held-exit guard");
// The push-ups (#126): a HIT's finger lifting is a RELEASE once reports with no finger go on
// for LiftConfirm::CONFIRM_MS, timed at the first of them; one report with no finger between
// contacts is the same finger; the exit chip never releases.
constexpr bool releaseIsConfirmedAndTimedAtTheLift() {
  T t;
  if (t.update(true, 233, 233, 0) != T::HIT) return false;
  for (uint32_t n = 20; n <= 1000; n += 20)
    if (t.update(true, 233, 233, n) != T::NONE) return false;
  return t.update(false, 0, 0, 1020) == T::NONE && t.update(false, 0, 0, 1040) == T::NONE &&
         t.update(false, 0, 0, 1060) == T::RELEASE && t.liftAt() == 1020 &&
         t.update(false, 0, 0, 1080) == T::NONE;
}
constexpr bool oneEmptyReportIsNotALetGo() {
  T t;
  auto contact = decodeTrainingReport(down, sizeof(down));
  auto empty = decodeTrainingReport(stale, sizeof(stale));
  if (t.update(contact.pressed, contact.x, contact.y, 0) != T::HIT) return false;
  for (uint32_t n = 20; n <= 2000; n += 40) {
    if (t.update(empty.pressed, empty.x, empty.y, n) == T::RELEASE) return false;
    if (t.update(contact.pressed, contact.x, contact.y, n + 20) == T::RELEASE) return false;
  }
  return true;
}
constexpr bool exitChipNeverReleases() {
  T t;
  for (uint32_t n = 0; n < 500; n += 20)
    if (t.update(true, 233, 405, n) != T::NONE) return false;
  for (uint32_t n = 500; n < 1500; n += 20)
    if (t.update(false, 0, 0, n) != T::NONE) return false;
  return true;
}
// A push-up let go and pressed again 45 ms later, as the 1.75C's chip reported it on the
// hardware try (2026-11-28, TRAINTRACE; the first 12 of each read's 15 bytes): its release
// report, an empty read, the contact back. A let-go timed at the release report, and a new
// press (R2-2); one report with no finger before a contact would be the same finger.
constexpr uint8_t triedDown[] = {0x06, 0x0C, 0x08, 0x3B, 0x00, 0x01, 0xAB, 0, 0, 0, 0x3C, 0x4A};
constexpr uint8_t triedLift[] = {0x00, 0x0C, 0x08, 0x3B, 0x00, 0x01, 0xAB, 0, 0, 0, 0x3C, 0x4A};
constexpr uint8_t triedEmpty[] = {0x0C, 0x08, 0x3B, 0x00, 0x01, 0xAB, 0, 0, 0, 0x3C, 0x4A, 0x09};
constexpr uint8_t triedBack[] = {0x06, 0x0C, 0x08, 0x9D, 0x00, 0x01, 0xAB, 0, 0, 0, 0x3C, 0x4A};
constexpr bool quickRePressIsALetGoAndANewPress() {
  T t;
  auto down = decodeTrainingReport(triedDown, sizeof(triedDown));
  auto lift = decodeTrainingReport(triedLift, sizeof(triedLift));
  auto empty = decodeTrainingReport(triedEmpty, sizeof(triedEmpty));
  auto back = decodeTrainingReport(triedBack, sizeof(triedBack));
  if (t.update(down.pressed, down.x, down.y, 1157839) != T::HIT) return false;
  for (uint32_t n = 1157859; n <= 1158499; n += 20)
    if (t.update(down.pressed, down.x, down.y, n) != T::NONE) return false;
  if (t.update(lift.pressed, lift.x, lift.y, 1158519) != T::NONE) return false;
  if (t.update(empty.pressed, empty.x, empty.y, 1158539) != T::NONE) return false;
  if (t.update(back.pressed, back.x, back.y, 1158564) != T::RELEASE_HIT || t.liftAt() != 1158519) return false;
  if (t.update(back.pressed, back.x, back.y, 1158584) != T::NONE) return false;
  // the new finger lets go in its turn, timed at its own first report with no finger
  if (t.update(lift.pressed, lift.x, lift.y, 1159135) != T::NONE) return false;
  if (t.update(empty.pressed, empty.x, empty.y, 1159155) != T::NONE) return false;
  return t.update(empty.pressed, empty.x, empty.y, 1159175) == T::RELEASE && t.liftAt() == 1159135;
}
// ...and pressed again on the exit chip: only the let-go (the chip's hold arms as usual)
constexpr bool quickRePressOnTheExitChipOnlyLetsGo() {
  T t;
  if (t.update(true, 233, 233, 0) != T::HIT) return false;
  if (t.update(false, 0, 0, 20) != T::NONE || t.update(false, 0, 0, 40) != T::NONE) return false;
  return t.update(true, 233, 405, 50) == T::RELEASE && t.liftAt() == 20 && t.exitHeldMs(60) == 10;
}
static_assert(quickRePressIsALetGoAndANewPress(), "a re-press after two reports with no finger lets go, then presses");
static_assert(quickRePressOnTheExitChipOnlyLetsGo(), "a re-press on the exit chip only lets go");
static_assert(releaseIsConfirmedAndTimedAtTheLift(), "a let-go counts once confirmed, timed when it left");
static_assert(oneEmptyReportIsNotALetGo(), "an ACK/empty report between contacts is not a let-go");
static_assert(exitChipNeverReleases(), "a touch on the exit chip never lets a push-up go");
static_assert(T::POLL_MS == 20, "keep proven cadence, not four-times-faster ACK traffic");
}  // namespace
