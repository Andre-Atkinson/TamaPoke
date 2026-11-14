// Setting the wall clock (clockset.h, #45): serial TIME and the clock screen's OK. The
// pet's saved time (NVS `seen`) and the bedtime clock follow only when the clock took the
// value. The clock here is the simulator's (sim/rtcbat_sim.cpp), which behaves like the
// 1.75C soft clock: a value before 2025 is ignored. The original 1.75's PCF85063 path in
// rtcbat.cpp is not compiled on the PC.
#include "framework.h"
#include "../clockset.h"
#include "../sim/simhw.h"

static const uint32_t T0 = 1794564000UL;  // 2026-11-13 10:00: the clock and `seen` before
static const uint32_t STRAY = 5;          // "TIME 5": the 1.75C soft clock ignores it

static int bedCalls = 0;
static uint32_t bedLast = 0;
static void bedFollow(uint32_t e) {  // stands in for bedClockSet() in the sketch
  bedCalls++;
  bedLast = e;
}

static uint8_t savedJoy() {
  Preferences raw;
  raw.begin("tamapoke", true);
  uint8_t v = raw.getUChar("joy", 0);
  raw.end();
  return v;
}

// A hatched pet whose clock and saved time are both T0. The destructor leaves the
// simulator's clock with no time, as the other tests expect it.
struct ClockedPet {
  Pet p;
  ClockedPet() {
    mockNvsReset();
    mockSetMillis(0);
    p.begin();
    if (p.awaitingStarter()) p.chooseStarter(4);
    p.eggTap();
    p.eggTap();
    p.eggTap();
    simClockSet(T0);
    p.setClock(T0);  // saves: `seen` = T0
    bedCalls = 0;
    bedLast = 0;
  }
  ~ClockedPet() { simClockSet(0); }
};

TEST(ClockSet, a_time_the_clock_ignores_leaves_seen_and_writes_nothing) {
  ClockedPet c;
  uint8_t joy = savedJoy();
  c.p.joy = joy == 50 ? 51 : 50;  // RAM only: any save would write it
  CHECK(!setWallClock(c.p, STRAY, bedFollow));
  CHECK_EQ(c.p.savedEpoch(), T0);
  CHECK_EQ(c.p.lastSeenEpoch, T0);
  CHECK_EQ(savedJoy(), joy);
  CHECK_EQ(rtcEpoch(), T0);
}

TEST(ClockSet, a_time_the_clock_ignores_leaves_the_bedtime_clock_alone) {
  ClockedPet c;
  CHECK(!setWallClock(c.p, STRAY, bedFollow));
  CHECK_EQ(bedCalls, 0);
}

TEST(ClockSet, a_time_the_clock_takes_becomes_the_saved_time) {
  ClockedPet c;
  const uint32_t t1 = T0 + 3 * 3600;
  CHECK(setWallClock(c.p, t1, bedFollow));
  CHECK_EQ(rtcEpoch(), t1);
  CHECK_EQ(c.p.savedEpoch(), t1);
  CHECK_EQ(c.p.lastSeenEpoch, t1);
}

TEST(ClockSet, a_time_the_clock_takes_reaches_the_bedtime_clock) {
  ClockedPet c;
  const uint32_t t1 = T0 + 3 * 3600;
  CHECK(setWallClock(c.p, t1, bedFollow));
  CHECK_EQ(bedCalls, 1);
  CHECK_EQ(bedLast, t1);
}
