// TamaPoke on the Mac, interactive: the real TamaPoke.ino on the simulated board
// (simhw.h) in an SDL2 window. Mouse = finger, keys = PWR/BOOT, the terminal = the
// serial console. Opt-in: only `make window` / `tools/sim.sh --window` build it.
//
//   tamapoke_window [--lang ja] [--clock "2026-11-13 10:00"|none] [--sprites DIR]
//                   [--square] [--out DIR] [--serial-log FILE] [--quiet]
//                   [--quit-after DUR] [--selftest]
//
// Press H in the window for the keys. tools/sim.sh --window builds and runs this.
#include <Arduino.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <string>
#include "boot.h"
#include "interactive.h"
#include "script.h"
#include "simhw.h"
#include "window.h"
#include "../board.h"

void setup();
void loop();

namespace {

struct Options {
  int lang = -1;
  bool clockGiven = false;
  uint32_t clock = 0;
  std::string sprites = "tools/sdcard", out = "shots", serialLog;
  bool square = false, quiet = false, selftest = false;
  uint64_t quitAfterMs = 0;  // wall time; 0 = never
};

void usage() {
  fprintf(stderr,
          "usage: tamapoke_window [--lang CODE] [--clock \"YYYY-MM-DD HH:MM\"|none] [--sprites DIR]\n"
          "                       [--square] [--out DIR] [--serial-log FILE] [--quiet]\n"
          "                       [--quit-after DUR] [--selftest]\n"
          "  languages: es en fr de it pt ja ko (default: en)\n"
          "  the default clock is this computer's local time; none = boot with no time\n"
          "  --out is where S saves screenshots; --quit-after is wall time (2m, 30s...)\n"
          "  firmware serial output goes to the terminal unless --serial-log FILE\n"
          "  --selftest runs a fixed sequence of synthetic events and exits 0 or 1\n");
}

bool parseArgs(int argc, char **argv, Options &o) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](std::string &dst) {
      if (i + 1 >= argc) return false;
      dst = argv[++i];
      return true;
    };
    std::string v;
    if (a == "--lang") {
      if (!next(v) || simLangIndex(v) < 0) { fprintf(stderr, "bad --lang\n"); return false; }
      o.lang = simLangIndex(v);
    } else if (a == "--clock") {
      if (!next(v)) return false;
      o.clockGiven = true;
      if (v == "none") o.clock = 0;
      else if (!simParseClock(v, o.clock)) { fprintf(stderr, "bad --clock '%s'\n", v.c_str()); return false; }
    } else if (a == "--sprites") {
      if (!next(o.sprites)) return false;
    } else if (a == "--out") {
      if (!next(o.out)) return false;
    } else if (a == "--serial-log") {
      if (!next(o.serialLog)) return false;
    } else if (a == "--quit-after") {
      if (!next(v) || !simParseDuration(v, o.quitAfterMs) || o.quitAfterMs == 0) {
        fprintf(stderr, "bad --quit-after\n");
        return false;
      }
    } else if (a == "--square") {
      o.square = true;
    } else if (a == "--quiet") {
      o.quiet = true;
    } else if (a == "--selftest") {
      o.selftest = true;
    } else if (a == "-h" || a == "--help") {
      usage();
      exit(0);
    } else {
      fprintf(stderr, "unknown option '%s'\n", a.c_str());
      return false;
    }
  }
  return true;
}

// this computer's local time as the board's clock (the firmware keeps local time as seconds)
bool localClockNow(uint32_t &epoch) {
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[32];
  strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
  return simParseClock(buf, epoch);
}

// ---------------------------------------------------------------- selftest
// Synthetic SDL events through winFrame() -> winHandleEvent(), the path the real window
// uses, with the firmware running between them in simulated time. Each check names
// what a broken mapping would get wrong.
struct Selftest {
  WinApp &app;
  int checks = 0;
  std::string failed;

  explicit Selftest(WinApp &a) : app(a) {}

  void check(bool ok, const char *what) {
    checks++;
    if (!ok && failed.empty()) failed = what;
  }
  bool ok() const { return failed.empty(); }

