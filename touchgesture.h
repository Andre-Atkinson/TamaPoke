#pragma once
#include <stdint.h>

// Main-screen touch resolution and the pet gestures (#21). Pure logic with no Arduino
// calls: test/test_touchgesture.cpp drives it with synthetic touch traces.
//
// handleTouch() in TamaPoke.ino feeds one contact at a time: down() when the finger lands,
// held() on every 20 ms poll while it stays down, lift() when it comes up. What they return
// is what the contact does. The swipe, tap and long-press thresholds are the ones
// handleTouch() always used; the gestures on the pet are new:
//
//   stroke       back and forth over the pet (two turns along either axis). One pat, and
//                one more per STROKE_EVERY_MS while the finger keeps stroking.
//   double tap   the second quick tap on the pet: it hops. One pat, like any tap.
//   tickle       the sixth quick tap in a row: it giggles. One pat per tap, like any tap.
//   huff         the twelfth quick tap on: a gentle huff. No pat and no stat change.
//   hold + drag  rest DRAG_REST_MS on the pet, then slide sideways: it walks after the
//                finger. One pat when the drag starts, none while it goes on.
//
// "Quick" means within QUICK_TAP_MS of the previous tap on the pet, so six quick taps take
// about two seconds. Every pat is one Pet::caress() (+5 joy, +1 bond under the shared daily
// bond cap), so N gestures never earn more than N taps.
//
// A single tap still resolves on lift, as before, and is a pat at once: a double tap upgrades
// the second tap's reaction instead of holding back the first. Nothing here waits.
//
// Conflicts. A stroke or a drag claims its contact: from then on it never resolves as a
// swipe, a tap or the 3 s long-press. Before that, a contact behaves exactly as it always
// did. A drag needs a rest first, so a swipe (which moves at once) never becomes one, and it
// starts only beyond TAP_SLOP from where the finger landed, so it cannot take a tap or the
// long-press, whose finger stays inside LONG_PRESS_SLOP < TAP_SLOP. A long-press that fired
// first claims the contact too: no stroke or drag after it. Gestures need down(..., live)
// true: the caller passes false for a touch that only wakes the screen, off the pet's body,
// or with gestures off (petGesturesLive).

enum TouchAct : uint8_t {
  TOUCH_NONE = 0,
  TOUCH_TAP,          // lift: a tap at (x0, y0) -> onTap()
  TOUCH_SWIPE_RIGHT,  // lift: onSwipe(+1)
  TOUCH_SWIPE_LEFT,   // lift: onSwipe(-1)
  TOUCH_SWIPE_DOWN,   // lift: onSwipeV(+1)
  TOUCH_SWIPE_UP,     // lift: onSwipeV(-1)
  TOUCH_LONG_PRESS,   // held: the "release?" dialog
  TOUCH_STROKE,       // held: a stroke on the pet (one pat)
  TOUCH_DRAG_START,   // held: a hold-then-drag began at dragX (one pat)
  TOUCH_DRAG_MOVE,    // held: the dragging finger moved to dragX (no pat)
};

// What a tap on the pet's body is (TouchGesture::petTap).
enum PetTap : uint8_t {
  PET_TAP_PAT = 0,  // a plain pat: the 1st tap, or the 3rd to 5th quick one
  PET_TAP_DOUBLE,   // the 2nd quick tap: it hops
  PET_TAP_TICKLE,   // the 6th to 11th: it giggles
  PET_TAP_HUFF,     // the 12th on: a gentle huff, no pat
};

inline bool petTapEarnsPat(PetTap t) { return t != PET_TAP_HUFF; }

// When the pet gestures are live. Off for an egg, during a ceremony, evolution, a meal or a
// bath, with the pet asleep, while choosing a starter or an egg, on any screen but the main
// one, with a dialog or the feed menu open, and for a touch that only wakes the screen.
// Then a tap on the pet is the plain pat it always was.
struct PetGestureGate {
  bool dark = false;         // the touch only wakes a dimmed or off screen
  bool egg = false;
  bool ceremony = false;     // farewell or runaway
  bool sleeping = false;
  bool evolving = false;
  bool eating = false;
  bool bathing = false;
  bool choosing = false;     // starter or egg choice
  bool otherScreen = false;  // gallery, card, keyboard, clock, minigame, strength training
  bool dialog = false;       // evolve/farewell choice, "release?", feed menu
};

