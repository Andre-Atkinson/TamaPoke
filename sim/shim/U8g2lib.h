// Simulador: solo las fuentes CJK que usa uitext.cpp. Los datos salen de la
// libreria U8g2 instalada (clib/u8g2_fonts.c), extraidos por sim/extract_fonts.py
// al compilar; Arduino_GFX trae su propio decodificador de fuentes U8g2.
#pragma once
#include <stdint.h>

#ifndef U8G2_WITH_UNICODE
#define U8G2_WITH_UNICODE
#endif
#ifndef U8G2_FONT_SECTION
#define U8G2_FONT_SECTION(name)
#endif
#ifndef U8X8_FONT_SECTION
#define U8X8_FONT_SECTION(name)
#endif

extern "C" {
extern const uint8_t u8g2_font_unifont_t_japanese3[];
extern const uint8_t u8g2_font_unifont_t_korean2[];
}
