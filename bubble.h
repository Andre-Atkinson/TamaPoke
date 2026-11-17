// Speech bubbles (#23): now and then the pet says what it needs, or something friendly.
// The idea comes from Mu-Muu/TamaPoke (MIT; pickBubble, bubbleTick and drawBubble in
// scene.ino), rewritten here without their network features.
//
// Pure logic (no hardware, no clock reads), tested on the PC (test/test_bubble.cpp):
//  - bubblePick(): which line. A special day's greeting that is due comes first (#26,
//    holiday.h). Then a low need always wins, with the same need and threshold
//    as the "!" bubble, the status line and the attention call (attnShown, ATTN_LOW), so
//    the four never disagree. Then, in the evening, now and then a hint at warm milk (#13,
//    MilkHint). Otherwise, at random, a morning or night line (only with a clock that is set
//    and not stale), a line of the pet's nature (#86), a friendly line, or "I love you" (with
//    the player's name when one is set, owner.h). Right after a hatch the nature's first line,
//    which names it as the stat card does, comes before the warm-milk hint and the rest.
//  - bubbleChoose(): bubblePick() plus the greeting's rules (a few a day, never in quiet hours)
//    and the hatch line's.
//  - HatchLine: whether a pet that has just hatched still has to name its nature (#86).
//  - BubbleTimer: when. A bubble is due 1-2 min after the last one started. It waits
//    until the pet screen has been free and awake for BUBBLE_SETTLE_MS, then shows for
//    BUBBLE_SHOW_MS. With the default settings the screen dims after 90 s untouched (and
//    sooner with a screen-off setting), so a fixed "every 1-2 min" would mostly land on a
//    dim screen; this way waking the screen after a while gets a line within seconds.
//  - bubbleSplit() and bubbleBoxAt(): how it is laid out on the round screen.
// bubble.cpp holds the state and draws it; the sketch tells it when the screen is free.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "attention.h"
#include "holiday.h"
#include "nature.h"
#include "playarea.h"   // PET_WALK_MIN..MAX, PLAY_MID_X (#66)
#include "skylayout.h"  // skyNightHour, SKY_NIGHT_TO (#66)

#define BUB_NAT_LINES 3  // each nature's lines (#86)

// The lines, in the order of their StrIds (S_SAY_HUNGRY...). The four needs come first,
// in AttnNeed order.
enum BubbleLine : uint8_t {
  BUB_HUNGRY = 0, BUB_BATH, BUB_TIRED, BUB_BORED,  // = BUB_HUNGRY + AttnNeed
  BUB_MORNING, BUB_STARS,
  BUB_MILK,                                        // "Sleepy... warm milk?" (#13, MilkHint)
  BUB_HI, BUB_HAPPY, BUB_PAT,                      // the friendly lines
  BUB_LOVE_NAME,                                   // "I love you, %s!" (a name is set)
  BUB_LOVE,                                        // "I love you!" (no name)
  // the natures' lines (#86, nature.h): BUB_NAT_LINES for each nature, in NAT_* order
  // (bubbleNatureLine). The first names the nature as the stat card does ("I'm playful!"),
  // the others show it. None asks for care: only a low need's line does.
  BUB_NAT_FIRST,
  // the greetings of the special days (#26, holiday.h), in Greet order: "Merry Christmas,
  // <name>!" (or without the name when none is set)
  BUB_CHRISTMAS = BUB_NAT_FIRST + NAT_COUNT * BUB_NAT_LINES, BUB_NEW_YEAR, BUB_VALENTINE, BUB_EASTER, BUB_HALLOWEEN, BUB_BIRTHDAY,
  BUB_LINES
};
#define BUB_GREET_FIRST BUB_CHRISTMAS  // = BUB_GREET_FIRST + Greet - GREET_CHRISTMAS

// Line i (0 names the nature, 1 and 2 show it; taken modulo BUB_NAT_LINES) of `nature`; a
// nature out of range says easygoing's, as Pet::load() reads it.
constexpr uint8_t bubbleNatureLine(uint8_t nature, uint8_t i) {
  return (uint8_t)(BUB_NAT_FIRST + (nature < NAT_COUNT ? nature : (uint8_t)NAT_EASYGOING) * BUB_NAT_LINES +
                   i % BUB_NAT_LINES);
}
constexpr bool bubbleIsNatureLine(uint8_t line) { return line >= BUB_NAT_FIRST && line < BUB_GREET_FIRST; }

