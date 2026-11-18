// Accessories earned by care (#116): what the pet can wear on its head, what earns each one,
// the hat a special day puts on, their pixel art, and where the head is on a sprite's frame.
//
// The owner's decisions of 2026-11-17 (the design comment on #116):
//  - Seven to earn, each from this pet's own medals (pet.h's MED_*), so a pet's wardrobe is
//    its own and starts empty when an egg hatches: the Bow (favourite berry found), the
//    Flowers (a 7-day streak), the Bobble hat (full bond), the Cap (level 10), the Top hat
//    (level 25), the Crown (level 50) and the Laurel (any training ribbon, or In shape).
//  - A special day puts its hat on by itself, over whatever the pet wears, and it isn't kept:
//    the Santa hat all December, the witch hat on 31 Oct, the bunny ears on Easter Sunday,
//    the heart clip on 14 Feb, the party hat on 1 Jan and on the player's birthday (which
//    wins over a holiday on the same day). It needs a trusted clock, like the decorations
//    (holiday.h). A pick on the Wardrobe page that day wins until midnight (RAM only).
//  - Every species wears it on its head; asleep (and dozing) the pet puts it away.
//
// Where it goes: on each frame, the first row whose widest stretch of opaque pixels is at
// least half the frame's widest (a tail tip, an ear tip or a crest above it is narrower),
// centred on the row under it (accFindHead). sdmon.cpp finds it for every frame when a
// sprite loads; accfix.h holds the hand-made nudges for the poses where that rule misses.
//
// Pure (no hardware): pet.h, sdmon.cpp, scene.cpp and screens.cpp use it, and
// test/test_accessory.cpp checks it.
#pragma once
#include <stdint.h>
#include <string.h>
#include "holiday.h"  // HOL_*

// ---------------------------------------------------------------- what there is
enum : uint8_t {
  ACC_NONE = 0,
  // earned (ACC_EARNED of them), in the Wardrobe page's order
  ACC_BOW, ACC_FLOWERS, ACC_BOBBLE, ACC_CAP, ACC_TOPHAT, ACC_CROWN, ACC_LAUREL,
  // a special day's, worn that day only
  ACC_SANTA, ACC_WITCH, ACC_BUNNY, ACC_HEART, ACC_PARTY,
  ACC_COUNT
};
#define ACC_EARNED 7
#define ACC_EARNED_MASK ((uint16_t)(((1u << ACC_EARNED) - 1) << ACC_BOW))  // bits ACC_BOW..ACC_LAUREL

inline bool accIsEarned(uint8_t id) { return id >= ACC_BOW && id <= ACC_LAUREL; }
inline bool accIsHoliday(uint8_t id) { return id >= ACC_SANTA && id < ACC_COUNT; }

// The names the serial commands use (`ACC <name>`, `ACC?`), in ACC_* order
inline const char *const ACC_KEYS[ACC_COUNT] = {
  "none", "bow", "flowers", "bobble", "cap", "tophat", "crown", "laurel",
  "santa", "witch", "bunny", "heart", "party",
};
// ACC_* for a serial name; -1 if there is none
inline int accFromKey(const char *s) {
  for (int i = 0; i < ACC_COUNT; i++)
    if (strcmp(s, ACC_KEYS[i]) == 0) return i;
  return -1;
}

