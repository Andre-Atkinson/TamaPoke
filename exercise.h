// The two exercises the stat card's TRAIN button offers (#78): the rhythm punch trains
// strength, the push-ups train stamina (speed stays with the Play minigames). Only their
// rules live here, with no drawing and no touch reading, so the host tests
// (test/test_trainingtouch.cpp) and both boards share them. gameui.cpp draws them and feeds
// them touches; Pet::trainStrength() and Pet::trainStamina() pay for them.
//
// Endless and skill-based (#126, owner's decisions 2026-11-28). A session has no fixed
// length: it runs until the first miss and gets harder with every hit, so every run ends
// sooner or later. The miss only ends the run: the session still pays for everything done,
// and the pay per hit rises the longer the run goes (exerciseGain), so going on is worth it.
// Until the first hit lands the session is a warm-up, where a miss costs nothing and doesn't
// end it (an off-beat tap does nothing; push-ups let go at the wrong moment are shown and
// tried again), so a start that takes a try or two never ends at 0. Leaving early (the exit)
// ends the session and pays for what was done (owner's decision, 2026-11-16). Until #126
// neither exercise could be failed, and each was 12 punches or reps.
#pragma once
#include <math.h>
#include <stdint.h>
#include "gamecore.h"  // GameRng

// A session ends by itself at EX_MAX_COUNT hits (no run gets near it), so the count and the
// records stay 8-bit, and a record loaded from NVS above it is damaged.
#define EX_MAX_COUNT 200
// The pay per hit goes up a step every EX_TIER_HITS hits (the screen's "x2", "x3"...).
#define EX_TIER_HITS 5
// The most a session can pay: a training stat's top.
#define EX_GAIN_MAX 100

enum : uint8_t { EX_PUNCH = 0, EX_HOLD = 1, EX_NONE = 0xFF };  // EX_HOLD: the push-ups

// The step the next hit is paid at, with `done` hits so far: 1 for the 1st to 5th hit, 2 for
// the 6th to 10th...
inline uint8_t exerciseTier(uint8_t done) { return (uint8_t)(done / EX_TIER_HITS + 1); }

// Training points for a session of `count` hits: hit n is worth half its step (0.5 for the
// first five, 1 for the next five, 1.5...), summed and rounded half up, never past
// EX_GAIN_MAX. 10 hits are 8, 12 are 11, 15 are 15, 20 are 25, 22 are 30 (the strength
// ribbon), 30 are 53.
inline uint8_t exerciseGain(uint8_t count) {
  if (count > EX_MAX_COUNT) count = EX_MAX_COUNT;
  uint32_t full = count / EX_TIER_HITS, rest = count % EX_TIER_HITS;
  uint32_t halves = EX_TIER_HITS * full * (full + 1) / 2 + rest * (full + 1);
  uint32_t points = (halves + 1) / 2;
  return points > EX_GAIN_MAX ? (uint8_t)EX_GAIN_MAX : (uint8_t)points;
}

// t is at or after `since`, across the millis() wrap
inline bool exAtOrAfter(uint32_t t, uint32_t since) { return (int32_t)(t - since) >= 0; }

