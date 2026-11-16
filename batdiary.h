#pragma once
#include <stdint.h>
#include <stdio.h>        // snprintf
#include "clockprompt.h"  // clockEpochValid, CLOCK_EPOCH_MIN/END

// Battery diary (#14): a row every hour of uptime, and one at boot, when USB is plugged in
// or pulled out, and at the low-battery power-off, with the battery's level, voltage and
// charge state and how long the screen was lit and the CPU in light sleep since the
// previous row. The newest BATLOG_ROWS rows are kept, in RAM and, one 8-byte NVS entry per
// row, in their own namespace (batdiary.cpp), so a run that ends with the battery flat can
// be read back after the next boot (BATLOG?, tools/board.py batlog). Pure logic here, with
// tests in test/test_batdiary.cpp.
//
// No extra wakeups: the loop, which already runs at least every 200 ms (power.h), asks
// due() each pass, a millis() comparison; the battery readings come from the cached reads
// in rtcbat.cpp, plus one voltage read per row.

#define BATLOG_ROWS 96                // about four days of hourly rows
#define BATLOG_PERIOD_MS 3600000UL    // a row every hour of uptime (any row restarts the hour)
#define BATLOG_EVENT_GAP_MS 60000UL   // a plug or unplug row waits a minute after the last row
#define BATLOG_MV_MAX 5000            // a single LiPo cell never reads more (4.2 V full)
#define BATLOG_MIN_MAX 60             // screen-on and asleep minutes per row (one period)
#define BATLOG_PCT_NONE 127           // no battery reading (batPercent() < 0)
// What the stored rows mean: the row layout below and the ring size. A diary saved with
// another value is not read (batdiary.cpp starts a new one), so a change to either never
// shows old rows in the wrong order or with the wrong fields. Bump the layout byte when the
// packing changes; a new BATLOG_ROWS changes the value by itself.
#define BATLOG_FMT ((uint16_t)(0x0100 | BATLOG_ROWS))

// Why a row was written. The names are the dump's `event` column.
enum BatLogKind : uint8_t {
  BL_HOUR = 0,    // an hour since the previous row
  BL_BOOT = 1,    // the first row after the board started (any reset or power-on)
  BL_UNPLUG = 2,  // USB pulled out: a run on battery starts
  BL_PLUG = 3,    // USB plugged in
  BL_CUTOFF = 4,  // the low-battery power-off (batShouldShutdown): the end of a run
  BL_KINDS = 5,
  BL_NONE = 0xFF  // due(): no row now
};
static const char *const BATLOG_KIND_KEYS[BL_KINDS] = { "hour", "boot", "unplug", "plug", "cutoff" };

// The last minute the diary can stamp: the wall clock's range (clockprompt.h), in minutes
// from CLOCK_EPOCH_MIN, plus one (0 is "no time").
const uint32_t BATLOG_MINUTE_MAX = (CLOCK_EPOCH_END - CLOCK_EPOCH_MIN) / 60;

// One row, unpacked.
struct BatRow {
  uint32_t minute = 0;    // wall-clock minute + 1 since CLOCK_EPOCH_MIN; 0 = the clock was not trusted
  uint8_t kind = BL_HOUR;
  uint8_t pct = BATLOG_PCT_NONE;  // 0-100, or BATLOG_PCT_NONE
  uint16_t mv = 0;        // battery voltage, 0 = no reading
  bool usb = false;       // USB power present
  bool charging = false;  // the charger is charging (USB present and not full)
  uint8_t onMin = 0;      // minutes the screen was lit since the previous row
  uint8_t sleepMin = 0;   // minutes in light sleep since the previous row
};

// The wall-clock epoch of a minute stamp (0 for "no time").
inline uint32_t batLogEpoch(uint32_t minute) {
  return minute ? CLOCK_EPOCH_MIN + (minute - 1) * 60 : 0;
}
// The minute stamp of a wall-clock epoch: 0 when it is outside the clock's range.
inline uint32_t batLogMinute(uint32_t epoch) {
  return clockEpochValid(epoch) ? (epoch - CLOCK_EPOCH_MIN) / 60 + 1 : 0;
}

