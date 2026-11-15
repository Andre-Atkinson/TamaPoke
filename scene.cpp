// The main screen's scene and pet drawing (scene.h). Moved out of TamaPoke.ino (#41).
#include <Arduino.h>
#include "app.h"
#include "attention.h"
#include "blit.h"
#include "bubble.h"
#include "clockprompt.h"
#include "dex.h"
#include "i18n.h"
#include "idlelife.h"
#include "idleplan.h"
#include "power.h"
#include "rtcbat.h"
#include "scene.h"
#include "sdmon.h"
#include "settingslayout.h"
#include "species.h"
#include "standby.h"
#include "uitext.h"

#define PET_WALK_MIN IDLE_WALK_MIN  // where the pet may walk to (idleplan.h, and after a finger)
#define PET_WALK_MAX IDLE_WALK_MAX
#define PET_FOLLOW_PX_S 120.0f  // walking after a finger (a stroll is 30 px/s)

// Used only in this file, and called above their definitions (in TamaPoke.ino the
// prototypes arduino-cli generates covered that).
static void drawPetPMD();
static void drawPetSD();
static void overlayEye(const Species &sp, int x, int y, int s, int col);
static void overlayMouth(const Species &sp, int x, int y, int s, bool open);
static void drawBar(int x, int y, const char *label, uint8_t val);
static int followLean(uint32_t now);
static bool heartShown(uint32_t now);
static void drawHuffPuff(int x, int y, uint32_t now);

PetBeh beh;
static uint32_t walkLast = 0;  // instante del ultimo paso del paseo (0 = ninguno aun)

// escena de bano: espuma sobre el bicho y limpieza al reventar
uint32_t bathUntil = 0;
static bool bathPending = false;
static struct { int16_t x, y; uint8_t r, ph; } bubbles[14];

// ---------- render ----------

bool gNight = false;  // noche real (por hora) o durmiendo: lo fija render()
uint16_t inkColor() { return gNight ? UI_INK_NIGHT : UI_INK; }

// ---------- escena de fondo: bioma del tipo + hora real del RTC ----------

#define HORIZON 232  // linea donde el cielo se encuentra con el suelo

uint16_t lerp565(uint16_t a, uint16_t b, int i, int n) {
  if (n <= 0) return a;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)((((ar + (br - ar) * i / n) << 11)) |
                    (((ag + (bg - ag) * i / n) << 5)) | (ab + (bb - ab) * i / n));
}

// the integer square root of v >= 0 (the fog banks' outline)
static int isqrt(int v) {
  int r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}

// hora del dia 0-23 (de la hora real cacheada cada 30s; 13 si no hay reloj)
int sceneHour() {
  uint32_t e = pet.lastSeenEpoch;
  return e ? (int)((e / 3600) % 24) : 13;
}

// ---------- travelling and weather (weather.h, #60) ----------

uint8_t sceneForcePlace = SCENE_AUTO, sceneForceWx = SCENE_AUTO, sceneForceSeason = SCENE_AUTO;  // SCENE
static SceneLatch sceneLatch;  // the place on screen: it follows sceneToday() only unseen (weather.h)

// the clock the scene follows: 0 while it is not trusted (never set, or reseeded)
static uint32_t sceneClock() { return sceneEpoch(pet.lastSeenEpoch, bedEdge.stale()); }

uint8_t sceneHome() {
  if (pet.isEgg() || pet.speciesId > DEX_COUNT) return PLACE_MEADOW;
  uint8_t b = DEX_TBL[pet.speciesId].biome;
  return b < PLACES ? b : (uint8_t)PLACE_MEADOW;
}

// an egg stays in the meadow, where it has always been drawn
uint8_t sceneToday() { return pet.isEgg() ? (uint8_t)PLACE_MEADOW : travelPlace(sceneHome(), sceneClock()); }

uint8_t scenePlace() {
  if (sceneForcePlace < PLACES) return sceneForcePlace;
  return sceneLatch.shown < PLACES ? sceneLatch.shown : sceneToday();
}

// The farewell and the run-away keep their own look: a clear sky (the run-away draws its
// own rain, drawCeremony()).
WxNow sceneWeather() { return sceneWx(pet.ceremony, sceneForceWx, scenePlace(), sceneClock(), sceneForceSeason); }

// The clock's weather is the weather now at today's place (the forced place, if any): the
// latched place only moves when drawScene() draws a frame, which the standby clock never does.
WxNow sceneWeatherNow() {
  uint8_t place = sceneForcePlace < PLACES ? sceneForcePlace : sceneToday();
  return sceneWx(pet.ceremony, sceneForceWx, place, sceneClock(), sceneForceSeason);
}

uint8_t sceneSeason() { return sceneSeasonOf(pet.ceremony, sceneForceSeason, sceneClock()); }

bool sceneStarsOut() { return wxStarsOut(sceneWeather()); }

// The place for this frame: a new day's place (or a new pet's home) is taken up only while
// nobody is looking, or when the pet turns into an egg (SceneLatch), so the pet is never
// seen jumping from one place to another.
static uint8_t scenePlaceForFrame(uint32_t now) {
  sceneLatch.frame(now, sceneToday(), pet.isEgg(), dimStage == DIM_OFF);
  return scenePlace();
}

// The weather's colours: the sky greys toward the overcast colour (wxGrey) and veils toward
// the fog colour (wxFog); the clouds darken with it.
#define WX_OVERCAST_DAY C565(0x9c, 0xa6, 0xb4)
#define WX_OVERCAST_NIGHT C565(0x22, 0x27, 0x34)
#define WX_FOG_DAY C565(0xd8, 0xde, 0xe4)
#define WX_FOG_NIGHT C565(0x3c, 0x43, 0x52)

// Up to six clouds drifting right across the sky, spread along it; the first two are the
// ones a clear day has always had. `s` is their size in sixteenths (16 = the clear day's).
#define SCENE_CLOUDS_MAX 6
static void drawClouds(uint32_t now, int n, int s, uint16_t col) {
  static const int16_t AT[SCENE_CLOUDS_MAX][2] = { { 0, 70 }, { 250, 104 }, { 125, 40 }, { 375, 58 }, { 190, 132 }, { 470, 88 } };
  for (int k = 0; k < n && k < SCENE_CLOUDS_MAX; k++) {
    int cx = (int)((now / 50 + AT[k][0]) % 560) - 40;
    int cy = AT[k][1];
    gfx->fillCircle(cx, cy, 16 * s / 16, col);
    gfx->fillCircle(cx + 18 * s / 16, cy + 3 * s / 16, 13 * s / 16, col);
    gfx->fillCircle(cx - 15 * s / 16, cy + 4 * s / 16, 12 * s / 16, col);
  }
}

// Rain: n drops falling at a slant (a storm's steeper and longer), down to the bottom panel.
// Each drop moves along its own slant, so at 10 fps they read as streaks of rain.
static void drawRain(uint32_t now, int n, bool storm, bool night) {
  uint16_t col = night ? C565(0x7c, 0x92, 0xbc) : C565(0x48, 0x66, 0x9c);
  int len = storm ? 16 : 12, slant = storm ? 6 : 3;
  uint32_t fall = storm ? now * 2 / 3 : now / 2;  // px: 670 or 500 a second
  for (int i = 0; i < n; i++) {
    int y = (int)((fall + i * 91) % 330) - 18;
    int x = 486 - (int)((fall * slant / len + i * 47 + i / 3 * 13) % 520);
    gfx->drawLine(x, y, x - slant, y + len, col);  // 2 px wide: a 1 px line is a hair on the panel
    gfx->drawLine(x + 1, y, x + 1 - slant, y + len, col);
  }
}

// Snow: n flakes drifting down to the bottom panel (as the snowfield always had, over the
// ground too).
static void drawSnow(uint32_t now, int n, bool night) {
  uint16_t col = night ? C565(0xc4, 0xcc, 0xda) : UI_WHITE;
  for (int f = 0; f < n; f++) {
    int fx = (int)((f * 53 + now / 40) % 466);
    int fy = (int)((f * 90 + now / 18) % 312);
    int sz = f % 3 == 0 ? 5 : 4;
    gfx->fillRect(fx, fy, sz, sz, col);
  }
}

// A storm's lightning: a pale zigzag bolt for 150 ms every 8 s, somewhere in the sky. Only
// the bolt is lit; the screen never flashes.
static void drawLightning(uint32_t now) {
  const uint32_t every = 8000, lit = 150;
  if (now % every >= lit) return;
  uint32_t r = sceneHash(now / every);
  int x = 70 + (int)(r % 320), y = 24;
  uint16_t col = C565(0xff, 0xf4, 0xc0);
  for (int seg = 0; seg < 5; seg++) {
    int nx = x + (int)((r >> (seg * 3)) % 25) - 12, ny = y + 30;
    for (int d = -1; d <= 1; d++) gfx->drawLine(x + d, y, nx + d, ny, col);
    x = nx;
    y = ny;
  }
}

