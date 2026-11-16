// The battery diary (batdiary.h, #14): how a row is packed and checked, the ring, when a row
// is due, and its NVS copy (batdiary.cpp, on the in-memory Preferences shim): what a row
// costs in flash writes, and that everything read back is range-checked.
#include "framework.h"
#include "../batdiary.h"
#include "../serialcmd.h"  // SERIAL_TX_TIMEOUT_MS
#include "../sim/simhw.h"  // gSimPower: what rtcbat_sim.cpp reports
#include <Arduino.h>      // the fake clock and Serial
#include <Preferences.h>
#include <stdio.h>
#include <string>
#include <vector>

static const uint32_t MIN_MS = 60000UL;
static const uint32_t HOUR_MS = 3600000UL;
static const uint32_t T0 = 1794564000UL;  // 2026-11-13 10:00, a time the clock takes

static BatRow sampleRow() {
  BatRow r;
  r.minute = batLogMinute(T0);
  r.kind = BL_UNPLUG;
  r.pct = 87;
  r.mv = 4012;
  r.usb = false;
  r.charging = false;
  r.onMin = 12;
  r.sleepMin = 40;
  return r;
}

static bool sameRow(const BatRow &a, const BatRow &b) {
  return a.minute == b.minute && a.kind == b.kind && a.pct == b.pct && a.mv == b.mv && a.usb == b.usb &&
         a.charging == b.charging && a.onMin == b.onMin && a.sleepMin == b.sleepMin;
}

// ---------------------------------------------------------------- packing

TEST(BatDiary, pack_and_unpack_keep_every_field_at_its_limits) {
  BatRow r = sampleRow();
  BatRow out;
  CHECK(batRowUnpack(batRowPack(r), out));
  CHECK(sameRow(r, out));
  BatRow hi;
  hi.minute = BATLOG_MINUTE_MAX;
  hi.kind = BL_CUTOFF;
  hi.pct = 100;
  hi.mv = BATLOG_MV_MAX;
  hi.usb = true;
  hi.charging = true;
  hi.onMin = BATLOG_MIN_MAX;
  hi.sleepMin = BATLOG_MIN_MAX;
  CHECK(batRowUnpack(batRowPack(hi), out));
  CHECK(sameRow(hi, out));
  BatRow lo;  // no time, no battery, no voltage, nothing counted
  lo.minute = 0;
  lo.kind = BL_HOUR;
  lo.pct = BATLOG_PCT_NONE;
  CHECK(batRowUnpack(batRowPack(lo), out));
  CHECK(sameRow(lo, out));
  BatRow zero;  // 0 % is a reading, not "none"
  zero.pct = 0;
  CHECK(batRowUnpack(batRowPack(zero), out));
  CHECK_EQ(out.pct, (uint8_t)0);
}

// A value read back from NVS that batRowPack() cannot have written is refused, field by field.
TEST(BatDiary, unpack_refuses_what_pack_never_writes) {
  BatRow out = sampleRow();
  const uint64_t good = batRowPack(sampleRow());
  CHECK(!batRowUnpack(0, out));                                // an unused slot (a missing key reads 0)
  CHECK(!batRowUnpack(good & ~((uint64_t)1 << 63), out));      // no used bit
  auto with = [&](int shift, uint64_t mask, uint64_t v) { return (good & ~(mask << shift)) | (v << shift); };
  CHECK(!batRowUnpack(with(0, 0x3FFFFFF, BATLOG_MINUTE_MAX + 1), out));  // past 2100
  CHECK(!batRowUnpack(with(0, 0x3FFFFFF, 0x3FFFFFF), out));
  for (uint64_t k = BL_KINDS; k <= 7; k++) CHECK_MSG(!batRowUnpack(with(26, 0x7, k), out), std::to_string(k));
  CHECK(!batRowUnpack(with(29, 0x7F, 101), out));
  CHECK(!batRowUnpack(with(29, 0x7F, 126), out));
  CHECK(batRowUnpack(with(29, 0x7F, BATLOG_PCT_NONE), out));    // "none" is allowed
  CHECK(!batRowUnpack(with(36, 0x1FFF, BATLOG_MV_MAX + 1), out));
  CHECK(!batRowUnpack(with(36, 0x1FFF, 0x1FFF), out));
  CHECK(!batRowUnpack(with(51, 0x3F, BATLOG_MIN_MAX + 1), out));
  CHECK(!batRowUnpack(with(51, 0x3F, 0x3F), out));
  CHECK(!batRowUnpack(with(57, 0x3F, BATLOG_MIN_MAX + 1), out));
  CHECK(!batRowUnpack(with(57, 0x3F, 0x3F), out));
  BatRow keep = sampleRow();
  keep.pct = 5;
  BatRow probe = keep;
  CHECK(!batRowUnpack(with(26, 0x7, 7), probe));
  CHECK(sameRow(probe, keep));
}

