// The interactive simulator window (see window.h).
#include "window.h"
#include <Arduino.h>
#include <math.h>
#include <stdarg.h>
#include <poll.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "boot.h"
#include "png.h"
#include "../rtcbat.h"

void loop();

namespace {

const uint8_t BEZEL_R = 0x14, BEZEL_G = 0x14, BEZEL_B = 0x18;  // dark bezel, also the letterbox colour
const uint64_t TITLE_EVERY_US = 500000;                         // the title refreshes about twice a second

bool fileExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

// the bezel: opaque dark outside the round glass, clear inside, one pixel of anti-aliasing
// on the edge. The glass is the circle simInGlass() uses (radius SIM_W / 2 from the centre).
SDL_Texture *makeBezel(SDL_Renderer *r) {
  SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, SIM_W, SIM_H);
  if (!t) return nullptr;
  std::vector<uint32_t> px((size_t)SIM_W * SIM_H);
  const double cx = (SIM_W - 1) / 2.0, cy = (SIM_H - 1) / 2.0, rad = SIM_W / 2.0;
  for (int y = 0; y < SIM_H; y++) {
    for (int x = 0; x < SIM_W; x++) {
      double d = sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
      double cover = d - (rad - 0.5);  // 0 inside by half a pixel, 1 outside by half a pixel
      cover = cover < 0 ? 0 : cover > 1 ? 1 : cover;
      uint32_t alpha = (uint32_t)(cover * 255.0 + 0.5);
      px[(size_t)y * SIM_W + x] = (alpha << 24) | ((uint32_t)BEZEL_R << 16) | ((uint32_t)BEZEL_G << 8) | BEZEL_B;
    }
  }
  SDL_UpdateTexture(t, nullptr, px.data(), SIM_W * 4);
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  return t;
}

void say(const WinApp &a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void say(const WinApp &a, const char *fmt, ...) {
  if (a.opt.quiet) return;
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
}

void fingerUp(WinApp &a) {
  if (!a.fingerDown) return;
  a.fingerDown = false;
  a.releasePending = false;
  simTouchSet(false, 0, 0);
  a.inputThisFrame = true;
}

void bootUp(WinApp &a) {
  if (!a.bootHeld) return;
  a.bootHeld = false;
  simBootButton(false);
  a.inputThisFrame = true;
}

// The button went up. The finger lifts at once if it has been down long enough for the
// firmware (which polls the chip every 20 ms) to have seen it; otherwise it lifts in
// winFrame() once SIM_TAP_DOWN_MS of simulated time have passed. Pause and focus loss lift
// it at once (fingerUp).
void mouseUp(WinApp &a) {
  if (!a.fingerDown) return;
  if (mockMillis64() - a.downAtMs < SIM_TAP_DOWN_MS) {
    a.releasePending = true;
    return;
  }
  fingerUp(a);
}

void markTitleStale(WinApp &a) { a.titleDueUs = 0; }

void setPaused(WinApp &a, bool paused) {
  if (paused) {  // nothing stays pressed across a pause
    fingerUp(a);
    bootUp(a);
  }
  simPaceSetPaused(a.pace, paused);
  markTitleStale(a);
  say(a, "sim: %s", paused ? "paused" : "running");
}

void saveShot(WinApp &a) {
  if (!simMkdirs(a.opt.out)) {
    fprintf(stderr, "sim: cannot create %s\n", a.opt.out.c_str());
    return;
  }
  std::string path = simShotPath(a.opt.out, a.shotCounter, fileExists);
  if (!pngWriteRgb565(path, simShownPixels(), SIM_W, SIM_H, !a.opt.square)) {
    fprintf(stderr, "sim: cannot write %s\n", path.c_str());
    return;
  }
  a.shotCounter++;
  a.shotsSaved++;
  a.lastShot = path;
  say(a, "sim: %s%s", path.c_str(), simPanelVisible() ? "" : " (panel dark: screen off or locked)");
}

void handleKey(WinApp &a, const SDL_KeyboardEvent &k) {
  const bool down = k.type == SDL_KEYDOWN;
  // a held BOOT always lets go with its key, whatever else is going on
  if (!down) {
    if (k.keysym.sym == SDLK_b) bootUp(a);
    return;
  }
  if (k.repeat) return;
  if (k.keysym.mod & (KMOD_CTRL | KMOD_GUI | KMOD_ALT)) return;  // Cmd-Q and friends are the system's
  switch (k.keysym.sym) {
    case SDLK_ESCAPE:
    case SDLK_q:
      a.quit = true;
      break;
    case SDLK_h:
      winPrintHelp();
      break;
    case SDLK_s:
      saveShot(a);
      break;
    case SDLK_SPACE:
      setPaused(a, !a.pace.paused);
      break;
    case SDLK_LEFTBRACKET:
    case SDLK_RIGHTBRACKET: {
      a.pace.speedIndex = simSpeedStep(a.pace.speedIndex, k.keysym.sym == SDLK_RIGHTBRACKET ? 1 : -1);
      markTitleStale(a);
      say(a, "sim: speed %ux", (unsigned)a.pace.speed());
      break;
    }
    case SDLK_u:
      gSimPower.usb = !gSimPower.usb;
      markTitleStale(a);
      say(a, "sim: USB %s", gSimPower.usb ? "plugged in" : "unplugged");
      break;
    case SDLK_c:
      gSimPower.charging = !gSimPower.charging;
      markTitleStale(a);
      say(a, "sim: charging %s (it only counts with USB)", gSimPower.charging ? "on" : "off");
      break;
    case SDLK_MINUS:
    case SDLK_KP_MINUS:
    case SDLK_EQUALS:
    case SDLK_PLUS:
    case SDLK_KP_PLUS: {
      bool up = k.keysym.sym == SDLK_EQUALS || k.keysym.sym == SDLK_PLUS || k.keysym.sym == SDLK_KP_PLUS;
      gSimPower.batPct = simBatteryStep(gSimPower.batPct, up ? 1 : -1);
      markTitleStale(a);
      say(a, "sim: battery %d%%", gSimPower.batPct);
      break;
    }
    case SDLK_p:  // the firmware's buttons and touch are off while paused
      if (a.pace.paused) break;
      simPwrPress();
      a.inputThisFrame = true;
      break;
    case SDLK_b:
      if (a.pace.paused || a.bootHeld) break;
      a.bootHeld = true;
      simBootButton(true);
      a.inputThisFrame = true;
      break;
    default:
      break;
  }
}

void handleMouse(WinApp &a, const SDL_Event &e) {
  if (a.pace.paused) return;
  // SDL maps window coordinates to the 466x466 logical ones (SDL_RenderSetLogicalSize)
  if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
    int x = e.button.x, y = e.button.y;
    bool onPanel = a.opt.square ? (x >= 0 && y >= 0 && x < SIM_W && y < SIM_H) : simInGlass(x, y);
    // a press that starts off the glass is not a touch; a second press while the last click
    // still waits to be released is ignored (a double click that fast is not a human one)
    if (!onPanel || a.fingerDown) return;
    a.fingerDown = true;
    a.downAtMs = mockMillis64();
    simTouchSet(true, (int16_t)x, (int16_t)y);
    a.inputThisFrame = true;
    a.touchStarted = true;
  } else if (e.type == SDL_MOUSEMOTION && a.fingerDown && !a.releasePending) {
    simTouchSet(true, (int16_t)simClampCoord(e.motion.x), (int16_t)simClampCoord(e.motion.y));
    a.inputThisFrame = true;
  } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
    mouseUp(a);
  }
}