// ---------------------------------------------------------------- what earns each one
// The medals (pet.h's MED_* bits; pet.h checks these numbers against them) that earn
// accessory `id`: any one of them does. Nothing for ACC_NONE and the special days' hats.
// MED_FINAL earns nothing: 25 species hatch in their final form (decision 1).
#define ACCM_LV10 (1u << 0)
#define ACCM_LV25 (1u << 1)
#define ACCM_LV50 (1u << 2)
#define ACCM_BERRY (1u << 3)
#define ACCM_STREAK7 (1u << 4)
#define ACCM_BOND (1u << 5)
#define ACCM_FINAL (1u << 6)
#define ACCM_FIT (1u << 7)
#define ACCM_RIBBONS ((1u << 8) | (1u << 9) | (1u << 10))  // strength, stamina, speed
inline uint16_t accMedalsFor(uint8_t id) {
  switch (id) {
    case ACC_BOW: return ACCM_BERRY;
    case ACC_FLOWERS: return ACCM_STREAK7;
    case ACC_BOBBLE: return ACCM_BOND;
    case ACC_CAP: return ACCM_LV10;
    case ACC_TOPHAT: return ACCM_LV25;
    case ACC_CROWN: return ACCM_LV50;
    case ACC_LAUREL: return ACCM_FIT | ACCM_RIBBONS;
    default: return 0;
  }
}
// The accessories `medals` earn, as bits (1 << ACC_*), inside ACC_EARNED_MASK
inline uint16_t accUnlockMask(uint16_t medals) {
  uint16_t m = 0;
  for (uint8_t id = ACC_BOW; id <= ACC_LAUREL; id++)
    if (medals & accMedalsFor(id)) m |= (uint16_t)(1u << id);
  return m;
}
// The lowest accessory in `accs` (bits) that one of the medals in `medalBits` earns; 0: none
inline uint8_t accEarnedBy(uint16_t medalBits, uint16_t accs) {
  for (uint8_t id = ACC_BOW; id <= ACC_LAUREL; id++)
    if ((accs & (1u << id)) && (medalBits & accMedalsFor(id))) return id;
  return ACC_NONE;
}
// The lowest accessory in `accs` (bits); ACC_NONE if none
inline uint8_t accLowest(uint16_t accs) {
  for (uint8_t id = ACC_BOW; id < ACC_COUNT; id++)
    if (accs & (1u << id)) return id;
  return ACC_NONE;
}

// ---------------------------------------------------------------- special days
// The hat a special day puts on (holiday.h's HOL_*): the player's birthday wins over a
// holiday on the same day. ACC_NONE on any other day, and without a trusted clock (then
// holidayAt() says HOL_NONE and no birthday).
inline uint8_t accForDay(uint8_t hol, bool birthday) {
  if (birthday) return ACC_PARTY;
  switch (hol) {
    case HOL_DECEMBER:
    case HOL_CHRISTMAS: return ACC_SANTA;
    case HOL_NEW_YEAR: return ACC_PARTY;
    case HOL_VALENTINE: return ACC_HEART;
    case HOL_EASTER: return ACC_BUNNY;
    case HOL_HALLOWEEN: return ACC_WITCH;
    default: return ACC_NONE;
  }
}
// What the pet shows: today's special hat (dayHat), unless the player picked something on the
// Wardrobe page today (pickDay == today, both local days, epoch / 86400); otherwise what it
// wears. today 0: no trusted clock, so no special hat either.
inline uint8_t accShown(uint8_t worn, uint8_t dayHat, uint32_t pickDay, uint32_t today) {
  if (dayHat != ACC_NONE && !(today && pickDay == today)) return dayHat;
  return worn;
}

