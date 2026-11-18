// Walks (#117): what the pet's step counter (steps.h) turns into. Pure bookkeeping (no
// hardware, no clock reads), tested on the PC (test/test_walks.cpp). Pet::walkMinute()
// (pet.cpp) feeds it once a minute and pet.h's Pet::walk holds it, saved with the pet.
//
// The owner's decisions (the design comments on #117, 2026-11-18):
//  - Steps count only awake and off the charger: counting is "open" off USB and outside the
//    bedtime window (an unknown or stale clock has no bedtime, so open all day). While it is
//    closed the steps are discarded, not saved up.
//  - 1 walk treat per WALK_STEPS_PER_TREAT counted steps, at most WALK_TREATS_DAY_MAX earned a
//    day; the bank holds WALK_BANK_MAX and keeps overnight. The pet spends them off the
//    charger (Pet::tick): a need under SELFCARE_BELOW goes up to WALK_TREAT_LEVEL (80).
//  - Stamina training: +1 per WALK_STEPS_PER_STAMINA counted steps, at most
//    WALK_STAMINA_DAY_MAX a day.
//  - The walking ribbon (MED_WALK): WALK_RIBBON_STEPS counted steps in one day.
//  - A speech bubble after a good walk (WalkBubble).
#pragma once
#include <stdint.h>

#define WALK_STEPS_PER_TREAT 250
#define WALK_TREATS_DAY_MAX 40
#define WALK_BANK_MAX 40
#define WALK_STEPS_PER_STAMINA 1000
#define WALK_STAMINA_DAY_MAX 5
#define WALK_RIBBON_STEPS 5000
// The most one minute may add: a brisk run is about 180 steps a minute, so 300 is out of
// reach of a body, and a bad read of the chip (a torn read, a restart that read back a
// stale count) cannot add more than this.
#define WALK_MINUTE_MAX 300
// What a day's steps can come to: a full 1,440 minutes at the cap. NVS holds nothing higher.
#define WALK_DAY_STEPS_MAX ((uint32_t)WALK_MINUTE_MAX * 1440UL)
// The QMI8658's step counter is 24 bits (registers 0x5A to 0x5C)
#define WALK_CHIP_MASK 0xFFFFFFUL
// The speech bubble: a walk of WALK_SAY_STEPS or more counted steps, then WALK_SAY_QUIET_MIN
// minutes with no new ones
#define WALK_SAY_STEPS 1000
#define WALK_SAY_QUIET_MIN 10

// Steps the chip counted since the last look. Its count starts over when the engine restarts
// (a boot, a reset), and wraps at 24 bits: a count below the last one is read as a fresh
// start, so the steps are the count itself. (After a real wrap that undercounts by a few
// steps once, which is harmless: 16.7 million steps is a lifetime.)
inline uint32_t walkChipDelta(uint32_t last, uint32_t now) {
  last &= WALK_CHIP_MASK;
  now &= WALK_CHIP_MASK;
  return now >= last ? now - last : now;
}

// Is `today` (the local day, epoch / 86400, from a clock that is set and trusted; 0 when it
// is not) a new day for steps stored under `stored`? A stale or unknown clock never rolls,
// and neither does a clock a little behind (set back by a day or two, as registerCare keeps
// the streak then); a bigger step back means `stored` itself is wrong (a far-future clock
// that was put right), so it rolls. `stored` 0 (no day yet) is adopted, not rolled.
inline bool walkDayRolls(uint32_t stored, uint32_t today) {
  if (!today || !stored || today == stored) return false;
  return !(today < stored && stored - today <= 2);
}

// What one minute did, for the pet to apply to what is not the ledger's own
struct WalkGain {
  uint16_t steps = 0;    // counted now
  uint8_t treats = 0;    // added to the bank (earned: a full bank loses the excess)
  uint8_t stamina = 0;   // stamina training points earned
  bool rolled = false;   // the day rolled over
};

// The speech bubble's rule: a walk is a run of minutes with steps in them; it ends when
// WALK_SAY_QUIET_MIN minutes in a row have none. If it came to WALK_SAY_STEPS or more by
// then, the pet has something to say (due() is true until said()), and either way the next
// walk starts from nothing. RAM only: a restart drops it.
class WalkBubble {
 public:
  void minute(uint32_t counted) {
    if (counted) {
      if (run_ < 0xFFFFFFu) run_ += counted;
      quiet_ = 0;
      return;
    }
    if (!run_) return;  // not on a walk
    if (++quiet_ < WALK_SAY_QUIET_MIN) return;
    if (run_ >= WALK_SAY_STEPS) due_ = true;
    run_ = 0;
    quiet_ = 0;
  }
  bool due() const { return due_; }
  void said() { due_ = false; }