void pumpStdin(WinApp &a) {
  if (!a.stdinOpen) return;
  for (int round = 0; round < 16; round++) {  // a bounded slice per frame
    struct pollfd pfd = { 0, POLLIN, 0 };
    if (poll(&pfd, 1, 0) <= 0) return;
    if (pfd.revents & (POLLERR | POLLNVAL)) { a.stdinOpen = false; return; }
    if (!(pfd.revents & (POLLIN | POLLHUP))) return;
    char buf[512];
    ssize_t n = read(0, buf, sizeof buf);  // blocking fd left as it was: it is the shell's terminal
    if (n <= 0) {  // end of input (or no terminal to read): stop reading
      a.stdinOpen = false;
      return;
    }
    std::vector<std::string> lines;
    a.stdinLines.feed(buf, (size_t)n, lines);
    for (const std::string &l : lines) {
      std::string withEol = l + "\n";
      mockSerialFeed(withEol.c_str());
    }
  }
}

void render(WinApp &a) {
  SDL_SetRenderDrawColor(a.renderer, BEZEL_R, BEZEL_G, BEZEL_B, 255);
  SDL_RenderClear(a.renderer);
  SDL_UpdateTexture(a.panelTex, nullptr, simShownPixels(), SIM_W * 2);
  uint8_t m = simBrightnessMod(gSimPanel.brightness);
  SDL_SetTextureColorMod(a.panelTex, m, m, m);
  SDL_RenderCopy(a.renderer, a.panelTex, nullptr, nullptr);
  if (!a.opt.square) SDL_RenderCopy(a.renderer, a.bezelTex, nullptr, nullptr);
  SDL_RenderPresent(a.renderer);
  a.framesRendered++;
}