// The rhythm punch. The bag swings in to the pet and back out, and is at the pet halfway
// through each swing. A tap within the swing's window of that moment, either side, lands the
// swing's punch. Each punch landed makes the next swing shorter and its window narrower,
// down to a floor past what anyone keeps up for long. Before the first punch the swings
// repeat at the first one's pace from the start: the warm-up, where an off-beat tap does
// nothing and a swing may pass. From the first punch on, every swing must land: a swing whose
// window closes with no punch, a tap while no window is open, or a second tap in a swing that
// already landed, is the miss that ends the run. A second touch within BOUNCE_MS of a landed
// punch is the same tap (a bounce of the panel's reports), never a miss.
// On the 1.75C a tap is judged on when it was captured (TrainingCapture's timestamp), so a
// frame being drawn never moves it out of its window. The original 1.75 has no capture
// worker: a tap is timed at the loop's next poll (exerciseTap(millis()) in handleTouch),
// which doesn't run while a frame draws, so it can be up to a frame late (POWER_GAME_FRAME_MS,
// 85 ms); even the narrowest window, 2 x WINDOW_MIN_MS wide, is wider than that (a
// static_assert in TamaPoke.ino checks it, and the push-ups' narrowest band).
struct RhythmPunch {
  static constexpr uint32_t SWING0_MS = 1500, SWING_STEP_MS = 35, SWING_MIN_MS = 350;
  static constexpr uint32_t WINDOW0_MS = 350, WINDOW_STEP_MS = 12, WINDOW_MIN_MS = 60;
  // A window that closed with no punch is the miss once this has passed too, so a tap
  // captured just before it closed and not yet read (the next poll, or the drain after the
  // frame) is judged first.
  static constexpr uint32_t SETTLE_MS = 60;
  static constexpr uint32_t BOUNCE_MS = 150;
  static constexpr uint32_t WARMUP_MS = 15000;  // no punch by then: the session ends, nothing paid
  enum Tap : uint8_t { TAP_NOTHING = 0, TAP_LANDED, TAP_MISSED };

  // The swing after `n` punches landed, and its window either side of the contact.
  static uint32_t swingMs(uint8_t n) {
    uint32_t cut = SWING_STEP_MS * n;
    return cut + SWING_MIN_MS >= SWING0_MS ? SWING_MIN_MS : SWING0_MS - cut;
  }
  static uint32_t windowMs(uint8_t n) {
    uint32_t cut = WINDOW_STEP_MS * n;
    return cut + WINDOW_MIN_MS >= WINDOW0_MS ? WINDOW_MIN_MS : WINDOW0_MS - cut;
  }
  // `phase` ms into a swing of `len` with window `w` either side of its middle
  static bool inWindow(uint32_t phase, uint32_t len, uint32_t w) {
    return phase + w >= len / 2 && phase < len / 2 + w;
  }

  uint32_t t0 = 0;           // when the first swing started
  uint8_t punches = 0;
  uint32_t lastPunchAt = 0;  // capture time of the last punch (the pet's lunge, the bag's knock)
  uint32_t nextStart = 0;    // from the first punch: when the swing the next punch lands in starts
  bool ended = false;        // over: a miss, the warm-up running out, or EX_MAX_COUNT punches
  bool missed = false;       // ...ended by a miss
  uint32_t endAt = 0;

  void begin(uint32_t now) {
    t0 = now;
    punches = 0;
    lastPunchAt = nextStart = endAt = 0;
    ended = missed = false;
  }
  uint32_t elapsed(uint32_t t) const { return t - t0; }

