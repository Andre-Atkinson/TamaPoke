// The standby clock (#60): plugged in and left alone, the screen shows a dim clock with the
// date and the weather on black instead of going dark. Pure logic (no hardware), host-tested
// in test/test_standby.cpp; the sketch keeps the flag (TamaPoke.ino) and scene.cpp draws it.
//
// The owner's decision (#60): it takes the place of screen-off while on USB and the pet is
// awake. When the screen would turn off (the screen-off setting), or at 90 s when that is
// Never, the clock shows instead. Any touch, BOOT or PWR (which then turns the screen fully
// off) ends it; an attention call shows the pet for its window and the clock comes back after;
// self-care while charging (#24) carries on (the clock doesn't count as a touch). Nothing
// changes on battery, while the pet sleeps (the screen goes dark as before), or while the
// clock is not trusted (after a power cut, until the time is set).
#pragma once
#include <stdint.h>
#include "power.h"  // POWER_DIM1_MS

struct StandbyIn {
  bool usb;         // on USB power, with the PMU answering (as self-care reads it, #24)
  bool clockKnown;  // the clock is set and trusted (not reseeded since a power cut)
  bool petAwake;    // a pet or an egg not asleep, and no ceremony, evolution or starter/egg choice
  bool screenOn;    // not turned off (PWR, the timeout) and not pocket-locked
  bool gameOpen;    // a minigame or the training bag: they never time out
  uint32_t idleMs;  // since the last touch
  uint32_t offMs;   // the screen-off setting (0 = Never)
};

// when the clock comes up: at the screen-off time, or at the first dim (90 s) with Never
inline uint32_t standbyAfterMs(uint32_t offMs) { return offMs ? offMs : POWER_DIM1_MS; }

// what the clock needs to show (and keep showing): a touch ends it on its own (wakeUI())
inline bool standbyAllowed(const StandbyIn &s) {
  return s.usb && s.clockKnown && s.petAwake && s.screenOn && !s.gameOpen;
}
inline bool standbyStarts(const StandbyIn &s) { return standbyAllowed(s) && s.idleMs >= standbyAfterMs(s.offMs); }

// Against AMOLED burn-in the clock steps to a new spot each minute, at most 4 px from the
// centre, through 8 spots.
#define STANDBY_SHIFTS 8
#define STANDBY_SHIFT_MAX 4
inline void standbyShift(uint32_t minute, int &dx, int &dy) {
  static const int8_t AT[STANDBY_SHIFTS][2] = { { 0, 0 }, { 3, 2 }, { -2, 4 }, { 4, -3 },
                                                { -4, -2 }, { 2, -4 }, { -3, 3 }, { 1, -1 } };
  dx = AT[minute % STANDBY_SHIFTS][0];
  dy = AT[minute % STANDBY_SHIFTS][1];
}