TEST(BatDiary, pack_clamps_a_field_out_of_range_so_the_row_still_unpacks) {
  BatRow r = sampleRow();
  r.minute = BATLOG_MINUTE_MAX + 5;  // no such time: stored as "no time"
  r.kind = 9;
  r.pct = 120;
  r.mv = 9000;
  r.onMin = 70;
  r.sleepMin = 255;
  BatRow out;
  CHECK(batRowUnpack(batRowPack(r), out));
  CHECK_EQ(out.minute, (uint32_t)0);
  CHECK_EQ(out.kind, (uint8_t)BL_HOUR);
  CHECK_EQ(out.pct, (uint8_t)BATLOG_PCT_NONE);
  CHECK_EQ(out.mv, (uint16_t)BATLOG_MV_MAX);
  CHECK_EQ(out.onMin, (uint8_t)BATLOG_MIN_MAX);
  CHECK_EQ(out.sleepMin, (uint8_t)BATLOG_MIN_MAX);
}

TEST(BatDiary, a_row_from_a_reading_clamps_it) {
  BatReading b;
  b.pct = -1;
  b.mv = -20;
  b.usb = true;
  b.charging = true;
  BatRow r = batRowFrom(BL_PLUG, b, 0);
  CHECK_EQ(r.pct, (uint8_t)BATLOG_PCT_NONE);
  CHECK_EQ(r.mv, (uint16_t)0);
  CHECK_EQ(r.minute, (uint32_t)0);
  CHECK(r.usb && r.charging);
  CHECK_EQ(r.kind, (uint8_t)BL_PLUG);
  b.pct = 104;
  b.mv = 7000;
  r = batRowFrom(77, b, T0);
  CHECK_EQ(r.pct, (uint8_t)100);
  CHECK_EQ(r.mv, (uint16_t)BATLOG_MV_MAX);
  CHECK_EQ(r.kind, (uint8_t)BL_HOUR);
  CHECK_EQ(r.minute, batLogMinute(T0));
}

TEST(BatDiary, the_minute_stamp_follows_the_clocks_range) {
  CHECK_EQ(batLogMinute(0), (uint32_t)0);
  CHECK_EQ(batLogMinute(CLOCK_EPOCH_MIN - 1), (uint32_t)0);
  CHECK_EQ(batLogMinute(CLOCK_EPOCH_MIN), (uint32_t)1);
  CHECK_EQ(batLogMinute(CLOCK_EPOCH_MIN + 59), (uint32_t)1);
  CHECK_EQ(batLogMinute(CLOCK_EPOCH_MIN + 60), (uint32_t)2);
  CHECK_EQ(batLogMinute(CLOCK_EPOCH_END - 1), BATLOG_MINUTE_MAX);
  CHECK_EQ(batLogMinute(CLOCK_EPOCH_END), (uint32_t)0);
  CHECK(BATLOG_MINUTE_MAX < (1u << 26));  // fits the 26-bit field
  CHECK_EQ(batLogEpoch(0), (uint32_t)0);
  CHECK_EQ(batLogEpoch(batLogMinute(T0 + 59)), T0);  // to the minute
  CHECK_EQ(batLogEpoch(BATLOG_MINUTE_MAX), CLOCK_EPOCH_END - 60);
}

TEST(BatDiary, a_dump_row_is_the_csv_board_py_reads) {
  char line[64];
  BatRow r = sampleRow();
  batLogFormatRow(line, sizeof line, r);
  CHECK_STREQ(line, "1794564000,unplug,87,4012,0,0,12,40");
  BatRow none;
  none.kind = BL_BOOT;
  none.usb = true;
  batLogFormatRow(line, sizeof line, none);
  CHECK_STREQ(line, "0,boot,-1,0,1,0,0,0");
  BatRow wide;  // the widest row fits the dump's 64-byte line
  wide.minute = BATLOG_MINUTE_MAX;
  wide.kind = BL_UNPLUG;
  wide.pct = 100;
  wide.mv = BATLOG_MV_MAX;
  wide.usb = wide.charging = true;
  wide.onMin = wide.sleepMin = BATLOG_MIN_MAX;
  CHECK(batLogFormatRow(line, sizeof line, wide) < (int)sizeof line);
  CHECK_STREQ(BATLOG_CSV_HEADER, "epoch,event,pct,mv,usb,chg,on_min,sleep_min");
}

// ---------------------------------------------------------------- the ring

static uint64_t rowNo(uint32_t i) {  // a distinct row per i (its minute)
  BatRow r = sampleRow();
  r.minute = i + 1;
  return batRowPack(r);
}
static uint32_t minuteOf(uint64_t v) {
  BatRow r;
  return batRowUnpack(v, r) ? r.minute : 0;
}