// The string a line says, as its offset from S_SAY_HUNGRY in i18n.h (bubble.cpp checks the
// order): one string a line up to the natures' last (S_SAY_LAZY_3), then two for each greeting, with the player's
// name and without (S_SAY_CHRISTMAS_FMT, S_SAY_CHRISTMAS...).
constexpr int bubbleStrOffset(uint8_t line, bool named) {
  if (line >= BUB_GREET_FIRST && line < BUB_LINES) return BUB_GREET_FIRST + 2 * (line - BUB_GREET_FIRST) + (named ? 0 : 1);
  return line;
}
// Does that string take the name (a "%s")? The love line as picked, a greeting when a name is set.
constexpr bool bubbleTakesName(uint8_t line, bool named) {
  return line == BUB_LOVE_NAME || (named && line >= BUB_GREET_FIRST && line < BUB_LINES);
}
#define BUB_NONE 0xFF
#define BUB_FRIENDLY 3
// each line's name in the serial log ("say hungry") and in the simulator's "say" step
inline const char *const BUBBLE_KEYS[BUB_LINES] = {
  "hungry", "bath", "tired", "bored", "morning", "stars", "milk", "hi", "happy", "pat", "love-name", "love",
  "easygoing-1", "easygoing-2", "easygoing-3", "greedy-1", "greedy-2", "greedy-3",
  "playful-1", "playful-2", "playful-3", "tidy-1", "tidy-2", "tidy-3", "lazy-1", "lazy-2", "lazy-3",
  "christmas", "newyear", "valentine", "easter", "halloween", "birthday",
};

#define BUBBLE_SHOW_MS 4500UL      // how long one stays up
#define BUBBLE_SETTLE_MS 3000UL    // the screen free and awake this long before one starts
#define BUBBLE_GAP_MS 60000UL      // from one start to the next is this...
#define BUBBLE_GAP_SPAN_MS 60000UL // ...plus up to this much (random)

// The morning and night lines' hours. Night is the night scene's (skyNightHour, skylayout.h),
// and with BubbleIn::starsOut "look at the stars" only shows when the stars are drawn (not
// under cloud, rain, snow or fog, #60). Morning starts as the night ends.
#define BUBBLE_MORNING_FROM SKY_NIGHT_TO
#define BUBBLE_MORNING_TO 10  // exclusive
// The share of the ordinary picks (r % 100) a nature's line takes (#86): 20 %, always,
// right after the time lines' 30 so it never takes their place.
#define BUBBLE_NAT_FROM 30
#define BUBBLE_NAT_TO 50  // exclusive

struct BubbleIn {
  AttnLevels needs;
  bool clockKnown;  // the clock is set and not stale (attnLoop's test)
  uint8_t hour;     // 0-23; only read when clockKnown
  bool starsOut = true;  // the night sky shows its stars (sceneStarsOut(), weather.h, #60)
  bool named;       // the player's name is set
  uint8_t last;     // the previous line, or BUB_NONE
  uint8_t greet = BUB_NONE;  // a special day's greeting that is due (HolidayGreeter, holiday.h), or BUB_NONE
  bool milkHint = false;  // the warm-milk hint is due (MilkHint::due, #13)
  uint8_t nature = NAT_EASYGOING;  // the pet's (Pet::nature, #86); out of range counts as easygoing
  bool hatchLine = false;  // the pet just hatched: the nature's first line is due (#86)
};

