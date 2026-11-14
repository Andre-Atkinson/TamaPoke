// What TamaPoke.ino shares with the modules split out of it (#41): the sketch's own
// globals and functions that a module uses. Each one is defined once, in TamaPoke.ino;
// what a module defines for the others is declared in that module's header instead.
#pragma once
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "board.h"
#include "bedtime.h"
#include "pet.h"
#include "sdmon.h"
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
#endif

void bedClockSet(uint32_t set);  // the clock was set: the bedtime edge starts over
void loadClockFields(uint32_t e);  // the clock screen's fields from an epoch