TEST(BatRing, keeps_the_newest_rows_oldest_first) {
  BatRing g;
  for (uint32_t i = 0; i < 10; i++) g.append(rowNo(i));
  CHECK_EQ(g.filled(), (uint16_t)10);
  for (uint16_t i = 0; i < 10; i++) CHECK_EQ(minuteOf(g.slot[g.slotOf(i)]), (uint32_t)i + 1);
  for (uint32_t i = 10; i < BATLOG_ROWS + 50; i++) g.append(rowNo(i));
  CHECK_EQ(g.filled(), (uint16_t)BATLOG_ROWS);
  for (uint16_t i = 0; i < BATLOG_ROWS; i++)
    CHECK_EQ(minuteOf(g.slot[g.slotOf(i)]), (uint32_t)(50 + i + 1));
}

TEST(BatRing, the_counter_stays_in_range_and_never_wraps) {
  BatRing g;
  for (uint32_t i = 0; i < 1000; i++) {
    g.append(rowNo(i));
    CHECK(BatRing::countValid(g.n));
    if (i + 1 >= BATLOG_ROWS) CHECK_RANGE(g.n, (uint16_t)BATLOG_ROWS, (uint16_t)(2 * BATLOG_ROWS - 1));
    CHECK_EQ(minuteOf(g.slot[g.slotOf(g.filled() - 1)]), i + 1);  // the newest is where it should be
  }
  CHECK(!BatRing::countValid(2 * BATLOG_ROWS));
  CHECK(BatRing::countValid(2 * BATLOG_ROWS - 1));
}

TEST(BatRing, unsaved_rows_are_the_newest_and_at_most_a_ring) {
  BatRing g;
  for (uint32_t i = 0; i < 5; i++) g.append(rowNo(i));
  CHECK_EQ(g.unsaved, (uint8_t)5);
  CHECK_EQ(minuteOf(g.slot[g.unsavedSlot(0)]), (uint32_t)1);
  g.unsaved = 0;
  for (uint32_t i = 5; i < 8; i++) g.append(rowNo(i));
  CHECK_EQ(minuteOf(g.slot[g.unsavedSlot(0)]), (uint32_t)6);
  for (uint32_t i = 8; i < 400; i++) g.append(rowNo(i));
  CHECK_EQ(g.unsaved, (uint8_t)BATLOG_ROWS);
  CHECK_EQ(minuteOf(g.slot[g.unsavedSlot(0)]), (uint32_t)(400 - BATLOG_ROWS + 1));
}

// ---------------------------------------------------------------- when a row is due

TEST(BatRecorder, the_first_row_is_the_boot_row_then_one_an_hour) {
  BatRecorder rec;
  CHECK_EQ(rec.due(5000, false), (uint8_t)BL_BOOT);
  BatRow r;
  rec.took(5000, false, r);
  CHECK_EQ(rec.due(5000, false), (uint8_t)BL_NONE);
  CHECK_EQ(rec.due(5000 + HOUR_MS - 1, false), (uint8_t)BL_NONE);
  CHECK_EQ(rec.due(5000 + HOUR_MS, false), (uint8_t)BL_HOUR);
  rec.took(5000 + HOUR_MS + 300, false, r);  // the hour counts from the row actually written
  CHECK_EQ(rec.due(5000 + 2 * HOUR_MS, false), (uint8_t)BL_NONE);
  CHECK_EQ(rec.due(5000 + 2 * HOUR_MS + 300, false), (uint8_t)BL_HOUR);
}

TEST(BatRecorder, plug_and_unplug_wait_a_minute_after_the_last_row) {
  BatRecorder rec;
  BatRow r;
  rec.took(1000, false, r);
  CHECK_EQ(rec.due(1000 + 30000, true), (uint8_t)BL_NONE);         // plugged 30 s after the row: waits
  CHECK_EQ(rec.due(1000 + MIN_MS - 1, true), (uint8_t)BL_NONE);
  CHECK_EQ(rec.due(1000 + MIN_MS, true), (uint8_t)BL_PLUG);
  CHECK_EQ(rec.due(1000 + MIN_MS, false), (uint8_t)BL_NONE);       // came back out within it: nothing
  rec.took(1000 + MIN_MS, true, r);
  CHECK_EQ(rec.due(1000 + 2 * MIN_MS, true), (uint8_t)BL_NONE);    // still plugged: no row
  CHECK_EQ(rec.due(1000 + 2 * MIN_MS, false), (uint8_t)BL_UNPLUG);
}

TEST(BatRecorder, a_plug_row_restarts_the_hour) {
  BatRecorder rec;
  BatRow r;
  rec.took(0, false, r);
  CHECK_EQ(rec.due(10 * MIN_MS, true), (uint8_t)BL_PLUG);
  rec.took(10 * MIN_MS, true, r);
  CHECK_EQ(rec.due(HOUR_MS, true), (uint8_t)BL_NONE);
  CHECK_EQ(rec.due(HOUR_MS + 10 * MIN_MS, true), (uint8_t)BL_HOUR);
}

