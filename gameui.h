// The minigames' screens and the strength-training sack (#41: moved out of
// TamaPoke.ino): starting and finishing a game, its touches and sounds, and drawing it.
// The games' own logic is in minigames.h; render() and handleTouch() call in here.
#pragma once
#include <stdint.h>
#include "minigames.h"  // GAME_COUNT
#include "species.h"    // the SPR_ICON_* sprites

// icono de cada juego (portada y fila de records de la ficha), en el orden de GameId
static const char *const *const GAME_ICON[GAME_COUNT] = { SPR_ICON_PLAY, SPR_ICON_FOOD, SPR_ICON_BUSH,
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

// Used only inside gameui.cpp once the code has moved there (#41); a later commit
// makes them static there.
void gameSounds();
void finishGame();
void drawGameScene();
bool gameNight();
void drawGamePet(uint8_t act, int cx, int groundY, uint8_t maxS);
void drawGameHud(uint16_t ink, uint16_t score);
void drawGameBar(uint32_t left, uint32_t total);
void drawGameExit();
void drawGameTitle(uint16_t ink);
void drawKeepy(uint16_t ink);
void drawCatch(uint16_t ink);
void drawBush(int x, int y, bool front, bool night);
void drawPeek(uint16_t ink);
void drawSong(uint16_t ink);
void drawGameResult(uint16_t ink);
