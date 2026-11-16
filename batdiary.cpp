// The battery diary's firmware side (batdiary.h, #14): the RAM copy, its NVS copy in the
// "batlog" namespace, and the readings from rtcbat.h.
#include <Arduino.h>
#include <Preferences.h>
#include "batdiary.h"
#include "rtcbat.h"
#include "serialcmd.h"  // SERIAL_TX_TIMEOUT_MS
#include <string.h>

static const char *const BATLOG_NS = "batlog";

static BatRing ring;
static BatRecorder rec;
// The namespace holds this BATLOG_FMT and the rows below match it. False on a new board, after
// BATLOG CLEAR, and when the saved diary was refused: the next save then starts it over.
static bool fmtSaved = false;
static uint16_t savedN = 0;        // the "n" in NVS (meaningful while fmtSaved)
static bool saveFailed = false;    // the last write failed: no retry until the next row
static uint8_t lastKind = BL_NONE; // the newest row's kind since boot or BATLOG CLEAR

const BatRing &batLogRing() { return ring; }

static void slotKey(char (&k)[6], uint16_t slot) { snprintf(k, sizeof k, "r%u", (unsigned)slot); }

// Every value is checked: a "fmt" of another layout or ring size, or an "n" out of range,
// starts a new diary (the old one is erased at the next save); a slot that does not unpack
// is left empty and the dump skips it.
void batLogLoad() {
  ring.reset();
  rec = BatRecorder();  // a boot: the next row due is the boot row
  lastKind = BL_NONE;
  fmtSaved = false;
  savedN = 0;
  saveFailed = false;
  Preferences p;
  if (!p.begin(BATLOG_NS, true)) {  // never saved on this board
    Serial.println("battery diary: 0 rows");
    return;
  }
  uint16_t fmt = p.getUShort("fmt", 0);
  uint16_t n = p.getUShort("n", 0xFFFF);
  if (fmt == BATLOG_FMT && BatRing::countValid(n)) {
    ring.n = n;
    for (uint16_t i = 0; i < ring.filled(); i++) {
      uint16_t s = ring.slotOf(i);
      char k[6];
      slotKey(k, s);
      uint64_t v = p.getULong64(k, 0);
      BatRow r;
      if (batRowUnpack(v, r)) ring.slot[s] = v;
    }
    fmtSaved = true;
    savedN = n;
  }
  p.end();
  Serial.printf("battery diary: %u rows%s\n", (unsigned)ring.filled(),
                fmtSaved || fmt == 0 ? "" : " (an unreadable diary was set aside)");
}

uint8_t batLogDue(uint32_t now) { return rec.due(now, usbPresent()); }

void batLogRecord(uint8_t kind, uint32_t now, uint32_t epoch) {
  BatReading b;
  b.usb = usbPresent();
  b.pct = batPercent();
  b.mv = batMillivolts();  // a direct read: once per row
  b.charging = batCharging();
  BatRow r = batRowFrom(kind, b, epoch);
  rec.took(now, b.usb, r);
  ring.append(batRowPack(r));
  lastKind = r.kind;
  saveFailed = false;  // a new row: try the flash again
}

// Only rows not yet written count: with saveFailed clear and no unsaved row, "n" in NVS already
// equals ring.n (saveFailed is cleared by a new row, which raises unsaved, by batLogLoad() and
// batLogClear(), or by a save that wrote the counter too). batLogSave() itself still checks the
// counter, because batLogCutoff() calls it after a save that wrote the rows but not "n".
bool batLogSaveDue() { return !saveFailed && ring.unsaved; }