TEST(BatRecorder, the_hour_survives_millis_wrapping) {
  BatRecorder rec;
  BatRow r;
  uint32_t at = 0xFFFFFFFFUL - 1000;
  rec.took(at, false, r);
  CHECK_EQ(rec.due(at + HOUR_MS - 1, false), (uint8_t)BL_NONE);
  CHECK_EQ(rec.due(at + HOUR_MS, false), (uint8_t)BL_HOUR);
}

TEST(BatRecorder, minutes_are_whole_and_the_rest_carries_to_the_next_row) {
  BatRecorder rec;
  BatRow r;
  rec.took(0, false, r);
  rec.addOn(90000);       // 1.5 min
  rec.addSleep(59999);    // just under a minute
  rec.took(HOUR_MS, false, r);
  CHECK_EQ(r.onMin, (uint8_t)1);
  CHECK_EQ(r.sleepMin, (uint8_t)0);
  rec.addOn(30000);
  rec.addSleep(1);
  rec.took(2 * HOUR_MS, false, r);
  CHECK_EQ(r.onMin, (uint8_t)1);      // 30 s carried + 30 s
  CHECK_EQ(r.sleepMin, (uint8_t)1);   // 59.999 s carried + 1 ms
}

TEST(BatRecorder, minutes_are_capped_at_a_period_and_the_excess_is_not_carried) {
  BatRecorder rec;
  BatRow r;
  rec.took(0, false, r);
  rec.addOn(0xFFFFFFF0UL);
  rec.addOn(0x20);  // would wrap a plain sum to 16 ms
  rec.addSleep(5 * HOUR_MS);
  rec.took(HOUR_MS, false, r);
  CHECK_EQ(r.onMin, (uint8_t)BATLOG_MIN_MAX);
  CHECK_EQ(r.sleepMin, (uint8_t)BATLOG_MIN_MAX);
  rec.took(2 * HOUR_MS, false, r);
  CHECK_EQ(r.onMin, (uint8_t)0);
  CHECK_EQ(r.sleepMin, (uint8_t)0);
}

TEST(BatRecorder, restart_counts_from_now_and_drops_what_was_counted) {
  BatRecorder rec;
  BatRow r;
  rec.took(0, false, r);
  rec.addOn(20 * MIN_MS);
  rec.restart(30 * MIN_MS, true);
  CHECK_EQ(rec.due(30 * MIN_MS + HOUR_MS - 1, true), (uint8_t)BL_NONE);
  CHECK_EQ(rec.due(30 * MIN_MS + HOUR_MS, true), (uint8_t)BL_HOUR);
  CHECK_EQ(rec.due(31 * MIN_MS, false), (uint8_t)BL_UNPLUG);
  rec.took(31 * MIN_MS, false, r);
  CHECK_EQ(r.onMin, (uint8_t)0);
}

// ---------------------------------------------------------------- the NVS copy (batdiary.cpp)

// A fresh board for one test: no NVS, on battery at 80 %. What the test changed (the sim's
// power state, the NVS store, the diary in RAM) is put back when it ends.
struct FreshBoard {
  FreshBoard() {
    mockNvsReset();
    mockSetMillis(10000);
    gSimPower = SimPower();
    gSimPower.batPct = 80;
    gSimPower.usb = false;
    batLogLoad();
  }
  ~FreshBoard() {
    mockNvsFailAfter(-1);
    mockNvsReset();
    gSimPower = SimPower();
    batLogLoad();
  }
};

// Writes the row due now (as the loop does) and returns its kind, or BL_NONE.
static uint8_t poll(uint32_t now, uint32_t epoch = T0) {
  uint8_t k = batLogDue(now);
  if (k != BL_NONE) batLogRecord(k, now, epoch);
  return k;
}

static std::vector<BatRow> rowsInRam() {
  std::vector<BatRow> out;
  const BatRing &g = batLogRing();
  for (uint16_t i = 0; i < g.filled(); i++) {
    BatRow r;
    if (batRowUnpack(g.slot[g.slotOf(i)], r)) out.push_back(r);
  }
  return out;
}

static std::vector<BatRow> rowsAfterReboot() {
  batLogLoad();
  return rowsInRam();
}

static void putRaw16(const char *k, uint16_t v) {
  Preferences p;
  p.begin("batlog", false);
  p.putUShort(k, v);
  p.end();
}
static void putRaw64(const char *k, uint64_t v) {
  Preferences p;
  p.begin("batlog", false);
  p.putULong64(k, v);
  p.end();
}

static std::string captureDump() {
  FILE *f = tmpfile();
  mockSerialOutput(f);
  batLogPrint();
  mockSerialOutput(nullptr);  // silent again, as the tests run
  std::string out;
  rewind(f);
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  fclose(f);
  return out;
}

