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
// the gap from that value to the real time as up to 14 days offline. `bedClockFollow` is
// bedClockSet() in the sketch (a parameter so the host tests can see whether it ran).
// Returns whether the clock took the value.
inline bool setWallClock(Pet &pet, uint32_t e, void (*bedClockFollow)(uint32_t)) {
  rtcSetEpoch(e);
  if (!clockSetTook(e, rtcEpoch())) return false;
  pet.setClock(e);
  bedClockFollow(e);
  return true;
}

// The time in a serial TIME or RTCSET line (#65), the text after the command: seconds since
// 1970 and nothing else (one digit or more: no sign, no space, nothing after them), inside
// the clock's range (clockEpochValid). The value is refused as soon as it passes that range,
// so a number of any length cannot overflow. These lines used String::toInt(), which is
// atol() with the ESP32's 32-bit long: a digit too many clamped to 2147483647 (2038-01-19)
// and a minus sign wrapped round to a time in 2025-2099, both of which the clock took, and
// every real time from 2038-01-19 to 2099 clamped to 2038.
// Returns whether it is such a time; `e` is set only then.
inline bool clockEpochParse(const char *s, uint32_t &e) {
  uint64_t v = 0;  // "" stays 0: outside the range below
  for (; *s; s++) {
    if (*s < '0' || *s > '9') return false;
    v = v * 10 + (uint64_t)(*s - '0');
    if (v >= CLOCK_EPOCH_END) return false;  // more digits only make it bigger
  }
  if (!clockEpochValid((int64_t)v)) return false;
  e = (uint32_t)v;
  return true;
}