// Fog: pale banks drifting slowly across the horizon, over the far scenery and behind the
// pet. Each bank is a long ellipse drawn on every other row, so the scene shows through it
// as through mist. `fog` 0-16 thickens them as the fog comes in.
static void drawFog(uint32_t now, uint8_t fog, uint16_t col) {
  static const int16_t BANKS[4][3] = {  // start x, centre y, half width
    { 0, HORIZON - 58, 170 }, { 300, HORIZON - 26, 210 }, { 120, HORIZON + 8, 190 }, { 470, HORIZON + 36, 160 } };
  int hh = 18 * fog / WX_MIX_FULL;  // half height
  if (hh < 2) return;
  for (int k = 0; k < 4; k++) {
    int hw = BANKS[k][2], cx = (int)((now / 120 + BANKS[k][0]) % 900) - 220, cy = BANKS[k][1];
    for (int dy = -hh + ((cy + hh) & 1); dy <= hh; dy += 2) {  // the same rows in every bank
      int half = hw * isqrt(hh * hh - dy * dy) / hh;
      gfx->fillRect(cx - half, cy + dy, 2 * half, 1, col);
    }
  }
}

// ---------- the seasons (weather.h, #60) ----------

// a triangle wave from -a to a and back, `period` ms long, at phase `ph` ms
static int sway(uint32_t now, uint32_t period, uint32_t ph, int a) {
  int t = (int)((now + ph) % period), half = (int)(period / 2);
  int v = t < half ? t : (int)period - t;  // 0..half..0
  return v * 4 * a / (int)period - a;
}

// spots on the ground (y 238-300, between the horizon and the bottom panel) for the spring
// flowers and the fallen autumn leaves
static const int16_t GROUND_SPOTS[10][2] = { { 40, 252 }, { 98, 276 }, { 150, 244 }, { 205, 292 }, { 262, 254 },
                                             { 318, 282 }, { 372, 246 }, { 424, 270 }, { 70, 298 }, { 396, 300 } };

// what lies on the ground with the season: spring flowers, autumn's fallen leaves, winter's
// snowcaps on the forest's trees and the mountain's peaks (the snow cover and autumn's russet
// are in the soil colour, drawScene())
static void drawSeasonGround(uint8_t ground, uint8_t biome, bool night, uint16_t snowCol) {
  if (ground == GROUND_FLOWERS || ground == GROUND_AUTUMN) {
    static const uint16_t FLOWERS[3] = { C565(0xf4, 0x9c, 0xc8), C565(0xff, 0xff, 0xff), C565(0xc8, 0xa8, 0xf0) };
    static const uint16_t LEAVES[3] = { C565(0xe8, 0x84, 0x2c), C565(0xc8, 0x40, 0x2c), C565(0xe8, 0xb8, 0x38) };
    uint16_t dusk = C565(0x16, 0x1c, 0x30);
    for (int i = 0; i < 10; i++) {
      int x = GROUND_SPOTS[i][0], y = GROUND_SPOTS[i][1];
      if (ground == GROUND_FLOWERS) {  // four petals round a yellow heart
        uint16_t c = FLOWERS[i % 3], mid = C565(0xff, 0xd8, 0x40);
        if (night) { c = lerp565(c, dusk, 9, 16); mid = lerp565(mid, dusk, 9, 16); }
        gfx->fillRect(x - 2, y - 4, 4, 2, c);
        gfx->fillRect(x - 2, y + 2, 4, 2, c);
        gfx->fillRect(x - 4, y - 2, 2, 4, c);
        gfx->fillRect(x + 2, y - 2, 2, 4, c);
        gfx->fillRect(x - 2, y - 2, 4, 4, mid);
      } else {  // a fallen leaf
        uint16_t c = night ? lerp565(LEAVES[i % 3], dusk, 9, 16) : LEAVES[i % 3];
        gfx->fillRect(x - 3, y - 1, 6, 3, c);
        gfx->fillRect(x - 1, y - 2, 2, 5, c);
      }
    }
  } else if (ground == GROUND_SNOW) {
    if (biome == PLACE_FOREST)  // snow on the tree tops
      for (int tx : { 60, 150, 360, 416 }) gfx->fillTriangle(tx, HORIZON - 60, tx - 5, HORIZON - 48, tx + 5, HORIZON - 48, snowCol);
    else if (biome == PLACE_MOUNTAIN) {  // snowcaps on the peaks
      gfx->fillTriangle(140, HORIZON - 50, 118, HORIZON - 36, 162, HORIZON - 36, snowCol);
      gfx->fillTriangle(330, HORIZON - 38, 309, HORIZON - 28, 351, HORIZON - 28, snowCol);
    }
  }
}

// What flies or falls with the season (seasonFx), only at the full frame rate.
static void drawSeasonFx(uint32_t now, uint8_t fx, bool night) {
  if (fx == SEASON_FX_PETALS || fx == SEASON_FX_LEAVES) {
    bool petals = fx == SEASON_FX_PETALS;
    static const uint16_t LEAVES[4] = { C565(0xe8, 0x84, 0x2c), C565(0xc8, 0x40, 0x2c), C565(0xa0, 0x64, 0x30),
                                        C565(0xe8, 0xb8, 0x38) };
    int n = petals ? 12 : 10;
    for (int i = 0; i < n; i++) {
      uint32_t fall = now / (petals ? 26 : 22);
      int y = (int)((fall + i * 53) % 330) - 14;
      int x = (int)((i * 71 + now / 60) % 500) - 17 + sway(now, petals ? 2600 : 1800, i * 377, petals ? 12 : 18);
      uint16_t c = petals ? C565(0xf6, 0xb4, 0xd4) : LEAVES[i % 4];
      if (night) c = lerp565(c, C565(0x16, 0x1c, 0x30), 8, 16);
      bool turned = ((now + i * 211) / 300) % 2;  // a leaf tumbles, a petal flutters
      if (petals) gfx->fillRect(x, y, turned ? 3 : 4, turned ? 3 : 2, c);
      else gfx->fillRect(x, y, turned ? 3 : 5, turned ? 5 : 3, c);
    }
  } else if (fx == SEASON_FX_BUTTERFLIES) {
    static const uint16_t WINGS[3] = { UI_WHITE, C565(0xff, 0xd8, 0x40), C565(0xf4, 0x98, 0x3c) };
    for (int i = 0; i < 3; i++) {
      int x = 120 + i * 110 + sway(now, 9000 + i * 1300, i * 2500, 70);
      int y = 168 + i * 18 + sway(now, 5200 + i * 900, i * 1700, 26);
      bool open = ((now + i * 70) / 140) % 2;  // the wings beat
      uint16_t c = WINGS[i];
      if (open) {
        gfx->fillTriangle(x, y, x - 7, y - 6, x - 6, y + 4, c);
        gfx->fillTriangle(x, y, x + 7, y - 6, x + 6, y + 4, c);
      } else {
        gfx->fillTriangle(x, y, x - 2, y - 7, x - 1, y + 3, c);
        gfx->fillTriangle(x, y, x + 2, y - 7, x + 1, y + 3, c);
      }
      gfx->fillRect(x - 1, y - 3, 2, 6, C565(0x30, 0x28, 0x24));  // the body
    }
  } else if (fx == SEASON_FX_FIREFLIES) {
    for (int i = 0; i < 9; i++) {
      if ((now + i * 397) % 2400 >= 1000) continue;  // each glows for 1 s in 2.4
      int x = 40 + (int)((i * 47 + now / 120) % 390) + sway(now, 3000, i * 500, 8);
      int y = 160 + (i * 37) % 130 + sway(now, 2200, i * 300, 10);
      gfx->fillRect(x - 2, y - 2, 5, 5, C565(0x58, 0x6c, 0x2c));  // the glow
      gfx->fillRect(x - 1, y - 1, 3, 3, C565(0xec, 0xff, 0x88));
    }
  }
}