TEST(BatLogNvs, a_row_takes_the_batterys_cached_readings) {
  FreshBoard board;
  gSimPower.batPct = 55;
  CHECK_EQ(poll(10000, 0), (uint8_t)BL_BOOT);
  std::vector<BatRow> rows = rowsInRam();
  CHECK_EQ(rows.size(), (size_t)1);
  CHECK_EQ(rows[0].pct, (uint8_t)55);
  CHECK_EQ(rows[0].mv, (uint16_t)(3300 + 55 * 9));  // rtcbat_sim.cpp's voltage
  CHECK(!rows[0].usb && !rows[0].charging);
  CHECK_EQ(rows[0].minute, (uint32_t)0);  // clock not trusted: no time
  gSimPower.usb = true;
  gSimPower.charging = true;
  CHECK_EQ(poll(10000 + 30000), (uint8_t)BL_NONE);
  CHECK_EQ(poll(10000 + MIN_MS), (uint8_t)BL_PLUG);
  rows = rowsInRam();
  CHECK(rows[1].usb && rows[1].charging);
  CHECK_EQ(rows[1].minute, batLogMinute(T0));
  gSimPower.pmuOk = false;  // no PMU: no reading, USB reads as present
  CHECK_EQ(poll(10000 + MIN_MS + HOUR_MS), (uint8_t)BL_HOUR);
  rows = rowsInRam();
  CHECK_EQ(rows[2].pct, (uint8_t)BATLOG_PCT_NONE);
  CHECK_EQ(rows[2].mv, (uint16_t)0);
}

TEST(BatLogNvs, the_rows_survive_a_restart) {
  FreshBoard board;
  poll(10000);
  poll(10000 + HOUR_MS);
  CHECK(batLogSaveDue());
  CHECK(batLogSave());
  CHECK(!batLogSaveDue());
  std::vector<BatRow> before = rowsInRam();
  std::vector<BatRow> after = rowsAfterReboot();
  CHECK_EQ(after.size(), (size_t)2);
  for (size_t i = 0; i < after.size() && i < before.size(); i++) CHECK(sameRow(before[i], after[i]));
  CHECK_EQ(after[0].kind, (uint8_t)BL_BOOT);
  CHECK_EQ(after[1].kind, (uint8_t)BL_HOUR);
  CHECK_EQ(batLogDue(20000), (uint8_t)BL_BOOT);  // and after the restart the next row is a boot row
}

// The flash wear the issue asks to keep down: after the first save, a row costs two
// entry writes (its slot and the counter), whatever the number of rows kept.
TEST(BatLogNvs, a_row_costs_two_entry_writes) {
  FreshBoard board;
  poll(10000);
  CHECK(batLogSave());
  for (uint32_t h = 1; h <= 3 * BATLOG_ROWS; h++) {
    poll(10000 + h * HOUR_MS);
    size_t w = mockNvsWrites();
    CHECK(batLogSave());
    CHECK_EQ(mockNvsWrites() - w, (size_t)2);
  }
  CHECK_EQ(mockNvsKeyCount("batlog"), (size_t)(BATLOG_ROWS + 2));  // r0..r95, fmt, n
  size_t w = mockNvsWrites();
  CHECK(batLogSave());  // nothing new: no write at all
  CHECK_EQ(mockNvsWrites(), w);
}

TEST(BatLogNvs, the_order_survives_restarts_after_the_ring_wraps) {
  FreshBoard board;
  uint32_t now = 10000;
  for (uint32_t h = 0; h < 250; h++) {
    poll(now, T0 + h * 3600);
    batLogSave();
    if (h % 37 == 0) batLogLoad();  // a reset now and then: the next row is a boot row
    now += HOUR_MS;
  }
  std::vector<BatRow> ram = rowsInRam();
  std::vector<BatRow> after = rowsAfterReboot();
  CHECK_EQ(after.size(), (size_t)BATLOG_ROWS);
  for (size_t i = 0; i < after.size() && i < ram.size(); i++) CHECK(sameRow(ram[i], after[i]));
  for (size_t i = 1; i < after.size(); i++) CHECK(after[i].minute == after[i - 1].minute + 60);
  CHECK_EQ(batLogEpoch(after.back().minute), T0 + 249 * 3600);
}

TEST(BatLogNvs, a_diary_of_another_format_is_not_read_and_is_erased_at_the_next_save) {
  FreshBoard board;
  putRaw16("fmt", (uint16_t)(BATLOG_FMT + 1));
  putRaw16("n", 3);
  for (int i = 0; i < 3; i++) {
    char k[6];
    snprintf(k, sizeof k, "r%d", i);
    putRaw64(k, rowNo(i));
  }
  putRaw64("r150", rowNo(9));  // a key a bigger ring would have left
  CHECK_EQ(rowsAfterReboot().size(), (size_t)0);
  poll(10000);
  CHECK(batLogSave());
  CHECK_EQ(mockNvsKeyCount("batlog"), (size_t)3);  // fmt, n, r0: the old keys are gone
  std::vector<BatRow> after = rowsAfterReboot();
  CHECK_EQ(after.size(), (size_t)1);
  CHECK_EQ(after[0].kind, (uint8_t)BL_BOOT);
}

