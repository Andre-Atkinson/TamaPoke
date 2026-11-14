// Pure helpers of the interactive simulator window (see interactive.h).
#include "interactive.h"
#include <stdio.h>
#include <time.h>

int simSpeedStep(int index, int dir) {
  int i = index + (dir > 0 ? 1 : dir < 0 ? -1 : 0);
  if (i < 0) i = 0;
  if (i > SIM_SPEED_COUNT - 1) i = SIM_SPEED_COUNT - 1;
  return i;
}

int simBatteryStep(int pct, int dir) {
  int p = pct < 0 ? 0 : pct;
  p += dir > 0 ? 10 : dir < 0 ? -10 : 0;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

bool simInGlass(int x, int y) {
  if (x < 0 || y < 0 || x >= SIM_W || y >= SIM_H) return false;
  // same test as pngEncodeRgb565's round mask: twice the offset from the centre,
  // squared, against the diameter squared
  long dx = 2L * x - (SIM_W - 1), dy = 2L * y - (SIM_H - 1);
  long d = SIM_W < SIM_H ? SIM_W : SIM_H;
  return dx * dx + dy * dy <= d * d;
}

int simClampCoord(int v) {
  const int hi = SIM_W - 1;  // the panel is square
  return v < 0 ? 0 : v > hi ? hi : v;
}

uint8_t simBrightnessMod(uint8_t brightness) {
  const int full = 145, floorMod = 64;
  if (brightness >= full) return 255;
  return (uint8_t)(floorMod + (255 - floorMod) * brightness / full);
}

void simPaceSetPaused(SimPace &p, bool paused) {
  p.paused = paused;
  p.debtUs = 0;
  p.graceUs = 0;
}

std::string simTitleText(const char *board, uint32_t epoch, uint32_t speed, bool behind,
                         double effectiveSpeed, int batPct, bool usb, bool charging, bool paused,
                         bool slowed) {
  char buf[256];
  size_t n = (size_t)snprintf(buf, sizeof buf, "TamaPoke %s | ", board);
  auto room = [&] { return n < sizeof buf ? sizeof buf - n : (size_t)0; };
  if (epoch == 0) {
    n += (size_t)snprintf(buf + n, room(), "no clock");
  } else {
    time_t t = (time_t)epoch;  // local time kept as seconds, so the UTC fields are the wall-clock ones
    struct tm tmv;
    gmtime_r(&t, &tmv);
    n += strftime(buf + n, room(), "%Y-%m-%d %H:%M:%S", &tmv);
  }
  n += (size_t)snprintf(buf + n, room(), " | %ux", (unsigned)speed);
  if (slowed) n += (size_t)snprintf(buf + n, room(), " (1x while touching)");
  else if (behind) n += (size_t)snprintf(buf + n, room(), " (running at %.1fx)", effectiveSpeed);
  if (batPct >= 0) n += (size_t)snprintf(buf + n, room(), " | battery %d%%", batPct);
  else n += (size_t)snprintf(buf + n, room(), " | no battery");
  if (usb) n += (size_t)snprintf(buf + n, room(), ", USB%s", charging ? ", charging" : "");
  if (paused) n += (size_t)snprintf(buf + n, room(), " | paused");
  return buf;
}

void SimLineSplitter::feed(const char *data, size_t n, std::vector<std::string> &lines) {
  for (size_t i = 0; i < n; i++) {
    char c = data[i];
    if (c == '\n' || c == '\r') {  // CRLF is two terminators; the empty line between them is dropped
      if (!tooLong_ && !cur_.empty()) lines.push_back(cur_);
      cur_.clear();
      tooLong_ = false;
    } else if (c == '\0') {
      continue;
    } else if (!tooLong_) {
      if (cur_.size() >= SIM_LINE_MAX) {
        tooLong_ = true;
        cur_.clear();
      } else {
        cur_ += c;
      }
    }
  }
}

std::string simShotPath(const std::string &dir, int &counter, bool (*exists)(const std::string &)) {
  if (counter < 1) counter = 1;
  for (;; counter++) {
    char name[32];
    snprintf(name, sizeof name, "window-%03d.png", counter);
    std::string path = dir.empty() ? std::string(name) : dir + "/" + name;
    if (!exists(path)) return path;
  }
}
