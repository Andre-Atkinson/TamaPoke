// The minigames' screens and the training exercises (#41: moved out of TamaPoke.ino; #78
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
extern bool trainOpen;  // an exercise is on screen (the rhythm punch or the push-ups), or its result
extern bool trainPick;  // the stat card shows the exercise choice in place of the stats

void startGame();
#if HOST_SIM
// The simulator's "game NAME" step (sim/script.h): the next Play deals this game (GameId)
// in place of the bag's pick, so a script doesn't depend on how the dice fall (every
// random() call before it, the idle life's and the scene's too, moves them).
void gameForceNext(uint8_t g);
// The simulator's "gameplay point|miss" step (#127): the open game's next point, or its miss, as
// a touch the player could make now. The games' dice decide where the ball, the pet, the berry
// or the next note is, so a script can't name the place; this asks the game. Returns:
// GAMEAIM_WAIT nothing to answer yet (title card, a tune playing, no pop-up or berry up);
// GAMEAIM_IDLE no touch makes it (a keepy-uppy ball dropped only by leaving it): wait for the
// game to end; GAMEAIM_TAP a tap at (x, y); GAMEAIM_HOLD a finger down at (x, y) until the
// score changes (Berry Catch: the pet walks to the finger).
enum { GAMEAIM_WAIT = 0, GAMEAIM_IDLE, GAMEAIM_TAP, GAMEAIM_HOLD };
uint8_t gameSimAim(bool point, int16_t &x, int16_t &y);
uint16_t gameSimScore();  // the open game's score so far (0 with none open, or none played)
bool gameSimOpen();       // a game is on screen and not yet over (its title card, or in play)
#endif
void gameTouch(bool pressed, int16_t x, int16_t y);
void renderGame();

// The exercises (#78, endless since #126). kind: EX_PUNCH or EX_HOLD (the push-ups, exercise.h).
void startExercise(uint8_t kind);
// A touch-down while training, at the time it was captured (the 1.75C's HIT; the 1.75's
// poll): on the punch screen a punch (or the miss), on the push-ups going down. A release
// is never a tap.
void exerciseTap(uint32_t at);
// A confirmed lift of a finger that went down as a tap (LiftConfirm, trainingtouch.h), timed
// when it left: on the push-ups, letting go (a rep, or the miss).
void exerciseRelease(uint32_t at);
// The pet's sprite was just reloaded (ensureMon): the punch's lunge is planned again (#90).
void exerciseSpriteChanged();
// The exit while training: ends the session and pays for what was done (nothing on the
// result screen). True when the training screen has closed.
bool exerciseExit();
// The screen went dark with a minigame or an exercise open (#88, #94: PWR and the pocket
// lock, from pollPwrButton; the battery shutdown; bedtime's lights out, which can't meet one
// any more): ends it at once and pays for what was done so far, as its own end does (a
// minigame: Pet::playGame with its score, nothing if nobody touched it; an exercise: as its
// exit), with no result screen and no sound, and closes its screen. A result screen only
// closes (paid at its end). Nothing if neither is open.
void playScreenOff();
// The same, with `why` at the end of its serial line (" (screen off)" for playScreenOff()):
// an ending is starting (#33, closeForEnding in TamaPoke.ino), and the pet is paid before the
// ceremony, which takes nothing.
void playEndNow(const char *why);
#if !BOARD_AMOLED_175C
// The 1.75's exit: a touch-down above this y (the top strip) leaves. The exercise screens
// draw "tap: back" inside it, so it is no longer invisible, and end it above the hint.
constexpr int TRAIN_EXIT_STRIP_H = 42;
#endif
#if HOST_SIM
// The simulator's `pushup` step: ms from now until the lift of the push-up going down: the
// middle of its green band, or with `edge` just before the band's end. -1 when no push-up
// is down.
int32_t exerciseMsToLift(uint32_t now, bool edge);
// ...and its `repress`: the reports with no finger the touch poll has read since the finger
// lifted, 0 when no lift waits to be confirmed (TamaPoke.ino, LiftConfirm, R2-2).
uint8_t exerciseLiftReports();
#endif
void renderExercise();
void renderTrainPick();                     // the choice, on the stat card
uint8_t trainPickAt(int16_t x, int16_t y);  // the choice a tap is on: EX_PUNCH, EX_HOLD or EX_NONE