TEST(BatLogNvs, a_counter_out_of_range_starts_a_new_diary) {
  FreshBoard board;
  poll(10000);
  poll(10000 + HOUR_MS);
  CHECK(batLogSave());
  putRaw16("n", 2 * BATLOG_ROWS);  // corrupt: the ring's counter never reaches it
  CHECK_EQ(rowsAfterReboot().size(), (size_t)0);
  putRaw16("n", 2 * BATLOG_ROWS - 1);  // the last valid value: a full ring (of which two rows are left)
  CHECK_EQ(rowsAfterReboot().size(), (size_t)2);
  Preferences p;
  p.begin("batlog", false);
  p.remove("n");  // a counter that was never written
  p.end();
  CHECK_EQ(rowsAfterReboot().size(), (size_t)0);
}

TEST(BatLogNvs, a_corrupt_row_is_skipped_and_the_others_are_kept) {
  FreshBoard board;
  for (uint32_t h = 0; h < 4; h++) poll(10000 + h * HOUR_MS, T0 + h * 3600);
  CHECK(batLogSave());
  putRaw64("r1", batRowPack(sampleRow()) | ((uint64_t)7 << 26));  // kind 7: corrupt
  std::vector<BatRow> after = rowsAfterReboot();
  CHECK_EQ(after.size(), (size_t)3);
  CHECK_EQ(batLogEpoch(after[0].minute), T0);
  CHECK_EQ(batLogEpoch(after[1].minute), T0 + 2 * 3600);
  CHECK_EQ(batLogEpoch(after[2].minute), T0 + 3 * 3600);
  CHECK_EQ(batLogRing().slot[1], (uint64_t)0);  // what was refused is not kept either
  std::string dump = captureDump();  // ...nor dumped, nor counted
  CHECK_EQ(dump.find("BATLOG rows=3 "), (size_t)0);
  size_t lines = 0;
  for (char c : dump) lines += c == '\n';
  CHECK_EQ(lines, (size_t)5);
}

TEST(BatLogNvs, a_failed_write_keeps_the_rows_and_waits_for_the_next_row) {
  FreshBoard board;
  poll(10000);
  mockNvsFailWrites(true);
  CHECK(!batLogSave());
  CHECK(!batLogSaveDue());  // no retry every pass of the loop
  CHECK_EQ(rowsInRam().size(), (size_t)1);
  mockNvsFailWrites(false);
  poll(10000 + HOUR_MS);
  CHECK(batLogSaveDue());
  CHECK(batLogSave());
  CHECK_EQ(rowsAfterReboot().size(), (size_t)2);
}

// A cut (or a full flash) between a row and the counter: the counter in NVS still says the
// rows before, so those are what comes back, and the next rows reuse the slots.
TEST(BatLogNvs, a_cut_between_a_row_and_the_counter_loses_only_the_rows_not_counted) {
  FreshBoard board;
  poll(10000, T0);
  CHECK(batLogSave());
  poll(10000 + HOUR_MS, T0 + 3600);
  poll(10000 + 2 * HOUR_MS, T0 + 7200);
  mockNvsFailAfter(1);  // the first of the two rows goes, then the flash fails
  CHECK(!batLogSave());
  mockNvsFailAfter(-1);
  std::vector<BatRow> after = rowsAfterReboot();
  CHECK_EQ(after.size(), (size_t)1);
  CHECK_EQ(batLogEpoch(after[0].minute), T0);
  poll(10000, T0 + 9000);  // the boot row after the restart, in the slot the lost row had
  CHECK(batLogSave());
  after = rowsAfterReboot();
  CHECK_EQ(after.size(), (size_t)2);
  CHECK_EQ(batLogRing().n, (uint16_t)2);  // no gap left where the lost rows were
  CHECK_EQ(batLogEpoch(after[1].minute), T0 + 9000 - 9000 % 60);
}

TEST(BatLogNvs, the_cutoff_row_is_saved_at_once) {
  FreshBoard board;
  poll(10000);
  CHECK(batLogSave());
  batLogCutoff(10000 + 5 * MIN_MS, T0);  // no batLogSave(): the power goes right after
  std::vector<BatRow> after = rowsAfterReboot();
  CHECK_EQ(after.size(), (size_t)2);
  CHECK_EQ(after[1].kind, (uint8_t)BL_CUTOFF);
}

