// What TamaPoke.ino shares with the modules split out of it (#41): the sketch's own
// globals and functions that a module uses. Each one is defined once, in TamaPoke.ino;
// what a module defines for the others is declared in that module's header instead.
#pragma once
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "board.h"
#include "bedtime.h"
#include "clockprompt.h"
#include "pet.h"
#include "power.h"
#include "sdmon.h"
#include "touchgesture.h"
#if BOARD_AMOLED_175C
#include "screenlock.h"
#include "trainingcapture.h"
#endif

#define CX 233  // centro de la pantalla redonda
#define CY 233

// Reaction to a gesture on the pet (#21, touchgesture.h): drawPet* show it in place of the
// pet's own behaviour until `until`.
enum : uint8_t { REACT_NONE = 0, REACT_HOP, REACT_GIGGLE, REACT_HUFF, REACT_STROKE };
struct PetReact {
  uint8_t kind = REACT_NONE;
  int8_t away = 1;  // the huff turns this way: away from the finger (-1 left, +1 right)
  uint32_t t0 = 0, until = 0;
};

#define PET_CY 202  // centro vertical del sprite

static const uint16_t INK_K = 0x18C4;  // spriteColor('k')

// botones de icono siguiendo el arco inferior de la pantalla redonda
// (los exteriores van mas altos para no salirse del circulo)
struct Btn {
  int16_t cx, cy;
  const char *const *icon;
};
#define BTN_HALF 26  // boton de 52x52
#define BTN_HIT 36   // radio tactil (un poco mas generoso)

// boton-CTA de evolucion (centrado, mitad de pantalla)
#define EVO_BTN_W 256
#define EVO_BTN_H 64
#define EVO_BTN_X (CX - EVO_BTN_W / 2)
#define EVO_BTN_Y 172  // test/test_clockprompt.cpp (ClockBadge) mirrors this: update it too
// boton-CTA de despedida (mas ancho: lleva el nombre + frase)
#define FAR_BTN_W 408
#define FAR_BTN_H 58
#define FAR_BTN_X (CX - FAR_BTN_W / 2)
#define FAR_BTN_Y 176  // mirrored in test/test_clockprompt.cpp (ClockBadge) too

extern Arduino_Canvas *gfx;  // the whole frame, in PSRAM; flushUI() sends it to the panel
extern Pet pet;
extern SdMon mon;            // the black-and-white sprite (no PMD sprite on the SD)
extern PmdMon pmd;           // the main screen's PMD sprite
extern PmdMon galleryPmd;    // the gallery's detail-page sprite
extern bool galleryOpen;
extern bool galleryDirty;
extern int16_t galleryDetail;  // dex shown in detail, 0 = the grid
extern bool clockOpen;         // the clock and settings screen
extern BedEdge bedEdge;
extern bool panelAsleep;       // syncPanelPower() has put the CO5300 to sleep
#if BOARD_AMOLED_175C
extern ScreenLock screenLock;
extern TrainingCapture trainingCapture;
extern bool trainingCaptureReady;  // the training capture task started
#endif
extern PmdMon evoPmd;          // the form before an evolution, during its flash
extern PetReact petReact;      // the pet's reaction to a gesture (touchgesture.h)
extern Btn buttons[4];         // the four care buttons along the bottom arc
extern uint32_t feedMenuUntil;  // the food picker is open until this millis()
extern uint32_t confirmUntil;   // the "release?" dialog is open until this millis()
extern uint8_t choiceKind;      // the choice dialog: 0 none, 1 evolve, 2 farewell
extern uint32_t choiceUntil;    // the choice dialog closes by itself at this millis()
extern bool batAsPercent;       // settings: the battery as "87%" instead of the icon
extern ClockPrompt clockPrompt;
extern TouchGesture touchG;     // the gesture in progress
extern uint8_t dimStage;        // 0 awake, 1 dimmed, 2 nearly off
extern bool screenOff;
extern bool holdFired;          // this touch already fired its long press
extern bool swallowGesture;     // the touch that wakes the screen does nothing else
extern bool wasPressed;         // a finger was on the glass at the last poll

void bedClockSet(uint32_t set);  // the clock was set: the bedtime edge starts over
void loadClockFields(uint32_t e);  // the clock screen's fields from an epoch
int flashIdxForDex(int16_t dex);   // the flash sprite of a dex number, -1 = none
PowerCtx powerCtx(uint32_t now);
bool screenIsLocked();
#if BOARD_AMOLED_175C
void showControlToast(const char *text, uint32_t duration = 1200);
#endif
void flushUI();   // every renderer sends its frame through this
void wakeUI();
#if BOARD_AMOLED_175C
void leaveSack();   // the sack screen's exit (defined in TamaPoke.ino)
#endif
// the pet's reactions to gestures (touchgesture.h), drawn by drawPet*
uint8_t reactAct(uint8_t kind);
bool reacting(uint32_t now);
void reactOffset(uint32_t now, bool animated, int &dx, int &dy);
bool petFollowing(uint32_t now);
int followLean(uint32_t now);
bool heartShown(uint32_t now);
void drawHuffPuff(int x, int y, uint32_t now);
