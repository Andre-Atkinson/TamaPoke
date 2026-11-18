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
// the stamina bar's blue (RGB565), which the stamina hold's screens (gameui.cpp) share
#define FIT_STA_COLOR 0x4C98
#define FIT_LABEL_X 96   // drawCardStat's label
#define FIT_BAR_END 310  // drawCardStat's bar ends here
#define FIT_BAR_MIN_W 100  // a bar never narrower than this: every label fits FIT_LABEL_MAX
#define FIT_LABEL_MAX ((FIT_BAR_END - FIT_BAR_MIN_W - 12 - FIT_LABEL_X) / CARD_LETTER_W)  // letters

inline int fitRowY(uint8_t stat) { return FIT_ROW_Y + stat * FIT_ROW_H; }

// The TRAIN button under the bars (x FIT_BTN_X, FIT_BTN_W wide). The 1.75C's page (walks,
// #117) adds two size-2 lines between the weight row and the button, "Steps today: 3,240" at
// FIT_STEPS_Y and "Walk treats: 12" at FIT_TREATS_Y, and the button moves down and loses 2
// px of height to make room, clear of the page dots (y 369). `walks` picks the 1.75C's.
#define FIT_BTN_X 96
#define FIT_BTN_W 274
#define FIT_BTN_Y 300
#define FIT_BTN_H 40
#define FIT_STEPS_Y 272
#define FIT_TREATS_Y 292
#define FIT_BTN_Y_WALK 316
#define FIT_BTN_H_WALK 38
#define FIT_LINE_H 14  // a size-2 line's height
inline int fitBtnY(bool walks) { return walks ? FIT_BTN_Y_WALK : FIT_BTN_Y; }
inline int fitBtnH(bool walks) { return walks ? FIT_BTN_H_WALK : FIT_BTN_H; }
inline bool fitBtnHit(int x, int y, bool walks) {
  return y >= fitBtnY(walks) && y <= fitBtnY(walks) + fitBtnH(walks) && x >= FIT_BTN_X && x <= FIT_BTN_X + FIT_BTN_W;
}

// ---------------------------------------------------------------- Medals page
// The 8 care medals in a 2x4 grid (bits 0-7), then the 3 training ribbons in one row
// (bits 8-10), then the Play minigames' records. A row's tiles follow the round edge.
// The 1.75C's page (`walks`, #117) has a fourth ribbon, the walking one (bit 11): the ribbons
// make two rows of two, and the grid and the ribbon rows are 4 px closer, so the records
// still end clear of the page dots (y 369).
#define MEDAL_TITLE_Y 44   // size 3
#define MEDAL_CARE 8       // the grid's tiles
#define MEDAL_RIBBONS 4    // the ribbons, the walking one included
#define MEDAL_ROW_Y 74
#define MEDAL_ROW_H 44     // from one grid row to the next
#define MEDAL_TILE_H 38
#define MEDAL_ROW_H_WALK 40   // the same on the 1.75C's page
#define MEDAL_TILE_H_WALK 36
#define MEDAL_GAP 10       // between the two tiles of a grid row
#define RIBBON_GAP 6       // between two tiles of the ribbon row
#define MEDAL_EDGE 6       // a row's outer tiles keep this far inside the disc
#define MEDAL_CAP 200      // half a grid row at most (the middle rows)
#define RIBBON_CAP 220     // half the ribbon row at most
#define RIBBON_ROW_Y (MEDAL_ROW_Y + 4 * MEDAL_ROW_H + 2)
#define RECORDS_Y (RIBBON_ROW_Y + MEDAL_TILE_H + 10)  // the minigames' records, 40 px tall
#define RIBBON_ROW_Y_WALK (MEDAL_ROW_Y + 4 * MEDAL_ROW_H_WALK)  // the 1.75C's first ribbon row; the second is MEDAL_ROW_H_WALK under it
#define RECORDS_Y_WALK (RIBBON_ROW_Y_WALK + 2 * MEDAL_ROW_H_WALK + 8)
#define MEDAL_TEXT_X 38    // a care medal's text, from its tile's left (the tick goes before it)
#define RIBBON_ROSETTE_X 16  // a ribbon's rosette, its centre from its tile's left
#define RIBBON_TEXT_X 28   // a ribbon's name, from its tile's left (past the rosette's tails)
#define MEDAL_TEXT_PAD 6   // kept clear at a tile's right end

// The ribbon tiles this page has: 3 on the original 1.75, 4 with the walking one.
inline uint8_t medalRibbons(bool walks) { return walks ? MEDAL_RIBBONS : MEDAL_RIBBONS - 1; }
inline int medalTileH(bool walks) { return walks ? MEDAL_TILE_H_WALK : MEDAL_TILE_H; }
inline int recordsY(bool walks) { return walks ? RECORDS_Y_WALK : RECORDS_Y; }

