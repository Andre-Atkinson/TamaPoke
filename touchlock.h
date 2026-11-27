#pragma once
#include <stdint.h>

// Automatic touch lock. Not the triple-press pocket lock: that one stays
// until another triple press. This one starts when the screen turns off and
// a short PWR press both wakes the screen and clears it.
// TOUCH_LOCK_NEVER disables it. 0 locks on the same moment the screen goes off.

constexpr uint32_t TOUCH_LOCK_NEVER = 0xFFFFFFFFu;

struct TouchLock {
  uint32_t delayMs = 10000;
  bool active = false;
  bool tracking = false;
  uint32_t offAt = 0;

  // +1 newly locked, -1 newly unlocked, 0 no change.
  int update(bool screenOff, uint32_t now) {
    if (!screenOff || delayMs == TOUCH_LOCK_NEVER) {
      int edge = active ? -1 : 0;
      active = false;
      tracking = false;
      return edge;
    }
    if (!tracking) {
      tracking = true;
      offAt = now;
      if (delayMs == 0) {
        active = true;
        return 1;
      }
      return 0;
    }
    if (!active && (uint32_t)(now - offAt) >= delayMs) {
      active = true;
      return 1;
    }
    return 0;
  }
};

inline uint32_t touchLockNext(uint32_t ms) {
  if (ms == 10000) return 30000;
  if (ms == 30000) return 60000;
  if (ms == 60000) return TOUCH_LOCK_NEVER;
  if (ms == TOUCH_LOCK_NEVER) return 0;
  return 10000;
}

inline uint8_t touchLockSel(uint32_t ms) {
  if (ms == 30000) return 1;
  if (ms == 60000) return 2;
  if (ms == 0) return 3;
  if (ms == TOUCH_LOCK_NEVER) return 4;
  return 0;
}

inline uint32_t touchLockFromSel(uint8_t sel) {
  if (sel == 1) return 30000;
  if (sel == 2) return 60000;
  if (sel == 3) return 0;
  if (sel == 4) return TOUCH_LOCK_NEVER;
  return 10000;
}

// TP_INT may be read, armed as a light-sleep wake source or have its ISR attached only
// while the CST9217 is awake and initialised. touch.sleep() (auto lock only) makes the pin
// open drain (input buffer off, reads LOW). The pocket lock does not sleep the chip, it
// detaches the ISR; it still disables the pin because the chip may be asleep from the auto
// lock underneath it and nothing re-arms TP_INT until the pocket unlock. A failed
// touch.begin() (touchReady false) leaves the pin without a known level, so it is disabled
// until a retry succeeds.
inline bool touchWakeEnabled(bool pocketLocked, bool autoActive, bool touchReady) {
  return !pocketLocked && !autoActive && touchReady;
}

// The screen as the player sees it: dark when it timed out (screenOff), except while an
// attention call's window (#9, attnShowUntil) lights it to show the pet asking for help.
// The auto touch lock reads this rather than screenOff, so it lets go when the window opens
// (the sketch brings the chip up, as for any unlock) and a touch on the lit pet works. A
// window that ends with no touch leaves the screen dark again, and the lock starts over from
// then (delayMs). It used to read screenOff, so the lit pet ignored every touch until PWR.
// A screen turned off with PWR, or pocket-locked, gets no lit window (attnDecide), so
// neither changes.
inline bool touchScreenDark(bool screenOff, bool callShowing) {
  return screenOff && !callShowing;
}

// A touch that starts on a dimmed or dark screen only wakes it (the sketch swallows that
// gesture), since she can't see what she is touching. A call's window shows the pet at full
// brightness (powerCtx() reads it as "just touched", so dimStage is 0): a touch there does
// what it touches, as on any lit screen, so the call can be answered with one tap.
inline bool touchOnlyWakes(uint8_t dimStage, bool screenOff, bool callShowing) {
  return dimStage > 0 || touchScreenDark(screenOff, callShowing);
}
