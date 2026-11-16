// Speech bubbles (bubble.h, #23): the bubble's state, its text and its drawing.
#include <Arduino.h>
#include <string.h>
#include "app.h"
#include "attention.h"
#include "bubble.h"
#include "holiday.h"
#include "i18n.h"
#include "owner.h"
#include "scene.h"
#include "uitext.h"

static_assert((int)S_SAY_HUNGRY + (int)BUB_LOVE_NAME == (int)S_SAY_LOVE_FMT &&
                  (int)S_SAY_HUNGRY + (int)BUB_LOVE == (int)S_SAY_LOVE,
              "the S_SAY_* strings follow BubbleLine");
// each greeting has two strings, with the name and without, in BubbleLine order (#26):
// bubbleStrOffset() finds them
static_assert((int)S_SAY_HUNGRY + bubbleStrOffset(BUB_CHRISTMAS, true) == (int)S_SAY_CHRISTMAS_FMT &&
                  (int)S_SAY_HUNGRY + bubbleStrOffset(BUB_CHRISTMAS, false) == (int)S_SAY_CHRISTMAS &&
                  (int)S_SAY_HUNGRY + bubbleStrOffset(BUB_BIRTHDAY, true) == (int)S_SAY_BIRTHDAY_FMT &&
                  (int)S_SAY_HUNGRY + bubbleStrOffset(BUB_BIRTHDAY, false) == (int)S_SAY_BIRTHDAY,
              "the greeting strings follow BubbleLine");
static_assert((int)BUB_GREET_FIRST + (int)GREET_BIRTHDAY - (int)GREET_CHRISTMAS == (int)BUB_BIRTHDAY &&
                  (int)BUB_BIRTHDAY + 1 == (int)BUB_LINES,
              "the greeting lines follow Greet");

static BubbleTimer bubbleTimer;
static HolidayGreeter greeter;  // how many greetings today, and when the last (holiday.h, #26)
static uint8_t bubbleLine = BUB_NONE;  // the line showing, or the last one said
// The line in the active font's encoding. Two lines are split at a space, which becomes
// the first line's terminator: line 2 starts at bubbleText + bubbleBreak + 1.
static char bubbleText[48];
static int bubbleBreak = -1;  // -1: one line
static uint16_t bubbleW1 = 0, bubbleW2 = 0;  // each line's width in px

static bool badgeShowing() { return clockPrompt.asking(bedEdge.stale()); }

// Width of the first `len` bytes of s at size 2.
static int measure(const char *s, int len) {
  char t[sizeof bubbleText];
  if (len >= (int)sizeof t) len = (int)sizeof t - 1;
  memcpy(t, s, len);
  t[len] = 0;
  return (int)textW(t, 2);
}

// Makes the text of `line` and lays it out; false if it does not fit in two lines.
static bool compose(uint8_t line) {
  char say[48];
  char name[OWNER_NAME_MAX + 1];
  ownerNameForScreen(name, sizeof name);
  // "I love you, <name>!" and a greeting with the name when one is set (bubbleTakesName)
  const char *fmt = T((StrId)(S_SAY_HUNGRY + bubbleStrOffset(line, name[0] != 0)));
  if (bubbleTakesName(line, name[0] != 0)) snprintf(say, sizeof say, fmt, name);
  else snprintf(say, sizeof say, "%s", fmt);
  setSize(2);
  int n = (int)strlen(say);
  int br = bubbleSplit(say, n, bubbleTextMax(badgeShowing()), measure);
  if (br == -2) return false;
  memcpy(bubbleText, say, n + 1);
  bubbleBreak = br;
  if (br >= 0) bubbleText[br] = 0;
  bubbleW1 = (uint16_t)textW(bubbleText, 2);
  bubbleW2 = br >= 0 ? (uint16_t)textW(bubbleText + br + 1, 2) : 0;
  return true;
}

