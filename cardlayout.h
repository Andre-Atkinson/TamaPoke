#pragma once
#include <math.h>
#include <stdint.h>

// The stat card's Fitness and Medals pages (#76): where their rows and tiles go.
// renderCardStats() and renderCardMedals() (screens.cpp) draw from these, and
// test/test_i18n.cpp checks that every label, in every language, fits the place it is
// drawn. Pure (no hardware). Like settingslayout.h, it keeps its own copy of the panel's
// centre; screens.cpp checks it against CX and CY.
//
// The card draws on a disc of radius CARD_R around (CARD_C, CARD_C). Size-2 text is
// CARD_LETTER_W px a letter (the classic 5x7 font, one byte per letter: i18n.h).

constexpr int CARD_C = 233;        // the panel's centre, x and y
constexpr int CARD_R = 231;        // the card's background disc (renderCard)
constexpr int CARD_LETTER_W = 12;  // a size-2 letter

// Half the width of the disc along the band y0..y1, less `margin`, at most `cap`: a band
// near the top or the bottom is narrower, so its tiles stay on the glass.
inline int cardHalfWidth(int y0, int y1, int margin, int cap) {
  int y = (y1 < CARD_C) ? y0 : (y0 > CARD_C) ? y1 : CARD_C;  // the band's edge farthest from the centre
  int dy = y - CARD_C;
  int hw = (int)sqrtf((float)(CARD_R * CARD_R - dy * dy)) - margin;
  return hw < cap ? hw : cap;
}

struct CardRect {
  int16_t x, y, w, h;
};

// ---------------------------------------------------------------- Fitness page
// Three trained stats, each a bar row (label, bar, number; drawCardStat) with its hint
// line under it, then the weight's bar row. The bars run from statBarX() to x 310.
#define FIT_TITLE_Y 42   // size 3
#define FIT_ROW_Y 86     // the first bar row's label
#define FIT_ROW_H 52     // from one trained stat's row to the next
#define FIT_HINT_DY 22   // the hint line, under its bar row
#define FIT_WGT_Y 248    // the weight's bar row
#define FIT_STATS 3
#define FIT_LABEL_X 96   // drawCardStat's label
#define FIT_BAR_END 310  // drawCardStat's bar ends here
#define FIT_BAR_MIN_W 100  // a bar never narrower than this: every label fits FIT_LABEL_MAX
#define FIT_LABEL_MAX ((FIT_BAR_END - FIT_BAR_MIN_W - 12 - FIT_LABEL_X) / CARD_LETTER_W)  // letters

inline int fitRowY(uint8_t stat) { return FIT_ROW_Y + stat * FIT_ROW_H; }

// ---------------------------------------------------------------- Medals page
// The 8 care medals in a 2x4 grid (bits 0-7), then the 3 training ribbons in one row
// (bits 8-10), then the Play minigames' records. A row's tiles follow the round edge.
#define MEDAL_TITLE_Y 40   // size 3
#define MEDAL_CARE 8       // the grid's tiles
#define MEDAL_RIBBONS 3    // the ribbon row's tiles
#define MEDAL_ROW_Y 74
#define MEDAL_ROW_H 44     // from one grid row to the next
#define MEDAL_TILE_H 38
#define MEDAL_GAP 10       // between the two tiles of a grid row
#define RIBBON_GAP 6       // between two tiles of the ribbon row
#define MEDAL_EDGE 6       // a row's outer tiles keep this far inside the disc
#define MEDAL_CAP 200      // half a grid row at most (the middle rows)
#define RIBBON_CAP 220     // half the ribbon row at most
#define RIBBON_ROW_Y (MEDAL_ROW_Y + 4 * MEDAL_ROW_H + 2)
#define RECORDS_Y (RIBBON_ROW_Y + MEDAL_TILE_H + 10)  // the minigames' records, 40 px tall
#define MEDAL_TEXT_X 38    // a care medal's text, from its tile's left (the tick goes before it)
#define RIBBON_ROSETTE_X 16  // a ribbon's rosette, its centre from its tile's left
#define RIBBON_TEXT_X 28   // a ribbon's name, from its tile's left (past the rosette's tails)
#define MEDAL_TEXT_PAD 6   // kept clear at a tile's right end

// Medal i's tile: 0-7 the grid, 8-10 the ribbon row.
inline CardRect medalTile(uint8_t i) {
  CardRect r;
  r.h = MEDAL_TILE_H;
  if (i < MEDAL_CARE) {
    int y = MEDAL_ROW_Y + (i / 2) * MEDAL_ROW_H;
    int hw = cardHalfWidth(y, y + MEDAL_TILE_H, MEDAL_EDGE, MEDAL_CAP);
    r.y = (int16_t)y;
    r.w = (int16_t)(hw - MEDAL_GAP / 2);
    r.x = (int16_t)((i % 2) ? CARD_C + MEDAL_GAP / 2 : CARD_C - hw);
    return r;
  }
  uint8_t k = (uint8_t)(i - MEDAL_CARE);
  int hw = cardHalfWidth(RIBBON_ROW_Y, RIBBON_ROW_Y + MEDAL_TILE_H, MEDAL_EDGE, RIBBON_CAP);
  int w = (2 * hw - (MEDAL_RIBBONS - 1) * RIBBON_GAP) / MEDAL_RIBBONS;
  r.y = RIBBON_ROW_Y;
  r.w = (int16_t)w;
  r.x = (int16_t)(CARD_C - hw + k * (w + RIBBON_GAP));
  return r;
}

// The px medal i's text (medalDesc) may take in its tile.
inline int medalTextRoom(uint8_t i) {
  return medalTile(i).w - (i < MEDAL_CARE ? MEDAL_TEXT_X : RIBBON_TEXT_X) - MEDAL_TEXT_PAD;
}
