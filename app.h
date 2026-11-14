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
