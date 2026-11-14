#pragma once
#include <stdint.h>
#include "bedtime.h"

// Attention calls (#9). Pure logic (no hardware, no clock reads) so it can be tested on
// the PC (test/test_attention.cpp). The sketch samples the needs on every loop pass,
// right after Pet::update(): the loop keeps running with the screen off (it light-sleeps
// at most POWER_SLEEP_MS between passes), so a need that drops in Pet::tick() is seen on
// the same pass, with no extra wakeup.
//
// When a need first drops below ATTN_LOW it "calls" once: the screen lights (or brightens)
// for ATTN_PEEK_MS without counting as a touch, one soft chirp plays, and a "!" bubble with
// the need's icon shows next to the pet. The same need can call again only after it is back at
// ATTN_REARM or more, so a small top-up (a +5 cuddle from 24 to 29) cannot make it call
// twice in a few minutes. The bubble itself is not tied to the call: it shows whenever a
// need is low (attnShown), silently.

// The needs in the order the status line names them (statusMsg() in TamaPoke.ino: food,
// hygiene, energy, joy). When several are low the bubble shows the first, so it agrees
// with the text above it.
enum AttnNeed : uint8_t { ATTN_FOOD = 0, ATTN_HYGIENE, ATTN_ENERGY, ATTN_JOY, ATTN_NEEDS };
#define ATTN_NONE 0xFF

#define ATTN_LOW 25           // below this a need is low: red bar, status text, sad pet, call
                              // (Pet::mood() in pet.cpp has its own 25: keep them in step)
#define ATTN_REARM 40         // a need that called may call again once it is back to this
#define ATTN_PEEK_MS 15000UL  // a call lights a dark screen, or brightens a dim one, for this long

struct AttnLevels { uint8_t v[ATTN_NEEDS]; };

// Pet field order (fullness, joy, energy, hygiene) in, status-line order out.
inline AttnLevels attnLevels(uint8_t food, uint8_t joy, uint8_t energy, uint8_t hygiene) {
  AttnLevels l;
  l.v[ATTN_FOOD] = food;
  l.v[ATTN_HYGIENE] = hygiene;
  l.v[ATTN_ENERGY] = energy;
  l.v[ATTN_JOY] = joy;
  return l;
}

// The need the bubble shows: the first low one in status-line order, or ATTN_NONE.
inline uint8_t attnShown(const AttnLevels &l) {
  for (uint8_t i = 0; i < ATTN_NEEDS; i++)
    if (l.v[i] < ATTN_LOW) return i;
  return ATTN_NONE;
}

// Turns successive samples into calls (one bit per need, bit i = AttnNeed i).
//
//  - First sample (boot): a need that is already low counts as having called; it calls
//    again only after recovering to ATTN_REARM. Someone just switched the board on and
//    is looking at it.
//  - A need that drops below ATTN_LOW while armed disarms and its call starts waiting.
//    It stays waiting while the sketch can't deliver it yet (a minigame open, say) and
//    is dropped if the need gets back to ATTN_LOW first.
//  - `away` (asleep, egg, choosing a starter or egg, a farewell or run-away): a drop
//    disarms the need without a call and anything waiting is dropped.
//  - done(): the waiting calls went out, or were spent silently (quiet hours, pocket lock).
class AttnArm {
 public:
  uint8_t update(const AttnLevels &l, bool away) {
    for (uint8_t i = 0; i < ATTN_NEEDS; i++) {
      uint8_t bit = (uint8_t)(1u << i);
      bool low = l.v[i] < ATTN_LOW;
      if (!have_) {
        if (!low) armed_ |= bit;  // armed_ starts at 0: a need already low stays unarmed
        continue;
      }
      if (low) {
        if (armed_ & bit) {
          armed_ &= (uint8_t)~bit;
          waiting_ |= bit;  // cleared below if away
        }
      } else {
        waiting_ &= (uint8_t)~bit;
        if (l.v[i] >= ATTN_REARM) armed_ |= bit;
      }
    }
    have_ = true;
    if (away) waiting_ = 0;
    return waiting_;
  }
  uint8_t waiting() const { return waiting_; }
  void done() { waiting_ = 0; }

 private:
  bool have_ = false;
  uint8_t armed_ = 0, waiting_ = 0;
};

// Quiet hours: the bedtime window if one is set, otherwise 22:00-07:00 (the same window
// as bedtime preset 2). A clock that isn't known (no clock, or stale after a full
// power-off until someone sets it) counts as quiet: better a silent call than a chirp
// and a lit panel at 3 am.
#define ATTN_NIGHT_PRESET 2
inline bool attnQuiet(uint8_t bedPreset, bool clockKnown, uint16_t minuteOfDay) {
  if (!clockKnown) return true;
  uint8_t p = (bedPreset >= 1 && bedPreset < BED_PRESETS) ? bedPreset : (uint8_t)ATTN_NIGHT_PRESET;
  return isBedtime(p, minuteOfDay);
}

