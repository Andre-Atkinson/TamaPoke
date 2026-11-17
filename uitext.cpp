// Text size, cursor and the extra Portuguese letters (uitext.h). Moved out of
// TamaPoke.ino (#41); the Japanese and Korean fonts went with #61.
#include <Arduino.h>
#include "app.h"
#include "ptglyphs.h"
#include "uitext.h"

// The classic 5x7 font anchors the cursor at the TOP of the text, and every y in
// the UI is written that way. It advances 6 px per character at size 1, and every
// character is one byte, so a width is strlen(s) * 6 * size.
void setSize(uint8_t n) { gfx->setTextSize(n); }

void setCur(int x, int y) { gfx->setCursor(x, y); }

uint16_t textW(const char *s, uint8_t size) { return (uint16_t)strlen(s) * 6 * size; }

// x del cursor para dejar el texto centrado en CX
int centerX(const char *s, uint8_t size) { return CX - textW(s, size) / 2; }

// ---------- the Portuguese letters the classic font lacks (#61), and its E-acute (#85) ----------
// They are drawn from a small Adafruit-style GFXfont built from ptglyphs.h, swapped
// in for that one character. Arduino_GFX then draws it with the text colour and size
// and moves the cursor exactly as it does for a classic glyph:
//  - yOffset 0 puts the glyph's row 0 at the cursor y, where the classic font puts
//    its row 0 (the GFXfont path draws row r at y + yOffset + r);
//  - width and xAdvance 6 match the classic font's 6 px cell, so the cursor advance
//    and the wrap test (cursor_x + 6 * size - 1 past the right edge) are the same;
//  - yAdvance 8 is the classic line height, used only if a line wraps.
// With a background colour (a two-argument setTextColor()) the GFXfont path of
// Arduino_GFX::drawChar() fills a box that starts baseline * size above the cursor, its
// baseline being yAdvance * 2 / 3 (5 rows here), so it would be misplaced behind these
// letters only.
// No text in the UI sets a background: test_tools.py checks that every setTextColor()
// has one argument.
// The glyphs sit at PTG_FIRST + index in this font. PTG_FIRST keeps them clear of
// '\n' and '\r', which write() handles before drawing.
#define PTG_FIRST 0x80

struct PtGfxGlyphs {
  GFXglyph g[PTG_COUNT];
};
static constexpr PtGfxGlyphs ptGfxGlyphs() {
  PtGfxGlyphs out{};
  for (int i = 0; i < PTG_COUNT; i++)
    out.g[i] = {(uint16_t)(i * PTG_BYTES), PTG_COLS, PTG_ROWS, PTG_COLS, 0, 0};
  return out;
}
static constexpr PtGfxGlyphs PT_GFX_GLYPHS = ptGfxGlyphs();
static constexpr uint8_t PT_Y_ADVANCE = PTG_ROWS;
static constexpr bool ptMetricsAre(uint16_t w, uint8_t xAdv, int8_t xOff, int8_t yOff) {
  for (int i = 0; i < PTG_COUNT; i++) {
    const GFXglyph &g = PT_GFX_GLYPHS.g[i];
    if (g.width != w || g.xAdvance != xAdv || g.xOffset != xOff || g.yOffset != yOff) return false;
  }
  return true;
}
// The metrics above must stay the classic font's cell, or these letters would sit
// or advance differently from the rest of a line. Checked when the sketch is built.
static_assert(ptMetricsAre(6, 6, 0, 0),
              "PT glyph metrics: width and xAdvance must be 6 (the classic font's 6 px cell, so the cursor advance and the wrap test match), xOffset and yOffset 0 (column 0 and row 0 go at the cursor, as in the classic font)");
// Each glyph's bits start at its own PTG_BYTES block of PT_GLYPH_BITS (ptglyphs.h packs
// them so) and are PTG_ROWS tall, or Arduino_GFX would draw another glyph's bits.
static constexpr bool ptBitsLaidOutAsPacked() {
  for (int i = 0; i < PTG_COUNT; i++) {
    const GFXglyph &g = PT_GFX_GLYPHS.g[i];
    if (g.bitmapOffset != i * PTG_BYTES || g.height != PTG_ROWS) return false;
  }
  return true;
}
static_assert(ptBitsLaidOutAsPacked(),
              "PT glyph bitmapOffset must be index * PTG_BYTES and height PTG_ROWS, the layout ptGlyphPack() writes");
static_assert(PT_Y_ADVANCE == 8, "PT font yAdvance must be 8, the classic font's line height (used when a line wraps)");
static_assert(PTG_FIRST > '\r', "PTG_FIRST must stay above '\\n' and '\\r', which write() handles before drawing");
static_assert(PTG_FIRST + PTG_COUNT - 1 <= 0xFF, "PTG_FIRST + PTG_COUNT - 1 must fit the one byte write() takes");
static const GFXfont PT_FONT = {(uint8_t *)PT_GLYPH_BITS.b, (GFXglyph *)PT_GFX_GLYPHS.g, PTG_FIRST,
                                PTG_FIRST + PTG_COUNT - 1, PT_Y_ADVANCE};

static void writeByte(uint8_t c) {
  int i = ptGlyphIndex(c);
  if (i < 0) {
    gfx->write(c);  // the classic font: every byte but ptglyphs.h's
    return;
  }
  gfx->setFont(&PT_FONT);
  gfx->write((uint8_t)(PTG_FIRST + i));
  gfx->setFont();  // back to the classic 5x7 font
}

void printT(char c) { writeByte((uint8_t)c); }

void printT(const char *s) {
  while (*s) writeByte((uint8_t)*s++);
}
