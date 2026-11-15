// The main screen's scene and pet drawing (#41: moved out of TamaPoke.ino): the sky and
// the biome, the pet with its PMD animation and walk, the bath, poops, the attention
// bubble, the header, bars and buttons, the endings and their buttons, the battery glyph
// and the clock and streak badges. render() in TamaPoke.ino puts them together.
#pragma once
#include <stdint.h>
#include "sdmon.h"    // PmdMon, PmdAct, PMD_*
#include "species.h"  // Species
#include "skylayout.h"  // STARS, the sun and the moon, the header (#70)
#include "weather.h"  // WxNow, ScenePlace (#60)

// comportamiento del bicho en pantalla
struct PetBeh {
  uint8_t mode = 0;     // 0 idle, 1 paseo, 2 gesto one-shot
  uint8_t activity = 0; // what the idle planner has it doing (IdleAct, idleplan.h; #22)
  uint8_t act = PMD_IDLE;
  uint32_t t0 = 0;      // inicio de la animacion en curso
  uint32_t until = 0;   // fin del estado actual
  float x = 233, targetX = 233;
  bool fast = false;    // walking after a dragging finger (#21) until it gets there
};

#define PET_GROUND 304  // linea de suelo de la mascota


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

extern PetBeh beh;          // the pet's walk and one-shot actions (the idle planner, idlelife.h)
extern bool gNight;         // the night scene: render() sets it from the hour or sleep
extern uint32_t bathUntil;  // the bath runs until this millis()

uint16_t inkColor();  // the UI ink for day or night
uint16_t lerp565(uint16_t a, uint16_t b, int i, int n);
int sceneHour();      // 0-23 from the last clock reading, 13 without one

// Travelling, weather and seasons (weather.h, #60). The place, the weather and the season
// come from the clock; the SCENE serial command can force each (SCENE_AUTO: the clock's).
extern uint8_t sceneForcePlace, sceneForceWx, sceneForceSeason;
uint8_t sceneHome();      // the pet's home place: its biome (the meadow for an egg)
uint8_t sceneToday();     // where the clock puts it today (home while the clock is unknown)
uint8_t scenePlace();     // the place on screen (today's before the first frame)
WxNow sceneWeather();     // the weather at scenePlace() now (fair during a ceremony)
WxNow sceneWeatherNow();  // the weather now at today's place (the standby clock's: the latch moves only in drawScene())
uint8_t sceneSeason();    // the season the scene shows (none during a ceremony or without a clock)
bool sceneStarsOut();     // the night sky shows its stars (the speech bubbles' night line)
// The sky, the place and the weather. The place changes only while nobody is looking
// (SceneLatch), so call this only for a frame of the main scene that is drawn.
void drawScene(uint32_t now, bool night);
// The standby clock (standby.h, #60): the time, the date and the weather now on black, and
// the battery; it steps a few px each minute. Draws the whole frame and flushes it.
void drawStandby(uint32_t epoch, uint8_t wxKind, bool night, int batPct, bool charging);
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
uint32_t pmdActTotalMs(const PmdAct &a);  // one run of an action's animation, ms
void drawPmdActM(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS);
void drawPmdAct(uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS);
void drawPoops();
// The top of the pet's head as drawPet() drew it this frame, hop included (#73): the emotes
// (#22) and both bubbles point at it. Call it after drawPet(). 0 when no pet was drawn.
int petHeadTop();
// The evolve, run-away or farewell button is drawn over the pet (render()): the bubbles stay
// above it (bubbleTipY in attention.h).
bool petButtonShown();
void drawAttnBubble();
void drawBars();
void drawButtons();
const char *eggMsg();
const char *statusMsg();
void drawMap(const char *const *map, int n, int x, int y, int s, bool silhouette);