  // The swing `t` falls in: its start and length.
  void swingAt(uint32_t t, uint32_t &start, uint32_t &len) const {
    if (!punches) {
      len = SWING0_MS;
      start = t0 + elapsed(t) / len * len;
    } else if (!exAtOrAfter(t, nextStart)) {  // the swing the last punch landed in
      len = swingMs(punches - 1);
      start = nextStart - len;
    } else {
      len = swingMs(punches);
      start = nextStart;
    }
  }
  // Inside a window whose punch hasn't landed yet: the screen's "now!" cue.
  bool open(uint32_t now) const {
    if (ended || !exAtOrAfter(now, t0)) return false;
    if (!punches) return inWindow(elapsed(now) % SWING0_MS, SWING0_MS, WINDOW0_MS);
    if (!exAtOrAfter(now, nextStart)) return false;
    uint32_t len = swingMs(punches), ph = now - nextStart;
    return ph < len && inWindow(ph, len, windowMs(punches));
  }
  // A tap captured at `at`.
  Tap tap(uint32_t at) {
    if (ended || !exAtOrAfter(at, t0)) return TAP_NOTHING;
    if (!punches) {  // the warm-up
      uint32_t e = elapsed(at);
      if (!inWindow(e % SWING0_MS, SWING0_MS, WINDOW0_MS)) return TAP_NOTHING;
      land(at, t0 + (e / SWING0_MS + 1) * SWING0_MS);
      return TAP_LANDED;
    }
    if (!exAtOrAfter(at, lastPunchAt + BOUNCE_MS)) return TAP_NOTHING;  // the same tap
    if (open(at)) {
      land(at, nextStart + swingMs(punches));
      return TAP_LANDED;
    }
    end(at, true);
    return TAP_MISSED;
  }
  // Every frame: a window that closed with no punch is the miss (SETTLE_MS later), and a
  // warm-up with no punch for WARMUP_MS ends the session.
  void update(uint32_t now) {
    if (ended || !exAtOrAfter(now, t0)) return;
    if (!punches) {
      if (elapsed(now) >= WARMUP_MS) end(now, false);
      return;
    }
    uint32_t close = nextStart + swingMs(punches) / 2 + windowMs(punches);
    if (exAtOrAfter(now, close + SETTLE_MS)) end(close, true);
  }
  bool over() const { return ended; }
  // When the next swing's window opens: the punch lunge and the bag's knock are over first
  // (punchlunge.h). Only meaningful after a punch.
  uint32_t nextOpen() const { return nextStart + swingMs(punches) / 2 - windowMs(punches); }
  // How far in the bag is at `now`: 0 all the way out (each swing's start and end) to 1 at the
  // pet (the swing's middle), along a cosine like a pendulum's. Out once it is over.
  float reach(uint32_t now) const {
    if (ended || !exAtOrAfter(now, t0)) return 0.0f;
    uint32_t start, len;
    swingAt(now, start, len);
    uint32_t ph = now - start;
    if (ph > len) ph = len;
    return 0.5f - 0.5f * cosf((float)ph / (float)len * 6.28318531f);
  }

 private:
  void land(uint32_t at, uint32_t next) {
    punches++;
    lastPunchAt = at;
    nextStart = next;
    if (punches >= EX_MAX_COUNT) end(at, false);
  }
  void end(uint32_t at, bool miss) {
    ended = true;
    missed = miss;
    endAt = at;
  }
};

// The push-ups. Press and hold: the pet lowers itself while a bar fills. Let go while the
// fill is in the green band and the pet pushes up: a rep. Letting go before the band, or still
// holding when the fill has passed it, is the miss. Each rep picks its own fill time and band
// position at random (so it is watched, not counted), and rep by rep the fill gets quicker and
// the band shorter, down to a floor. A first rep let go at the wrong moment is the warm-up:
// it shows which way it missed and plans a new rep. With no finger for IDLE_END_MS between
// reps the session ends and pays for the reps done (the screen stays on while training).
// The press is a touch-down (TrainingTouch::HIT), the let-go a confirmed lift at the moment
// the finger left (TrainingTouch::RELEASE, timed by its first report with no finger), so a
// report stream that pauses never reads as letting go. Times are ms after the press.
struct PushUps {
  static constexpr uint32_t FILL0_MS = 1800, FILL_STEP_MS = 50, FILL_MIN_MS = 500;
  static constexpr uint32_t BAND0_MS = 500, BAND_STEP_MS = 16, BAND_MIN_MS = 100;
  // still down this long after the band: too late (the lift's confirmation,
  // LiftConfirm::CONFIRM_MS, and the next poll may still be on their way)
  static constexpr uint32_t LATE_MS = 100;
  static constexpr uint32_t IDLE_END_MS = 15000;
  enum Phase : uint8_t { READY = 0, DOWN, OVER };
  enum Miss : uint8_t { MISS_NONE = 0, MISS_EARLY, MISS_LATE };
  enum Result : uint8_t { PU_NOTHING = 0, PU_REP, PU_RETRY, PU_MISSED };

  // The fill time before its random spread (80-120%), and the band, after `n` reps.
  static uint32_t fillBaseMs(uint8_t n) {
    uint32_t cut = FILL_STEP_MS * n;
    return cut + FILL_MIN_MS >= FILL0_MS ? FILL_MIN_MS : FILL0_MS - cut;
  }
  static uint32_t bandMs(uint8_t n) {
    uint32_t cut = BAND_STEP_MS * n;
    return cut + BAND_MIN_MS >= BAND0_MS ? BAND_MIN_MS : BAND0_MS - cut;
  }