  // sim time passes at 1x; a frame is 50 ms
  void run(uint32_t ms, bool readStdin = false) {
    uint32_t done = 0;
    do {  // at least one frame, so queued events are handled
      winFrame(app, 50000, readStdin);
      done += 50;
    } while (done < ms);
  }
  void push(SDL_Event e) { SDL_PushEvent(&e); }
  void mouse(uint32_t type, int x, int y) {
    SDL_Event e;
    SDL_zero(e);
    if (type == SDL_MOUSEMOTION) {
      e.type = type;
      e.motion.windowID = SDL_GetWindowID(app.window);  // SDL maps only events of the renderer's window
      e.motion.x = x;
      e.motion.y = y;
      e.motion.state = SDL_BUTTON_LMASK;
    } else {
      e.type = type;
      e.button.windowID = SDL_GetWindowID(app.window);
      e.button.button = SDL_BUTTON_LEFT;
      e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
      e.button.x = x;
      e.button.y = y;
    }
    push(e);
  }
  void key(SDL_Keycode sym, bool down, bool repeat = false) {
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.sym = sym;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.repeat = repeat ? 1 : 0;
    push(e);
  }
  // a key press and release, then a short run
  void tapKey(SDL_Keycode sym, uint32_t ms = 50) {
    key(sym, true);
    key(sym, false);
    run(ms);
  }
};

// colour of one rendered pixel (logical 1:1 window): 0xRRGGBB
bool readPixel(WinApp &a, int x, int y, uint32_t &rgb) {
  std::vector<uint32_t> px;
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(a.renderer, &w, &h);
  if (w <= x || h <= y) return false;
  px.resize((size_t)w * h);
  if (SDL_RenderReadPixels(a.renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), w * 4) != 0) return false;
  rgb = px[(size_t)y * w + x] & 0xFFFFFF;
  return true;
}