// r: any random number. A special day's greeting that is due comes first, even before a
// need: it is rare (holiday.h) and the need's "!" bubble is back as soon as it ends.
// Then a low need, then right after a hatch the line that names the pet's nature (#86), then
// the warm-milk hint when it is due (#13). Otherwise r % 100 picks the kind: 30% a time line
// when one applies (r % 100 under 30), 20% a line of the pet's nature (30-49), 20% love
// (80-99), and the rest friendly (30% with a time line, 60% without); r / 100 picks which
// friendly or nature line. A need repeats while it lasts; any other line is not said twice in
// a row (the hatch line and the milk hint either: then they wait for the next bubble, and
// are only counted once said).
inline uint8_t bubblePick(const BubbleIn &in, uint32_t r) {
  if (in.greet >= BUB_GREET_FIRST && in.greet < BUB_LINES) return in.greet;
  uint8_t need = attnShown(in.needs);
  if (need < ATTN_NEEDS) return (uint8_t)(BUB_HUNGRY + need);
  uint8_t naming = bubbleNatureLine(in.nature, 0);
  if (in.hatchLine && in.last != naming) return naming;
  if (in.milkHint && in.last != BUB_MILK) return BUB_MILK;
  uint32_t p = r % 100;
  uint8_t which = (uint8_t)((r / 100) % BUB_FRIENDLY);  // BUB_FRIENDLY == BUB_NAT_LINES
  uint8_t friendly = (uint8_t)(BUB_HI + which);
  uint8_t line;
  if (in.clockKnown && p < 30 && in.hour >= BUBBLE_MORNING_FROM && in.hour < BUBBLE_MORNING_TO) line = BUB_MORNING;
  else if (in.clockKnown && in.starsOut && p < 30 && skyNightHour(in.hour))
    line = BUB_STARS;
  else if (p >= BUBBLE_NAT_FROM && p < BUBBLE_NAT_TO) line = bubbleNatureLine(in.nature, which);
  else if (p < 80) line = friendly;
  else line = in.named ? BUB_LOVE_NAME : BUB_LOVE;
  if (line != in.last) return line;
  // the same line again: another of its kind, never the one just said
  if (line == friendly) return (uint8_t)(BUB_HI + (which + 1) % BUB_FRIENDLY);
  if (bubbleIsNatureLine(line)) return bubbleNatureLine(in.nature, (uint8_t)(which + 1));
  return friendly;
}
static_assert(BUB_FRIENDLY == BUB_NAT_LINES, "bubblePick() picks a friendly or nature line with the same index");

// The warm-milk hint (#13): now and then in the evening the pet asks for warm milk, which the
// moon button's strip gives. The evening runs from MILK_HINT_FROM until quiet hours begin
// (attnQuietAt: the bedtime window, or 22:00 with bedtime off; every window runs past
// midnight, so an evening never does), so never in quiet hours, and never asleep: no bubble
// shows then. At most MILK_HINT_MAX an evening, MILK_HINT_GAP_S apart, and none once the pet
// had its milk that evening. Each bubble that comes due in the evening is the hint 1 time in
// MILK_HINT_ODDS (a low need still comes first). Counted in RAM by the evening's local date:
// a restart may hint again.
#define MILK_HINT_FROM 19          // 19:00
#define MILK_HINT_MAX 2
#define MILK_HINT_GAP_S (45UL * 60)
#define MILK_HINT_ODDS 3

struct MilkHint {
  // May the bubble that is due now be the hint? `clock`: the local epoch from a clock that is
  // set and not stale, 0 otherwise (no hint); `quiet`: quiet hours now. rnd() gives a random
  // number; it is only called when everything else allows the hint, so outside the evening
  // the random sequence the rest of the game sees doesn't move.
  template <typename Rnd>
  bool due(uint32_t clock, bool quiet, Rnd rnd) {
    if (!evening(clock) || quiet) return false;
    roll(clock);
    if (count_ >= MILK_HINT_MAX || (count_ && clock - lastAt_ < MILK_HINT_GAP_S)) return false;
    return rnd() % MILK_HINT_ODDS == 0;
  }
  void said(uint32_t clock) {  // the hint was shown
    roll(clock);
    count_++;
    lastAt_ = clock;
  }
  void milkGiven(uint32_t clock) {  // the pet had its milk: no more hints this evening
    if (!evening(clock)) return;    // a nap's milk earlier in the day doesn't count
    roll(clock);
    count_ = MILK_HINT_MAX;
  }

 private:
  uint32_t night_ = 0;  // the evening counted, as its local date (epoch / 86400)
  uint8_t count_ = 0;   // hints said that evening (MILK_HINT_MAX once the milk was given)
  uint32_t lastAt_ = 0; // when the last one was said (epoch)
  static bool evening(uint32_t clock) { return clock && (clock / 3600) % 24 >= MILK_HINT_FROM; }
  void roll(uint32_t clock) {
    if (clock / 86400 == night_) return;
    night_ = clock / 86400;
    count_ = 0;
  }
};