// How the screen is when a call is ready to go out.
enum AttnScreen : uint8_t {
  ATTN_SCR_LIT = 0,     // on, possibly dimmed
  ATTN_SCR_LIT_IDLE,    // on but almost off (DIM_OFF: untouched for 5 min, screen-off set to
                        // never): whatever is open was left behind, as with a timeout
  ATTN_SCR_TIMED_OUT,   // turned off by the screen-off timer
  ATTN_SCR_PWR_OFF,     // turned off with a short PWR press: she wants it dark
  ATTN_SCR_POCKET,      // 1.75C triple-press pocket lock: nothing gets through
};

// The screen's state for AttnScene::screen. The pocket lock wins over everything (the
// auto-off can set screenOff while it is on); a dark screen is PWR-off or timed out; a lit
// one is LIT_IDLE once it is almost off (`almostOff`: dimStageNow() == DIM_OFF).
inline uint8_t attnScreen(bool pocketLocked, bool screenOff, bool offByPwr, bool almostOff) {
  if (pocketLocked) return ATTN_SCR_POCKET;
  if (screenOff) return offByPwr ? ATTN_SCR_PWR_OFF : ATTN_SCR_TIMED_OUT;
  return almostOff ? ATTN_SCR_LIT_IDLE : ATTN_SCR_LIT;
}

struct AttnScene {
  bool busy;        // evolving, eating, bathing, or a dialog open: wait, whatever the screen
  bool playing;     // a minigame or the training bag is open
  bool browsing;    // the keyboard, clock/settings, gallery or stat card is open
  uint8_t screen;   // AttnScreen
  bool quiet;       // attnQuiet()
};

struct AttnAction {
  bool wait;        // keep the call waiting; ask again on a later pass
  bool chirp;       // one SFX_CALL (silent anyway if the sound is off)
  bool brighten;    // lit screen: full brightness for ATTN_PEEK_MS
  bool peek;        // timed-out screen: light it for ATTN_PEEK_MS; it stays logically off
                    // (screenOff), so the auto touch lock, if on, keeps holding
  // Both open the sketch's "showing" window (attnShowUntil): full brightness, no auto-off
  // and no dimming until it ends, and no touch is recorded (lastInteract is left alone), so
  // idle-based features (self-care while charging, #24) keep counting from her last real touch.
  bool closeViews;  // go back to the pet screen first (drops an unsaved clock edit or a
                    // half-typed name, both left behind when the screen timed out)
};

// With nothing set (no wait, chirp or light) the call is spent silently: the bubble is
// there when she next looks.
inline AttnAction attnDecide(const AttnScene &s) {
  AttnAction a = {};
  bool lit = s.screen == ATTN_SCR_LIT || s.screen == ATTN_SCR_LIT_IDLE;
  if (s.busy || (lit && s.playing) || (s.screen == ATTN_SCR_LIT && s.browsing)) {
    a.wait = true;
    return a;
  }
  if (s.quiet || s.screen == ATTN_SCR_POCKET) return a;
  a.chirp = true;
  if (lit) {
    a.brighten = true;
    a.closeViews = s.screen == ATTN_SCR_LIT_IDLE && s.browsing;
  } else if (s.screen == ATTN_SCR_TIMED_OUT && !s.playing) {
    // a minigame or the bag can't time out (autoScreenOff), so `playing` here would be a
    // state the sketch never reaches; it stays dark rather than showing a game
    a.peek = true;
    a.closeViews = s.browsing;
  }
  return a;
}

// Where the bubble goes: beside the pet's head, on the side with more room, clear of the
// status line above (ends near y 106) and of the evolve/farewell/run-away buttons below
// (the evolve button's top reaches y 167 at the biggest pulse: EVO_BTN_Y 172 - 5). The tail points down at the pet.
#define ATTN_BUBBLE_W 72
#define ATTN_BUBBLE_H 44
#define ATTN_BUBBLE_Y 112
#define ATTN_BUBBLE_TAIL 10  // how far the tail reaches below the bubble
#define ATTN_BUBBLE_GAP 36   // from the pet's centre to the near edge of the bubble

struct AttnBubble {
  int16_t x, y, w, h;
  bool right;  // the bubble is right of the pet (tail at its bottom-left)
};

inline AttnBubble attnBubbleAt(int petX) {
  if (petX < 150) petX = 150;  // the walk range (behNext: 150..325)
  if (petX > 325) petX = 325;
  AttnBubble b;
  b.right = petX <= 233;  // the screen's centre column
  b.w = ATTN_BUBBLE_W;
  b.h = ATTN_BUBBLE_H;
  b.y = ATTN_BUBBLE_Y;
  b.x = (int16_t)(b.right ? petX + ATTN_BUBBLE_GAP : petX - ATTN_BUBBLE_GAP - ATTN_BUBBLE_W);
  return b;
}