// Medal i's tile (0 to MEDAL_CARE + medalRibbons(walks) - 1): 0-7 the grid, then the ribbons,
// in one row of 3 (the original 1.75) or two rows of 2 (`walks`, the 1.75C).
inline CardRect medalTile(uint8_t i, bool walks) {
  CardRect r;
  int th = medalTileH(walks), rowH = walks ? MEDAL_ROW_H_WALK : MEDAL_ROW_H;
  r.h = (int16_t)th;
  if (i < MEDAL_CARE) {
    int y = MEDAL_ROW_Y + (i / 2) * rowH;
    int hw = cardHalfWidth(y, y + th, MEDAL_EDGE, MEDAL_CAP);
    r.y = (int16_t)y;
    r.w = (int16_t)(hw - MEDAL_GAP / 2);
    r.x = (int16_t)((i % 2) ? CARD_C + MEDAL_GAP / 2 : CARD_C - hw);
    return r;
  }
  uint8_t k = (uint8_t)(i - MEDAL_CARE);
  uint8_t perRow = walks ? 2 : 3;
  int y = (walks ? RIBBON_ROW_Y_WALK : RIBBON_ROW_Y) + (k / perRow) * rowH;
  int hw = cardHalfWidth(y, y + th, MEDAL_EDGE, RIBBON_CAP);
  int w = (2 * hw - (perRow - 1) * RIBBON_GAP) / perRow;
  r.y = (int16_t)y;
  r.w = (int16_t)w;
  r.x = (int16_t)(CARD_C - hw + (k % perRow) * (w + RIBBON_GAP));
  return r;
}

// The px medal i's text (medalDesc) may take in its tile.
inline int medalTextRoom(uint8_t i, bool walks) {
  return medalTile(i, walks).w - (i < MEDAL_CARE ? MEDAL_TEXT_X : RIBBON_TEXT_X) - MEDAL_TEXT_PAD;
}

// ---------------------------------------------------------------- Care page
// Four rows (#15 added the fourth, sickness), each a button that cycles one setting: pace,
// bedtime, eggs and sickness. 60 px rows, 6 apart, under the size-3 title at y 44: the label
// at size 2, the value at size 3, so the last row ends at y 334, clear of the page dots
// (y 369). careRowAt() is the tap's row (renderCardCare and careTap, screens.cpp).
#define CARE_ROWS 4
#define CARE_BTN_X 73
#define CARE_BTN_W 320
#define CARE_BTN_H 60
#define CARE_ROW_Y 76     // the first row's top
#define CARE_ROW_STEP 66  // from one row's top to the next
#define CARE_LABEL_DY 6   // the size-2 label, from the row's top
#define CARE_VALUE_DY 28  // the size-3 value
enum : uint8_t { CARE_ROW_PACE = 0, CARE_ROW_BED, CARE_ROW_EGGS, CARE_ROW_SICK };

inline int careRowY(uint8_t row) { return CARE_ROW_Y + row * CARE_ROW_STEP; }
// The row under (x, y), or -1 (the gaps between rows, and off the buttons, are no row).
inline int careRowAt(int x, int y) {
  if (x < CARE_BTN_X || x > CARE_BTN_X + CARE_BTN_W) return -1;
  for (uint8_t r = 0; r < CARE_ROWS; r++)
    if (y >= careRowY(r) && y <= careRowY(r) + CARE_BTN_H) return r;
  return -1;
}

// ---------------------------------------------------------------- Wardrobe page (#116)
// The card's last page: a 3x3 grid of tiles under the size-3 title, in ACC_* order (accessory.h):
// Nothing, the seven to earn, and on a special day today's hat in the ninth. Under it two
// size-2 lines (what it wears or a locked tile's hint; today's hat), clear of the page dots
// (y 369). wardTileAt() is the tap's tile (renderCardWardrobe and wardrobeTap, screens.cpp).
#define WARD_TITLE_Y 44   // size 3
#define WARD_COLS 3
#define WARD_TILES 9
#define WARD_TILE_W 92
#define WARD_TILE_H 74
#define WARD_GAP 10
#define WARD_GRID_Y 74    // the first row's top
#define WARD_GRID_X (CARD_C - (WARD_COLS * WARD_TILE_W + (WARD_COLS - 1) * WARD_GAP) / 2)
#define WARD_ART_S 4      // an accessory's pixel in its tile: the widest (14 x 11) is 56 x 44
#define WARD_LINE_Y 322   // the first line; the second is WARD_LINE_H under it
#define WARD_LINE_H 22
#define WARD_LINE_MAX 30  // letters a line may have: 360 px, inside the disc at that height

inline CardRect wardTile(uint8_t i) {
  CardRect r;
  r.x = (int16_t)(WARD_GRID_X + (i % WARD_COLS) * (WARD_TILE_W + WARD_GAP));
  r.y = (int16_t)(WARD_GRID_Y + (i / WARD_COLS) * (WARD_TILE_H + WARD_GAP));
  r.w = WARD_TILE_W;
  r.h = WARD_TILE_H;
  return r;
}
// The tile under (x, y), 0 to WARD_TILES - 1, or -1 (the gaps between tiles are none)
inline int wardTileAt(int x, int y) {
  for (uint8_t i = 0; i < WARD_TILES; i++) {
    CardRect r = wardTile(i);
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
  }
  return -1;
}