// The line after a hatch (#86): due from the first pass that sees a hatched pet after an
// egg until its nature's first line is said (BubbleIn::hatchLine). everyPass() runs on every
// loop pass, free screen or not: an egg is never free (petSceneFree), so the pet is always
// seen as an egg first. In RAM: an egg that hatches while the board is off (Pet::syncClock)
// is never seen, and its pet names its nature only when that line comes up by chance, as
// every nature line does now and then.
struct HatchLine {
  // True on the pass the pet is first seen hatched: the caller forgets the last line said
  // (BubbleIn::last), which was the previous pet's.
  bool watch(bool egg) {
    if (egg) {
      egg_ = true;
      due_ = false;  // a pet that left before saying it: the next one says its own
      return false;
    }
    if (!egg_) return false;
    egg_ = false;
    due_ = true;
    return true;
  }
  // What bubbleLoop() calls on every pass: watch(), and on the pass the pet is first seen
  // hatched it forgets `last` (the last line said, BubbleIn::last), which was the previous
  // pet's. Otherwise a new pet whose naming line was the previous pet's last line would wait
  // a bubble for it.
  void everyPass(bool egg, uint8_t &last) {
    if (watch(egg)) last = BUB_NONE;
  }
  bool due() const { return due_; }
  // a bubble showed `line`: the nature's first line, by the hatch or by chance, ends it
  void said(uint8_t line, uint8_t nature) {
    if (line == bubbleNatureLine(nature, 0)) due_ = false;
  }

 private:
  bool egg_ = false, due_ = false;
};

// The line for a bubble that starts now: bubblePick() with a special day's greeting that is
// due (HolidayGreeter `g`: `clock` the scene's trusted clock or 0, `quiet` quiet hours now,
// `greet` today's, holidayNow().greet) and the line that names a new pet's nature (HatchLine
// `h`, #86). Counts the greeting in `g` when that is the line, so at most HOLIDAY_GREETS come
// a day and none in quiet hours, and ends the hatch line in `h` when the pet's first nature
// line is what comes up. `in.greet` and `in.hatchLine` are set here. Both are counted before
// the caller lays the line out: test_bubble.cpp proves every line can be.
inline uint8_t bubbleChoose(BubbleIn in, uint32_t r, HolidayGreeter &g, HatchLine &h, uint32_t clock, bool quiet,
                            uint8_t greet) {
  in.greet = g.due(clock, quiet, greet) ? (uint8_t)(BUB_GREET_FIRST + greet - GREET_CHRISTMAS) : (uint8_t)BUB_NONE;
  in.hatchLine = h.due();
  uint8_t line = bubblePick(in, r);
  if (in.greet != BUB_NONE && line == in.greet) g.said(clock);
  h.said(line, in.nature);
  return line;
}

// What `line` says, with the player's `name` ("" for none): "I love you, <name>!" and a
// greeting with the name when one is set (bubbleTakesName). text(offset) is the string at
// S_SAY_HUNGRY + offset (i18n.h, not included here); bubble.cpp's compose() and the tests
// share this.
template <typename Text>
inline void bubbleFormat(char *say, size_t n, uint8_t line, const char *name, Text text) {
  bool named = name[0] != 0;
  const char *fmt = text(bubbleStrOffset(line, named));
  if (bubbleTakesName(line, named)) snprintf(say, n, fmt, name);
  else snprintf(say, n, "%s", fmt);
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
  if (petX < PET_WALK_MIN) petX = PET_WALK_MIN;  // the walk range (playarea.h)
  if (petX > PET_WALK_MAX) petX = PET_WALK_MAX;
  int xMin = badge ? BUBBLE_X_MIN_BADGE : BUBBLE_X_MIN;
  BubbleBox b;
  b.w = (int16_t)(textW + 2 * BUBBLE_PAD_X);
  if (b.w < BUBBLE_W_MIN) b.w = BUBBLE_W_MIN;  // the text is centred in it
  if (b.w > BUBBLE_X_MAX - xMin) b.w = (int16_t)(BUBBLE_X_MAX - xMin);
  b.h = (int16_t)(2 * BUBBLE_PAD_Y + lines * BUBBLE_LINE_H + (lines - 1) * BUBBLE_LINE_GAP);
  int y = bubbleTipY(headTop, button) - BUBBLE_TAIL - b.h;
  if (y < BUBBLE_Y_MIN) y = BUBBLE_Y_MIN;
  b.y = (int16_t)y;
  bool right = petX <= PLAY_MID_X;  // the screen's centre column
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
void bubbleMilkGiven();                    // the pet had warm milk (#13): no more hints tonight
void drawSpeechBubble();                   // render(), on the pet screen, after the "!" bubble
#if HOST_SIM
void bubbleSayNow(uint8_t line);           // the simulator's "say" step (sim/script.h)
#endif