// batPowerOff() with USB plugged in at that moment leaves the board running, and the firmware
// asks for the power-off again each time the voltage stays low for 30 s.
TEST(BatLogNvs, a_board_still_running_after_the_cutoff_logs_it_once) {
  FreshBoard board;
  poll(10000);
  for (uint32_t i = 0; i < 200; i++) batLogCutoff(10000 + MIN_MS + i * 30000, T0);
  CHECK_EQ(rowsInRam().size(), (size_t)2);
  CHECK(!batLogSaveDue());
  poll(10000 + MIN_MS + HOUR_MS);  // still running an hour later: the hourly row, then one more cutoff
  batLogCutoff(10000 + MIN_MS + HOUR_MS + 30000, T0);
  std::vector<BatRow> after = rowsAfterReboot();
  CHECK_EQ(after.size(), (size_t)4);
  CHECK_EQ(after[1].kind, (uint8_t)BL_CUTOFF);
  CHECK_EQ(after[2].kind, (uint8_t)BL_HOUR);
  CHECK_EQ(after[3].kind, (uint8_t)BL_CUTOFF);
  poll(20000, T0);  // after a restart, a cutoff is logged again
  batLogCutoff(30000, T0);
  CHECK_EQ(rowsInRam().size(), (size_t)6);
}

TEST(BatLogNvs, clear_erases_only_the_diary) {
  FreshBoard board;
  Preferences pet;
  pet.begin("tamapoke", false);
  pet.putUChar("full", 70);
  pet.end();
  poll(10000);
  CHECK(batLogSave());
  CHECK(batLogClear(20000));
  CHECK_EQ(rowsInRam().size(), (size_t)0);
  CHECK_EQ(mockNvsKeyCount("batlog"), (size_t)0);
  pet.begin("tamapoke", true);
  CHECK_EQ(pet.getUChar("full", 0), (uint8_t)70);
  pet.end();
  CHECK_EQ(batLogDue(20000 + HOUR_MS - 1), (uint8_t)BL_NONE);  // no boot row: the hour from the clear
  CHECK_EQ(batLogDue(20000 + HOUR_MS), (uint8_t)BL_HOUR);
  // ...and WIPE (Pet::factoryReset clears "tamapoke") leaves the diary
  poll(20000 + HOUR_MS);
  CHECK(batLogSave());
  pet.begin("tamapoke", false);
  pet.clear();
  pet.end();
  CHECK_EQ(rowsAfterReboot().size(), (size_t)1);
}

TEST(BatLogNvs, a_failed_clear_keeps_the_rows) {
  FreshBoard board;
  poll(10000);
  CHECK(batLogSave());
  mockNvsFailWrites(true);
  CHECK(!batLogClear(20000));
  mockNvsFailWrites(false);
  CHECK_EQ(rowsInRam().size(), (size_t)1);
  CHECK_EQ(rowsAfterReboot().size(), (size_t)1);
}

TEST(BatLogNvs, the_dump_lists_the_rows_oldest_first) {
  FreshBoard board;
  gSimPower.batPct = 90;
  poll(10000, 0);
  gSimPower.batPct = 84;
  batLogScreenOn(5 * MIN_MS);
  batLogAsleep(50 * MIN_MS);
  poll(10000 + HOUR_MS, T0);
  CHECK(batLogSave());
  gSimPower.batPct = 79;
  poll(10000 + 2 * HOUR_MS, T0 + 3600);
  std::string out = captureDump();
  CHECK_STREQ(out.c_str(),
              "BATLOG rows=3 cap=96 unsaved=1 period_min=60\r\n"
              "epoch,event,pct,mv,usb,chg,on_min,sleep_min\r\n"
              "0,boot,90,4110,0,0,0,0\r\n"
              "1794564000,hour,84,4056,0,0,5,50\r\n"
              "1794567600,hour,79,4011,0,0,0,0\r\n");
}

// The dump is about 4 KB, more than the USB transmit buffer: it waits for the host while it
// prints (a longer transmit timeout), sends each line as one write call, and puts the console's
// timeout back.
TEST(BatLogNvs, each_dump_line_is_one_write_with_the_dumps_own_transmit_wait) {
  FreshBoard board;
  for (uint32_t h = 0; h < 3; h++) poll(10000 + h * HOUR_MS, T0 + h * 3600);
  Serial.setTxTimeoutMs(SERIAL_TX_TIMEOUT_MS);
  mockSerialLogClear();
  batLogPrint();
  size_t calls = mockSerialWriteCalls();
  CHECK_EQ(calls, (size_t)5);  // the BATLOG line, the header and three rows: one call each
  for (size_t i = 0; i < calls; i++) {
    CHECK_EQ(mockSerialWriteTimeout(i), (uint32_t)BATLOG_TX_WAIT_MS);
    CHECK(!mockSerialWriteShort(i));
  }
  CHECK_EQ(mockSerialTxTimeout(), (uint32_t)SERIAL_TX_TIMEOUT_MS);  // and the console's again
}

