// The screens other than the main one (#41: moved out of TamaPoke.ino): the starter and
// egg choice, the pet card's five pages, the clock and settings screen, the nickname
// keyboard and the Pokedex gallery, each with its taps and its state. render(), onTap()
// and onSwipe*() in TamaPoke.ino choose which one is open.
#pragma once
#include <stdint.h>
#include "sdmon.h"  // PmdMon

#define CARD_PAGE_CARE 4
#define CARD_LAST_PAGE CARD_PAGE_CARE

// eleccion de inicial (primera partida): Bulbasaur / Charmander / Squirtle, 3 filas
static const int16_t STARTER_DEX[3] = { 1, 4, 7 };
#define STARTER_ROW_Y 110
#define STARTER_ROW_H 70
#define STARTER_ROW_GAP 8
