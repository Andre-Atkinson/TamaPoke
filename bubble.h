// Speech bubbles (#23): now and then the pet says what it needs, or something friendly.
// The idea comes from Mu-Muu/TamaPoke (MIT; pickBubble, bubbleTick and drawBubble in
// scene.ino), rewritten here without their network features.
//
// Pure logic (no hardware, no clock reads), tested on the PC (test/test_bubble.cpp):
//  - bubblePick(): which line. A low need always wins, with the same need and threshold
//    as the "!" bubble, the status line and the attention call (attnShown, ATTN_LOW), so
//    the four never disagree. Otherwise, at random, a morning or night line (only with a
//    clock that is set and not stale), a friendly line, or "I love you" (with the
//    player's name when one is set, owner.h).
//  - BubbleTimer: when. A bubble is due 1-2 min after the last one started. It waits
//    until the pet screen has been free and awake for BUBBLE_SETTLE_MS, then shows for
//    BUBBLE_SHOW_MS. With the default settings the screen dims after 90 s untouched (and
//    sooner with a screen-off setting), so a fixed "every 1-2 min" would mostly land on a
//    dim screen; this way waking the screen after a while gets a line within seconds.
//  - bubbleSplit() and bubbleBoxAt(): how it is laid out on the round screen.
// bubble.cpp holds the state and draws it; the sketch tells it when the screen is free.
#pragma once
#include <stdint.h>
#include "attention.h"

// The lines, in the order of their StrIds (S_SAY_HUNGRY...). The four needs come first,
// in AttnNeed order.
enum BubbleLine : uint8_t {
  BUB_HUNGRY = 0, BUB_BATH, BUB_TIRED, BUB_BORED,  // = BUB_HUNGRY + AttnNeed
  BUB_MORNING, BUB_STARS,
  BUB_HI, BUB_HAPPY, BUB_PAT,                      // the friendly lines
  BUB_LOVE_NAME,                                   // "I love you, %s!" (a name is set)
  BUB_LOVE,                                        // "I love you!" (no name)
  BUB_LINES
};
#define BUB_NONE 0xFF
#define BUB_FRIENDLY 3
// each line's name in the serial log ("say hungry") and in the simulator's "say" step
inline const char *const BUBBLE_KEYS[BUB_LINES] = {
  "hungry", "bath", "tired", "bored", "morning", "stars", "hi", "happy", "pat", "love-name", "love",
};

#define BUBBLE_SHOW_MS 4500UL      // how long one stays up
#define BUBBLE_SETTLE_MS 3000UL    // the screen free and awake this long before one starts
#define BUBBLE_GAP_MS 60000UL      // from one start to the next is this...
#define BUBBLE_GAP_SPAN_MS 60000UL // ...plus up to this much (random)

// The morning and night lines' hours. Night is the night scene's (render(): h < 6 or
// h >= 20), and with BubbleIn::starsOut "look at the stars" only shows when the stars are
// drawn (not under cloud, rain, snow or fog, #60).
#define BUBBLE_MORNING_FROM 6
#define BUBBLE_MORNING_TO 10  // exclusive
#define BUBBLE_NIGHT_FROM 20
#define BUBBLE_NIGHT_TO 6     // exclusive, the next morning

struct BubbleIn {
  AttnLevels needs;
  bool clockKnown;  // the clock is set and not stale (attnLoop's test)
  uint8_t hour;     // 0-23; only read when clockKnown
  bool starsOut = true;  // the night sky shows its stars (sceneStarsOut(), weather.h, #60)
  bool named;       // the player's name is set
  uint8_t last;     // the previous line, or BUB_NONE
};

// r: any random number. r % 100 picks the kind (about 30% a time line when one applies,
// 50% friendly, 20% love; 80% friendly when no time line applies) and r / 100 which
// friendly line. A need repeats while it lasts; any other line is not said twice in a row.
inline uint8_t bubblePick(const BubbleIn &in, uint32_t r) {
  uint8_t need = attnShown(in.needs);
  if (need < ATTN_NEEDS) return (uint8_t)(BUB_HUNGRY + need);
  uint32_t p = r % 100;
  uint8_t friendly = (uint8_t)(BUB_HI + (r / 100) % BUB_FRIENDLY);
  uint8_t line;
  if (in.clockKnown && p < 30 && in.hour >= BUBBLE_MORNING_FROM && in.hour < BUBBLE_MORNING_TO) line = BUB_MORNING;
  else if (in.clockKnown && in.starsOut && p < 30 && (in.hour >= BUBBLE_NIGHT_FROM || in.hour < BUBBLE_NIGHT_TO))
    line = BUB_STARS;
  else if (p < 80) line = friendly;
  else line = in.named ? BUB_LOVE_NAME : BUB_LOVE;
  if (line != in.last) return line;
  // the same line again: a friendly one instead, never the one just said
  if (line == friendly) return (uint8_t)(BUB_HI + (friendly - BUB_HI + 1) % BUB_FRIENDLY);
  return friendly;
}