void drawScene(uint32_t now, bool night) {
  uint8_t biome = scenePlaceForFrame(now);
  WxNow wx = sceneWeather();
  uint8_t grey = wxGrey(wx), fog = wxFog(wx);
  bool live = wxParticlesLive(frameIntervalMs(powerCtx(now)));
  int h = sceneHour();
  uint16_t top, bot;
  if (night)            { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (h < 8)       { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }  // amanecer
  else if (h < 18)      { top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }  // dia
  else                  { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }  // atardecer
  uint16_t fogCol = night ? WX_FOG_NIGHT : WX_FOG_DAY;
  if (grey) {
    uint16_t g = night ? WX_OVERCAST_NIGHT : WX_OVERCAST_DAY;
    top = lerp565(top, g, grey, 16);
    bot = lerp565(bot, g, grey, 16);
  }
  if (fog) {  // the fog is thickest low down
    top = lerp565(top, fogCol, fog / 2, 16);
    bot = lerp565(bot, fogCol, fog * 3 / 4, 16);
  }

  // cielo en bandas
  for (int y = 0; y < HORIZON; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, HORIZON));

  // sol o luna (sin estrellas ni astros bajo un cielo cubierto), below the header (#70)
  bool sunOut = wxSunOut(wx);
  if (night) {
    if (sunOut) {
      gfx->fillCircle(SKY_MOON_X, SKY_MOON_Y, SKY_MOON_R, C565(0xe8, 0xee, 0xf5));
      // creciente: the sky's own colour at that height bites the moon
      gfx->fillCircle(SKY_MOON_X + 10, SKY_MOON_Y - 6, SKY_MOON_R - 2, lerp565(top, bot, SKY_MOON_Y - 6, HORIZON));
    }
    if (wxStarsOut(wx))
      for (auto &st : STARS) gfx->fillRect(st[0], st[1], SKY_STAR_SIZE, SKY_STAR_SIZE, UI_WHITE);
  } else if (h < 18) {
    if (sunOut)
      gfx->fillCircle(SKY_SUN_X, SKY_SUN_Y, SKY_SUN_R, h < 8 ? C565(0xff, 0xd9, 0x8a) : C565(0xff, 0xe7, 0x9f));
  } else if (sunOut) {
    gfx->fillCircle(233, HORIZON - 6, 34, C565(0xff, 0xf1, 0xc8));  // sol poniente
  }
  // nubes: las dos de siempre de dia con buen tiempo; mas, mayores y mas oscuras con el
  // tiempo que trae nubes, tambien al atardecer y de noche
  uint8_t clouds = wxClouds(wx, (!night && h < 18) ? 2 : 0);
  if (clouds) {
    uint16_t light = night ? C565(0x44, 0x4c, 0x66) : h >= 18 ? C565(0xf2, 0xc8, 0xb0) : UI_WHITE;
    uint16_t dark = night ? C565(0x2a, 0x30, 0x40) : C565(0x74, 0x7c, 0x8c);
    drawClouds(now, clouds, 16 + grey * 3 / 4, lerp565(light, dark, grey, 16));
  }

  // mar de la playa: una franja de agua sobre la arena
  uint16_t soil = BIOME_SOIL[biome < 6 ? biome : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  uint8_t wet = wxWet(wx);
  if (wet) soil = lerp565(soil, C565(0x20, 0x28, 0x30), wet / 4, 16);  // ground darkened by the rain
  if (fog) soil = lerp565(soil, fogCol, fog / 3, 16);
  uint8_t snowy = wxAmount(wx, WX_BIT(WX_SNOW));
  uint16_t snowCol = night ? C565(0x9c, 0xa6, 0xb8) : C565(0xf2, 0xf6, 0xfa);
  if (snowy) soil = lerp565(soil, snowCol, snowy * 5 / 16, 16);  // dusted
  uint8_t season = sceneSeason(), ground = seasonGround(season, biome);
  if (ground == GROUND_AUTUMN) soil = lerp565(soil, night ? C565(0x4a, 0x34, 0x24) : C565(0xc4, 0x8c, 0x40), 6, 16);
  else if (ground == GROUND_SNOW) soil = lerp565(soil, snowCol, 12, 16);  // winter: snow lies on the ground
  if (biome == 1) {
    uint16_t sea = night ? C565(0x1c, 0x34, 0x52) : C565(0x4f, 0x96, 0xc4);
    gfx->fillRect(0, HORIZON - 26, 466, 26, sea);
    for (int i = 0; i < 3; i++) {
      int wy = HORIZON - 22 + i * 7;
      uint16_t fc = night ? C565(0x3a, 0x58, 0x78) : C565(0xbf, 0xe6, 0xf5);
      gfx->fillRect(60 + ((now / 60 + i * 30) % 60), wy, 26, 2, fc);
      gfx->fillRect(300 - ((now / 60 + i * 20) % 60), wy, 26, 2, fc);
    }
  }

  // suelo
  gfx->fillRect(0, HORIZON, 466, 466 - HORIZON, soil);
  uint16_t hill = lerp565(soil, night ? C565(0x0c, 0x12, 0x24) : C565(0xff, 0xff, 0xff), ground == GROUND_SNOW ? 8 : 3, 16);
  gfx->fillRoundRect(-60, HORIZON - 14, 586, 60, 30, hill);

  // detalles del bioma
  uint16_t dk = lerp565(soil, C565(0x10, 0x18, 0x20), night ? 11 : 7, 16);
  if (biome == 2) {  // bosque: coniferas en silueta
    for (int tx : { 60, 150, 360, 416 }) {
      gfx->fillTriangle(tx, HORIZON - 46, tx - 16, HORIZON, tx + 16, HORIZON, dk);
      gfx->fillTriangle(tx, HORIZON - 60, tx - 12, HORIZON - 28, tx + 12, HORIZON - 28, dk);
    }
  } else if (biome == 3) {  // volcan: rocas y brasas
    gfx->fillTriangle(70, HORIZON, 40, HORIZON + 30, 100, HORIZON + 30, dk);
    gfx->fillTriangle(400, HORIZON + 4, 372, HORIZON + 30, 430, HORIZON + 30, dk);
    if (!night)
      for (int e = 0; e < 4; e++)
        gfx->fillRect(120 + e * 70, HORIZON + 8 + (e % 2) * 6, 4, 4, C565(0xff, 0x9b, 0x3a));
  } else if (biome == 4) {  // montana: cumbres al fondo
    gfx->fillTriangle(140, HORIZON - 50, 60, HORIZON, 220, HORIZON, dk);
    gfx->fillTriangle(330, HORIZON - 38, 250, HORIZON, 410, HORIZON, dk);
  } else if (biome == 0) {  // pradera: matas de hierba
    for (int gx : { 80, 175, 300, 395 })
      for (int b = -1; b <= 1; b++)
        gfx->fillRect(gx + b * 5, HORIZON + 6, 2, 8 + (b == 0 ? 4 : 0), dk);
  }
  // (the snowfield's falling flakes are its snowy weather now, #60)
  drawSeasonGround(ground, biome, night, snowCol);

  // el tiempo por delante del paisaje y por detras del bicho
  if (fog) drawFog(now, fog, fogCol);
  if (wet >= WX_MIX_FULL / 2) {  // charcos
    uint16_t pud = lerp565(soil, night ? C565(0x3a, 0x4c, 0x6c) : C565(0x9c, 0xb8, 0xd4), 10, 16);
    gfx->fillEllipse(58, 286, 30, 5, pud);
    gfx->fillEllipse(410, 296, 24, 4, pud);
  }
  if (!live) return;  // dimmed or slow frames: no falling rain or snow (they would step)
  drawSeasonFx(now, seasonFx(season, biome, night, wx), night);
  uint8_t drops = wxDrops(wx), flakes = wxFlakes(wx);
  if (drops) drawRain(now, drops, wx.kind == WX_STORM || wx.prev == WX_STORM, night);
  if (flakes) drawSnow(now, flakes, night);
  if (wxLightning(wx)) drawLightning(now);
}

// ---------- the standby clock (standby.h, #60) ----------

#define STANDBY_INK C565(0xd8, 0xde, 0xe8)   // the digits
#define STANDBY_DATE C565(0xb4, 0xbc, 0xcc)  // the date
#define STANDBY_DIM C565(0x80, 0x88, 0x9a)   // the battery

// a cloud of three circles centred on (cx, cy), `s` px across its middle puff
static void drawIconCloud(int cx, int cy, int s, uint16_t col) {
  gfx->fillCircle(cx - s * 7 / 10, cy + s / 5, s * 2 / 3, col);
  gfx->fillCircle(cx + s * 7 / 10, cy + s / 5, s * 2 / 3, col);
  gfx->fillCircle(cx, cy - s / 5, s, col);
  gfx->fillRect(cx - s * 7 / 10, cy + s / 5, s * 14 / 10, s * 2 / 3, col);
}

// a crescent moon of radius r centred on (cx, cy), cut out of the black standby screen
static void drawIconMoon(int cx, int cy, int r, uint16_t col) {
  gfx->fillCircle(cx, cy, r, col);
  gfx->fillCircle(cx + r * 2 / 5, cy - r * 3 / 10, r * 4 / 5, 0x0000);
}

// the weather now as a small picture centred on (cx, cy), ~64 px across (no words: the
// same in every language); at night a moon where the sun would be
static void drawWxIcon(uint8_t kind, int cx, int cy, bool night) {
  const uint16_t sun = C565(0xf0, 0xc8, 0x5a), cloud = C565(0xb4, 0xbc, 0xca), dark = C565(0x6c, 0x74, 0x86),
                 rain = C565(0x6a, 0x9c, 0xe0), bolt = C565(0xff, 0xe0, 0x60);
  switch (kind) {
    case WX_CLEAR:
      if (night) {
        drawIconMoon(cx, cy, 18, C565(0xe8, 0xee, 0xf5));
        break;
      }
      gfx->fillCircle(cx, cy, 14, sun);
      for (int i = 0; i < 8; i++) {  // rays every 45 degrees
        static const int8_t D[8][2] = { { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 } };
        int r0 = D[i][0] && D[i][1] ? 15 : 20, r1 = D[i][0] && D[i][1] ? 22 : 29;
        for (int t = -1; t <= 1; t++)
          gfx->drawLine(cx + D[i][0] * r0 + (D[i][1] ? t : 0), cy + D[i][1] * r0 + (D[i][0] ? t : 0),
                        cx + D[i][0] * r1 + (D[i][1] ? t : 0), cy + D[i][1] * r1 + (D[i][0] ? t : 0), sun);
      }
      break;
    case WX_CLOUDY:
      if (night) drawIconMoon(cx + 12, cy - 10, 13, C565(0xe8, 0xee, 0xf5));  // the moon behind a cloud
      else gfx->fillCircle(cx + 12, cy - 10, 13, sun);                         // the sun behind a cloud
      drawIconCloud(cx - 4, cy + 4, 15, cloud);
      break;
    case WX_RAIN:
    case WX_STORM:
      drawIconCloud(cx, cy - 8, 15, kind == WX_STORM ? dark : cloud);
      if (kind == WX_STORM) {
        gfx->fillTriangle(cx + 2, cy + 6, cx - 9, cy + 22, cx + 1, cy + 22, bolt);
        gfx->fillTriangle(cx - 1, cy + 18, cx + 9, cy + 18, cx - 4, cy + 34, bolt);
      } else {
        for (int i = -1; i <= 1; i++)
          for (int t = 0; t < 2; t++) gfx->drawLine(cx + i * 12 + t, cy + 12, cx + i * 12 - 5 + t, cy + 26, rain);
      }
      break;
    case WX_SNOW:
      drawIconCloud(cx, cy - 8, 15, cloud);
      for (int i = -1; i <= 1; i++) gfx->fillRect(cx + i * 12 - 2, cy + 16 + (i & 1) * 6, 5, 5, UI_WHITE);
      break;
    default:  // fog
      for (int i = 0; i < 3; i++) gfx->fillRoundRect(cx - 26 + (i & 1) * 8, cy - 14 + i * 12, 44, 6, 3, cloud);
      break;
  }
}

void drawStandby(uint32_t epoch, uint8_t wxKind, bool night, int batPct, bool charging) {
  gfx->fillScreen(0x0000);  // black: an AMOLED lights only the digits
  int dx, dy;
  standbyShift(epoch / 60, dx, dy);
  char t[8];
  snprintf(t, sizeof t, "%02u:%02u", (unsigned)((epoch / 3600) % 24), (unsigned)((epoch / 60) % 60));
  gfx->setTextColor(STANDBY_INK);
  setSize(9);
  setCur(centerX(t, 9) + dx, STANDBY_TIME_Y + dy);
  printT(t);
  CivilDate d = civilDate(epoch);
  char date[STANDBY_DATE_MAX];
  formatDate(date, sizeof date, d.weekday, d.day, d.month);
  gfx->setTextColor(STANDBY_DATE);
  setSize(3);
  setCur(centerX(date, 3) + dx, STANDBY_DATE_Y + dy);
  printT(date);
  drawWxIcon(wxKind, CX + dx, STANDBY_WX_Y + dy, night);
  if (batPct >= 0) {  // the glyph (a bolt while charging) and the percentage
    char s[8];
    batteryPctLabel(s, sizeof s, batPct, charging);
    setSize(2);
    int w = 32 + 8 + textW(s, 2), x = CX - w / 2 + dx, y = STANDBY_BAT_Y + dy;
    drawBatteryGlyph(x, y, 32, 14, batPct, charging, batteryColor(batPct, charging, STANDBY_DIM), C565(0xff, 0xd9, 0x4a));
    gfx->setTextColor(STANDBY_DIM);
    setCur(x + 40, y - 1);
    printT(s);
  }
  flushUI();
}

// battery colour: green while charging, else `ink` down to 40 %, then amber, then red
uint16_t batteryColor(int pc, bool charging, uint16_t ink) {
  return charging ? UI_BAR_OK : pc >= 40 ? ink : pc >= 15 ? UI_BAR_WARN : UI_BAR_BAD;
}

// The battery glyph from (x, y): a w x h body with the level (or a bolt while charging) and
// a terminal on its right. The header draws it 24 x 11; the battery pill draws it larger,
// and every length scales from the 11 px design (at h = 11 nothing changes). From h = 14
// the outline is 2 px thick, so the bigger glyph keeps the weight of the pill's other icons.
void drawBatteryGlyph(int x, int y, int w, int h, int pc, bool charging, uint16_t col, uint16_t bolt) {
  auto s = [h](int v) { return (v * h + 5) / 11; };  // a length at h = 11, scaled to h
  int th = h >= 14 ? 2 : 1;
  for (int i = 0; i < th; i++) gfx->drawRoundRect(x + i, y + i, w - 2 * i, h - 2 * i, 2, col);
  gfx->fillRect(x + w, y + s(3), s(3), s(5), col);  // borne
  if (charging) {
    // rayo de carga (zigzag) en vez de la barra de nivel
    int bx = x + w / 2;
    gfx->fillTriangle(bx + s(3), y + s(1), bx - s(4), y + s(6), bx + s(1), y + s(6), bolt);
    gfx->fillTriangle(bx - s(1), y + s(5), bx + s(4), y + s(5), bx - s(3), y + s(10), bolt);
  } else {
    int in = th + 1;  // the level sits 1 px inside the outline
    int fw = (w - 2 * in) * pc / 100;
    if (fw > 0) gfx->fillRect(x + in, y + in, fw, h - 2 * in, col);
  }
}

// Clock badge (clockprompt.h) while the clock was reseeded and not set: a clock face with
// a red dot, in the sky left of the pet. A tap opens the clock screen (onTap).
void drawClockBadge() {
  if (!clockPrompt.asking(bedEdge.stale())) return;
  const int x = CLOCK_BADGE_X, y = CLOCK_BADGE_Y, r = CLOCK_BADGE_R;
  gfx->fillCircle(x, y, r, UI_WHITE);
  gfx->drawCircle(x, y, r, UI_INK);
  gfx->drawCircle(x, y, r - 1, UI_INK);
  for (int i = 0; i < 4; i++) {  // marks at 12, 3, 6 and 9
    int dx = i == 1 ? 1 : i == 3 ? -1 : 0, dy = i == 0 ? -1 : i == 2 ? 1 : 0;
    gfx->fillCircle(x + dx * (r - 6), y + dy * (r - 6), 2, UI_INK);
  }
  for (int d = -1; d <= 1; d++) {  // hands 3 px thick: minute to 12, hour to 4
    gfx->drawLine(x + d, y, x + d, y - (r - 9), UI_INK);
    gfx->drawLine(x, y + d, x + 10, y + 6 + d, UI_INK);
  }
  gfx->fillCircle(x, y, 3, UI_INK);
  gfx->fillCircle(x + r - 5, y - r + 5, 7, UI_BAR_BAD);  // "needs you" dot
  gfx->drawCircle(x + r - 5, y - r + 5, 7, UI_WHITE);
}

// llama + numero de racha arriba a la izquierda
void drawStreakBadge() {
  if (pet.streak < 1) return;
  int x = 26, y = 16;
  gfx->fillTriangle(x + 8, y, x + 1, y + 17, x + 15, y + 17, UI_BAR_BAD);
  gfx->fillTriangle(x + 8, y + 7, x + 4, y + 17, x + 12, y + 17, UI_BAR_WARN);
  char s[6];
  snprintf(s, sizeof(s), "%u", pet.streak);
  gfx->setTextColor(inkColor());
  setSize(2);
  setCur(x + 22, y + 2);
  printT(s);
}

// banner temporal: medalla nueva o hito de racha
void drawCelebration() {
  const char *l1 = nullptr, *l2 = nullptr;
  // TestBuffersDeTexto (test/test_tools.py) vigila que "%u DIAS SEGUIDOS!" quepa.
  char buf[32];
  if (pet.showMedal()) {
    for (int i = 0; i < MED_COUNT; i++)
      if (pet.newMedal & (1 << i)) { l2 = medalName(i); break; }
    l1 = T(S_MEDAL_BANNER);
  } else if (pet.showMilestone()) {
    snprintf(buf, sizeof(buf), T(S_STREAK_DAYS_FMT), pet.streak);
    l1 = T(S_GREAT);
    l2 = buf;
  }
  if (!l1) return;
  gfx->fillRoundRect(73, 150, 320, 96, 16, UI_BAR_WARN);
  gfx->drawRoundRect(73, 150, 320, 96, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(l1, 3), 176);
  printT(l1);
  setSize(2);
  setCur(centerX(l2, 2), 212);
  printT(l2);
}

static void drawBattery() {
  int pc = batPercent();
  if (pc < 0) return;  // sin bateria conectada
  bool charging = batCharging();
  uint16_t col = batteryColor(pc, charging, inkColor());
  if (batAsPercent) {
    char s[8];
    batteryPctLabel(s, sizeof(s), pc, charging);
    gfx->setTextColor(col);
    setSize(2);
    setCur(centerX(s, 2), 8);
    printT(s);
    return;
  }
  drawBatteryGlyph(CX - 14, 12, 24, 11, pc, charging, col, C565(0xff, 0xd9, 0x4a));
}

void drawHeader(const char *name, uint16_t nameColor, const char *msg) {
  drawBattery();
  gfx->setTextColor(nameColor);
  uint8_t hs = headerSize((unsigned)strlen(name));  // size 3, or 2 for a name too wide for the round edge
  setSize(hs);
  setCur(centerX(name, hs), hs == 3 ? HEADER_Y3 : HEADER_Y2);
  printT(name);
  gfx->setTextColor(inkColor());
  setSize(2);
  setCur(centerX(msg, 2), HEADER_STATUS_Y);
  printT(msg);
}

// animacion de la ceremonia (10s): despedida = reverencia con corazones y se
// aleja caminando; escapada = se asusta y sale corriendo. Sustituye al idle.
void drawCeremony() {
  if (!pmd.loaded) { drawPet(); return; }  // respaldo si no hay sprite PMD
  uint32_t now = millis();
  float t = pet.ceremonyT();               // 0..1 a lo largo de los 10s
  bool panic = (pet.ceremony == CER_RUNAWAY);
  int x = CX, y = PET_GROUND;
  uint8_t act = PMD_IDLE;

  if (panic) {
    // final triste: penumbra azulada + lluvia
    for (int i = 0; i < 46; i++) {
      int rx = (i * 47 + now / 3) % 466;
      int ry = (i * 91 + now / 2) % 470;
      gfx->drawLine(rx, ry, rx - 3, ry + 12, C565(0x6a, 0x84, 0xb0));
    }
    bool fade = false;
    if (t < 0.30f) {                       // cabizbajo, temblando
      act = pmd.has(PMD_HURT) ? PMD_HURT : PMD_IDLE;
      x = CX + (int)(4 * sinf(now * 0.04f));
    } else {                               // se aleja despacio y se desvanece
      act = pmd.has(PMD_WALKL) ? PMD_WALKL : PMD_IDLE;
      x = CX - (int)(((t - 0.30f) / 0.70f) * (CX + 120));
      fade = (t > 0.6f) && ((now / 160) % 2 == 0);  // parpadea hacia la silueta
    }
    drawPmdAct(act, x, y, now, true, fade, 5);  // fade=silueta: se difumina al irse
    // lagrima cayendo del bicho
    if (t < 0.55f) {
      int ty = y - 150 + (int)((now / 6) % 40);
      gfx->fillRect(x + 6, ty, 3, 6, C565(0x9a, 0xc4, 0xe8));
    }
    return;
  }

  // despedida epica: halo dorado pulsante + chispas y corazones que ascienden
  int gcy = PET_GROUND - 96;
  for (int k = 0; k < 4; k++) {
    int r = 60 + k * 34 + (int)(10 * sinf(now * 0.02f));
    gfx->drawCircle(CX, gcy, r, C565(0xff, 0xdf, 0x8a));
  }
  for (int i = 0; i < 16; i++) {
    int px = (i * 71 + 28) % 466;
    int py = 410 - (int)((now / 8 + i * 70) % 360);   // suben y reaparecen abajo
    if (py < 30) continue;
    if (i % 4 == 0) drawMap(SPR_HEART, 32, px - 8, py - 8, 1, false);  // corazoncito
    else gfx->fillRect(px, py, 4, 4, (i % 2) ? C565(0xff, 0xe7, 0x9f) : C565(0xff, 0x9a, 0xc0));
  }

  if (t < 0.45f) {                         // reverencia / pose de despedida
    act = pmd.has(PMD_POSE) ? PMD_POSE : (pmd.has(PMD_NOD) ? PMD_NOD : PMD_IDLE);
  } else {                                 // se aleja por la derecha
    act = pmd.has(PMD_WALKR) ? PMD_WALKR : PMD_IDLE;
    x = CX + (int)(((t - 0.45f) / 0.55f) * (CX + 140));
  }
  drawPmdAct(act, x, y, now, true, false, 5);
  if (pet.showHeart())                     // corazon grande siguiendo al bicho
    drawMap(SPR_HEART, 32, x + 50, y - 190, 2, false);
}

// dialogo de decision (2 botones apilados): evolucionar/mantener o despedirse/quedaros
void drawChoiceDialog() {
  const char *q, *o1, *o2;
  uint16_t c1, c2, t1, t2;
  if (choiceKind == 1) {  // evolucion
    q = T(S_EVO_Q); o1 = T(S_EVO_TAP); o2 = T(S_EVO_KEEP);
    c1 = UI_BAR_BAD; t1 = UI_WHITE; c2 = UI_TRACK; t2 = UI_INK;
  } else {                // despedida
    q = T(S_FAR_Q); o1 = T(S_FAR_GO); o2 = T(S_FAR_STAY);
    c1 = UI_BAR_WARN; t1 = UI_INK; c2 = UI_BAR_OK; t2 = UI_WHITE;
  }
  gfx->fillRoundRect(73, 156, 320, 188, 16, UI_WHITE);
  gfx->drawRoundRect(73, 156, 320, 188, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(centerX(q, 2), 176);
  printT(q);
  gfx->fillRoundRect(93, 206, 280, 52, 12, c1);     // boton accion
  gfx->setTextColor(t1);
  setCur(centerX(o1, 2), 224);
  printT(o1);
  gfx->fillRoundRect(93, 268, 280, 52, 12, c2);     // boton mantener/quedaros
  gfx->setTextColor(t2);
  setCur(centerX(o2, 2), 286);
  printT(o2);
}

// boton-CTA rojo y grande para evolucionar (pulsa para llamar la atencion)
void drawEvolveButton() {
  uint32_t now = millis();
  int p = (int)(5 * sinf(now * 0.006f));  // late: -5..5
  int x = EVO_BTN_X - p, y = EVO_BTN_Y - p, w = EVO_BTN_W + 2 * p, h = EVO_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 18, UI_BAR_BAD);
  gfx->drawRoundRect(x, y, w, h, 18, UI_WHITE);
  gfx->drawRoundRect(x + 2, y + 2, w - 4, h - 4, 16, UI_WHITE);
  gfx->setTextColor(UI_WHITE);
  setSize(3);
  const char *t = T(S_EVO_TAP);
  setCur(centerX(t, 3), y + h / 2 - 11);
  printT(t);
}

// boton-CTA dorado de despedida: "<nombre> quiere decirte algo..."
void drawFarewellButton() {
  uint32_t now = millis();
  int p = (int)(4 * sinf(now * 0.005f));
  int x = FAR_BTN_X - p, y = FAR_BTN_Y - p, w = FAR_BTN_W + 2 * p, h = FAR_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 16, UI_BAR_WARN);
  gfx->drawRoundRect(x, y, w, h, 16, UI_INK);
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
  snprintf(buf, sizeof(buf), T(S_FAREWELL_BTN), nm);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(centerX(buf, 2), y + h / 2 - 8);
  printT(buf);
}

