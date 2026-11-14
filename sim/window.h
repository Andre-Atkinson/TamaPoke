// The interactive simulator window: SDL2 front end over the same fake board as the
// scripted runner (simhw.h). The window shows the panel, turns the mouse into the
// finger and the keys into the PWR and BOOT buttons, and runs the firmware in step
// with the wall clock (the testable parts are in interactive.h).
//
// The main loop (main_window.cpp) and --selftest drive the same winFrame() and
// winHandleEvent(), so the selftest exercises the real event path.
#pragma once
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdint.h>
#include <string>
#include "interactive.h"

struct WinOptions {
  std::string board;            // "1.75C" or "1.75", for the title
  std::string out = "shots";    // where S saves screenshots
  bool square = false;          // no round glass: the whole panel square
  bool quiet = false;
};

struct WinApp {
  WinOptions opt;
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *panelTex = nullptr;   // the panel, streamed every frame
  SDL_Texture *bezelTex = nullptr;   // dark bezel with a round hole, over the panel
  SimPace pace;
  SimLineSplitter stdinLines;
  bool stdinOpen = true;

  bool quit = false;
  std::string quitReason;            // why the firmware stopped, if it did (printed by main)

  // input state
  bool fingerDown = false;           // a mouse press that started on the glass is held
  bool releasePending = false;       // the button is up but the finger stays down until it has been down SIM_TAP_DOWN_MS
  bool touchStarted = false;         // a press became the finger during this event batch
  uint64_t downAtMs = 0;             // simulated time (mockMillis64) the finger went down
  bool bootHeld = false;             // B is held
  bool inputThisFrame = false;       // a touch or button event came in since the last frame

  // bookkeeping the selftest and the title read
  uint32_t framesRendered = 0;
  int shotCounter = 1;
  int shotsSaved = 0;
  std::string lastShot;
  uint64_t wallUs = 0;               // wall time seen by winFrame()
  uint64_t titleDueUs = 0;
  uint32_t framesSkipped = 0;        // frames not drawn because the window was minimized or hidden
  bool noBudget = false;             // selftest: no wall budget per frame (a busy host must not change the result)
};

// SDL, the window, the renderer (accelerated, else software) and the textures.
bool winCreate(WinApp &a, bool vsync, std::string &err);
void winDestroy(WinApp &a);

// One SDL event: mouse = finger, keys = buttons and controls. The only place input is handled.
// A click is not lost between two reads: winFrame() stops reading events after one that
// puts the finger down (the firmware runs with that contact first, then the rest of the
// batch is handled), and a release waits until the finger has been down SIM_TAP_DOWN_MS of
// simulated time.
void winHandleEvent(WinApp &a, const SDL_Event &e);

// One frame: queued events, stdin lines (readStdin), the firmware for the wall time that
// passed (see simPaceFrame), the panel drawn, the title every half second.
void winFrame(WinApp &a, uint32_t wallDeltaUs, bool readStdin);

void winPrintHelp();