// ---------------------------------------------------------------- the art
// Each accessory is a little map, one character a sprite pixel, drawn at the pet's own
// sprite scale so its pixels match the pet's. '.' is clear. `sink`: how many of its bottom
// rows go down over the head's top row (a cap's band sits on the head, not above it).
struct AccArt {
  uint8_t w, h, sink;
  const char *const *rows;
};
inline uint16_t accColor(char c) {  // RGB565
  switch (c) {
    case 'K': return 0x18E4;  // #1c1c24 the outline
    case 'R': return 0xEA87;  // #e8503a red
    case 'r': return 0xA9A5;  // #a8352a dark red
    case 'W': return 0xFFFF;  // white
    case 'w': return 0xCE7B;  // #c8ccd8 pale grey
    case 'Y': return 0xFE47;  // #f8c838 gold
    case 'y': return 0xBC43;  // #b88a18 dark gold
    case 'P': return 0xF476;  // #f48cb4 pink
    case 'p': return 0xC28F;  // #c4507e dark pink
    case 'G': return 0x5DCD;  // #58b868 green
    case 'g': return 0x2BC7;  // #2f7a3e dark green
    case 'B': return 0x3B5A;  // #3c6ad0 blue
    case 'b': return 0x2211;  // #24408a dark blue
    case 'N': return 0x3187;  // #30303c the top hat's black
    case 'n': return 0x5ACD;  // #585868 its sheen
    case 'O': return 0xF485;  // #f0902c orange
    case 'V': return 0x8AB9;  // #8a54c8 violet
    case 'v': return 0x5191;  // #54308a dark violet
    case 'L': return 0x8EDF;  // #8ad8ff the crown's gem
    default: return 0;
  }
}
inline const char *const ACC_MAP_BOW[] = {
  "KKK.....KKK", "KPPK...KPPK", "KPPPKKKPPPK", "KPPpKPKpPPK", "KPPPKKKPPPK", "KPPK...KPPK", "KKK.....KKK",
};
inline const char *const ACC_MAP_FLOWERS[] = {
  ".KKK.KKK.KKK.", "KPYPKWYWKPYPK", ".KPKgKWKgKPK.", ".gGGGgGGGgGG.",
};
inline const char *const ACC_MAP_BOBBLE[] = {
  ".....KKK.....", "....KWWWK....", ".....KKK.....", "...KKRRRKK...", "..KRRRRRRRK..",
  ".KRRWRRRWRRK.", ".KRRRRRRRRRK.", "KWWWWWWWWWWWK", "KwWwWwWwWwWwK", "KKKKKKKKKKKKK",
};
inline const char *const ACC_MAP_CAP[] = {
  "....KKKKK....", "..KKBBBBBKK..", ".KBBBWWBBBBK.", ".KBBBBBBBBBK.", "KBBBBBBBBBBBK",
  "KKbbbbbbbbbKK", ".KbbbbbbbbbK.", "..KKKKKKKKK..",
};
inline const char *const ACC_MAP_TOPHAT[] = {
  "..KKKKKKKK..", "..KNNNNNNK..", "..KNnNNNNK..", "..KNnNNNNK..", "..KNNNNNNK..",
  "..KRRRRRRK..", "..KNNNNNNK..", "KKKKKKKKKKKK", "KNNNNNNNNNNK", "KKKKKKKKKKKK",
};
inline const char *const ACC_MAP_CROWN[] = {
  ".K....K....K.", "KYK..KYK..KYK", "KYYK.KYK.KYYK", "KYYYKYYYKYYYK", "KYYYYYLYYYYYK",
  "KYRYYYYYYYRYK", "KyyyyyyyyyyyK", "KKKKKKKKKKKKK",
};
inline const char *const ACC_MAP_LAUREL[] = {
  "G...........G", "gG.........Gg", ".gGG.....GGg.", "..ggGGYGGgg..",
};
inline const char *const ACC_MAP_SANTA[] = {
  ".........KKK.", ".......KKWWWK", ".....KKRRKWK.", "...KKRRRRKK..", "..KRRRRRRK...",
  ".KRRRRRRRRK..", "KWWWWWWWWWWK.", "KwWWWWWWWWwK.", ".KKKKKKKKKK..",
};
inline const char *const ACC_MAP_WITCH[] = {
  "......KK......", ".....KVK......", ".....KVVK.....", "....KVVVK.....", "....KVVVVK....",
  "...KVVVVVK....", "...KOOOOOOK...", "..KVVVVVVVVK..", "KKVVVVVVVVVVKK", "KvvvvvvvvvvvvK",
  ".KKKKKKKKKKKK.",
};
inline const char *const ACC_MAP_BUNNY[] = {
  ".KK.....KK.", "KWWK...KWWK", "KWPK...KWPK", "KWPK...KWPK", "KWPK...KWPK", "KWPK...KWPK",
  ".KWWK.KWWK.", "..KWWKWWK..", "...KKKKK...",
};
inline const char *const ACC_MAP_HEART[] = {
  ".KK...KK.", "KRRK.KRRK", "KRWRKRRRK", "KRRRRRRRK", ".KRRRRRK.", "..KRRRK..", "...KRK...", "....K....",
};
inline const char *const ACC_MAP_PARTY[] = {
  "...KYK...", "...KKK...", "...KBK...", "..KBYBK..", "..KYBYK..", ".KBYBYBK.", ".KYBYBYK.",
  "KBYBYBYBK", "KKKKKKKKK",
};
// ACC_* order; ACC_NONE's is empty (w 0)
inline const AccArt ACC_ART[ACC_COUNT] = {
  { 0, 0, 0, nullptr },
  { 11, 7, 1, ACC_MAP_BOW },
  { 13, 4, 1, ACC_MAP_FLOWERS },
  { 13, 10, 2, ACC_MAP_BOBBLE },
  { 13, 8, 2, ACC_MAP_CAP },
  { 12, 10, 1, ACC_MAP_TOPHAT },
  { 13, 8, 1, ACC_MAP_CROWN },
  { 13, 4, 1, ACC_MAP_LAUREL },
  { 13, 9, 1, ACC_MAP_SANTA },
  { 14, 11, 1, ACC_MAP_WITCH },
  { 11, 9, 1, ACC_MAP_BUNNY },
  { 9, 8, 1, ACC_MAP_HEART },
  { 9, 9, 1, ACC_MAP_PARTY },
};
#define ACC_ART_MAX_W 14  // the widest map (the witch hat)
#define ACC_ART_MAX_H 11  // the tallest (the witch hat)