// The rows not yet saved, oldest first, then the counter. A power cut between them loses only
// the rows whose counter was not written: the counter in NVS still says the rows before, and
// the next rows go to the same slots. (With the ring full, the slot written held the oldest
// row: that row is gone, and the new one shows first in the dump until the next row takes the
// slot again.)
bool batLogSave() {
  if (!ring.unsaved && fmtSaved && ring.n == savedN) return true;
  Preferences p;
  if (!p.begin(BATLOG_NS, false)) { saveFailed = true; return false; }
  bool ok = true;
  // A new diary (a new board, BATLOG CLEAR, or one refused at boot): nothing old left behind.
  // Every row kept is then one of the unsaved ones, since the ring started empty.
  if (!fmtSaved) {
    ok = p.clear() && p.putUShort("fmt", BATLOG_FMT) == sizeof(uint16_t);
    if (ok) {
      fmtSaved = true;
      savedN = 0xFFFF;
    }
  }
  while (ok && ring.unsaved) {
    uint16_t s = ring.unsavedSlot(0);
    char k[6];
    slotKey(k, s);
    ok = p.putULong64(k, ring.slot[s]) == sizeof(uint64_t);
    if (ok) ring.unsaved--;
  }
  if (ok && ring.n != savedN) {
    ok = p.putUShort("n", ring.n) == sizeof(uint16_t);
    if (ok) savedN = ring.n;
  }
  p.end();
  saveFailed = !ok;
  return ok;
}

// A board still running after batPowerOff() (USB plugged in at that moment) comes back here
// each time the voltage stays low long enough: one cutoff row, not one every 30 s pushing the
// run out of the ring.
void batLogCutoff(uint32_t now, uint32_t epoch) {
  if (lastKind != BL_CUTOFF) batLogRecord(BL_CUTOFF, now, epoch);
  Serial.println(batLogSave() ? "battery diary: power-off row saved"
                              : "battery diary: power-off row not saved (the flash write failed)");
}

void batLogScreenOn(uint32_t ms) { rec.addOn(ms); }
void batLogAsleep(uint32_t ms) { rec.addSleep(ms); }

bool batLogClear(uint32_t now) {
  Preferences p;
  if (!p.begin(BATLOG_NS, false)) return false;
  bool ok = p.clear();
  p.end();
  if (!ok) return false;  // the rows stay, in RAM and in NVS
  ring.reset();
  lastKind = BL_NONE;
  fmtSaved = false;
  savedN = 0;
  saveFailed = false;
  rec.restart(now, usbPresent());
  return true;
}

// The dump is up to about 4 KB and the USB serial's transmit buffer holds 256 bytes; with the
// console's 0 ms timeout (SERIAL_TX_TIMEOUT_MS) the rest would be dropped. While it prints, the
// transmit timeout is BATLOG_TX_WAIT_MS (batdiary.h), so a host that reads gets every line. A
// host that has stopped reading costs one write call, which HWCDC retries up to
// BATLOG_TX_RETRIES times: at most BATLOG_STALL_MAX_MS (100 ms) with the loop frozen. Each line
// goes out as one write call (its own "\r\n") and the first one that comes back short ends the
// dump. tools/board.py checks that it got every row the first line announces.
// One line in one write call: `len` characters are in line[], which has room for "\r\n".
static bool batLogPutLine(char (&line)[80], int len) {
  if (len < 0) len = 0;
  if (len > (int)sizeof line - 2) len = (int)sizeof line - 2;
  line[len++] = '\r';
  line[len++] = '\n';
  return Serial.write((const uint8_t *)line, (size_t)len) == (size_t)len;
}

void batLogPrint() {
  uint16_t rows = 0;
  BatRow r;
  for (uint16_t i = 0; i < ring.filled(); i++)
    if (batRowUnpack(ring.slot[ring.slotOf(i)], r)) rows++;
  char line[80];
  Serial.setTxTimeoutMs(BATLOG_TX_WAIT_MS);
  int n = snprintf(line, sizeof line - 2, "BATLOG rows=%u cap=%u unsaved=%u period_min=%lu",
                   (unsigned)rows, (unsigned)BATLOG_ROWS, (unsigned)ring.unsaved,
                   (unsigned long)(BATLOG_PERIOD_MS / 60000UL));
  bool sent = batLogPutLine(line, n);
  if (sent) {
    n = snprintf(line, sizeof line - 2, "%s", BATLOG_CSV_HEADER);
    sent = batLogPutLine(line, n);
  }
  for (uint16_t i = 0; sent && i < ring.filled(); i++) {
    if (!batRowUnpack(ring.slot[ring.slotOf(i)], r)) continue;
    n = batLogFormatRow(line, sizeof line - 2, r);
    sent = batLogPutLine(line, n);
  }
  Serial.setTxTimeoutMs(SERIAL_TX_TIMEOUT_MS);
}
