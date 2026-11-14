#pragma once
#include <stdint.h>

// Asking for the time after the clock was reseeded (#10). Pure logic (no hardware, no
// clock reads) so it can be tested on the PC (test/test_clockprompt.cpp).
//
// With no time at boot (the 1.75C after the AXP2101 cut power; the original 1.75 when
// its PCF85063 lost backup power) setup() seeds the clock with the last saved time,
// which is wrong by however long the board was off. Until somebody sets it the clock is
// "stale". The flag lives in BedEdge (bedtime.h), which pauses bedtime while it is set;
// anything else that must not trust the wall clock (holidays, quiet hours) reads
// bedEdge.stale() too. While it is stale:
//  - the clock screen opens by itself once per boot, as soon as nothing more important
//    is on screen (ClockPrompt::due);
//  - a round clock badge on the pet and egg screens opens it with a tap
//    (ClockPrompt::asking, clockBadgeHit).
// Closing the clock screen without setting the time only skips the automatic opening:
// the badge stays. Setting the clock by any route (the clock screen's OK, serial TIME,
// which tools/update_175c.sh sends) clears the flag, and with it both.

// ---------------------------------------------------------------- across soft resets
// The flag is also kept in ESP32 RTC memory (RTC_NOINIT_ATTR in the sketch). A soft
// reset (crash, watchdog, ESP.restart) keeps that memory and also keeps the soft clock
// of the 1.75C running, still wrong; only power loss clears them, and then the clock
// has no time and is reseeded anyway. RTC memory holds garbage after power-on, so the
// mark is a magic word plus its complement: garbage reads as "not stale".
// Keep it a plain struct with no initializers: a constructor would run at every boot
// and wipe the copy the reset was meant to keep.
struct ClockStaleMark {
  uint32_t magic, check;
};
const uint32_t CLOCK_STALE_MAGIC = 0x434C4B3FUL;  // "CLK?"

inline void clockMarkStale(ClockStaleMark &m, bool stale) {
  m.magic = stale ? CLOCK_STALE_MAGIC : 0;
  m.check = stale ? (uint32_t)~CLOCK_STALE_MAGIC : 0;
}

inline bool clockMarkedStale(const ClockStaleMark &m) {
  return m.magic == CLOCK_STALE_MAGIC && m.check == (uint32_t)~CLOCK_STALE_MAGIC;
}

// Is the clock stale at boot? `reseeded`: it had no time and setup() seeded it.
inline bool clockStaleAtBoot(bool reseeded, const ClockStaleMark &m) {
  return reseeded || clockMarkedStale(m);
}

// Did setting the clock take? `readBack` is the clock read right after the set. The
// 1.75C soft clock ignores values before 2025, and a missing PCF85063 keeps reading 0:
// then the clock is still what it was and nothing counts as set.
inline bool clockSetTook(uint32_t requested, uint32_t readBack) {
  if (!readBack) return false;
  uint32_t d = readBack > requested ? readBack - requested : requested - readBack;
  return d <= 5;
}

// ---------------------------------------------------------------- automatic opening
// What is up when the clock screen wants to open by itself. Anything true holds it
// back until it ends; then it opens.
struct ClockPromptHold {
  bool choosing = false;     // starter or egg choice
  bool ceremony = false;     // farewell, runaway or goodbye
  bool evolving = false;     // evolution animation
  bool dialog = false;       // release, food menu, evolve or farewell choice
  bool otherScreen = false;  // gallery, game, sack, keyboard, card or the clock screen
};

inline bool clockPromptHeld(const ClockPromptHold &h) {
  return h.choosing || h.ceremony || h.evolving || h.dialog || h.otherScreen;
}

class ClockPrompt {
 public:
  // setup(), once the clock is seeded. `settable`: the clock reads a time, so setting
  // it can work (a missing PCF85063 keeps reading 0: no prompt and no badge at all).
  // One automatic opening is owed; due() only gives it while the clock is stale.
  void boot(bool settable) { settable_ = owed_ = settable; }

  // Each loop pass: true once, when the clock screen should open by itself. The clock
  // is only ever marked stale at boot, so once it is not, nothing is owed any more.
  bool due(bool stale, const ClockPromptHold &h) {
    if (!stale) owed_ = false;  // never stale, or set by another route first (TIME)
    if (!owed_ || clockPromptHeld(h)) return false;
    owed_ = false;
    return true;
  }

  // The clock screen opened (by itself, from the badge or with a swipe down): closing
  // it without setting the time does not open it again by itself.
  void opened() { owed_ = false; }

  // The clock wants setting and can be set: the badge shows and the clock screen asks.
  bool asking(bool stale) const { return stale && settable_; }

 private:
  bool owed_ = false, settable_ = false;
};

// ---------------------------------------------------------------- the badge
// Round badge in the sky, left of the pet. The hit circle is larger than the drawing so
// a finger finds it, and stays inside the round panel, left of the pet zone (x > 110,
// inPetZone in TamaPoke.ino) and above the evolve and farewell buttons (y >= 172). The
// drawing stays clear of the status line (y < 106, centred) and of the celebration
// banner (y >= 150), which is drawn over it anyway.
#define CLOCK_BADGE_X 76
#define CLOCK_BADGE_Y 125
#define CLOCK_BADGE_R 24    // drawn radius
#define CLOCK_BADGE_HIT 34  // touch radius (68 px across)

inline bool clockBadgeHit(int x, int y) {
  int dx = x - CLOCK_BADGE_X, dy = y - CLOCK_BADGE_Y;
  return dx * dx + dy * dy <= CLOCK_BADGE_HIT * CLOCK_BADGE_HIT;
}