// When a bubble shows. update() runs on every loop pass with `free`: the pet screen is
// up with nothing on or around the pet, awake (not dimmed, off or locked), not during an
// attention call's lit window, and the pet is awake (the sketch's bubbleFree()).
// No clock of its own: it only compares millis() stamps, so it adds no wakeup.
class BubbleTimer {
 public:
  // rnd() gives a random number; it is only called when a bubble starts, so the random
  // sequence the rest of the game sees moves only then.
  // Returns true on the pass a bubble starts: the caller picks its line then.
  // (Stamps are compared as now - then, so millis() wrapping is fine. Only a gap of more
  // than 49 days with no bubble at all could read as "not due yet", for one gap at most.)
  template <typename Rnd>
  bool update(uint32_t now, bool free, Rnd rnd) {
    if (!free) {
      free_ = false;
      on_ = false;  // a bubble that loses the screen is over; it does not come back
      return false;
    }
    if (!free_) {
      free_ = true;
      freeFrom_ = now;
    }
    if (on_) {
      if (now - startedAt_ < BUBBLE_SHOW_MS) return false;
      on_ = false;
    }
    bool due = !started_ || now - startedAt_ >= gap_;
    if (!due || now - freeFrom_ < BUBBLE_SETTLE_MS) return false;
    on_ = started_ = true;
    startedAt_ = now;
    gap_ = BUBBLE_GAP_MS + (uint32_t)rnd() % BUBBLE_GAP_SPAN_MS;
    return true;
  }
  bool showing() const { return on_; }
  void cancel() { on_ = false; }  // the line could not be laid out: nothing shows this time
  // The simulator's "say" step: a bubble starts now, as if it were due (the next update()
  // still ends it if the screen is not free).
  void start(uint32_t now) {
    on_ = started_ = true;
    startedAt_ = now;
    gap_ = BUBBLE_GAP_MS;
  }

 private:
  bool on_ = false, started_ = false, free_ = false;
  uint32_t freeFrom_ = 0, startedAt_ = 0, gap_ = 0;
};

// Text size 2: 16 px per line (the classic font is 8 px tall at x2).
#define BUBBLE_LINE_H 16
#define BUBBLE_PAD_X 12
#define BUBBLE_PAD_Y 6
#define BUBBLE_LINE_GAP 3
#define BUBBLE_Y_MIN 110  // the highest the box goes: the status line ends near y 106 (drawHeader: size 2 at y 90)
#define BUBBLE_TAIL 8     // how far the tail reaches below the box
#define BUBBLE_GAP 36     // from the pet's centre to the near edge, as the "!" bubble (attnBubbleAt)
#define BUBBLE_RADIUS 12  // corner radius
#define BUBBLE_W_MIN 48   // the tail's 14 px base needs a straight stretch between the corners
// Inside the round panel at BUBBLE_Y_MIN with a 6 px margin, corners included: x 37..429.
// That is the narrowest the panel gets where the box can be: lower down (to y 356, and the
// tail stops at BUBBLE_TIP_LOW 250) it is wider. A wider box would reach outside the
// circle (test_bubble.cpp checks the corners).
#define BUBBLE_X_MIN 37
#define BUBBLE_X_MAX 429  // exclusive right edge
// The clock badge (clockprompt.h: centre x 76, radius 24) when it asks for the time: the
// bubble stays right of it so it can still be seen and tapped.
#define BUBBLE_X_MIN_BADGE 106

// Widest text line for the box's limits: 368 px (30 Latin characters) or 299 with the badge.
inline int bubbleTextMax(bool badge) {
  return (BUBBLE_X_MAX - (badge ? BUBBLE_X_MIN_BADGE : BUBBLE_X_MIN)) - 2 * BUBBLE_PAD_X;
}