 private:
  uint32_t run_ = 0;
  uint8_t quiet_ = 0;
  bool due_ = false;
};

struct WalkLedger {
  // ---- saved with the pet (NVS wsteps, wday, wbank, wearn, wsta) ----
  uint32_t steps = 0;    // counted today
  uint32_t day = 0;      // the local day `steps` belong to; 0 until a trusted clock gives one
  uint8_t bank = 0;      // walk treats banked, 0 to WALK_BANK_MAX
  uint8_t earned = 0;    // treats earned today (bank or not), 0 to WALK_TREATS_DAY_MAX
  uint8_t stamina = 0;   // stamina points earned today, 0 to WALK_STAMINA_DAY_MAX
  // ---- RAM ----
  WalkBubble say;

  // The values as NVS gave them, checked: nothing out of range survives, and no value
  // earns more than the steps allow (a day's earned treats and stamina points never exceed
  // what steps/250 and steps/1000 give, so a damaged count can't mint treats). `dayLo` and
  // `dayHi` are the clock's range as days: a day outside reads as none.
  void load(uint32_t s, uint32_t d, uint8_t b, uint8_t e, uint8_t st, uint32_t dayLo, uint32_t dayHi) {
    steps = s <= WALK_DAY_STEPS_MAX ? s : 0;
    day = (d >= dayLo && d < dayHi) ? d : 0;
    bank = b <= WALK_BANK_MAX ? b : 0;
    uint32_t maxEarned = steps / WALK_STEPS_PER_TREAT;
    if (maxEarned > WALK_TREATS_DAY_MAX) maxEarned = WALK_TREATS_DAY_MAX;
    earned = e <= maxEarned ? e : (uint8_t)maxEarned;
    uint32_t maxSta = steps / WALK_STEPS_PER_STAMINA;
    if (maxSta > WALK_STAMINA_DAY_MAX) maxSta = WALK_STAMINA_DAY_MAX;
    stamina = st <= maxSta ? st : (uint8_t)maxSta;
  }

  // Where the chip's count stands now, with no steps added (the boot, when the engine has
  // just started at 0)
  void start(uint32_t chip) {
    chip_ = chip & WALK_CHIP_MASK;
    have_ = true;
  }

  // One minute: the chip's count now, whether counting is open, and the local day from a
  // trusted clock (0 when there is none). The steps since the last look are counted only
  // while open; a minute's worth is capped at WALK_MINUTE_MAX.
  WalkGain minute(uint32_t chipNow, bool open, uint32_t today) {
    WalkGain g;
    uint32_t delta = have_ ? walkChipDelta(chip_, chipNow) : 0;  // the first look only sets the baseline
    chip_ = chipNow & WALK_CHIP_MASK;
    have_ = true;
    if (delta > WALK_MINUTE_MAX) delta = WALK_MINUTE_MAX;

    if (walkDayRolls(day, today)) {
      steps = 0;
      earned = 0;
      stamina = 0;
      g.rolled = true;
    }
    if (today && (!day || g.rolled)) day = today;

    uint32_t add = open ? delta : 0;
    if (add) {
      steps = steps + add > WALK_DAY_STEPS_MAX ? WALK_DAY_STEPS_MAX : steps + add;
      g.steps = (uint16_t)add;
      uint32_t treats = steps / WALK_STEPS_PER_TREAT;
      if (treats > WALK_TREATS_DAY_MAX) treats = WALK_TREATS_DAY_MAX;
      if (treats > earned) {
        uint32_t got = treats - earned;
        earned = (uint8_t)treats;
        uint32_t room = WALK_BANK_MAX - bank;
        g.treats = (uint8_t)(got < room ? got : room);
        bank = (uint8_t)(bank + g.treats);
      }
      uint32_t sta = steps / WALK_STEPS_PER_STAMINA;
      if (sta > WALK_STAMINA_DAY_MAX) sta = WALK_STAMINA_DAY_MAX;
      if (sta > stamina) {
        g.stamina = (uint8_t)(sta - stamina);
        stamina = (uint8_t)sta;
      }
    }
    say.minute(add);
    return g;
  }

  // The pet spends one treat; false when there is none
  bool spend() {
    if (!bank) return false;
    bank--;
    return true;
  }

  bool ribbon() const { return steps >= WALK_RIBBON_STEPS; }

 private:
  uint32_t chip_ = 0;
  bool have_ = false;
};
