#pragma once
#include <stdint.h>
#include "clockprompt.h"
#include "pet.h"
#include "rtcbat.h"

// Setting the wall clock: serial TIME (tools/board.py clock, which tools/update_175c.sh
// runs) and the clock screen's OK (#45). The clock is set and read back first. The pet's
// last-seen time (NVS key `seen`, where the next boot's offline time starts) and the
// bedtime clock follow only when the clock really took the value (clockSetTook): a value
// it ignores (outside 2025-2099, or any value on a 1.75 whose PCF85063 is missing)
// leaves the save untouched, with no NVS write. Saving it anyway made the next boot count
// the gap from that value to the real time as up to 14 days offline. `bedClockFollow` is bedClockSet() in the sketch (a parameter so the host
// tests can see whether it ran). Returns whether the clock took the value.
inline bool setWallClock(Pet &pet, uint32_t e, void (*bedClockFollow)(uint32_t)) {
  rtcSetEpoch(e);
  if (!clockSetTook(e, rtcEpoch())) return false;
  pet.setClock(e);
  bedClockFollow(e);
  return true;
}
