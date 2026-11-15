// The 12 letters European Portuguese needs that the classic 5x7 font lacks (#61).
//
// The UI font is Arduino_GFX's classic 5x7 font, a CP437 table. It has every
// Portuguese lowercase accent except a~ and o~, and of the accented capitals only
// C-cedilla (\200) and E-acute (\220). These 12 fill the gap: a~ o~ and
// A' A` A^ A~ E^ I' O' O^ O~ U'.
//
// Each one is written in a string as its Latin-1 byte, which is also its Unicode
// code point, so "N\303O" is NAO with a tilde and any Latin-1 table confirms it. In
// CP437 those bytes are box-drawing pieces, pi and the bottom half of an integral,
// which no UI string uses. One byte per letter keeps the strlen(t)*6 centring.
// Every other accented letter keeps its CP437 byte (\240 a-acute, \207 c-cedilla...).
//
// printT() (uitext.cpp) draws these bytes from PT_GLYPH_BITS through a small
// Adafruit-style GFXfont and every other byte from the classic font, so nothing
// already on screen moves. owner.h maps the same letters in the player's name.
//
// The look follows the font's own accented letters: an acute or grave capital has its
// accent on row 0, a blank row and a 5-row letter (like E-acute); a tilde or a
// circumflex takes 2 rows, a blank row and a 4-row letter (like the font's o^). The
// tilde is a wave, so a~ never looks like a^ (the font draws a^ with a flat bar).
// Header-only and pure, so the tests check the bitmaps and the lookup.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define PTG_COLS 6  // 5 columns of ink and the blank one the classic font leaves after each letter
#define PTG_ROWS 8  // rows 0-6 of ink; row 7 is the classic font's descender row, blank here
#define PTG_BYTES ((PTG_COLS * PTG_ROWS + 7) / 8)  // one glyph packed row by row, MSB first

struct PtGlyph {
  uint8_t code;                      // its byte in a string: Latin-1 = Unicode code point
  char art[PTG_ROWS][PTG_COLS + 1];  // '#' = ink, '.' = background
};

inline constexpr PtGlyph PT_GLYPHS[] = {
  {0xE3, {".##.#.", "#..#..", "......", ".###..", "#..#..", "#..#..", ".####.", "......"}},  // a~
  {0xF5, {".##.#.", "#..#..", "......", ".###..", "#...#.", "#...#.", ".###..", "......"}},  // o~
  {0xC1, {"..##..", "......", ".###..", "#...#.", "#####.", "#...#.", "#...#.", "......"}},  // A'
  {0xC0, {".##...", "......", ".###..", "#...#.", "#####.", "#...#.", "#...#.", "......"}},  // A`
  {0xC2, {"..#...", ".#.#..", "......", ".###..", "#...#.", "#####.", "#...#.", "......"}},  // A^
  {0xC3, {".##.#.", "#..#..", "......", ".###..", "#...#.", "#####.", "#...#.", "......"}},  // A~
  {0xCA, {"..#...", ".#.#..", "......", "####..", "###...", "#.....", "####..", "......"}},  // E^
  {0xCD, {"..##..", "......", ".###..", "..#...", "..#...", "..#...", ".###..", "......"}},  // I'
  {0xD3, {"..##..", "......", ".###..", "#...#.", "#...#.", "#...#.", ".###..", "......"}},  // O'
  {0xD4, {"..#...", ".#.#..", "......", ".###..", "#...#.", "#...#.", ".###..", "......"}},  // O^
  {0xD5, {".##.#.", "#..#..", "......", ".###..", "#...#.", "#...#.", ".###..", "......"}},  // O~
  {0xDA, {"..##..", "......", "#...#.", "#...#.", "#...#.", "#...#.", ".###..", "......"}},  // U'
};
constexpr int PTG_COUNT = (int)(sizeof(PT_GLYPHS) / sizeof(PT_GLYPHS[0]));

// The 12 codes alone. ptGlyphIndex() reads this, not PT_GLYPHS, so the art stays a compile-time
// input (PT_GLYPH_BITS) and nothing but these 12 bytes and the packed bits reaches the image.
struct PtGlyphCodes {
  uint8_t c[PTG_COUNT];
};
constexpr PtGlyphCodes ptGlyphCodes() {
  PtGlyphCodes out{};
  for (int i = 0; i < PTG_COUNT; i++) out.c[i] = PT_GLYPHS[i].code;
  return out;
}
inline constexpr PtGlyphCodes PT_GLYPH_CODES = ptGlyphCodes();

// The glyph's index in PT_GLYPHS for byte c, or -1: every other byte is drawn from the
// classic font.
constexpr int ptGlyphIndex(uint8_t c) {
  for (int i = 0; i < PTG_COUNT; i++)
    if (PT_GLYPH_CODES.c[i] == c) return i;
  return -1;
}

// The art packed the way an Adafruit GFXfont stores a bitmap: each glyph's rows one
// after another, PTG_COLS bits each, most significant bit first, a glyph starting on a
// fresh byte. Built at compile time, so the art above is the only copy to edit.
struct PtGlyphBits {
  uint8_t b[PTG_COUNT * PTG_BYTES];
};
constexpr PtGlyphBits ptGlyphPack() {
  PtGlyphBits out{};
  for (int g = 0; g < PTG_COUNT; g++)
    for (int i = 0; i < PTG_ROWS * PTG_COLS; i++)
      if (PT_GLYPHS[g].art[i / PTG_COLS][i % PTG_COLS] == '#')
        out.b[g * PTG_BYTES + i / 8] |= (uint8_t)(0x80 >> (i % 8));
  return out;
}
inline constexpr PtGlyphBits PT_GLYPH_BITS = ptGlyphPack();