// Packing, low bit first: minute 26 | kind 3 | pct 7 | mv 13 | usb 1 | charging 1 |
// on 6 | asleep 6 | used 1 = 64 bits. The top bit marks a used slot, so the 0 a missing
// NVS key reads back as is never a row. Out-of-range values are clamped here and refused
// by batRowUnpack(), so a stored row always unpacks.
inline uint64_t batRowPack(const BatRow &r) {
  uint64_t minute = r.minute <= BATLOG_MINUTE_MAX ? r.minute : 0;
  uint64_t kind = r.kind < BL_KINDS ? r.kind : (uint8_t)BL_HOUR;
  uint64_t pct = r.pct <= 100 ? r.pct : BATLOG_PCT_NONE;
  uint64_t mv = r.mv <= BATLOG_MV_MAX ? r.mv : BATLOG_MV_MAX;
  uint64_t on = r.onMin <= BATLOG_MIN_MAX ? r.onMin : BATLOG_MIN_MAX;
  uint64_t sl = r.sleepMin <= BATLOG_MIN_MAX ? r.sleepMin : BATLOG_MIN_MAX;
  return minute | kind << 26 | pct << 29 | mv << 36 | (uint64_t)(r.usb ? 1 : 0) << 49 |
         (uint64_t)(r.charging ? 1 : 0) << 50 | on << 51 | sl << 57 | (uint64_t)1 << 63;
}

// A stored value back into a row. False (and `out` untouched) for anything batRowPack()
// cannot have written: an unused slot, or a value the flash or a bug corrupted.
inline bool batRowUnpack(uint64_t v, BatRow &out) {
  if (!(v >> 63)) return false;
  BatRow r;
  r.minute = (uint32_t)(v & 0x3FFFFFF);
  r.kind = (uint8_t)(v >> 26 & 0x7);
  r.pct = (uint8_t)(v >> 29 & 0x7F);
  r.mv = (uint16_t)(v >> 36 & 0x1FFF);
  r.usb = (v >> 49 & 1) != 0;
  r.charging = (v >> 50 & 1) != 0;
  r.onMin = (uint8_t)(v >> 51 & 0x3F);
  r.sleepMin = (uint8_t)(v >> 57 & 0x3F);
  if (r.minute > BATLOG_MINUTE_MAX || r.kind >= BL_KINDS) return false;
  if (r.pct > 100 && r.pct != BATLOG_PCT_NONE) return false;
  if (r.mv > BATLOG_MV_MAX || r.onMin > BATLOG_MIN_MAX || r.sleepMin > BATLOG_MIN_MAX) return false;
  out = r;
  return true;
}

// The ring of packed rows. `n` counts the rows written and stays in [0, 2 * BATLOG_ROWS):
// fewer than BATLOG_ROWS while it fills, then it cycles through [BATLOG_ROWS, 2 * BATLOG_ROWS),
// so it never wraps and a value outside that range read back from NVS is plainly corrupt.
// The next row goes to slot n % BATLOG_ROWS.
struct BatRing {
  uint64_t slot[BATLOG_ROWS] = {};  // packed rows; 0 = unused (or refused when loaded)
  uint16_t n = 0;
  uint8_t unsaved = 0;               // the newest rows not yet in NVS (at most BATLOG_ROWS)