inline bool petGesturesLive(const PetGestureGate &g) {
  return !(g.dark || g.egg || g.ceremony || g.sleeping || g.evolving || g.eating || g.bathing ||
           g.choosing || g.otherScreen || g.dialog);
}

// Turns of one coordinate: a turn is the finger coming back STROKE_LEG px from the furthest
// point of a leg that itself ran at least STROKE_LEG px.
struct TouchAxis {
  int16_t ext = 0;  // the start point, then the furthest point of the current leg
  int8_t dir = 0;   // 0 until the first leg has run STROKE_LEG px, then +1 / -1
  uint8_t turns = 0;

  void reset(int16_t v) { ext = v; dir = 0; turns = 0; }
  void feed(int16_t v, int leg);
};

struct TouchGesture {
  // existing thresholds (handleTouch() used these before the gestures)
  static constexpr int SWIPE_MIN = 80;        // px along the swipe
  static constexpr int SWIPE_CROSS = 70;      // px across it, at most
  static constexpr uint32_t SWIPE_MS = 800;   // a swipe is quicker than this
  static constexpr int TAP_SLOP = 40;         // a tap ends this close to where it began
  static constexpr uint32_t TAP_MS = 1500;    // and is shorter than this
  static constexpr uint32_t LONG_PRESS_MS = 3000;
  static constexpr int LONG_PRESS_SLOP = 30;  // the finger stays this close (< TAP_SLOP)

  // the pet gestures
  static constexpr uint32_t QUICK_TAP_MS = 500;   // lift to lift: the next tap continues a run
  static constexpr uint8_t TICKLE_TAPS = 6;
  static constexpr uint8_t HUFF_TAPS = 12;
  static constexpr int STROKE_LEG = 20;           // px each way, so a resting finger never strokes
  static constexpr uint8_t STROKE_TURNS = 2;      // back and forth twice
  static constexpr uint32_t STROKE_EVERY_MS = 1200;  // at most one stroke pat this often
  static constexpr uint32_t DRAG_REST_MS = 250;   // the finger rests this long first
  static constexpr int DRAG_REST_SLOP = 20;       // moving more during the rest rules a drag out
  static constexpr int DRAG_START = TAP_SLOP;     // sideways px from the landing point to start
  static constexpr uint32_t DRAG_STALE_MS = 250;  // no sample for this long: not dragging

  enum Claim : uint8_t { CLAIM_NONE = 0, CLAIM_LONG, CLAIM_STROKE, CLAIM_DRAG };

  // the current contact
  bool active = false;  // down() .. lift()
  bool live = false;    // pet gestures possible on this contact
  uint8_t claim = CLAIM_NONE;
  bool restBroken = false;
  int16_t x0 = 0, y0 = 0;  // where the finger landed (taps resolve here)
  int16_t xl = 0, yl = 0;  // the last sample
  int16_t dragX = 0;       // where a drag's finger is
  uint32_t t0 = 0, lastAt = 0;
  TouchAxis ax, ay;

  // across contacts
  bool stroked = false;     // strokeAt is valid
  uint32_t strokeAt = 0;    // the last stroke pat
  uint8_t run = 0;          // quick taps on the pet in a row
  uint32_t tapAt = 0;       // the last tap on the pet
  bool petTapped = false;   // this contact ended as a tap on the pet

  void down(uint32_t now, int16_t x, int16_t y, bool petLive) {
    if (!petTapped) run = 0;  // anything but a tap on the pet breaks a run of quick taps
    petTapped = false;
    active = true;
    live = petLive;
    claim = CLAIM_NONE;
    restBroken = false;
    x0 = xl = dragX = x;
    y0 = yl = y;
    t0 = lastAt = now;
    ax.reset(x);
    ay.reset(y);
  }

  // longPressOk: the long-press may open the "release?" dialog (the caller's screen state
  // and the landing point in the pet zone), as handleTouch() always checked.
  TouchAct held(uint32_t now, int16_t x, int16_t y, bool longPressOk);

  // blocked: the caller's own reasons to resolve nothing (the touch woke the screen, or a
  // minigame already took it).
  TouchAct lift(uint32_t now, bool blocked);

  // A lift that onTap() resolved as a tap on the pet's body, with the gestures live.
  PetTap petTap(uint32_t now);

  bool claimed() const { return claim != CLAIM_NONE; }
  bool dragging(uint32_t now) const {
    return active && claim == CLAIM_DRAG && now - lastAt < DRAG_STALE_MS;
  }

  static int iabs(int v) { return v < 0 ? -v : v; }
};

