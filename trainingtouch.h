#pragma once
#include <stdint.h>
#include <stddef.h>

// A finger lifting, confirmed (#126): the push-ups judge the moment it let go, so one report
// with no finger between contacts (an EMPTY or ACK from the chip, decodeTrainingReport) must
// not read as letting go. The lift is timed at the first report with no finger (lost) and
// counts once reports with no finger have gone on for CONFIRM_MS (confirm). Before then:
// - A contact after one report with no finger, however late it is read, is the same finger
//   (sameFinger, then back): the original 1.75 reads the chip only between frames, so its
//   reports can be 65 ms or more apart (R2-1).
// - A contact after LET_GO_REPORTS of them is a new press, and the finger did let go, at `at`
//   (pressedAgain, then back): a push-up let go and pressed again within CONFIRM_MS (R2-2).
// The 1.75C's hardware try (2026-11-28, TRAINTRACE, about 1000 reads over 45 holds and taps
// of up to 1.3 s) found no report with no finger while a finger was held. Every lift was
// the chip's release report and then empty reads, and a quick re-press came back after two.
// Both boards use it: TrainingTouch on the 1.75C, the sketch's own touch poll on the
// original 1.75.
struct LiftConfirm {
  static constexpr uint32_t CONFIRM_MS = 40;  // two more 20 ms polls with no finger
  static constexpr uint8_t LET_GO_REPORTS = 2;  // ...and a contact after this many: a new press
  bool pending = false;
  uint8_t reports = 0;  // reports with no finger since `at` (stops counting at LET_GO_REPORTS)
  uint32_t at = 0;  // the first report with no finger
  constexpr void reset() { pending = false; reports = 0; at = 0; }
  // A report with no finger at `now`: the first starts the lift, and every one while it waits
  // counts. Call it for a finger that was down or a lift that waits.
  constexpr void lost(uint32_t now) {
    if (!pending) { pending = true; reports = 0; at = now; }
    if (reports < LET_GO_REPORTS) ++reports;
  }
  constexpr void back() { pending = false; reports = 0; }
  // A contact while a lift waits, after one report with no finger: the same finger (an ACK or
  // an EMPTY in the middle of a tap or a hold). Not a let-go, and not a new touch-down
  // either, or a punch's tap would count twice. Ask before back().
  constexpr bool sameFinger() const { return pending && reports < LET_GO_REPORTS; }
  // A contact while a lift waits, after LET_GO_REPORTS reports with no finger: the finger let
  // go at `at` and this is a new press. Ask before back().
  constexpr bool pressedAgain() const { return pending && reports >= LET_GO_REPORTS; }
  // A report with no finger at `now`: true once, when the lift is confirmed (timed at `at`).
  constexpr bool confirm(uint32_t now) {
    if (!pending || now - at < CONFIRM_MS) return false;
    pending = false;
    return true;
  }
};

// Pure training input policy (the rhythm punch and the push-ups, #78, #126). Coordinates
// are the existing native 466x466 UI coordinates (both axes mirrored by the board's
// touch setup). A touch-down anywhere but the exit chip is a HIT (a punch's tap, or a
// push-up going down); the confirmed lift of a HIT's finger is a RELEASE (a push-up let go),
// timed at liftAt(). A release is never a tap. A finger let go and put down again before
// its lift is confirmed is RELEASE_HIT: the RELEASE, timed at liftAt(), then a HIT now
// (LiftConfirm::pressedAgain, R2-2).
class TrainingTouch {
 public:
  enum Action : uint8_t { NONE, HIT, EXIT, RELEASE, RELEASE_HIT };
  static constexpr int EXIT_X = 153, EXIT_Y = 384, EXIT_W = 160, EXIT_H = 48;
  static constexpr uint32_t EXIT_HOLD_MS = 1000;
  static constexpr uint32_t REPORT_GAP_MS = 100;  // a missing stream cannot complete a hold
  static constexpr uint32_t POLL_MS = 20;  // restore the hardware-tested SensorLib cadence