// boton-CTA sombrio de escapada por abandono: "<nombre> se siente abandonado..."
// (final triste: azul-gris oscuro, latido lento y apagado)
void drawRunawayButton() {
  uint32_t now = millis();
  int p = (int)(3 * sinf(now * 0.003f));
  int x = FAR_BTN_X - p, y = FAR_BTN_Y - p, w = FAR_BTN_W + 2 * p, h = FAR_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 16, C565(0x3a, 0x44, 0x5a));
  gfx->drawRoundRect(x, y, w, h, 16, C565(0x70, 0x80, 0x98));
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
  snprintf(buf, sizeof(buf), T(S_RUNAWAY_BTN), nm);
  gfx->setTextColor(C565(0xc8, 0xd2, 0xe0));
  setSize(2);
  setCur(centerX(buf, 2), y + h / 2 - 8);
  printT(buf);
}

// animacion epica de evolucion: halo radial + rayos giratorios + parpadeo del
// sprite acelerando + chispas que salen disparadas + fogonazo final
static void drawEvolveFX(uint32_t now) {
  float t = pet.evolveT();          // 0..1
  int cx = CX, cy = PET_GROUND - 96;

  // halo radial que crece y pulsa
  int halo = 36 + (int)(t * 150) + (int)(8 * sinf(now * 0.02f));
  for (int k = 0; k < 4; k++) {
    int r = halo - k * 7;
    if (r > 0) gfx->drawCircle(cx, cy, r, UI_WHITE);
  }
  // rayos giratorios desde el centro del bicho
  float base = now * 0.004f;
  for (int i = 0; i < 12; i++) {
    float a = base + i * (float)(PI / 6);
    int len = 90 + (int)(70 * (0.5f + 0.5f * sinf(now * 0.012f + i)));
    gfx->drawLine(cx, cy, cx + (int)(cosf(a) * len), cy + (int)(sinf(a) * len), UI_WHITE);
  }
  // parpadeo entre la forma ANTERIOR y la NUEVA (siluetas), acelerando; al
  // final (t>0.9) se queda fija en la nueva para el fogonazo de revelado
  int period = 60 + (int)(220 * (1.0f - t));
  bool showOld = t < 0.9f && evoPmd.loaded && ((now / period) % 2) == 0;
  if (showOld) drawPmdActM(evoPmd, PMD_IDLE, cx, PET_GROUND, 0, true, true, 5);
  else drawPmdAct(PMD_IDLE, cx, PET_GROUND, 0, true, true, 5);
  // chispas que salen disparadas
  for (int i = 0; i < 10; i++) {
    float a = i * (float)(PI / 5) + t * 4.0f;
    int d = (int)((now / 14 + i * 33) % 200);
    int sx = cx + (int)(cosf(a) * d), sy = cy + (int)(sinf(a) * d);
    gfx->fillRect(sx - 2, sy - 2, 5, 5, (i & 1) ? C565(0xff, 0xe0, 0x70) : UI_WHITE);
  }
  // fogonazo final antes de revelar la forma nueva
  if (t > 0.9f) gfx->fillCircle(cx, cy, (int)(300 * (t - 0.9f) / 0.1f), UI_WHITE);
}

