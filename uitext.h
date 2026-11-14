// Text on the panel in every language (#41: moved out of TamaPoke.ino). The classic
// 5x7 font for the Latin languages, a U8g2 font for Japanese and Korean; with either,
// setCur()'s y is the top of the text. applyLangFont() runs after a language change.
#pragma once
#include <stdint.h>

void setSize(uint8_t n);                      // text scale (divided for the CJK font)
void setCur(int x, int y);                    // the cursor, y = the top of the text
void applyLangFont();                         // the active language's font
uint16_t textW(const char *s, uint8_t size);  // width in px at that size
int centerX(const char *s, uint8_t size);     // the x that centres s on CX
void printT(char c);
void printT(const char *s);                   // print in the active font