int chan(uint32_t rgb, int shift) { return (int)((rgb >> shift) & 0xFF); }
int absDiff(int a, int b) { return a > b ? a - b : b - a; }
bool closeTo(uint32_t a, uint32_t b, int tol) {
  return absDiff(chan(a, 16), chan(b, 16)) <= tol && absDiff(chan(a, 8), chan(b, 8)) <= tol &&
         absDiff(chan(a, 0), chan(b, 0)) <= tol;
}
// how many lines of a file hold `needle`
int countIn(const std::string &path, const char *needle) {
  FILE *f = fopen(path.c_str(), "r");
  if (!f) return -1;
  int n = 0;
  char line[1024];
  while (fgets(line, sizeof line, f)) n += strstr(line, needle) != nullptr;
  fclose(f);
  return n;
}
bool pngFileOk(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  uint8_t sig[8] = {};
  size_t n = fread(sig, 1, 8, f);
  fclose(f);
  const uint8_t want[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
  return n == 8 && memcmp(sig, want, 8) == 0;
}

int runSelftest(WinApp &app, const Options &o, FILE *serialOut, const std::string &logPath) {
  Selftest t(app);
  app.noBudget = true;

  // ---- the firmware runs and the panel gets drawn
  t.run(3000);
  t.check(gSimPanel.flushes > 0, "the firmware drew no frame");
  t.check(simPanelVisible(), "the panel is dark after boot");
  t.check(app.framesRendered > 0, "no window frame was rendered");

  // ---- what the window draws: pixel format and channel order, bezel, dimming
  {
    const uint16_t *fb = gSimPanel.fb;
    int bx = -1, by = -1, best = 0;
    for (int y = 60; y < SIM_H - 60; y++)
      for (int x = 60; x < SIM_W - 60; x++) {
        uint16_t c = fb[y * SIM_W + x];
        int r = (c >> 11) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
        int lo = r < g ? (r < b ? r : b) : (g < b ? g : b), hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
        if (simInGlass(x, y) && hi - lo > best) { best = hi - lo; bx = x; by = y; }
      }
    t.check(best >= 60, "no coloured pixel on the boot screen to compare");
    uint32_t seen = 0, corner = 0, dim = 0;
    bool got = bx >= 0 && readPixel(app, bx, by, seen);
    t.check(got, "cannot read the rendered pixels back");
    if (got) {
      uint16_t c = fb[by * SIM_W + bx];
      uint32_t want = ((uint32_t)((c >> 11) * 255 / 31) << 16) | ((uint32_t)(((c >> 5) & 63) * 255 / 63) << 8) |
                      (uint32_t)((c & 31) * 255 / 31);
      t.check(closeTo(seen, want, 8), "a panel pixel is drawn in the wrong colour");
      t.check(readPixel(app, 3, 3, corner) && closeTo(corner, 0x141418, 2), "the corner outside the glass is not the dark bezel");
      uint8_t savedBri = gSimPanel.brightness;
      gSimPanel.brightness = 20;
      winFrame(app, 0, false);
      bool darker = readPixel(app, bx, by, dim) && chan(dim, 16) + chan(dim, 8) + chan(dim, 0) <
                                                      (chan(seen, 16) + chan(seen, 8) + chan(seen, 0)) / 2;
      gSimPanel.brightness = savedBri;
      winFrame(app, 0, false);
      t.check(darker, "a dimmed panel is not drawn clearly darker");
    }
  }

  // ---- mouse = finger. The window is twice the logical size: SDL maps the coordinates.
  SDL_SetWindowSize(app.window, 2 * SIM_W, 2 * SIM_H);
  winFrame(app, 0, false);
  t.mouse(SDL_MOUSEBUTTONDOWN, 600, 400);
  t.run(100);
  t.check(gSimTouch.down && gSimTouch.x == 300 && gSimTouch.y == 200, "mouse down did not land at the mapped (300,200)");
  t.mouse(SDL_MOUSEMOTION, 700, 500);
  t.run(100);
  t.check(gSimTouch.down && gSimTouch.x == 350 && gSimTouch.y == 250, "dragging did not move the finger to (350,250)");
  t.mouse(SDL_MOUSEBUTTONUP, 700, 500);
  t.run(100);
  t.check(!gSimTouch.down, "mouse up did not lift the finger");
  t.mouse(SDL_MOUSEBUTTONDOWN, 10, 10);  // logical (5,5): on the square panel, off the round glass
  t.run(50);
  t.check(!gSimTouch.down, "a press that starts outside the round glass became a touch");
  t.mouse(SDL_MOUSEBUTTONUP, 10, 10);
  t.mouse(SDL_MOUSEBUTTONDOWN, 466, 466);
  t.mouse(SDL_MOUSEMOTION, 3000, -100);  // dragged far off the window
  t.run(100);
  t.check(gSimTouch.down && gSimTouch.x == SIM_W - 1 && gSimTouch.y == 0, "a drag off the window is not clamped to the panel");
  t.mouse(SDL_MOUSEBUTTONUP, 3000, -100);
  t.run(100);
  t.check(!gSimTouch.down, "the finger stayed down after the drag");

  // ---- keys = buttons. BOOT is on GPIO0, low while held.
  t.key(SDLK_b, true);
  t.run(100);
  t.check(digitalRead(0) == LOW, "B down did not press BOOT");
  t.key(SDLK_b, false);
  t.run(100);
  t.check(digitalRead(0) == HIGH, "B up did not release BOOT");
  t.key(SDLK_b, true, /*repeat=*/true);
  t.run(100);
  t.check(digitalRead(0) == HIGH, "a key repeat pressed BOOT");
  t.run(700);  // BOOT's double-press window closes
  // PWR: a short press turns the screen off, the next one on
  t.tapKey(SDLK_p, 1000);
  t.check(!simPanelVisible(), "P did not reach the firmware as a PWR press (screen still on)");
  t.tapKey(SDLK_p, 1000);
  t.check(simPanelVisible(), "the second P did not wake the screen");

  // ---- pause: nothing runs, the firmware's inputs are ignored, the controls still work
  uint32_t before = millis();
  t.mouse(SDL_MOUSEBUTTONDOWN, 466, 466);  // finger and BOOT held when the pause comes: both let go
  t.key(SDLK_b, true);
  t.run(100);
  t.check(gSimTouch.down && digitalRead(0) == LOW, "finger and BOOT were not down before the pause");
  before = millis();
  t.tapKey(SDLK_SPACE, 0);
  t.check(app.pace.paused, "Space did not pause");
  t.check(!gSimTouch.down && digitalRead(0) == HIGH, "pausing left the finger or BOOT held");
  t.mouse(SDL_MOUSEBUTTONUP, 466, 466);
  t.mouse(SDL_MOUSEBUTTONDOWN, 466, 466);
  t.key(SDLK_b, false);
  t.run(500);
  t.check(millis() == before, "time moved while paused");
  t.check(!gSimTouch.down, "a mouse press was accepted while paused");
  t.tapKey(SDLK_SPACE, 0);
  t.check(!app.pace.paused, "Space did not resume");
  t.run(500);
  t.check(millis() - before >= 400, "time did not move after resuming");
  t.mouse(SDL_MOUSEBUTTONUP, 466, 466);

  // ---- losing focus lets go of everything held (no key-up or mouse-up will arrive)
  t.mouse(SDL_MOUSEBUTTONDOWN, 466, 466);
  t.key(SDLK_b, true);
  t.run(100);
  t.check(gSimTouch.down && digitalRead(0) == LOW, "finger and BOOT were not down before the focus loss");
  {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_WINDOWEVENT;
    e.window.windowID = SDL_GetWindowID(app.window);
    e.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    t.push(e);
  }
  t.run(100);
  t.check(!gSimTouch.down && digitalRead(0) == HIGH, "losing focus left the finger or BOOT held");

  // ---- speed, power toggles, battery
  t.tapKey(SDLK_RIGHTBRACKET, 0);
  t.check(app.pace.speedIndex == 1 && app.pace.speed() == 10, "] did not raise the speed to 10x");
  t.tapKey(SDLK_LEFTBRACKET, 0);
  t.tapKey(SDLK_LEFTBRACKET, 0);
  t.check(app.pace.speedIndex == 0, "[ went below 1x");
  bool usb = gSimPower.usb, chg = gSimPower.charging;
  int bat = gSimPower.batPct;
  t.tapKey(SDLK_u, 0);
  t.tapKey(SDLK_c, 0);
  t.check(gSimPower.usb == !usb, "U did not toggle USB");
  t.check(gSimPower.charging == !chg, "C did not toggle charging");
  t.tapKey(SDLK_MINUS, 0);
  t.check(gSimPower.batPct == bat - 10, "- did not lower the battery by 10");
  t.tapKey(SDLK_EQUALS, 0);
  t.tapKey(SDLK_EQUALS, 0);
  t.check(gSimPower.batPct == bat + 10, "= did not raise the battery by 10");

  // ---- screenshots
  // (an --out folder with shots in it from an earlier run just numbers these higher)
  t.tapKey(SDLK_s, 0);
  const std::string shot1 = app.lastShot;
  t.check(shot1.compare(0, o.out.size() + 8, o.out + "/window-") == 0 && pngFileOk(shot1),
          "S did not write a window-NNN.png into --out");
  t.tapKey(SDLK_s, 0);
  t.check(app.shotsSaved == 2 && app.lastShot != shot1 && pngFileOk(app.lastShot),
          "a second S did not write a second file");

  // ---- the terminal: lines typed there reach the firmware's serial console, EOF stops the reading
  {
    int fds[2];
    int saved = dup(0);
    bool piped = saved >= 0 && pipe(fds) == 0;
    t.check(piped, "cannot make a pipe for the stdin check");
    if (piped) {
      dup2(fds[0], 0);
      close(fds[0]);
      fflush(serialOut);
      int before = countIn(logPath, "heap=");
      const char cmd[] = "HEALTH\r\n";  // the firmware answers with a line holding heap=
      if (write(fds[1], cmd, sizeof cmd - 1) != (ssize_t)(sizeof cmd - 1)) t.check(false, "cannot write to the stdin pipe");
      t.run(50, /*readStdin=*/true);
      fflush(serialOut);
      t.check(countIn(logPath, "heap=") == before + 1, "a line typed on stdin did not reach the firmware's serial console");
      close(fds[1]);
      t.run(50, true);
      t.check(!app.stdinOpen, "end of input did not stop the stdin reading");
      dup2(saved, 0);
      close(saved);
    }
  }

  // ---- quit: Q, the window's close button, the system's quit
  t.tapKey(SDLK_q, 0);
  t.check(app.quit, "Q did not quit");
  app.quit = false;
  {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_QUIT;
    t.push(e);
    t.run(0);
    t.check(app.quit, "the system's quit did not quit");
    app.quit = false;
    SDL_zero(e);
    e.type = SDL_WINDOWEVENT;
    e.window.windowID = SDL_GetWindowID(app.window);
    e.window.event = SDL_WINDOWEVENT_CLOSE;
    t.push(e);
    t.run(0);
    t.check(app.quit, "closing the window did not quit");
  }

  if (!t.ok()) {
    printf("window selftest FAILED: %s\n", t.failed.c_str());
    return 1;
  }
  printf("window selftest ok: %d checks, %u frames rendered\n", t.checks, (unsigned)app.framesRendered);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  Options o;
  if (!parseArgs(argc, argv, o)) {
    usage();
    return 2;
  }
  signal(SIGTTIN, SIG_IGN);  // a read from the terminal while in the background fails instead of stopping us
  if (!simMkdirs(o.out)) {
    fprintf(stderr, "cannot create %s\n", o.out.c_str());
    return 2;
  }

  // the selftest is deterministic: the headless default date, not the real time
  uint32_t clock = o.clock;
  if (!o.clockGiven) {
    bool ok = o.selftest ? simParseClock("2026-11-13 10:00", clock) : localClockNow(clock);
    if (!ok) {
      fprintf(stderr, "cannot read the local time; pass --clock\n");
      return 2;
    }
  }

  // serial output: the terminal, or a file; the selftest keeps its one line clean
  FILE *serialOut = stdout;
  std::string logPath = o.serialLog;
  if (logPath.empty() && o.selftest) logPath = o.out + "/serial.log";
  if (!logPath.empty()) {
    serialOut = fopen(logPath.c_str(), "w");
    if (!serialOut) {
      fprintf(stderr, "cannot write %s\n", logPath.c_str());
      return 2;
    }
  }

  SimBootConfig boot;
  boot.lang = o.lang;
  boot.clock = clock;
  boot.sprites = o.sprites;
  boot.serialOut = serialOut;
  simBootPrepare(boot);

  if (!o.quiet)
    printf("sim: %s, lang %s, clock %u, sprites %s\n", BOARD_AMOLED_175C ? "1.75C" : "1.75",
           o.lang >= 0 ? simLangCode(o.lang) : "default", clock, o.sprites.c_str());

  setup();

  WinApp app;
  app.opt.board = BOARD_AMOLED_175C ? "1.75C" : "1.75";
  app.opt.out = o.out;
  app.opt.square = o.square;
  app.opt.quiet = o.quiet;
  std::string err;
  if (!winCreate(app, !o.selftest, err)) {
    fprintf(stderr, "sim: cannot open the window: %s\n", err.c_str());
    winDestroy(app);
    return 1;
  }

  int rc = 0;
  if (o.selftest) {
    rc = runSelftest(app, o, serialOut, logPath);
  } else {
    if (!o.quiet) winPrintHelp();
    const uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t start = SDL_GetPerformanceCounter();
    uint64_t last = start;
    while (!app.quit) {
      uint64_t now = SDL_GetPerformanceCounter();
      uint32_t deltaUs = (uint32_t)((now - last) * 1000000ULL / freq);
      last = now;
      winFrame(app, deltaUs, true);
      if (o.quitAfterMs && (SDL_GetPerformanceCounter() - start) * 1000ULL / freq >= o.quitAfterMs) app.quit = true;
      // vsync may not pace us (hidden window, software renderer): never spin
      uint64_t spentUs = (SDL_GetPerformanceCounter() - now) * 1000000ULL / freq;
      if (spentUs < 4000) SDL_Delay((uint32_t)((4000 - spentUs + 999) / 1000));
    }
  }
  winDestroy(app);
  fflush(serialOut);
  if (serialOut != stdout) fclose(serialOut);
  if (!app.quitReason.empty()) printf("sim: %s\n", app.quitReason.c_str());
  if (!o.quiet && !o.selftest) printf("sim: done at %.1f s simulated\n", mockMillis64() / 1000.0);
  return rc;
}
