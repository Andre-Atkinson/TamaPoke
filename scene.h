// The main screen's scene and pet drawing (#41: moved out of TamaPoke.ino): the sky and
// the biome, the pet with its PMD animation and walk, the bath, poops, the attention
// bubble, the header, bars and buttons, the endings and their buttons, the battery glyph
// and the clock and streak badges. render() in TamaPoke.ino puts them together.
#pragma once
#include <stdint.h>
#include "sdmon.h"    // PmdMon, PmdAct, PMD_*
#include "species.h"  // Species

// comportamiento del bicho en pantalla
struct PetBeh {
  uint8_t mode = 0;     // 0 idle, 1 paseo, 2 gesto one-shot
  uint8_t act = PMD_IDLE;
  uint32_t t0 = 0;      // inicio de la animacion en curso
  uint32_t until = 0;   // fin del estado actual
  float x = 233, targetX = 233;
  bool fast = false;    // walking after a dragging finger (#21) until it gets there
};
#define PET_GROUND 304  // linea de suelo de la mascota
#define PET_WALK_MIN 150  // where the pet may walk to (behNext, and after a finger)
#define PET_WALK_MAX 325
#define PET_FOLLOW_PX_S 120.0f  // walking after a finger (a stroll is 30 px/s)

// estrellas del modo noche
inline const uint16_t STARS[][2] = { {120,140},{330,120},{370,210},{95,230},{280,90},{160,95} };

#define C565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

// suelo de cada bioma de dia (de noche se mezcla hacia el azul nocturno)
inline const uint16_t BIOME_SOIL[6] = {
  C565(0x7e, 0xc0, 0x7f),  // 0 pradera
  C565(0xdc, 0xca, 0x94),  // 1 playa (arena)
  C565(0x4f, 0x8a, 0x55),  // 2 bosque
  C565(0x8a, 0x55, 0x44),  // 3 volcan
  C565(0xa8, 0x90, 0x6a),  // 4 montana
  C565(0xe6, 0xee, 0xf5),  // 5 nieve
};

extern PetBeh beh;          // the pet's walk and one-shot actions (behNext())
extern bool gNight;         // the night scene: render() sets it from the hour or sleep
extern uint32_t bathUntil;  // the bath runs until this millis()

uint16_t inkColor();  // the UI ink for day or night
uint16_t lerp565(uint16_t a, uint16_t b, int i, int n);
int sceneHour();      // 0-23 from the last clock reading, 13 without one
void drawScene(uint8_t biome, uint32_t now, bool night);
uint16_t batteryColor(int pc, bool charging, uint16_t ink);
void drawBatteryGlyph(int x, int y, int w, int h, int pc, bool charging, uint16_t col, uint16_t bolt);
void drawClockBadge();
void drawStreakBadge();
void drawCelebration();
void drawHeader(const char *name, uint16_t nameColor, const char *msg);
void drawCeremony();
void drawChoiceDialog();
void drawEvolveButton();
void drawFarewellButton();
void drawRunawayButton();
void drawPet();
void startBath();
void drawBath();
void drawPmdActM(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS);
void drawPmdAct(uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS);
void drawPoops();
void drawAttnBubble();
void drawBars();
void drawButtons();
const char *eggMsg();
const char *statusMsg();
void drawMap(const char *const *map, int n, int x, int y, int s, bool silhouette);
