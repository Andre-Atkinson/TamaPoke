// Text on the panel (#41: moved out of TamaPoke.ino). The classic 5x7 font, plus the
// 12 Portuguese letters it lacks (ptglyphs.h), which printT() draws from their own
// small font. setCur()'s y is the top of the text.
#pragma once
#include <stdint.h>

void setSize(uint8_t n);                      // text scale
void setCur(int x, int y);                    // the cursor, y = the top of the text
uint16_t textW(const char *s, uint8_t size);  // width in px at that size
int centerX(const char *s, uint8_t size);     // the x that centres s on CX
void printT(char c);
void printT(const char *s);                   // print, extra Portuguese letters included
