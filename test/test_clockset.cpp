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

// "TIME -5" reaches the firmware as (uint32_t)-5 = 4294967291 (2106). A clock that took
// it would wrap to a tiny time within seconds, and the loop would copy that into the pet.
static const uint32_t PAST_RANGE[] = {4294967291UL, 4102444800UL};  // 2106-02-07, 2100-01-01

static void rawSeen(uint32_t v) {  // the pet's saved time written behind its back
  Preferences raw;
  raw.begin("tamapoke", false);
  raw.putUInt("seen", v);
  raw.end();
}

TEST(ClockSet, a_time_past_2099_is_refused_and_leaves_everything_alone) {
  ClockedPet c;
  uint8_t joy = savedJoy();
  c.p.joy = joy == 50 ? 51 : 50;  // RAM only: any save would write it
  for (uint32_t bad : PAST_RANGE) {
    CHECK(!setWallClock(c.p, bad, bedFollow));
    CHECK_EQ(c.p.savedEpoch(), T0);
    CHECK_EQ(c.p.lastSeenEpoch, T0);
    CHECK_EQ(savedJoy(), joy);
    CHECK_EQ(rtcEpoch(), T0);
    CHECK_EQ(bedCalls, 0);
  }
}

TEST(ClockSet, the_last_second_of_2099_is_taken) {
  ClockedPet c;
  CHECK(setWallClock(c.p, 4102444799UL, bedFollow));
  CHECK_EQ(rtcEpoch(), (uint32_t)4102444799UL);
  CHECK_EQ(c.p.lastSeenEpoch, (uint32_t)4102444799UL);
}

TEST(ClockSet, the_clock_reads_no_time_once_it_runs_past_2099_and_never_wraps) {
  ClockedPet c;
  simClockSet(4102444799UL);  // 2099-12-31 23:59:59
  CHECK_EQ(rtcEpoch(), (uint32_t)4102444799UL);
  mockAdvanceMillis(2000);
  CHECK_EQ(rtcEpoch(), (uint32_t)0);
  mockAdvanceMillis(86400000UL);
  CHECK_EQ(rtcEpoch(), (uint32_t)0);
}

// A pet saved while the old TIME bug could hold a time past 2099. setup() seeds the clock
// from savedEpoch(), and the clock refuses such a time, so it reads as none.
TEST(ClockSet, savedEpoch_is_0_for_a_saved_time_past_2099) {
  ClockedPet c;
  rawSeen(4294967291UL);
  CHECK_EQ(c.p.savedEpoch(), (uint32_t)0);
  rawSeen(4102444800UL);
  CHECK_EQ(c.p.savedEpoch(), (uint32_t)0);
}

TEST(ClockSet, savedEpoch_returns_a_valid_saved_time_unchanged) {
  ClockedPet c;
  rawSeen(4102444799UL);
  CHECK_EQ(c.p.savedEpoch(), (uint32_t)4102444799UL);
  rawSeen(T0);
  CHECK_EQ(c.p.savedEpoch(), T0);
}