// The separations the conflict rules above rely on.
static_assert(TouchGesture::DRAG_START > TouchGesture::DRAG_REST_SLOP,
              "a drag must not be able to start during its own rest");
static_assert(TouchGesture::LONG_PRESS_SLOP < TouchGesture::DRAG_START,
              "a still finger waiting for the long-press must not start a drag");
static_assert(TouchGesture::DRAG_START >= TouchGesture::TAP_SLOP,
              "a drag must not start inside a tap");

inline void TouchAxis::feed(int16_t v, int leg) {
  if (dir == 0) {  // no leg yet: ext is where the finger landed
    if (v - ext >= leg) { dir = 1; ext = v; }
    else if (ext - v >= leg) { dir = -1; ext = v; }
  } else if (dir > 0) {
    if (v > ext) ext = v;
    else if (ext - v >= leg) { dir = -1; ext = v; if (turns < 255) turns++; }
  } else {
    if (v < ext) ext = v;
    else if (v - ext >= leg) { dir = 1; ext = v; if (turns < 255) turns++; }
  }
}

inline TouchAct TouchGesture::held(uint32_t now, int16_t x, int16_t y, bool longPressOk) {
  if (!active) return TOUCH_NONE;
  xl = x;
  yl = y;
  lastAt = now;
  int dx = x - x0, dy = y - y0;
  uint32_t dt = now - t0;
  if (claim == CLAIM_LONG) return TOUCH_NONE;
  if (claim == CLAIM_DRAG) {
    if (x == dragX) return TOUCH_NONE;
    dragX = x;
    return TOUCH_DRAG_MOVE;
  }
  if (live) {
    if (dt < DRAG_REST_MS && (iabs(dx) > DRAG_REST_SLOP || iabs(dy) > DRAG_REST_SLOP)) restBroken = true;
    ax.feed(x, STROKE_LEG);
    ay.feed(y, STROKE_LEG);
    if (ax.turns >= STROKE_TURNS || ay.turns >= STROKE_TURNS) {
      claim = CLAIM_STROKE;  // a stroke now, even while it is too soon for another pat
      if (!stroked || now - strokeAt >= STROKE_EVERY_MS) {
        stroked = true;
        strokeAt = now;
        ax.turns = ay.turns = 0;  // the next pat needs two more turns
        return TOUCH_STROKE;
      }
    }
    // No dt >= DRAG_REST_MS test needed: reaching DRAG_START before then breaks the rest.
    if (claim == CLAIM_NONE && !restBroken && iabs(dx) >= DRAG_START && iabs(dx) > iabs(dy)) {
      claim = CLAIM_DRAG;
      dragX = x;
      return TOUCH_DRAG_START;
    }
  }
  if (claim == CLAIM_NONE && longPressOk && dt > LONG_PRESS_MS && iabs(dx) < LONG_PRESS_SLOP &&
      iabs(dy) < LONG_PRESS_SLOP) {
    claim = CLAIM_LONG;
    return TOUCH_LONG_PRESS;
  }
  return TOUCH_NONE;
}

inline TouchAct TouchGesture::lift(uint32_t now, bool blocked) {
  if (!active) return TOUCH_NONE;
  active = false;
  if (blocked || claim != CLAIM_NONE) return TOUCH_NONE;
  int dx = xl - x0, dy = yl - y0;
  uint32_t dt = now - t0;
  if (iabs(dx) > SWIPE_MIN && iabs(dy) < SWIPE_CROSS && dt < SWIPE_MS)
    return dx > 0 ? TOUCH_SWIPE_RIGHT : TOUCH_SWIPE_LEFT;
  if (iabs(dy) > SWIPE_MIN && iabs(dx) < SWIPE_CROSS && dt < SWIPE_MS)
    return dy > 0 ? TOUCH_SWIPE_DOWN : TOUCH_SWIPE_UP;
  if (dt < TAP_MS && iabs(dx) < TAP_SLOP && iabs(dy) < TAP_SLOP) return TOUCH_TAP;
  return TOUCH_NONE;
}

inline PetTap TouchGesture::petTap(uint32_t now) {
  if (run == 0 || now - tapAt > QUICK_TAP_MS) run = 1;
  else if (run < 255) run++;
  tapAt = now;
  petTapped = true;
  if (run >= HUFF_TAPS) return PET_TAP_HUFF;
  if (run >= TICKLE_TAPS) return PET_TAP_TICKLE;
  if (run == 2) return PET_TAP_DOUBLE;
  return PET_TAP_PAT;
}