void drawPet() {
  if (pmd.loaded) {
    drawPetPMD();
    return;
  }
  if (mon.loaded) {
    drawPetSD();
    return;
  }
  int fi = flashIdxForDex(pet.speciesId);
  if (fi < 0) {
    // sin SD y sin sprite de flash: aviso claro de que faltan sprites
    gfx->setTextColor(inkColor());
    setSize(6);
    setCur(CX - 18, PET_CY - 80);
    printT("?");
    setSize(2);
    const char *l1 = T(S_NO_SPRITES);
    setCur(centerX(l1, 2), PET_CY - 4);
    printT(l1);
    const char *l2 = T(S_LOAD_SPRITES);
    setCur(centerX(l2, 2), PET_CY + 20);
    printT(l2);
    return;
  }
  const Species &sp = SPECIES[fi];
  int s = sp.scale;
  int x = CX - 16 * s;
  int y = PET_CY - 16 * s;

  // animacion de evolucion: alterna la silueta de la forma anterior y la nueva
  if (pet.evolving()) {
    bool flash = (millis() / 300) % 2;
    int16_t showDex = (flash && pet.prevSpeciesId >= 0) ? pet.prevSpeciesId : pet.speciesId;
    int sfi = flashIdxForDex(showDex);
    if (sfi >= 0) {
      const Species &show = SPECIES[sfi];
      drawMap(show.sprite, SPRITE_H, CX - 16 * show.scale, PET_CY - 16 * show.scale, show.scale, flash);
    }
    return;
  }

  PetMood m = pet.mood();
  if (m == MOOD_HAPPY && (millis() / 500) % 2) y -= 6;  // saltito
  reactOffset(millis(), false, x, y);  // gestures (#21): no actions to play here
  x += followLean(millis());

  drawMap(sp.sprite, SPRITE_H, x, y, s, false);

  // expresiones superpuestas usando las anclas de la especie
  bool blink = (millis() % 3500 < 300);
  if (m == MOOD_SLEEPING || blink) {
    overlayEye(sp, x, y, s, sp.eyeColL);
    overlayEye(sp, x, y, s, sp.eyeColR);
  }
  if (m == MOOD_EATING) overlayMouth(sp, x, y, s, true);
  else if (m == MOOD_SAD) overlayMouth(sp, x, y, s, false);

  drawHuffPuff(x + 16 * s + petReact.away * 16 * s, y + 10 * s, millis());
  if (heartShown(millis())) drawMap(SPR_HEART, 32, x + 20 * s, y - 2 * s, 2, false);
}