void refreshTitle(WinApp &a) {
  if (a.wallUs < a.titleDueUs) return;
  a.titleDueUs = a.wallUs + TITLE_EVERY_US;
  double eff = a.pace.statWallUs ? (double)a.pace.statSimMs * 1000.0 / (double)a.pace.statWallUs : 0;
  std::string t = simTitleText(a.opt.board.c_str(), rtcEpoch(), a.pace.speed(), a.pace.statBehind, eff,
                               gSimPower.batPct, gSimPower.usb, gSimPower.charging, a.pace.paused);
  SDL_SetWindowTitle(a.window, t.c_str());
  a.pace.statSimMs = a.pace.statWallUs = 0;
  a.pace.statBehind = false;
  fflush(stdout);  // the firmware's serial output, if it goes there
}

}  // namespace

void winPrintHelp() {
  printf(
      "sim window keys:\n"
      "  mouse    left button is the finger (a press that starts off the round glass is ignored)\n"
      "  P        short PWR press          B   BOOT, held while the key is held\n"
      "  S        save a PNG of the panel (window-NNN.png in --out)\n"
      "  Space    pause / resume           [ ]  slower / faster: 1x 10x 60x 600x 3600x\n"
      "  U        USB in / out             C   charging on / off\n"
      "  - =      battery -10 / +10 %%      H   this help\n"
      "  Esc, Q or closing the window quits.\n"
      "serial console: type a line here (HATCH, STATS, SPEC 25...) and press Enter.\n");
}

bool winCreate(WinApp &a, bool vsync, std::string &err) {
  SDL_SetMainReady();
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");  // the click that focuses the window is a touch too
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    err = std::string("SDL_Init: ") + SDL_GetError();
    return false;
  }
  a.window = SDL_CreateWindow("TamaPoke", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SIM_W, SIM_H,
                              SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  if (!a.window) {
    err = std::string("SDL_CreateWindow: ") + SDL_GetError();
    return false;
  }
  a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
  if (!a.renderer) a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_SOFTWARE);
  if (!a.renderer) {
    err = std::string("SDL_CreateRenderer: ") + SDL_GetError();
    return false;
  }
  SDL_RenderSetLogicalSize(a.renderer, SIM_W, SIM_H);
  a.panelTex = SDL_CreateTexture(a.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, SIM_W, SIM_H);
  a.bezelTex = makeBezel(a.renderer);
  if (!a.panelTex || !a.bezelTex) {
    err = std::string("SDL_CreateTexture: ") + SDL_GetError();
    return false;
  }
  return true;
}

void winDestroy(WinApp &a) {
  if (a.panelTex) SDL_DestroyTexture(a.panelTex);
  if (a.bezelTex) SDL_DestroyTexture(a.bezelTex);
  if (a.renderer) SDL_DestroyRenderer(a.renderer);
  if (a.window) SDL_DestroyWindow(a.window);
  a.panelTex = a.bezelTex = nullptr;
  a.renderer = nullptr;
  a.window = nullptr;
  SDL_Quit();
}

void winHandleEvent(WinApp &a, const SDL_Event &e) {
  switch (e.type) {
    case SDL_QUIT:
      a.quit = true;
      break;
    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_CLOSE) a.quit = true;
      else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {  // no key-up will come for a held key
        fingerUp(a);
        bootUp(a);
      }
      break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      handleKey(a, e.key);
      break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEMOTION:
      handleMouse(a, e);
      break;
    default:
      break;
  }
}

void winFrame(WinApp &a, uint32_t wallDeltaUs, bool readStdin) {
  SDL_Event e;
  // SDL_PumpEvents + SDL_PeepEvents rather than SDL_PollEvent: a poll loop that is left early
  // (below) would leave SDL's poll sentinel in the queue, and everything that arrives after it
  // would wait a whole frame more
  SDL_PumpEvents();
  while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0) {
    winHandleEvent(a, e);
    if (a.touchStarted) {  // the firmware must read this contact before a move or release changes it
      a.touchStarted = false;
      break;  // the rest of the batch stays queued for the next frame
    }
  }
  if (readStdin) pumpStdin(a);

  const uint64_t freq = SDL_GetPerformanceFrequency();
  const uint64_t t0 = SDL_GetPerformanceCounter();
  auto spentUs = [&]() -> uint64_t {
    return a.noBudget ? 0 : (SDL_GetPerformanceCounter() - t0) * 1000000ULL / freq;
  };
  simPaceFrame(a.pace, wallDeltaUs, a.inputThisFrame, [] { loop(); }, [] { return gSimPower.poweredOff; },
               spentUs);
  a.inputThisFrame = false;
  if (a.releasePending && mockMillis64() - a.downAtMs >= SIM_TAP_DOWN_MS) fingerUp(a);  // the click's release
  if (gSimPower.poweredOff) {
    a.quit = true;
    a.quitReason = "the PMU powered the board off (battery at 0% without USB)";
  }

  a.wallUs += wallDeltaUs;
  // a minimized or hidden window is not drawn (nobody sees it); the firmware keeps running
  if (SDL_GetWindowFlags(a.window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) a.framesSkipped++;
  else render(a);
  refreshTitle(a);
}