// ---------------------------------------------------------------- where the head is
// On one frame of a sprite (w x h palette indices, 0xFF clear): the head's top row (crown)
// and, on the row under it, the stretch of opaque pixels a hat sits on (x0, w; w 0 when the
// frame has no opaque pixel at or under row `skip`). The crown is the first row whose widest
// stretch is at least `pct` per cent of the frame's widest (ACC_HEAD_PCT, half, unless accfix.h
// says less for a head narrower than that: a long neck under wide wings). Rows above `skip` are
// not looked at (accfix.h: a tail or an ear the rule would take for the head).
struct AccHead {
  uint8_t crown = 0, x0 = 0, w = 0;
};
#define ACC_HEAD_PCT 50
// The widest stretch of opaque pixels on a row: its first column and its length (0: none)
inline uint8_t accRowRun(const uint8_t *row, uint8_t w, uint8_t &start) {
  uint8_t best = 0;
  start = 0;
  for (int c = 0; c < w;) {
    if (row[c] == 0xFF) {
      c++;
      continue;
    }
    int c0 = c;
    while (c < w && row[c] != 0xFF) c++;
    if (c - c0 > best) {
      best = (uint8_t)(c - c0);
      start = (uint8_t)c0;
    }
  }
  return best;
}
// band: only the columns within `band` of column w / 2 + mid are looked at (0: all of them): a
// front-facing bird's head between wings, or a head at the front of a long back seen side-on,
// that the rows' widest stretches would otherwise miss.
inline AccHead accFindHead(const uint8_t *fr, uint8_t w, uint8_t h, uint8_t skip, uint8_t pct = ACC_HEAD_PCT,
                           uint8_t band = 0, int mid = 0) {
  AccHead hd;
  int c0 = 0, c1 = w;
  if (band) {
    int c = w / 2 + mid;
    c0 = c - band > 0 ? c - band : 0;
    c1 = c + band < w ? c + band : w;
    if (c1 <= c0) return hd;  // the band is off the canvas: nothing found
  }
  auto run = [&](int r, uint8_t &start) {  // row r's widest stretch inside the columns looked at
    uint8_t n = accRowRun(fr + r * w + c0, (uint8_t)(c1 - c0), start);
    start = (uint8_t)(start + c0);
    return n;
  };
  uint8_t widest = 0, st;
  for (int r = skip; r < h; r++) {
    uint8_t n = run(r, st);
    if (n > widest) widest = n;
  }
  if (!widest) return hd;
  if (pct < 1 || pct > 100) pct = ACC_HEAD_PCT;
  int crown = skip;
  while (crown < h && 100 * run(crown, st) < pct * widest) crown++;  // ends on the widest row
  uint8_t n = crown + 1 < h ? run(crown + 1, st) : 0;
  if (!n) n = run(crown, st);  // nothing under it: the crown's own row
  hd.crown = (uint8_t)crown;
  hd.x0 = st;
  hd.w = n;
  return hd;
}
// Where art `a` goes on a frame whose head is `hd`, in sprite pixels of the frame's canvas:
// its top-left column and row, centred on the head (half a pixel rounds right) and sunk
// `a.sink` rows into its top, then nudged by (dx, dy) (accfix.h)
inline void accPlace(const AccHead &hd, const AccArt &a, int dx, int dy, int &col, int &row) {
  int n = 2 * hd.x0 + hd.w - a.w + 1;  // twice the left column, plus one so that it rounds
  col = (n - (n < 0 ? 1 : 0)) / 2 + dx;
  row = hd.crown + a.sink - a.h + dy;
}