// Where to wrap `s` (`n` bytes, no terminator needed) so each line is at most maxW px:
// -1 for one line, the index of the space that starts the second line, or -2 when not even
// two lines fit. Of the spaces that work, the one whose longer line is shortest.
// measure(s, len) is the pixel width of the first len bytes of s.
template <typename Measure>
inline int bubbleSplit(const char *s, int n, int maxW, Measure measure) {
  if (measure(s, n) <= maxW) return -1;
  int best = -2, bestW = 0;
  for (int i = 1; i < n - 1; i++) {
    if (s[i] != ' ') continue;
    int w1 = measure(s, i), w2 = measure(s + i + 1, n - i - 1);
    int w = w1 > w2 ? w1 : w2;
    if (w <= maxW && (best == -2 || w < bestW)) {
      best = i;
      bestW = w;
    }
  }
  return best;
}

struct BubbleBox {
  int16_t x, y, w, h;
  int16_t tailX0, tailX1;  // the tail's base on the bottom edge (x0 < x1)
  int16_t tipX, tipY;      // its tip, pointing at the pet's head
};

// The box for text `textW` px wide in `lines` lines (1 or 2), with the pet at petX and the
// top of its head at headTop. Beside the head on the side with more room (as the "!"
// bubble), its tail pointing down at the head (bubbleTipY in attention.h; `button`: the
// evolve, farewell or run-away button is over the pet), its top no higher than
// BUBBLE_Y_MIN, slid toward the pet as far as it must to stay inside the round panel and
// right of the clock badge (`badge`).
inline BubbleBox bubbleBoxAt(int petX, int headTop, int textW, int lines, bool badge, bool button) {
  if (petX < 150) petX = 150;  // the walk range (scene.cpp: PET_WALK_MIN..PET_WALK_MAX)
  if (petX > 325) petX = 325;
  int xMin = badge ? BUBBLE_X_MIN_BADGE : BUBBLE_X_MIN;
  BubbleBox b;
  b.w = (int16_t)(textW + 2 * BUBBLE_PAD_X);
  if (b.w < BUBBLE_W_MIN) b.w = BUBBLE_W_MIN;  // the text is centred in it
  if (b.w > BUBBLE_X_MAX - xMin) b.w = (int16_t)(BUBBLE_X_MAX - xMin);
  b.h = (int16_t)(2 * BUBBLE_PAD_Y + lines * BUBBLE_LINE_H + (lines - 1) * BUBBLE_LINE_GAP);
  int y = bubbleTipY(headTop, button) - BUBBLE_TAIL - b.h;
  if (y < BUBBLE_Y_MIN) y = BUBBLE_Y_MIN;
  b.y = (int16_t)y;
  bool right = petX <= 233;  // the screen's centre column
  int x = right ? petX + BUBBLE_GAP : petX - BUBBLE_GAP - b.w;
  if (x + b.w > BUBBLE_X_MAX) x = BUBBLE_X_MAX - b.w;
  if (x < xMin) x = xMin;
  b.x = (int16_t)x;
  // the tip just beside the pet's centre, on the bubble's side; the base 14 px wide, kept
  // on the straight part of the bottom edge
  int tip = petX + (right ? 14 : -14);
  if (tip < x + 4) tip = x + 4;
  if (tip > x + b.w - 4) tip = x + b.w - 4;
  int base = tip + (right ? 10 : -10);
  if (base < x + BUBBLE_RADIUS + 7) base = x + BUBBLE_RADIUS + 7;
  if (base > x + b.w - BUBBLE_RADIUS - 7) base = x + b.w - BUBBLE_RADIUS - 7;
  b.tailX0 = (int16_t)(base - 7);
  b.tailX1 = (int16_t)(base + 7);
  b.tipX = (int16_t)tip;
  b.tipY = (int16_t)(b.y + b.h + BUBBLE_TAIL);
  return b;
}

// ---- the sketch's side (bubble.cpp) ----
// every loop pass, after attnLoop() and updateBrightness() (dimStage is then this pass's),
// before render(); free: the sketch's bubbleFree()
void bubbleLoop(uint32_t now, bool free);
bool bubbleShowing();                      // a bubble is up: the "!" bubble steps aside (drawAttnBubble)
void drawSpeechBubble();                   // render(), on the pet screen, after the "!" bubble
#if HOST_SIM
void bubbleSayNow(uint8_t line);           // the simulator's "say" step (sim/script.h)
#endif
