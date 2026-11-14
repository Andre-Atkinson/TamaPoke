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

extern PmdMon galleryPmd;      // the gallery's detail-page sprite
extern bool galleryOpen;
extern bool galleryDirty;      // the gallery has to be drawn again
extern int galleryPage;        // 10 pages of 16
extern int16_t galleryDetail;  // dex shown in detail, 0 = the grid
extern bool cardOpen;          // the pet card (vertical swipe)
extern bool kbOpen;            // the nickname keyboard
extern char nameBuf[12];       // the nickname being typed
extern uint8_t nameLen;
extern uint8_t cardPage;       // the card page shown, 0 to CARD_LAST_PAGE
extern bool clockOpen;         // the clock and settings screen
extern int clockH, clockM;     // the time being edited
extern int8_t eggSel;          // the egg tapped once in the egg choice, -1 = none

void renderStarterSelect();
void drawEggStyled(uint8_t rarity, uint16_t spot, int x, int y, int s);
const char *eggRarityText(uint8_t rarity);
uint16_t eggRarityColor(uint8_t rarity);
void renderEggSelect();
void eggSelectTap(int16_t x, int16_t y);
void loadClockFields(uint32_t e);  // the clock screen's fields from an epoch
void openClock();
void renderClock();
void clockTap(int16_t x, int16_t y);
void renderCard();
bool careTap(int16_t x, int16_t y);
void openKeyboard();
void renderKeyboard();
void keyboardTap(int16_t x, int16_t y);
void renderGallery();
void galleryTap(int16_t x, int16_t y);

// Used only inside screens.cpp once the code has moved there (#41); a later commit
// makes them static there.
void drawCardStat(int y, const char *label, uint16_t val, uint16_t maxBar, uint16_t color);
void applyClock();
void drawClockBtn(int x, int y, const char *l);
void drawMedalBadge(int x, int y, int i);
void renderCardProfile();
void renderCardStats();
void renderCardMedals();
void renderCardProgress();
void renderCardCare();
void drawThumb(const uint8_t *b, int x, int y, int s, bool sil);
