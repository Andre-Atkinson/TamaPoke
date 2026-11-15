// Speech bubbles (bubble.h, #23): the bubble's state, its text and its drawing.
#include <Arduino.h>
#include <string.h>
#include "app.h"
#include "attention.h"
#include "bubble.h"
#include "i18n.h"
#include "owner.h"
#include "scene.h"
#include "uitext.h"

static_assert((int)S_SAY_HUNGRY + (int)BUB_LOVE_NAME == (int)S_SAY_LOVE_FMT &&
                  (int)S_SAY_HUNGRY + (int)BUB_LOVE == (int)S_SAY_LOVE,
              "the S_SAY_* strings follow BubbleLine");

static BubbleTimer bubbleTimer;
static uint8_t bubbleLine = BUB_NONE;  // the line showing, or the last one said
// The line in the active font's encoding. Two lines are split at a space, which becomes
// the first line's terminator: line 2 starts at bubbleText + bubbleBreak + 1.
static char bubbleText[48];
static int bubbleBreak = -1;  // -1: one line
static uint16_t bubbleW1 = 0, bubbleW2 = 0;  // each line's width in px

static bool badgeShowing() { return clockPrompt.asking(bedEdge.stale()); }

// Width of the first `len` bytes of s at size 2, in the active font (setSize(2) first:
// textW() measures CJK text at the current size).
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
  if (line == BUB_LOVE_NAME) {
    char name[OWNER_NAME_MAX + 1];
    ownerNameForActiveFont(name, sizeof name);
    snprintf(say, sizeof(say), T(S_SAY_LOVE_FMT), name);
  } else {
    snprintf(say, sizeof say, "%s", T((StrId)(S_SAY_HUNGRY + line)));
  }
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
  in.hour = (uint8_t)((e / 3600) % 24);  // the same hour as the scene (sceneHour)
  in.named = ownerName()[0] != 0;
  in.last = bubbleLine;
  uint8_t line = bubblePick(in, (uint32_t)random(0x7FFFFFFF));
  if (!compose(line)) {
    bubbleTimer.cancel();
    Serial.printf("say %s: too wide\n", BUBBLE_KEYS[line]);
    return;
  }
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
  if (!bubbleTimer.showing() || bubbleLine >= BUB_LINES) return;
  int lines = bubbleBreak < 0 ? 1 : 2;
  BubbleBox b = bubbleBoxAt(pmd.loaded ? (int)beh.x : CX, bubbleW1 > bubbleW2 ? bubbleW1 : bubbleW2, lines,
                            badgeShowing());
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
