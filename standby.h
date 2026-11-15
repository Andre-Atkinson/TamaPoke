// The standby clock (#60): plugged in and left alone, the screen shows a dim clock with the
// date and the weather on black instead of going dark. Pure logic (no hardware), host-tested
// in test/test_standby.cpp; the sketch keeps the flag (TamaPoke.ino) and scene.cpp draws it.
//
// The owner's decisions (#60, #74): it takes the place of screen-off while on USB and the pet
// is awake. When the screen would turn off (the screen-off setting), or at 90 s when that is
// Never, the clock shows instead. A screen that already timed out lights up with the clock
// by itself as soon as it may show (the pet wakes on the charger in the morning, or a board
// whose screen timed out on battery is plugged in); only a screen turned off with PWR, or
// pocket-locked, stays dark. Any touch, BOOT or PWR (which then turns the screen fully off)
// ends it; an attention call shows the pet for its window and the clock comes back after;
// self-care while charging (#24) carries on (the clock doesn't count as a touch). Nothing
// changes on battery, while the pet sleeps (the screen goes dark as before), or while the
// clock is not trusted (after a power cut, until the time is set). The auto touch lock waits
// for a real screen-off: it never engages while the clock shows, so any touch still brings
// the pet back. When the clock comes up the other views are put away, as an attention call
// does when it lights a timed-out screen, so that touch finds the pet: settings (each choice
// is already saved), the card (its page), the gallery, and the keyboard and the clock screen,
// which drop a half-typed name or a time not yet confirmed.
#pragma once
#include <stdint.h>
#include "power.h"  // POWER_DIM1_MS

struct StandbyIn {
  bool usb;         // on USB power, with the PMU answering (as self-care reads it, #24)
  bool clockKnown;  // the clock is set and trusted (not reseeded since a power cut)
  bool petAwake;    // a pet or an egg not asleep, and no ceremony, evolution or starter/egg choice
  bool pwrOff;      // the screen was turned off with PWR: it stays dark (a timed-out one may light)
  bool locked;      // pocket-locked (the 1.75C's triple PWR)
  bool gameOpen;    // a minigame or the training bag: they never time out
  uint32_t idleMs;  // since the last touch
  uint32_t offMs;   // the screen-off setting (0 = Never)
};

// when the clock comes up: at the screen-off time, or at the first dim (90 s) with Never
inline uint32_t standbyAfterMs(uint32_t offMs) { return offMs ? offMs : POWER_DIM1_MS; }

// what the clock needs to show (and keep showing): a touch ends it on its own (wakeUI())
inline bool standbyAllowed(const StandbyIn &s) {
  return s.usb && s.clockKnown && s.petAwake && !s.pwrOff && !s.locked && !s.gameOpen;
}
inline bool standbyStarts(const StandbyIn &s) { return standbyAllowed(s) && s.idleMs >= standbyAfterMs(s.offMs); }

// Where its lines sit (drawStandby(), scene.cpp), before the burn-in step: the time at size
// 9 (72 px tall), the date at size 3, the weather now as a picture, the battery.
#define STANDBY_TIME_Y 128
#define STANDBY_DATE_Y 222
#define STANDBY_WX_Y 300
#define STANDBY_BAT_Y 364
#define STANDBY_DATE_MAX 24  // bytes for formatDate() (i18n.h): "s\240b, 15 nov" is 11

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
