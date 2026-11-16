// The two calm exercises the stat card's TRAIN button offers (#78): the rhythm punch
// trains strength, the stamina hold trains stamina (speed stays with the Play
// minigames). Only their rules live here, with no drawing and no touch reading, so the
// host tests (test/test_trainingtouch.cpp) and both boards share them. gameui.cpp draws
// them and feeds them taps and holds; Pet::trainStrength() and Pet::trainStamina() pay
// for them.
//
// Neither can be failed. A tap at the wrong time does nothing and costs nothing, and
// lifting the finger only pauses the hold. Leaving early (the exit) ends the session
// and pays for what was done (owner's decision, 2026-11-16).
#pragma once
#include <math.h>
#include <stdint.h>

// A session is EX_COUNT punches or reps at most, each worth 1.5 training points, so a
// full session gives EX_GAIN_CAP: the old tap-as-fast-as-you-can bag's cap, so the pace
// at which strength trains doesn't change. Stamina gets the same cap (its passive +1 per
// 12 h of good care stays, Pet::tick()).
#define EX_COUNT 12
#define EX_GAIN_CAP 18

enum : uint8_t { EX_PUNCH = 0, EX_HOLD = 1, EX_NONE = 0xFF };

// Training points for `count` punches or reps: 1.5 each, rounded down, never past
// EX_GAIN_CAP.
inline uint8_t exerciseGain(uint8_t count) {
  if (count > EX_COUNT) count = EX_COUNT;
  return (uint8_t)(count * EX_GAIN_CAP / EX_COUNT);
}

// The rhythm punch. The bag swings in to the pet and back out once every SWING_MS,
// EX_COUNT times, and is at the pet halfway through each swing (CONTACT_MS). A tap
// within WINDOW_MS of that moment, either side, lands that swing's punch: the window is
// almost half of every swing, so the timing is forgiving. Any other tap, or a second one
// in the same swing, does nothing. Taps are judged on when they were captured (the
// 1.75C's TrainingCapture timestamp; the 1.75's 20 ms poll), not on when the loop gets
// to them, so a frame being drawn never moves a tap out of its window.
struct RhythmPunch {
  static constexpr uint32_t SWING_MS = 1500;
  static constexpr uint32_t CONTACT_MS = SWING_MS / 2;
  static constexpr uint32_t WINDOW_MS = 350;
  static constexpr uint32_t TOTAL_MS = SWING_MS * EX_COUNT;

  uint32_t t0 = 0;           // when the first swing started
  uint8_t punches = 0;
  uint16_t landed = 0;       // bit i: swing i's punch landed
  uint32_t lastPunchAt = 0;  // capture time of the last punch (the pet's lunge, the bag's knock)

  void begin(uint32_t now) {
    t0 = now;
    punches = 0;
    landed = 0;
    lastPunchAt = 0;
  }
  // ms since the start; a time before the start wraps to a huge number, past the end
  uint32_t elapsed(uint32_t t) const { return t - t0; }
  bool over(uint32_t now) const { return elapsed(now) >= TOTAL_MS; }
  static bool inWindow(uint32_t phase) {
    return phase + WINDOW_MS >= CONTACT_MS && phase < CONTACT_MS + WINDOW_MS;
  }
  // The swing `t` falls in (EX_COUNT once the session is over).
  uint8_t swing(uint32_t t) const {
    uint32_t e = elapsed(t);
    return e >= TOTAL_MS ? (uint8_t)EX_COUNT : (uint8_t)(e / SWING_MS);
  }
  // Inside a window whose punch hasn't landed yet: the screen's "now!" cue.
  bool open(uint32_t now) const {
    uint32_t e = elapsed(now);
    if (e >= TOTAL_MS) return false;
    return inWindow(e % SWING_MS) && !((landed >> (e / SWING_MS)) & 1u);
  }
  // A tap captured at `at`. True when it lands a punch.
  bool tap(uint32_t at) {
    if (!open(at)) return false;
    landed |= (uint16_t)(1u << (elapsed(at) / SWING_MS));
    punches++;
    lastPunchAt = at;
    return true;
  }
  // How far in the bag is at `now`: 0 all the way out (each swing's start and end) to 1
  // at the pet (CONTACT_MS), along a cosine like a pendulum's. Out once it is over.
  float reach(uint32_t now) const {
    uint32_t e = elapsed(now);
    if (e >= TOTAL_MS) return 0.0f;
    float ph = (float)(e % SWING_MS) / (float)SWING_MS;
    return 0.5f - 0.5f * cosf(ph * 6.28318531f);
  }
};

// The stamina hold. While a finger rests on the screen the pet does reps: each REP_MS of
// holding is one rep, up to EX_COUNT. Lifting the finger pauses the rep where it is; it
// never resets or fails it. With no finger for IDLE_END_MS the session ends by itself
// and pays for the reps done, because a training screen never turns the screen off
// (autoScreenOff, power.h). Which touch counts as holding is the board's business: on
// the 1.75C one that didn't start on the exit chip (TrainingTouch::holding), on the
// original 1.75 one that didn't start on the top strip.
struct StaminaHold {
  static constexpr uint32_t REP_MS = 1500;
  static constexpr uint32_t STEP_MAX_MS = 250;     // a stalled frame can't skip a rep
  static constexpr uint32_t IDLE_END_MS = 15000;

  uint8_t reps = 0;
  uint32_t into = 0;      // ms held into the rep under way
  uint32_t last = 0;      // the previous step
  uint32_t lastHold = 0;  // the last step a finger was on (or the start)
  bool held = false;      // a finger was on at the previous step

  void begin(uint32_t now) {
    reps = 0;
    into = 0;
    last = lastHold = now;
    held = false;
  }
  // One step, every frame: `holding` says whether a finger is on now. Only the time
  // between two steps that both saw the finger counts, at most STEP_MAX_MS of it, so a
  // touch the board misses between frames never counts as held. True when a rep is done.
  bool step(uint32_t now, bool holding) {
    uint32_t dt = now - last;
    last = now;
    bool rep = false;
    if (holding && held && reps < EX_COUNT) {
      into += dt < STEP_MAX_MS ? dt : STEP_MAX_MS;
      if (into >= REP_MS) {
        reps++;
        into = reps < EX_COUNT ? into - REP_MS : 0;
        rep = true;
      }
    }
    held = holding;
    if (holding) lastHold = now;
    return rep;
  }
  bool done() const { return reps >= EX_COUNT; }
  // No finger for IDLE_END_MS (counted from the start until the first touch). A step
  // that sees the finger moves lastHold up to it, so a hold is never idle.
  bool idleOver(uint32_t now) const { return now - lastHold >= IDLE_END_MS; }
  bool over(uint32_t now) const { return done() || idleOver(now); }
  // The rep under way, 0 to 1 (the pet's lift and the bar)
  float progress() const { return (float)into / (float)REP_MS; }
};
