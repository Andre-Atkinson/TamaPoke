// Pure helpers of the interactive simulator window (sim/window.cpp, sim/main_window.cpp):
// everything that can be tested without SDL. No SDL include here, on purpose: the
// host tests (test/test_sim.cpp) compile this file.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "runner.h"
#include "simhw.h"

// ---------------------------------------------------------------- speeds
// Simulated seconds per wall second, slowest first. The window starts at 1x.
static const int SIM_SPEED_COUNT = 5;
static const uint32_t kSimSpeeds[SIM_SPEED_COUNT] = { 1, 10, 60, 600, 3600 };
// index after one step up (dir > 0) or down (dir < 0), clamped to the table
int simSpeedStep(int index, int dir);

// battery % after one 10-point step up (dir > 0) or down, clamped to 0..100. -1 (no
// battery) counts as 0.
int simBatteryStep(int pct, int dir);

// ---------------------------------------------------------------- glass
// Is the pixel inside the round glass? Same circle as the PNG writer's round crop
// (png.cpp: the circle inscribed in the panel), judged on the pixel's centre.
bool simInGlass(int x, int y);
// a drag that leaves the glass (or the window) keeps the finger on the panel
int simClampCoord(int v);

// Colour modulation (0..255 per channel) for a panel brightness (0..255). Awake, the
// firmware runs at 145 on battery and 180 on USB (power.h brightnessFor); those and
// above show in full colour. Below 145 the picture gets darker down to a floor, so a
// dimmed panel looks clearly dimmer but stays readable. 0 is a dark panel, which the
// window shows black instead.
uint8_t simBrightnessMod(uint8_t brightness);

// ---------------------------------------------------------------- pacing
// The window runs the firmware in step with the wall clock: each frame adds the wall
// time since the last one, times the speed, to a debt of simulated time and runs loop()
// passes until the debt is paid. Microseconds, so 16.7 ms frames do not lose a
// millisecond each.
static const uint32_t SIM_PACE_MAX_WALL_US = 250000;  // a stalled or hidden window must not jump ahead
static const uint32_t SIM_PACE_BUDGET_US = 12000;     // wall time one frame may spend running the firmware

struct SimPace {
  int speedIndex = 0;
  bool paused = false;
  // simulated time still owed, in microseconds. Negative: the last pass ran past what was
  // owed (a light sleep or delay() is not cut short), so later frames run nothing until
  // wall time catches up.
  int64_t debtUs = 0;
  // since the last title refresh, for the effective speed
  uint64_t statSimMs = 0, statWallUs = 0;
  bool statBehind = false;

  uint32_t speed() const { return kSimSpeeds[speedIndex]; }
};

struct SimPaceResult {
  uint64_t simMs = 0;      // simulated time that ran this frame
  uint64_t passes = 0;     // loop() passes
  uint64_t droppedMs = 0;  // owed time given up because the wall budget ran out
  bool behind = false;     // the host could not keep up this frame
};

// One frame. wallDeltaUs: wall time since the previous frame. input: a touch or button
// event came in this frame; it forgives a negative debt (the GPIO wakes the chip from
// light sleep at once), so a click never waits out a sleep. step() runs one loop()
// pass; stop() ends early (the PMU powered the board off); wallSpentUs() is the wall
// time this frame has spent so far, against the budget. Paused: nothing runs.
template <class Step, class Stop, class WallSpentUs>
SimPaceResult simPaceFrame(SimPace &p, uint32_t wallDeltaUs, bool input, Step step, Stop stop,
                           WallSpentUs wallSpentUs) {
  SimPaceResult r;
  if (p.paused) return r;
  if (input && p.debtUs < 0) p.debtUs = 0;
  uint32_t wall = wallDeltaUs > SIM_PACE_MAX_WALL_US ? SIM_PACE_MAX_WALL_US : wallDeltaUs;
  p.debtUs += (int64_t)wall * (int64_t)p.speed();
  p.statWallUs += wall;
  if (p.debtUs >= 1000) {
    uint64_t t0 = mockMillis64();
    r.passes = simRunFor((uint64_t)(p.debtUs / 1000), step,
                         [&] { return stop() || (uint64_t)wallSpentUs() >= SIM_PACE_BUDGET_US; });
    r.simMs = mockMillis64() - t0;
    p.debtUs -= (int64_t)r.simMs * 1000;
    if (p.debtUs >= 1000) {  // the run was cut short: give the rest up, do not pile it up
      r.droppedMs = (uint64_t)(p.debtUs / 1000);
      r.behind = !stop();
      p.debtUs = 0;
    }
    p.statSimMs += r.simMs;
    if (r.behind) p.statBehind = true;
  }
  return r;
}

// pause / resume. Pausing forgets the debt, so resuming does not run a burst.
void simPaceSetPaused(SimPace &p, bool paused);

// ---------------------------------------------------------------- title
// "TamaPoke 1.75C | 2026-11-13 10:23:45 | 10x | battery 80%, USB, charging | paused".
// epoch is the board's clock (local time as seconds, 0 = not set). Behind: the speed
// reads "10x (running at 4.2x)", with effectiveSpeed measured by the caller.
std::string simTitleText(const char *board, uint32_t epoch, uint32_t speed, bool behind,
                         double effectiveSpeed, int batPct, bool usb, bool charging, bool paused);

// ---------------------------------------------------------------- stdin
// Splits terminal input into complete lines for the firmware's serial console. Feed
// it whatever read() returned; a line comes out when its terminator (LF, CR or CRLF)
// has arrived, however the bytes were chunked. Empty lines are dropped (handleSerial
// ignores them anyway). handleSerial has no length limit of its own (an Arduino String
// grows), so the cap here is the bound: a line longer than SIM_LINE_MAX is dropped
// whole, not truncated, because a cut command could be a different, valid one. NUL
// bytes are skipped (the mock serial queue is fed C strings).
static const size_t SIM_LINE_MAX = 256;  // the longest real command is a PUT, ~100 characters

class SimLineSplitter {
public:
  void feed(const char *data, size_t n, std::vector<std::string> &lines);

private:
  std::string cur_;
  bool tooLong_ = false;
};

// ---------------------------------------------------------------- screenshots
// "DIR/window-NNN.png": the first number from `counter` up whose file does not exist
// yet, so a new run never overwrites an earlier one. counter is left at the number used.
std::string simShotPath(const std::string &dir, int &counter, bool (*exists)(const std::string &));