// A stalled write call costs about BATLOG_TX_RETRIES waits (HWCDC::write() retries before it
// returns short), not one: the wait is chosen so that stays within BATLOG_STALL_MAX_MS.
TEST(BatLogNvs, a_stalled_write_holds_the_loop_for_at_most_the_stall_bound) {
  CHECK_RANGE(BATLOG_TX_WAIT_MS * BATLOG_TX_RETRIES, 1, BATLOG_STALL_MAX_MS);
  FreshBoard board;
  poll(10000);
  Serial.setTxTimeoutMs(SERIAL_TX_TIMEOUT_MS);
  mockSerialLogClear();
  mockSerialTxRoom(0);  // nobody is reading
  batLogPrint();
  mockSerialTxRoom(-1);
  CHECK_EQ(mockSerialWriteCalls(), (size_t)1);
  CHECK(mockSerialWriteShort(0));
  CHECK_RANGE(mockSerialWriteTimeout(0) * BATLOG_TX_RETRIES, (uint32_t)1, (uint32_t)BATLOG_STALL_MAX_MS);
}

// With the host taking only the next `room` bytes (-1: all of them), how the dump ends: the
// write calls it made, and how many of them came back short.
static size_t dumpCallsWithRoom(long room, size_t &shortCalls) {
  Serial.setTxTimeoutMs(SERIAL_TX_TIMEOUT_MS);
  mockSerialLogClear();
  mockSerialTxRoom(room);
  batLogPrint();
  mockSerialTxRoom(-1);
  size_t calls = mockSerialWriteCalls();
  shortCalls = 0;
  for (size_t i = 0; i < calls; i++) shortCalls += mockSerialWriteShort(i);
  return calls;
}

// Where line `k` (0 = the BATLOG line, 1 = the header, 2 = the first row...) starts in a dump.
static long lineStart(const std::string &dump, int k) {
  size_t at = 0;
  while (k-- > 0) at = dump.find('\n', at) + 1;
  return (long)at;
}

TEST(BatLogNvs, the_dump_stops_at_the_first_write_the_host_does_not_take) {
  FreshBoard board;
  for (uint32_t h = 0; h < 40; h++) poll(10000 + h * HOUR_MS, T0 + h * 3600);
  std::string dump = captureDump();
  size_t shortCalls;
  // a cut inside a line makes that line's write the one short call, and the last
  CHECK_EQ(dumpCallsWithRoom(lineStart(dump, 0) + 10, shortCalls), (size_t)1);  // the BATLOG line
  CHECK_EQ(shortCalls, (size_t)1);
  CHECK_EQ(dumpCallsWithRoom(lineStart(dump, 1) + 5, shortCalls), (size_t)2);   // the header
  CHECK_EQ(shortCalls, (size_t)1);
  CHECK_EQ(dumpCallsWithRoom(lineStart(dump, 4) + 7, shortCalls), (size_t)5);   // the third row
  CHECK_EQ(shortCalls, (size_t)1);
  CHECK_EQ(mockSerialTxTimeout(), (uint32_t)SERIAL_TX_TIMEOUT_MS);
  // a cut exactly between two lines: the next line's write is the short one
  CHECK_EQ(dumpCallsWithRoom(lineStart(dump, 3), shortCalls), (size_t)4);
  CHECK_EQ(shortCalls, (size_t)1);
  // a host reading takes the 2 lines and the 40 rows, one call each
  CHECK_EQ(dumpCallsWithRoom(-1, shortCalls), (size_t)42);
  CHECK_EQ(shortCalls, (size_t)0);
}

TEST(BatLogNvs, a_cutoff_after_a_clear_is_logged_again) {
  FreshBoard board;
  poll(10000);
  batLogCutoff(10000 + MIN_MS, T0);
  CHECK_EQ(rowsInRam().size(), (size_t)2);
  CHECK(batLogClear(20000));
  batLogCutoff(20000 + MIN_MS, T0);  // the diary is empty: this cutoff is the first row of it
  std::vector<BatRow> rows = rowsInRam();
  CHECK_EQ(rows.size(), (size_t)1);
  CHECK_EQ(rows[0].kind, (uint8_t)BL_CUTOFF);
}

TEST(BatLogNvs, a_namespace_that_will_not_open_is_not_retried_every_pass) {
  FreshBoard board;
  poll(10000);
  mockNvsFailBegin(true);
  CHECK(!batLogSave());
  CHECK(!batLogSaveDue());  // as for a failed write: the next row tries again
  mockNvsFailBegin(false);
  CHECK(!batLogSaveDue());
  CHECK_EQ(rowsInRam().size(), (size_t)1);
  poll(10000 + HOUR_MS);
  CHECK(batLogSaveDue());
  CHECK(batLogSave());
  CHECK_EQ(rowsAfterReboot().size(), (size_t)2);
}