void bubbleLoop(uint32_t now, bool free) {
  if (!bubbleTimer.update(now, free, [] { return (uint32_t)random(BUBBLE_GAP_SPAN_MS); })) return;
  BubbleIn in;
  in.needs = attnLevels(pet.fullness, pet.joy, pet.energy, pet.hygiene);
  // the loop refreshes lastSeenEpoch from the clock every 30 s: no I2C read here
  uint32_t e = pet.lastSeenEpoch;
  in.clockKnown = e && !bedEdge.stale();
  in.hour = (uint8_t)sceneHour();  // the scene's own hour: the stars line only with the stars drawn
  in.starsOut = sceneStarsOut();   // ...and not under cloud, rain, snow or fog (#60)
  in.named = ownerName()[0] != 0;
  in.last = bubbleLine;
  // a special day's greeting (#26): the scene's clock (0 while it is not trusted), as the
  // decorations; never in quiet hours
  uint32_t clock = sceneEpoch(e, bedEdge.stale());
  uint8_t greet = holidayNow().greet;
  if (greeter.due(clock, attnQuietAt(pet.bedPreset, e, bedEdge.stale()), greet))
    in.greet = (uint8_t)(BUB_GREET_FIRST + greet - GREET_CHRISTMAS);
  uint8_t line = bubblePick(in, (uint32_t)random(0x7FFFFFFF));
  if (!compose(line)) {
    bubbleTimer.cancel();
    Serial.printf("say %s: too wide\n", BUBBLE_KEYS[line]);
    return;
  }
  if (line == in.greet) greeter.said(clock);
  bubbleLine = line;
  Serial.printf("say %s\n", BUBBLE_KEYS[line]);
}

#if HOST_SIM
// The simulator's "say" step: `line` shows now, laid out as if it had come up by itself,
// so a script can shoot any line in any language. The next loop pass still applies
// bubbleFree(): on a screen that is not free it ends at once.
void bubbleSayNow(uint8_t line) {
  if (line >= BUB_LINES || !compose(line)) return;
  bubbleTimer.start(millis());
  bubbleLine = line;  // no "say" log line: the serial log shows only the bubbles that came by themselves
}
#endif

bool bubbleShowing() { return bubbleTimer.showing(); }

void drawSpeechBubble() {
  if (!bubbleTimer.showing() || bubbleLine >= BUB_LINES || petHuffing()) return;  // the huff's puffs go by the head (#73)
  int lines = bubbleBreak < 0 ? 1 : 2;
  BubbleBox b = bubbleBoxAt(pmd.loaded ? (int)beh.x : CX, petHeadTop(), bubbleW1 > bubbleW2 ? bubbleW1 : bubbleW2,
                            lines, badgeShowing(), petButtonShown());
  uint16_t ink = inkColor();
  gfx->fillRoundRect(b.x, b.y, b.w, b.h, BUBBLE_RADIUS, UI_WHITE);
  gfx->drawRoundRect(b.x, b.y, b.w, b.h, BUBBLE_RADIUS, ink);
  // the tail: the white triangle covers the border where it joins, the two lines redraw
  // its outer edges (as the "!" bubble's)
  int base = b.y + b.h - 1;
  gfx->fillTriangle(b.tailX0, base, b.tailX1, base, b.tipX, b.tipY, UI_WHITE);
  gfx->drawLine(b.tailX0, base, b.tipX, b.tipY, ink);
  gfx->drawLine(b.tailX1, base, b.tipX, b.tipY, ink);
  gfx->setTextColor(UI_INK);  // dark on the white bubble, day or night
  setSize(2);
  int y = b.y + BUBBLE_PAD_Y;
  setCur(b.x + (b.w - bubbleW1) / 2, y);
  printT(bubbleText);
  if (lines == 2) {
    setCur(b.x + (b.w - bubbleW2) / 2, y + BUBBLE_LINE_H + BUBBLE_LINE_GAP);
    printT(bubbleText + bubbleBreak + 1);
  }
}