// ---------- escena de bano ----------

void startBath() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony || bathUntil) return;
  bathUntil = millis() + 3000;
  bathPending = true;
  int cx = (int)beh.x;
  for (auto &b : bubbles) {
    b.x = cx - 70 + random(140);
    b.y = PET_GROUND - random(150);
    b.r = 8 + random(16);
    b.ph = random(64);
  }
}

void drawBath() {
  uint32_t now = millis();
  if (!timeLeft(bathUntil)) {
    bathUntil = 0;
    if (bathPending) {
      bathPending = false;
      pet.clean();
      // pose de alegria al quedar limpio
      if (pmd.has(PMD_POSE)) {
        beh.mode = 2;
        beh.act = PMD_POSE;
        beh.t0 = now;
        beh.until = now + pmdActTotalMs(pmd.acts[PMD_POSE]) * 2;
      }
    }
    return;
  }
  uint32_t left = bathUntil - now;
  if (left > 800) {
    // espuma: pompas meciendose y subiendo poco a poco
    float t = now / 220.0f;
    for (auto &b : bubbles) {
      int bx = b.x + (int)(sinf(t + b.ph) * 6);
      int by = b.y - (int)((3000 - left) / 90);
      gfx->fillCircle(bx, by, b.r, UI_WHITE);
      gfx->drawCircle(bx, by, b.r, 0x7E3D);
      gfx->fillCircle(bx - b.r / 3, by - b.r / 3, b.r / 4, UI_BG_DAY);
    }
  } else {
    // las pompas revientan: destellos
    for (int i = 0; i < 8; i++) {
      auto &b = bubbles[i];
      int sx = b.x + (i % 3) * 6 - 6, sy = b.y - 18;
      uint16_t col = (i % 2) ? UI_BAR_WARN : UI_WHITE;
      gfx->fillRect(sx - 6, sy - 1, 13, 3, col);
      gfx->fillRect(sx - 1, sy - 6, 3, 13, col);
    }
  }
}

// ---------- mascota PMD: comportamiento ----------

uint32_t pmdActTotalMs(const PmdAct &a) {
  uint32_t t = 0;
  for (uint8_t i = 0; i < a.frames; i++) t += a.ms[i];
  return t ? t : 100;
}

static uint8_t pmdFrameAt(const PmdAct &a, uint32_t t, bool loop) {
  uint32_t total = pmdActTotalMs(a);
  if (!loop && t >= total) return a.frames - 1;
  t %= total;
  uint8_t i = 0;
  // guarda: nunca mas vueltas que frames, por si un .bin corrupto trae ms[]=0
  // pese al saneo de la carga (defensa en profundidad)
  for (uint8_t guard = 0; guard < a.frames && t >= a.ms[i]; guard++) {
    t -= a.ms[i];
    i = (i + 1) % a.frames;
  }
  return i;
}

// the scale drawPmdActM() draws action a of m at
static uint8_t pmdScale(const PmdMon &m, const PmdAct &a, uint8_t maxS) {
  uint8_t sBase = m.acts[PMD_IDLE].h ? 170 / m.acts[PMD_IDLE].h : 5;
  if (sBase < 2) sBase = 2;
  if (sBase > maxS) sBase = maxS;
  uint8_t s = sBase;
  while (s > 2 && a.h * s > 250) s--;  // acciones con frame grande (ataque)
  return s;
}

