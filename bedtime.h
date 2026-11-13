#pragma once
#include <stdint.h>

// Bedtime schedule. Pure logic (no hardware, no clock reads) so it can be tested
// on the PC (test/test_bedtime.cpp).
//
// The firmware keeps LOCAL wall-clock time in the RTC as if it were UTC, so the
// minute of the day is just (epoch % 86400) / 60. The user sets the clock by eye
// (clock screen), there is no timezone handling anywhere.

// Presets, stored in NVS as one byte (key "bed"). 0 = off, which is the default.
#define BED_PRESETS 4  // number of valid values: off + three windows

struct BedWindow { uint16_t start, end; };  // minutes of the day; the window is [start, end)

// Window of a preset, or {0, 0} (empty) for "off" and for any unknown value.
inline BedWindow bedWindow(uint8_t preset) {
  switch (preset) {
    case 1: return { 21 * 60, 7 * 60 };   // 21:00 - 07:00
    case 2: return { 22 * 60, 7 * 60 };   // 22:00 - 07:00
    case 3: return { 23 * 60, 8 * 60 };   // 23:00 - 08:00
    default: return { 0, 0 };
  }
}

// Minute of the local day (0..1439) of a wall-clock epoch.
inline uint16_t bedMinuteOfDay(uint32_t epoch) { return (uint16_t)((epoch % 86400UL) / 60); }

// Is `minuteOfDay` inside the window [start, end)? Start is inside, end is outside
// (21:00 in, 07:00 out, 06:59 in). An empty window (start == end) is never inside.
inline bool bedInWindow(BedWindow w, uint16_t minuteOfDay) {
  if (w.start == w.end) return false;
  if (w.start < w.end) return minuteOfDay >= w.start && minuteOfDay < w.end;
  return minuteOfDay >= w.start || minuteOfDay < w.end;  // wraps midnight
}

// ... for a preset (all three presets wrap past midnight). Off/unknown: never.
inline bool isBedtime(uint8_t preset, uint16_t minuteOfDay) {
  return bedInWindow(bedWindow(preset), minuteOfDay);
}

enum BedEvent : uint8_t { BED_NONE = 0, BED_SLEEP, BED_WAKE };

// Turns successive samples into edge events: entering the window -> BED_SLEEP,
// leaving it -> BED_WAKE. Because only the edges produce events, a manual
// override sticks: wake the pet at 02:00 and it stays awake until the next
// bedtime; put it to bed early and the morning edge still wakes it.
//
//  - First sample after boot: inside the window gives BED_SLEEP once, outside
//    gives nothing.
//  - Changing the preset re-baselines silently (no event): turning the feature
//    on or off, or picking another window, never puts the pet to sleep or wakes
//    it by itself; the next edge of the new window does.
//  - A clock that jumps (set by the user) is just another sample: jumping into
//    the window sleeps, jumping out wakes.
//  - A clock that was reseeded after a power loss is NOT trustworthy (the wall
//    time is only the last one saved): markStale() pauses the schedule, update()
//    gives no events and keeps no state, and clockSet() (the user set the clock)
//    resumes it as if this were a fresh boot: inside the window sleeps once,
//    outside does nothing.
class BedEdge {
 public:
  void markStale() { stale_ = true; }
  bool stale() const { return stale_; }
  void clockSet() { stale_ = false; have_ = false; }

  BedEvent update(uint8_t preset, uint16_t minuteOfDay) {
    if (stale_) return BED_NONE;
    bool in = isBedtime(preset, minuteOfDay);
    BedEvent ev = BED_NONE;
    if (!have_) {
      if (in) ev = BED_SLEEP;
    } else if (preset == preset_) {
      if (in && !was_) ev = BED_SLEEP;
      else if (!in && was_) ev = BED_WAKE;
    }  // preset changed: re-baseline only
    have_ = true;
    preset_ = preset;
    was_ = in;
    return ev;
  }

 private:
  bool have_ = false, was_ = false, stale_ = false;
  uint8_t preset_ = 0;
};