  void reset() {
    for (uint16_t i = 0; i < BATLOG_ROWS; i++) slot[i] = 0;
    n = 0;
    unsaved = 0;
  }
  static bool countValid(uint32_t c) { return c < 2u * BATLOG_ROWS; }
  uint16_t filled() const { return n < BATLOG_ROWS ? n : BATLOG_ROWS; }
  // The slot of the i-th oldest row kept (0 <= i < filled()).
  uint16_t slotOf(uint16_t i) const {
    return (uint16_t)((n + 2u * BATLOG_ROWS - filled() + i) % BATLOG_ROWS);
  }
  // The slot of the i-th oldest row not yet saved (0 <= i < unsaved).
  uint16_t unsavedSlot(uint8_t i) const {
    return (uint16_t)((n + 2u * BATLOG_ROWS - unsaved + i) % BATLOG_ROWS);
  }
  void append(uint64_t packed) {
    slot[n % BATLOG_ROWS] = packed;
    n = (uint16_t)(n + 1 < 2 * BATLOG_ROWS ? n + 1 : BATLOG_ROWS);
    if (unsaved < BATLOG_ROWS) unsaved++;
  }
};

// When a row is due, and the screen-on and light-sleep time since the previous one.
struct BatRecorder {
  bool started = false;   // the boot row is written
  uint32_t lastAt = 0;    // millis() of the previous row
  bool lastUsb = false;   // USB at the previous row
  uint32_t onMs = 0;      // screen lit since the previous row (the remainder of a minute carries over)
  uint32_t sleepMs = 0;   // light sleep since the previous row

  // Capped at a little over a period, so a stuck clock or a missed row cannot overflow them.
  void addOn(uint32_t ms) { onMs = capMs(onMs, ms); }
  void addSleep(uint32_t ms) { sleepMs = capMs(sleepMs, ms); }

  // The row due now, or BL_NONE. A change of USB waits BATLOG_EVENT_GAP_MS after the previous
  // row, so a loose plug that comes and goes logs one row at most a minute and none at all
  // when it is back as it was; the hour is counted from the previous row of any kind.
  uint8_t due(uint32_t now, bool usb) const {
    if (!started) return BL_BOOT;
    uint32_t since = now - lastAt;
    if (usb != lastUsb && since >= BATLOG_EVENT_GAP_MS) return usb ? BL_PLUG : BL_UNPLUG;
    if (since >= BATLOG_PERIOD_MS) return BL_HOUR;
    return BL_NONE;
  }

  // A row is written now: its screen-on and asleep minutes (whole minutes, the rest carries
  // to the next row), and the hour starts again.
  void took(uint32_t now, bool usb, BatRow &r) {
    r.onMin = takeMinutes(onMs);
    r.sleepMin = takeMinutes(sleepMs);
    started = true;
    lastAt = now;
    lastUsb = usb;
  }

  // BATLOG CLEAR: the next row comes in an hour (or at a change of USB), with the time from now.
  void restart(uint32_t now, bool usb) {
    started = true;
    lastAt = now;
    lastUsb = usb;
    onMs = 0;
    sleepMs = 0;
  }

 private:
  static uint32_t capMs(uint32_t have, uint32_t add) {
    const uint32_t cap = (BATLOG_MIN_MAX + 1) * 60000UL;
    return add >= cap - have ? cap : have + add;
  }
  static uint8_t takeMinutes(uint32_t &ms) {
    uint32_t m = ms / 60000UL;
    if (m > BATLOG_MIN_MAX) m = BATLOG_MIN_MAX;
    ms -= m * 60000UL;
    if (ms >= 60000UL) ms = 0;  // more than a period: the excess is not carried
    return (uint8_t)m;
  }
};

// The reading for a row (rtcbat.h's cached values, and one voltage read).
struct BatReading {
  int pct = -1;        // batPercent(): 0-100, -1 with no battery
  int mv = 0;          // batMillivolts()
  bool usb = false;
  bool charging = false;
};

// A row from a reading and the trusted wall clock (0 = not trusted).
inline BatRow batRowFrom(uint8_t kind, const BatReading &b, uint32_t epoch) {
  BatRow r;
  r.minute = batLogMinute(epoch);
  r.kind = kind < BL_KINDS ? kind : (uint8_t)BL_HOUR;
  r.pct = b.pct < 0 ? BATLOG_PCT_NONE : b.pct > 100 ? 100 : (uint8_t)b.pct;
  r.mv = b.mv <= 0 ? 0 : b.mv > BATLOG_MV_MAX ? BATLOG_MV_MAX : (uint16_t)b.mv;
  r.usb = b.usb;
  r.charging = b.charging;
  return r;
}