// How far above the ground the top of action actId of the main sprite is drawn, in px: its
// first frame's highest opaque row (the idle emotes sit by the head, #22). Scanned on each
// call, so drawPetPMD() calls it only on a frame that draws an emote (idleEmoteDue()).
static int pmdHeight(uint8_t actId, uint8_t maxS) {
  const PmdAct &a = pmd.acts[actId];
  if (!a.frames) return 0;
  int top = 0;
  while (top < a.h) {
    const uint8_t *row = a.data + top * a.w;
    bool any = false;
    for (int c = 0; c < a.w && !any; c++) any = row[c] != 0xFF;
    if (any) break;
    top++;
  }
  int base = a.base ? a.base : a.h;
  return base > top ? (base - top) * pmdScale(pmd, a, maxS) : 0;
}

// dibuja una accion anclada por la base (centro-x, suelo) y devuelve su escala
// dibuja una accion de un PmdMon concreto (m); drawPmdAct usa el global pmd
void drawPmdActM(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  const PmdAct &a = m.acts[actId];
  if (!a.frames) return;
  uint8_t s = pmdScale(m, a, maxS);
  uint8_t fi = pmdFrameAt(a, t, loop);
  const uint8_t *fr = a.data + (uint32_t)fi * a.w * a.h;
  // anclar por los pies (a.base), no por el alto del lienzo: asi las acciones
  // con padding distinto (Hurt, Eat...) quedan todas a la misma altura de suelo
  int x0 = cx - a.w * s / 2, y0 = groundY - (a.base ? a.base : a.h) * s;
  for (int r = 0; r < a.h; r++) {
    const uint8_t *row = fr + r * a.w;
    forRuns(a.w, [&](int c) { return indexedKey(row[c], sil); },
            [&](int c0, int len, int k) { gfx->fillRect(x0 + c0 * s, y0 + r * s, len * s, s, sil ? INK_K : m.pal[k]); });
  }
}
void drawPmdAct(uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  drawPmdActM(pmd, actId, cx, groundY, t, loop, sil, maxS);
}

static void drawPetPMD() {
  uint32_t now = millis();

  if (pet.evolving()) {
    drawEvolveFX(now);
    return;
  }
  if (evoPmd.loaded) evoPmd.unload();  // termino la evolucion: libera la forma anterior

  PetMood m = pet.mood();
  uint8_t act;
  bool loop = true;
  uint32_t t0 = beh.t0;
  int dx = 0, dy = 0;
  bool follow = petFollowing(now);  // hold-then-drag (#21): walk after the finger
  if (m == MOOD_SLEEPING && pmd.has(PMD_SLEEP)) {
    act = PMD_SLEEP;
    beh.mode = 0;
    beh.fast = false;
  } else if (m == MOOD_EATING && pmd.has(PMD_EAT)) {
    act = PMD_EAT;
    beh.t0 = 0;
    t0 = 0;
  } else if (reacting(now)) {  // a gesture's reaction (#21) shows even on a sad pet
    act = reactAct(petReact.kind);
    t0 = petReact.t0;
    if (petReact.kind == REACT_HUFF) {
      loop = false;                     // sits with its back turned
      if (act != PMD_SIT) t0 = now;     // or holds one still side-on frame
    }
    walkLast = 0;  // a walk under way pauses
    reactOffset(now, act != PMD_IDLE, dx, dy);
  } else if (m == MOOD_SAD && pmd.has(PMD_HURT) && !follow && !beh.fast) {
    act = PMD_HURT;
  } else {
    // contento: el planificador de su vida propia decide (#22, idlelife.cpp); con un dedo que
    // arrastra (#21) camina tras el dedo
    if (follow) {
      beh.mode = 1;
      beh.fast = true;
      beh.targetX = constrain((float)touchG.dragX, (float)PET_WALK_MIN, (float)PET_WALK_MAX);
      beh.until = now + 15000;
    } else {
      // a sad pet only gets here after a finger dragged it (beh.fast): the calm repertoire
      idleUpdate(now, petSceneFree() && m == MOOD_HAPPY);
    }
    if (beh.mode != 1) {
      walkLast = 0;  // al volver a caminar no se cuenta el tiempo parado
      beh.fast = false;
    }
    if (beh.mode == 1) {
      float d = beh.targetX - beh.x;
      if (fabsf(d) < 4) {
        act = PMD_IDLE;
        walkLast = 0;
        if (!follow) {  // a finger still down keeps it waiting there
          beh.fast = false;
          idleArrived(now);
        }
      } else {
        // 30 px/s de diseno (3 px por frame a 10 fps), midiendo el tiempo real: con menos frames
        // por segundo camina igual de rapido
        uint32_t dt = walkLast ? now - walkLast : 100;
        uint32_t cap = frameIntervalMs(powerCtx(now)) + 100;  // un hueco mayor (otra pantalla de por medio) no es un paso largo
        if (dt > cap) dt = cap;
        walkLast = now;
        float step = (beh.fast ? PET_FOLLOW_PX_S : idleWalkSpeed()) * dt / 1000.0f;
        if (step > fabsf(d)) step = fabsf(d);  // un frame lento no sobrepasa el destino
        beh.x += d > 0 ? step : -step;
        act = (d > 0) ? PMD_WALKR : PMD_WALKL;
      }
    } else {
      act = idleFrameAct(now, dy, t0, loop);  // standing, a doze, a hop, a gesture...
    }
    if (!pmd.has(act)) act = PMD_IDLE;
  }

  drawPmdAct(act, (int)beh.x + dx, PET_GROUND + dy, now - t0, loop || act == PMD_IDLE, false, 5);
  drawHuffPuff((int)beh.x + petReact.away * 56, PET_GROUND - 120, now);

  if (heartShown(now)) drawMap(SPR_HEART, 32, (int)beh.x + 50, PET_GROUND - 190, 2, false);
  // the ball and an emote (#22); the head's height only when an emote is drawn
  idleDrawOver(now, (int)beh.x, idleEmoteDue(now) ? PET_GROUND + dy - pmdHeight(act, 5) : 0);
}

// sprite animado desde la SD: zoom entero por pixel, frames a su ritmo
static void drawPetSD() {
  int s = mon.scale;
  int w = mon.w * s, h = mon.h * s;
  int x = CX - w / 2;
  int y = PET_CY - h / 2;

  bool sil = false;
  if (pet.evolving()) {
    sil = (millis() / 300) % 2;
  } else {
    if (pet.mood() == MOOD_HAPPY && (millis() / 500) % 2) y -= 6;  // saltito
    reactOffset(millis(), false, x, y);  // gestures (#21): no actions to play here
    x += followLean(millis());
  }

  uint16_t fm = mon.frameMs ? mon.frameMs : 100;
  uint16_t fi = pet.sleeping ? 0 : (millis() / fm) % mon.frames;
  const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
  for (int r = 0; r < mon.h; r++) {
    const uint8_t *row = fr + r * mon.w;
    forRuns(mon.w, [&](int c) { return indexedKey(row[c], sil); },
            [&](int c0, int len, int k) { gfx->fillRect(x + c0 * s, y + r * s, len * s, s, sil ? INK_K : mon.pal[k]); });
  }

  // emotes en vez de expresiones (los sprites importados no tienen anclas)
  drawHuffPuff(x + w / 2 + petReact.away * (w / 2), y + h / 3, millis());
  if (heartShown(millis())) drawMap(SPR_HEART, 32, x + w - 30, y - 50, 2, false);
}

// ojo cerrado: borra el ojo 3x4 y dibuja el parpado
static void overlayEye(const Species &sp, int x, int y, int s, int col) {
  gfx->fillRect(x + col * s, y + sp.eyeRow * s, 3 * s, 4 * s, sp.bodyColor);
  gfx->fillRect(x + col * s, y + (sp.eyeRow + 2) * s, 3 * s, s, INK_K);
}

// borra la sonrisa base y pinta boca abierta (comer) o ceno (triste)
static void overlayMouth(const Species &sp, int x, int y, int s, bool open) {
  int mc = sp.mouthCol, mr = sp.mouthRow;
  gfx->fillRect(x + (mc - 3) * s, y + mr * s, 7 * s, 2 * s, sp.bodyColor);
  if (open) {
    gfx->fillRect(x + (mc - 2) * s, y + mr * s, 5 * s, 2 * s, INK_K);
  } else {
    gfx->fillRect(x + (mc - 2) * s, y + mr * s, 5 * s, s, INK_K);
    gfx->fillRect(x + (mc - 3) * s, y + (mr + 1) * s, s, s, INK_K);
    gfx->fillRect(x + (mc + 3) * s, y + (mr + 1) * s, s, s, INK_K);
  }
}