  uint8_t phase = READY;
  uint8_t reps = 0;
  uint8_t miss = MISS_NONE;   // how the run's miss went (OVER)
  uint8_t retry = MISS_NONE;  // how the warm-up's last try went, shown for a moment
  uint32_t retryAt = 0;
  uint32_t fillMs = FILL0_MS, bandLo = 0, bandHi = 0;  // the rep under way or next
  uint32_t pressAt = 0;   // DOWN: when the finger went down
  uint32_t readyAt = 0;   // READY: since when (the idle end)
  uint32_t lastRepAt = 0; // the pet's push up
  uint32_t endAt = 0;
  GameRng rng;

  void begin(uint32_t now, uint32_t seed) {
    rng.seedWith(seed);
    phase = READY;
    reps = 0;
    miss = retry = MISS_NONE;
    retryAt = pressAt = lastRepAt = endAt = 0;
    readyAt = now;
    plan();
  }
  // The next rep's fill time and band: the band starts 35% to 95% minus its length into the
  // fill, so the bar is never full before it's too late (the test checks every rep count).
  void plan() {
    fillMs = fillBaseMs(reps) * (80 + rng.below(41)) / 100;
    uint32_t band = bandMs(reps);
    uint32_t lo = fillMs * 35 / 100, hi = fillMs * 95 / 100 - band;
    bandLo = lo + rng.below(hi - lo + 1);
    bandHi = bandLo + band;
  }
  // The finger went down at `at`. Another touch-down while one is down is the same finger
  // (its reports paused and came back) and does nothing.
  void press(uint32_t at) {
    if (phase != READY) return;
    phase = DOWN;
    pressAt = at;
  }
  // The finger left at `at`.
  Result release(uint32_t at) {
    if (phase != DOWN) return PU_NOTHING;
    uint32_t d = exAtOrAfter(at, pressAt) ? at - pressAt : 0;
    if (d < bandLo) return fail(MISS_EARLY, at);
    if (d > bandHi) return fail(MISS_LATE, at);
    reps++;
    lastRepAt = at;
    retry = MISS_NONE;
    if (reps >= EX_MAX_COUNT) {
      end(MISS_NONE, at);
      return PU_REP;
    }
    phase = READY;
    readyAt = at;
    plan();
    return PU_REP;
  }
  // Every frame: still down past the band is too late; idle too long between reps ends it.
  Result update(uint32_t now) {
    if (phase == DOWN && exAtOrAfter(now, pressAt + bandHi + LATE_MS)) return fail(MISS_LATE, now);
    if (phase == READY && exAtOrAfter(now, readyAt + IDLE_END_MS)) end(MISS_NONE, now);
    return PU_NOTHING;
  }
  bool over() const { return phase == OVER; }
  bool down() const { return phase == DOWN; }
  // How full the bar is at `now`, 0 to 1 (0 while no rep is under way)
  float fill(uint32_t now) const {
    if (phase != DOWN || !exAtOrAfter(now, pressAt)) return 0.0f;
    uint32_t d = now - pressAt;
    return d >= fillMs ? 1.0f : (float)d / (float)fillMs;
  }
  // The fill is in the green band now
  bool inBand(uint32_t now) const {
    if (phase != DOWN || !exAtOrAfter(now, pressAt)) return false;
    uint32_t d = now - pressAt;
    return d >= bandLo && d <= bandHi;
  }

 private:
  Result fail(uint8_t how, uint32_t at) {
    if (!reps) {  // the warm-up: show it and try again
      retry = how;
      retryAt = at;
      phase = READY;
      readyAt = at;
      plan();
      return PU_RETRY;
    }
    end(how, at);
    return PU_MISSED;
  }
  void end(uint8_t how, uint32_t at) {
    phase = OVER;
    miss = how;
    endAt = at;
  }
};