// The dump's header line and one row of it (BATLOG?), as tools/board.py batlog reads them:
//   epoch,event,pct,mv,usb,chg,on_min,sleep_min
// epoch is the wall-clock time of the row's minute (0 = the clock was not trusted), pct is
// -1 with no battery reading and mv is 0 with no voltage reading.
#define BATLOG_CSV_HEADER "epoch,event,pct,mv,usb,chg,on_min,sleep_min"
inline int batLogFormatRow(char *dst, unsigned n, const BatRow &r) {
  if (!dst || !n) return 0;
  return snprintf(dst, n, "%lu,%s,%d,%u,%d,%d,%u,%u", (unsigned long)batLogEpoch(r.minute),
                  BATLOG_KIND_KEYS[r.kind < BL_KINDS ? r.kind : (uint8_t)BL_HOUR],
                  r.pct == BATLOG_PCT_NONE ? -1 : (int)r.pct, (unsigned)r.mv, r.usb ? 1 : 0,
                  r.charging ? 1 : 0, (unsigned)r.onMin, (unsigned)r.sleepMin);
}

// How long the dump waits for the host while BATLOG? prints (batdiary.cpp). One write call that
// stalls costs far more than the transmit timeout: in arduino-esp32 3.3.10 HWCDC::write()
// (cores/esp32/HWCDC.cpp) retries xRingbufferSend() up to max_consec_timeouts = 20 times, each
// waiting the timeout (1 tick is 1 ms, CONFIG_FREERTOS_HZ=1000), before it returns a short
// count. The loop (screen and touch) is frozen for that long, so the product is kept small.
#define BATLOG_TX_WAIT_MS 5          // the transmit timeout while the dump prints
#define BATLOG_TX_RETRIES 20         // HWCDC::write()'s max_consec_timeouts (not in a header: re-check on a core update)
#define BATLOG_STALL_MAX_MS 100      // the longest one stalled write call may hold the loop
static_assert(BATLOG_TX_WAIT_MS * BATLOG_TX_RETRIES <= BATLOG_STALL_MAX_MS,
              "one stalled write call must not hold the loop for more than BATLOG_STALL_MAX_MS");

static_assert(BATLOG_ROWS <= 255, "BatRing::unsaved is a uint8_t");
static_assert(2 * BATLOG_ROWS <= 0xFFFF, "BatRing::n is a uint16_t");
static_assert(BATLOG_ROWS < 1000, "the slot keys are r0..r999");

// ---- the firmware's side (batdiary.cpp) ----
// The rows live in NVS namespace "batlog", apart from the pet's "tamapoke" (WIPE leaves
// them, BATLOG CLEAR erases only them): "fmt" (BATLOG_FMT), "n" (BatRing::n) and one
// uint64 per slot, "r0" to "r95". A row is one entry write plus the counter's.
void batLogLoad();                  // setup(): the saved rows, each one checked
uint8_t batLogDue(uint32_t now);    // the row due now, or BL_NONE (cached reads only)
void batLogRecord(uint8_t kind, uint32_t now, uint32_t epoch);  // takes a reading and keeps the row in RAM
bool batLogSaveDue();               // rows to write, and the last write did not fail
bool batLogSave();                  // writes them: call it only in the quiet moments of the pet's autosave
                                    // (screen dimmed or off, the pocket lock blank, or the pet asleep)
void batLogCutoff(uint32_t now, uint32_t epoch);  // the power-off row (one in a row), saved at once
void batLogScreenOn(uint32_t ms);   // the screen was lit for this long
void batLogAsleep(uint32_t ms);     // the CPU was in light sleep for this long
bool batLogClear(uint32_t now);     // BATLOG CLEAR: erases the namespace and the RAM copy
void batLogPrint();                 // BATLOG?: the dump on Serial (without DONE)
const BatRing &batLogRing();        // for the tests