  static constexpr bool onPanel(int x, int y) {
    return x >= 0 && x <= 466 && y >= 0 && y <= 466 &&
           (x - 233) * (x - 233) + (y - 233) * (y - 233) <= 233 * 233;
  }
  static constexpr bool inExit(int x, int y) {
    return x >= EXIT_X && x < EXIT_X + EXIT_W && y >= EXIT_Y && y < EXIT_Y + EXIT_H;
  }
  constexpr void reset(bool requireRelease = false) {
    down_ = false; blocked_ = requireRelease; exitArmed_ = false; hitDown_ = false;
    exitStart_ = lastReport_ = 0;
    lift_.reset();
  }
  constexpr bool down() const { return down_; }
  // When the finger of the last RELEASE or RELEASE_HIT left the glass (its first report with
  // no finger).
  constexpr uint32_t liftAt() const { return lift_.at; }
  // A lift waits for its confirming reports: the chip must still be read, as while a finger
  // is down (the reader reads only then, or on the chip's interrupt).
  constexpr bool liftPending() const { return lift_.pending; }
  // Reports with no finger the waiting lift has had, 0 with none waiting (the sim's re-press).
  constexpr uint8_t liftReports() const { return lift_.pending ? lift_.reports : 0; }
  constexpr bool waitingRelease() const { return blocked_; }
  constexpr uint32_t exitHeldMs(uint32_t now) const {
    return exitArmed_ && now - lastReport_ <= REPORT_GAP_MS ? now - exitStart_ : 0;
  }
  constexpr Action update(bool pressed, int x, int y, uint32_t now) {
    if (blocked_) {
      down_ = pressed;
      if (!pressed) blocked_ = false;
      return NONE;
    }
    bool start = pressed && !down_;
    if (down_ && now - lastReport_ > REPORT_GAP_MS) exitArmed_ = false;
    down_ = pressed;
    lastReport_ = now;
    if (!pressed) {
      exitArmed_ = false;
      // A HIT's finger left: a RELEASE once confirmed. A touch that began on the exit chip,
      // or one swallowed after an exit or a lock (requireRelease), never releases.
      if (hitDown_ || lift_.pending) lift_.lost(now);
      hitDown_ = false;
      return lift_.confirm(now) ? RELEASE : NONE;
    }
    if (start) {
      // The same finger back is not a new HIT (LiftConfirm::sameFinger); it still lets go
      // later. A new press before the lift was confirmed lets that finger go first (R2-2).
      bool same = lift_.sameFinger(), again = lift_.pressedAgain();
      lift_.back();
      exitArmed_ = onPanel(x, y) && inExit(x, y);
      exitStart_ = now;
      hitDown_ = onPanel(x, y) && !exitArmed_;
      if (again) return hitDown_ ? RELEASE_HIT : RELEASE;
      return hitDown_ && !same ? HIT : NONE;
    }
    // Sliding into the button or back in after leaving it never arms exit.
    if (!onPanel(x, y) || !inExit(x, y)) exitArmed_ = false;
    if (exitArmed_ && now - exitStart_ >= EXIT_HOLD_MS) {
      exitArmed_ = false;
      blocked_ = true;  // exit exactly once; swallow this contact on the next screen
      return EXIT;
    }
    return NONE;
  }
 private:
  bool down_ = false, blocked_ = false, exitArmed_ = false, hitDown_ = false;
  uint32_t exitStart_ = 0, lastReport_ = 0;
  LiftConfirm lift_;
};

struct TrainingReport {
  bool valid = false, pressed = false;
  int16_t x = 0, y = 0;
  enum Kind : uint8_t { ERROR, CONTACT, RELEASE, EMPTY, COVER };
  Kind kind = ERROR;
  bool attempted = false, irq = false;
  uint8_t bytes = 0, raw[12] = {};
};

// Match SensorLib 0.5.1 getTouchPoints()/getPoint() semantics, used by the
// pre-regression firmware: a completed empty/ACK/filtered report means zero
// points, NOT "keep the previous finger down". 0x06 means contact present,
// not a unique DOWN event; track IDs are slots, not monotonic tap counters.
// Never count every IRQ/contact frame or invent timed repeat strikes.
constexpr TrainingReport decodeTrainingReport(const uint8_t *b, size_t n) {
  TrainingReport r;
  r.attempted = true;
  r.bytes = n > 255 ? 255 : (uint8_t)n;
  for (size_t i = 0; i < n && i < sizeof(r.raw); ++i) r.raw[i] = b[i];
  if (n < 7) return r;
  r.valid = true;
  r.kind = TrainingReport::EMPTY;
  if (b[0] == 0xAB || b[6] != 0xAB) return r;
  if (b[0] == 0) { r.kind = TrainingReport::RELEASE; return r; }
  if (b[4] & 0x80) { r.kind = TrainingReport::COVER; return r; }
  uint8_t count = b[5] & 0x7F;
  if (count > 2) return r;
  if (!count) { r.kind = TrainingReport::RELEASE; return r; }
  // Like SensorLib, choose the FIRST ACTIVE point, not blindly record zero.
  // For two fingers the first slot may be up while the second is still down.
  for (uint8_t i = 0; i < count; ++i) {
    size_t offset = i ? 7 : 0;
    if (n < offset + 5) { r.valid = false; r.kind = TrainingReport::ERROR; return r; }
    const uint8_t *p = b + offset;
    if ((p[0] & 0x0F) != 0x06 || (p[0] >> 4) >= 2) continue;
    int x = (p[1] << 4) | (p[3] >> 4);
    int y = (p[2] << 4) | (p[3] & 0x0F);
    if (x > 466 || y > 466) { r.valid = false; r.kind = TrainingReport::ERROR; return r; }
    r.pressed = true; r.kind = TrainingReport::CONTACT;
    r.x = (int16_t)(466 - x); r.y = (int16_t)(466 - y);
    return r;
  }
  r.kind = TrainingReport::RELEASE;
  return r;
}
