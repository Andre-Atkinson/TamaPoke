// The minigames' screens and the calm exercises (#41: moved out of TamaPoke.ino; #78
// replaced the strength-training sack): starting and finishing a game or an exercise,
// its touches and sounds, and drawing it. The games' own logic is in minigames.h and the
// exercises' in exercise.h; render() and handleTouch() call in here.
#pragma once
#include <stdint.h>
#include "board.h"      // BOARD_AMOLED_175C
#include "exercise.h"   // EX_PUNCH, EX_HOLD, EX_NONE
#include "minigames.h"  // GAME_COUNT
#include "species.h"    // the SPR_ICON_* sprites

// icono de cada juego (portada y fila de records de la ficha), en el orden de GameId
inline const char *const *const GAME_ICON[GAME_COUNT] = { SPR_ICON_PLAY, SPR_ICON_FOOD, SPR_ICON_BUSH,
                                                          SPR_ICON_NOTE };

extern bool gameOpen;   // a minigame is on screen
extern bool trainOpen;  // an exercise is on screen (the rhythm punch or the stamina hold), or its result
extern bool trainPick;  // the stat card shows the exercise choice in place of the stats

void startGame();
void gameTouch(bool pressed, int16_t x, int16_t y);
void renderGame();

// The calm exercises (#78). kind: EX_PUNCH or EX_HOLD (exercise.h).
void startExercise(uint8_t kind);
// A touch-down while training, at the time it was captured (the 1.75C's HIT; the 1.75's
// poll): on the punch screen it may land a punch. A release is never a tap.
void exerciseTap(uint32_t at);
// The exit while training: ends the session and pays for what was done (nothing on the
// result screen). True when the training screen has closed.
bool exerciseExit();
// The screen went dark with an exercise open (#88: PWR, the pocket lock, the battery
// shutdown): ends the session at once and pays for what was done, as the exit does, with
// no result screen and no sound, and closes the training screen. Nothing if none is open.
void exerciseScreenOff();
#if !BOARD_AMOLED_175C
// The 1.75's exit: a touch-down above this y (the top strip) leaves. The exercise screens
// draw "tap: back" inside it, so it is no longer invisible, and end it above the hint.
constexpr int TRAIN_EXIT_STRIP_H = 42;
// The 1.75: whether a finger that counts for the hold is on. Call it after every
// successful poll: a hold goes stale when the reports stop (exerciseHolding in gameui.cpp).
void exerciseHeld(bool on);
#endif
void renderExercise();
void renderTrainPick();                     // the choice, on the stat card
uint8_t trainPickAt(int16_t x, int16_t y);  // the choice a tap is on: EX_PUNCH, EX_HOLD or EX_NONE
