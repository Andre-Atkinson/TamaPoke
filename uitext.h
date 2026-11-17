// Text on the panel (#41: moved out of TamaPoke.ino). The classic 5x7 font, plus the
// 12 Portuguese letters it lacks and its E-acute redrawn (ptglyphs.h), which printT()
// draws from their own small font. setCur()'s y is the top of the text.
#pragma once
#include <stdint.h>

void setSize(uint8_t n);                      // text scale
void setCur(int x, int y);                    // the cursor, y = the top of the text
uint16_t textW(const char *s, uint8_t size);  // width in px at that size
int centerX(const char *s, uint8_t size);     // the x that centres s on CX
void printT(char c);
void printT(const char *s);                   // print, extra Portuguese letters included

// The main screen's name header (drawHeader): size 3 (18 px a letter), but the panel is
// only about 293 px wide at its y, so a text wider than HEADER_MAX_W px at size 3 drops
// to size 2 (12 px a letter). Pure, so the host tests check it against every name.
#define HEADER_MAX_W 288
#define HEADER_Y3 52  // the top of the size-3 line
#define HEADER_Y2 56  // the top of the size-2 line, centred on the size-3 one (21 px vs 14 px tall)
inline uint8_t headerSize(unsigned len) { return len * 6 * 3 <= HEADER_MAX_W ? 3 : 2; }