void drawPoops() {
  for (int i = 0; i < pet.poops; i++) {
    drawMap(SPR_POOP, 32, 36 + i * 46, 244, 2, false);
  }
}

// The attention "!" (#9): a speech bubble beside the pet while a need is low, with the icon
// of the button that fixes it (attnShown: the same need the status line names). Silent and
// not tied to the call, so it is there whenever she looks. Hidden while something else is
// going on around the pet: sleep, eating, a heart, the bath, evolution, a banner or a dialog,
// and while a speech bubble (#23, bubble.h) shows, which says the same need in words.
void drawAttnBubble() {
  static const char *const *const ICONS[ATTN_NEEDS] = { SPR_ICON_FOOD, SPR_ICON_CLEAN, SPR_ICON_LIGHT, SPR_ICON_PLAY };
  uint8_t need = attnShown(attnLevels(pet.fullness, pet.joy, pet.energy, pet.hygiene));
  if (need >= ATTN_NEEDS || bubbleShowing()) return;
  if (pet.sleeping || pet.eating() || pet.showHeart() || pet.evolving() || timeLeft(bathUntil) || pet.showMedal() ||
      pet.showMilestone() || timeLeft(confirmUntil) || timeLeft(feedMenuUntil) || (choiceKind && timeLeft(choiceUntil)))
    return;
  AttnBubble b = attnBubbleAt(pmd.loaded ? (int)beh.x : CX);
  uint16_t ink = inkColor();
  gfx->fillRoundRect(b.x, b.y, b.w, b.h, 12, UI_WHITE);
  gfx->drawRoundRect(b.x, b.y, b.w, b.h, 12, ink);
  // tail: leaves the bottom edge near the pet's side and points down at it; the white
  // triangle covers the border where it joins, the two lines redraw its outer edges
  int base = b.y + b.h - 1, tipY = b.y + b.h + ATTN_BUBBLE_TAIL;
  int x0 = b.right ? b.x + 10 : b.x + b.w - 24, x1 = x0 + 14;
  int tipX = b.right ? b.x + 2 : b.x + b.w - 2;
  gfx->fillTriangle(x0, base, x1, base, tipX, tipY, UI_WHITE);
  gfx->drawLine(x0, base, tipX, tipY, ink);
  gfx->drawLine(x1, base, tipX, tipY, ink);
  // a red "!" drawn as shapes (no font: the same in every language) and the 16x16 icon at x2
  gfx->fillRoundRect(b.x + 12, b.y + 7, 8, 21, 3, UI_BAR_BAD);
  gfx->fillRoundRect(b.x + 12, b.y + 31, 8, 7, 3, UI_BAR_BAD);
  drawMap(ICONS[need], 16, b.x + 30, b.y + 6, 2, false);
}

void drawBars() {
  drawBar(78, 318, T(S_BAR_FOOD), pet.fullness);
  drawBar(244, 318, T(S_BAR_JOY), pet.joy);
  drawBar(78, 346, T(S_BAR_ENE), pet.energy);
  drawBar(244, 346, T(S_BAR_HYG), pet.hygiene);
}

// Separacion entre la etiqueta y su barra en la fila de necesidades: se deriva de
// la etiqueta mas larga a escala 2, con 48 px (4 letras) de suelo.
static int barLabelGap() {
  const StrId ids[] = { S_BAR_FOOD, S_BAR_JOY, S_BAR_ENE, S_BAR_HYG };
  int ancho = 0;
  for (StrId id : ids) {
    int w = textW(T(id), 2);
    if (w > ancho) ancho = w;
  }
  // con las etiquetas de 4 letras se deja EXACTAMENTE 48; por encima se anade aire
  // para que la etiqueta respire
  return ancho <= 48 ? 48 : ancho + 8;
}

static void drawBar(int x, int y, const char *label, uint8_t val) {
  gfx->setTextColor(inkColor());
  setSize(2);
  setCur(x, y);
  printT(label);
  int gap = barLabelGap();
  // las dos columnas empiezan en 78 y 244: la barra de la primera tiene que
  // acabar antes de la etiqueta de la segunda, y ambas se pintan igual de anchas
  int bw = 232 - (78 + gap);
  if (bw > 100) bw = 100;   // con etiquetas latinas sale 100, como estaba
  int bx = x + gap, bh = 15;
  uint16_t fill = (val >= 50) ? UI_BAR_OK : (val >= ATTN_LOW) ? UI_BAR_WARN : UI_BAR_BAD;
  gfx->fillRoundRect(bx, y, bw, bh, 4, UI_TRACK);
  int fw = (bw - 4) * val / 100;
  if (fw > 0) gfx->fillRoundRect(bx + 2, y + 2, fw, bh - 4, 3, fill);
}

void drawButtons() {
  for (int i = 0; i < 4; i++) {
    bool off = pet.sleeping && i != 2;  // durmiendo solo funciona LUZ
    int bx = buttons[i].cx - BTN_HALF, by = buttons[i].cy - BTN_HALF;
    if (!pet.sleeping) gfx->fillRoundRect(bx, by, 2 * BTN_HALF, 2 * BTN_HALF, 14, UI_WHITE);
    gfx->drawRoundRect(bx, by, 2 * BTN_HALF, 2 * BTN_HALF, 14, inkColor());
    if (!off) drawMap(buttons[i].icon, 16, buttons[i].cx - 16, buttons[i].cy - 16, 2, false);
  }
}

const char *eggMsg() {
  switch (pet.eggCracks()) {
    case 0: return T(S_EGG_TOUCH);
    case 1: return T(S_EGG_MOVES);
    default: return T(S_EGG_ALMOST);
  }
}

const char *statusMsg() {
  if (pet.evolving()) return T(S_EVOLVING);
  if (bathUntil) return "Splish splash!";  // onomatopeya universal
  if (pet.sleeping) return "Zzz...";
  if (pet.eating()) return T(S_EATING);
  if (heartShown(millis())) return T(S_LIKES);  // not over a huff (#21)
  // the same need the "!" bubble shows (attnShown), so the text and the bubble can't disagree
  switch (attnShown(attnLevels(pet.fullness, pet.joy, pet.energy, pet.hygiene))) {
    case ATTN_FOOD: return T(S_HUNGRY);
    case ATTN_HYGIENE: return T(S_NEEDS_BATH);
    case ATTN_ENERGY: return T(S_EXHAUSTED);
    case ATTN_JOY: return T(S_SAD);
    default: break;
  }
  if (pet.weight > PET_CHUBBY_WEIGHT) return T(S_CHUBBY);
  if (pet.shiny && pet.ageMinutes < 15) return T(S_IS_SHINY);
  return T(S_HAPPY);
}

// dibuja un mapa de n x n pixeles escalado; silhouette=true lo pinta en tinta
void drawMap(const char *const *map, int n, int x, int y, int s, bool silhouette) {
  for (int r = 0; r < n; r++) {
    const char *row = map[r];
    forRuns(n, [&](int c) { return charKey(row[c], silhouette); },
            [&](int c0, int len, int k) { gfx->fillRect(x + c0 * s, y + r * s, len * s, s, silhouette ? INK_K : spriteColor((char)k)); });
  }
}

// The fallback sprites do not walk: they lean toward a dragging finger and ease back after.
static int followLean(uint32_t now) {
  static int lean = 0;
  static uint32_t at = 0;
  if (petFollowing(now)) {
    lean = constrain((int)touchG.dragX - CX, -60, 60);
    at = now;
    return lean;
  }
  uint32_t e = now - at;
  if (e >= 400) lean = 0;
  return lean * (int)(400 - (e < 400 ? e : 400)) / 400;
}

static bool huffing(uint32_t now) { return reacting(now) && petReact.kind == REACT_HUFF; }

// The heart of the pat before the huff (the 11th tap) is not drawn while the pet turns away.
// Only the drawing skips it: pet.showHeart() is untouched for its other uses.
static bool heartShown(uint32_t now) { return pet.showHeart() && !huffing(now); }

// the huff: little puffs of breath blown away from the finger, from (x, y) beside the
// pet's head, each growing as it drifts off and up
static void drawHuffPuff(int x, int y, uint32_t now) {
  if (!huffing(now)) return;
  uint32_t e = now - petReact.t0;
  for (int i = 0; i < 2; i++) {
    uint32_t pe = (e + i * 350) % 700;  // each puff lives 700 ms
    int r = 6 + (int)(pe / 70);
    int px = x + petReact.away * (int)(pe / 10), py = y - (int)(pe / 25);
    gfx->fillCircle(px, py, r, UI_WHITE);
    gfx->drawCircle(px, py, r, UI_TRACK);
  }
}
