// The minigames' screens and the strength-training sack (#41: moved out of
// TamaPoke.ino): starting and finishing a game, its touches and sounds, and drawing it.
// The games' own logic is in minigames.h; render() and handleTouch() call in here.
#pragma once
#include <stdint.h>
#include "minigames.h"  // GAME_COUNT
#include "species.h"    // the SPR_ICON_* sprites

// icono de cada juego (portada y fila de records de la ficha), en el orden de GameId
inline const char *const *const GAME_ICON[GAME_COUNT] = { SPR_ICON_PLAY, SPR_ICON_FOOD, SPR_ICON_BUSH,
                                                          SPR_ICON_NOTE };

extern bool gameOpen;  // a minigame is on screen
extern bool sackOpen;  // the strength-training sack is on screen
extern uint32_t sackUntil, sackOverUntil;
extern uint16_t sackHits;
extern float sackShake;

void startGame();
void gameTouch(bool pressed, int16_t x, int16_t y);
void renderGame();
void startSack();
void sackTap();
void renderSack();
