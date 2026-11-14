// The screens other than the main one (screens.h). Moved out of TamaPoke.ino (#41).
#include <Arduino.h>
#include "app.h"
#include "audio.h"
#include "bedtime.h"
#include "blit.h"
#include "clockprompt.h"
#include "dex.h"
#include "gameui.h"
#include "i18n.h"
#include "power.h"
#include "rtcbat.h"
#include "scene.h"
#include "screens.h"
#include "sdmon.h"
#include "settingslayout.h"
#include "species.h"
#include "touchlock.h"
#include "uitext.h"

PmdMon galleryPmd;  // sprite grande de la vista detalle de la galeria (PMD/TPK2, legal)

// galeria pokedex
bool galleryOpen = false;
bool galleryDirty = false;
int galleryPage = 0;        // 10 paginas de 16
int16_t galleryDetail = 0;  // dex en vista detalle, 0 = rejilla

bool cardOpen = false;        // ficha del bicho (deslizar vertical)
bool kbOpen = false;          // teclado para renombrar al bicho
char nameBuf[12] = "";
uint8_t nameLen = 0;
uint8_t cardPage = 0;         // 0 perfil, 1 stats, 2 medallas, 3 progreso, 4 cuidados (ritmo y hora de dormir)
bool clockOpen = false;       // pantalla de ajuste de hora (deslizar abajo)
int clockH = 12, clockM = 0;  // hora en edicion

// eleccion de huevo (ajuste Care "Huevos"): tres huevos en fila. Un toque lo
// levanta y ensena su rareza; un segundo toque en el mismo lo elige.
#define EGGSEL_S 4      // escala: el huevo visible mide 68x92 px
#define EGGSEL_Y 150    // borde superior del sprite de 32x32 escalado
#define EGGSEL_LIFT 16  // cuanto sube el huevo tocado
#define EGGSEL_HIT 60   // medio ancho de la zona tactil de cada huevo
static const int16_t EGGSEL_CX[EGG_CHOICES] = { 93, 233, 373 };
int8_t eggSel = -1;     // huevo tocado una vez (-1 = ninguno); solo UI, no se guarda

// primera partida: elige inicial entre Bulbasaur / Charmander / Squirtle
void renderStarterSelect() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  const char *t = T(S_CHOOSE_STARTER);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(centerX(t, 2), 68);
  printT(t);
  for (int i = 0; i < 3; i++) {
    int16_t d = STARTER_DEX[i];
    const DexEntry &de = DEX_TBL[d];
    int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
    gfx->fillRoundRect(70, ry, 326, STARTER_ROW_H, 14, lerp565(de.accent, UI_WHITE, 6, 8));
    gfx->drawRoundRect(70, ry, 326, STARTER_ROW_H, 14, de.accent);
    const uint8_t *th = thumbs.get(d);     // miniatura del inicial (si la SD esta lista)
    if (th) drawThumb(th, 76, ry - 5, 3, false);
    gfx->setTextColor(UI_INK);
    setSize(3);
    setCur(178, ry + 24);
    printT(dexName(d));
  }
  flushUI();
}

// huevo con pista: cascara segun la rareza (comun crema, raro plata, legendario
// oro) y motas ('g' en SPR_EGG) del color del tipo. El contorno y las grietas
// siguen en tinta. La especie no se ve.
void drawEggStyled(uint8_t rarity, uint16_t spot, int x, int y, int s) {
  uint16_t shell, shade;
  if (rarity == R_LEGENDARIO) {
    shell = C565(0xff, 0xd9, 0x5e);
    shade = C565(0xd9, 0xa0, 0x2a);
  } else if (rarity == R_RARO) {
    shell = C565(0xdc, 0xe6, 0xf2);
    shade = C565(0x9c, 0xb0, 0xcc);
  } else {
    shell = C565(0xf8, 0xf1, 0xe0);
    shade = C565(0xd8, 0xcc, 0xae);
  }
  for (int r = 0; r < SPRITE_H; r++) {
    const char *row = SPR_EGG[r];
    forRuns(SPRITE_W, [&](int c) { return charKey(row[c], false); },
            [&](int c0, int len, int k) {
              char ch = (char)k;
              uint16_t col = ch == 'y' ? shell : ch == 'Y' ? shade : ch == 'g' ? spot : spriteColor(ch);
              gfx->fillRect(x + c0 * s, y + r * s, len * s, s, col);
            });
  }
}

const char *eggRarityText(uint8_t rarity) {
  return rarity == R_LEGENDARIO ? T(S_EGG_LEGEND) : rarity == R_RARO ? T(S_EGG_RARE) : T(S_EGG_COMMON);
}
uint16_t eggRarityColor(uint8_t rarity) {
  return rarity == R_LEGENDARIO ? UI_BAR_WARN : rarity == R_RARO ? 0x4C98 : inkColor();
}

// tras un final (con el ajuste Care "Huevos" en Elegir): tres huevos a elegir
void renderEggSelect() {
  int h = sceneHour();
  gNight = h < 6 || h >= 20;  // un huevo no duerme: solo la hora
  uint16_t bg = gNight ? UI_BG_NIGHT : UI_BG_DAY;
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, bg);
  const char *t = T(S_CHOOSE_EGG);
  gfx->setTextColor(inkColor());
  setSize(2);
  setCur(centerX(t, 2), 84);
  printT(t);
  for (int i = 0; i < EGG_CHOICES; i++) {
    int16_t d = pet.eggChoice(i);
    if (d < 1 || d > DEX_COUNT) continue;  // Pet ya lo garantiza; por si acaso
    const DexEntry &de = DEX_TBL[d];
    int lift = (i == eggSel) ? EGGSEL_LIFT : 0;
    int x = EGGSEL_CX[i] - 16 * EGGSEL_S, y = EGGSEL_Y - lift;
    // halo del tocado y despues la sombra en el suelo (se separa al levantarlo)
    if (i == eggSel) gfx->fillCircle(EGGSEL_CX[i], y + 17 * EGGSEL_S, 58, lerp565(bg, de.accent, 3, 8));
    gfx->fillEllipse(EGGSEL_CX[i], EGGSEL_Y + 29 * EGGSEL_S, 30 - lift / 2, 7, lerp565(bg, UI_INK, 2, 8));
    drawEggStyled(de.rarity, de.accent, x, y, EGGSEL_S);
  }
  if (eggSel >= 0 && eggSel < EGG_CHOICES) {
    int16_t d = pet.eggChoice(eggSel);
    uint8_t r = (d >= 1 && d <= DEX_COUNT) ? DEX_TBL[d].rarity : (uint8_t)R_COMUN;
    const char *rt = eggRarityText(r);
    gfx->setTextColor(eggRarityColor(r));
    setSize(2);
    setCur(centerX(rt, 2), 300);
    printT(rt);
    const char *hint = T(S_EGG_TAP_AGAIN);
    gfx->setTextColor(lerp565(inkColor(), bg, 3, 8));
    setCur(centerX(hint, 2), 336);
    printT(hint);
  }
  flushUI();
}

// toque en la eleccion de huevo: el primero levanta el huevo, el segundo lo elige
void eggSelectTap(int16_t x, int16_t y) {
  if (y < EGGSEL_Y - EGGSEL_LIFT || y > EGGSEL_Y + 30 * EGGSEL_S) return;
  for (int i = 0; i < EGG_CHOICES; i++) {
    if (abs(x - EGGSEL_CX[i]) > EGGSEL_HIT) continue;
    if (eggSel == i) {
      pet.chooseEgg((uint8_t)i);
      eggSel = -1;
      sfxPlay(SFX_HEART);  // no SFX_HATCH: ese suena al eclosionar, tres toques despues
    } else {
      eggSel = (int8_t)i;
      sfxPlay(SFX_TAP);
    }
    return;
  }
}


// ---------- ficha del bicho (deslizar vertical) ----------

// x donde arrancan las barras de la ficha. Estaba fijo en 150, que daba de sobra
// para etiquetas latinas de 3 caracteres pero no para las japonesas, mas anchas:
// こうげき se metia dentro de la barra. Se calcula a partir de la etiqueta mas
// larga para que las cuatro barras sigan alineadas en cualquier idioma, con el
// valor original como suelo (asi en los idiomas latinos no cambia nada).
// Requiere tener ya puesto el tamano de texto 2, porque textW() lo necesita.
static int statBarX() {
  const StrId ids[] = { S_STAT_ATK, S_STAT_DEF, S_STAT_SPE, S_STAT_WGT, S_VIN };
  int ancho = 0;
  for (StrId id : ids) {
    int w = textW(T(id), 2);
    if (w > ancho) ancho = w;
  }
  int x = 96 + ancho + 12;   // 12 px de aire entre etiqueta y barra
  return x < 150 ? 150 : x;
}

void drawCardStat(int y, const char *label, uint16_t val, uint16_t maxBar, uint16_t color) {
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(96, y);
  printT(label);
  char num[8];
  snprintf(num, sizeof(num), "%u", val);
  setCur(330, y);
  printT(num);
  int bx = statBarX();
  int bw = 310 - bx;   // la barra siempre acaba en 310, dejando aire hasta el numero
  int fw = (int)val * bw / maxBar;
  if (fw > bw) fw = bw;
  gfx->fillRoundRect(bx, y + 2, bw, 11, 3, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(bx, y + 2, fw, 11, 3, color);
}

// ---------- ajuste de hora en pantalla (deslizar abajo) ----------
// El usuario pone su hora LOCAL a ojo; el firmware la usa tal cual, asi que
// no hay que gestionar zona horaria. Preserva el dia (no rompe racha/edad).

void loadClockFields(uint32_t e) {
  clockH = (e / 3600) % 24;
  clockM = (e / 60) % 60;
}

void openClock() {
  loadClockFields(pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch());
  clockOpen = true;
  clockPrompt.opened();  // however it opened, it does not open again by itself
}

void applyClock() {
  uint32_t base = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  uint32_t e = (base / 86400) * 86400 + (uint32_t)clockH * 3600 + (uint32_t)clockM * 60;
  rtcSetEpoch(e);
  pet.setClock(e);
  bedClockSet(e);
  clockOpen = false;
}

void drawClockBtn(int x, int y, const char *l) {
  gfx->fillRoundRect(x, y, 58, 58, 12, UI_WHITE);
  gfx->drawRoundRect(x, y, 58, 58, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  setSize(4);
  setCur(x + 17, y + 15);
  printT(l);
}

// the pills' and the OK button's rects are in settingslayout.h (shared with clockTap)
static const char *const LANG_CODES[LANG_COUNT] = { "ES", "EN", "FR", "DE", "IT", "PT", "JA", "KO" };

// tres barras crecientes (brillo, volumen); las `lit` primeras van rellenas
static void drawLevelBars(int x, int lit) {
  for (int i = 0; i < 3; i++) {
    int bh = 8 + i * 5, bx = x + i * 14, by = LANG_PILL_Y + LANG_PILL_H - 6 - bh;
    if (i < lit) gfx->fillRect(bx, by, 9, bh, UI_BAR_OK);
    gfx->drawRect(bx, by, 9, bh, UI_INK);
  }
}

#define PILL_BAT_W 34  // the battery pill's glyph body; its terminal adds (3 * 16 + 5) / 11 = 4 px
#define PILL_BAT_H 16

// Power pill icons, drawn in UI_INK on the white pill, about 20 px tall around `cy`.
// A padlock 16 px wide; open (shackle lifted, right leg out) when the touch lock is off.
#define PADLOCK_W 16
static void drawPadlock(int x, int cy, bool open) {
  int sy = open ? cy - 15 : cy - 11;  // shackle: two nested round rects, 2 px thick
  gfx->drawRoundRect(x + 3, sy, 10, 16, 5, UI_INK);
  gfx->drawRoundRect(x + 4, sy + 1, 8, 14, 4, UI_INK);
  if (open) gfx->fillRect(x + 10, cy - 6, 4, 4, UI_WHITE);  // the right leg stops short of the body
  gfx->fillRoundRect(x, cy - 2, PADLOCK_W, 12, 2, UI_INK);  // body
  gfx->fillCircle(x + 8, cy + 2, 2, UI_WHITE);              // keyhole
  gfx->fillRect(x + 7, cy + 2, 2, 5, UI_WHITE);
}

// A display on its stand, 22 px wide, with a dark face: the screen that goes off.
#define SCREEN_ICON_W 22
static void drawScreenIcon(int x, int cy) {
  gfx->fillRoundRect(x, cy - 10, SCREEN_ICON_W, 16, 3, UI_INK);
  gfx->drawRect(x + 2, cy - 8, SCREEN_ICON_W - 4, 12, UI_WHITE);  // bezel line
  gfx->fillRect(x + 9, cy + 6, 4, 3, UI_INK);                      // stand
  gfx->fillRect(x + 5, cy + 8, 12, 2, UI_INK);
}

// An icon `iconW` px wide and `text` side by side, centred in `r` (text size 2): prints
// the text and returns the icon's left x.
static int pillIconText(const SetRect &r, int iconW, const char *text) {
  const int gap = 8;
  int x = r.x + (r.w - (iconW + gap + textW(text, 2))) / 2;
  setCur(x + iconW + gap, r.y + (r.h - 14) / 2);
  printT(text);
  return x;
}

// The power pills (#11): an icon plus a number, like the sound and brightness pills
// above them. The rects are settingslayout.h's, the same ones clockTap() hit-tests.
// "off" (T(S_TLOCK_OFF)) is the only word, and so the only translated label.
static void drawPowerPills() {
  for (int i = 0; i < POWER_PILL_COUNT; i++) {
    SetRect r = powerPillRect(i);
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 10, UI_WHITE);
    gfx->drawRoundRect(r.x, r.y, r.w, r.h, 10, UI_INK);
  }
  gfx->setTextColor(UI_INK);
  setSize(2);
  char s[12];

  // touch lock: seconds after the screen goes off ("0s": as it goes off)
  SetRect r = powerPillRect(PILL_TOUCH_LOCK);
  bool lockOn = secondsLabel(s, sizeof(s), touchLockMs, TOUCH_LOCK_NEVER);
  int ix = pillIconText(r, PADLOCK_W, lockOn ? s : T(S_TLOCK_OFF));
  drawPadlock(ix, r.y + r.h / 2 + 1, !lockOn);

  // screen off: seconds without a touch; "off" = never (it still dims, power.h)
  r = powerPillRect(PILL_SCREEN_OFF);
  bool offOn = secondsLabel(s, sizeof(s), powerOffMs, 0);
  ix = pillIconText(r, SCREEN_ICON_W, offOn ? s : T(S_TLOCK_OFF));
  drawScreenIcon(ix, r.y + r.h / 2);

  // battery: a preview of the header, its glyph or its percent, with the live level
  r = powerPillRect(PILL_BATTERY);
  int pc = batPercent();
  bool charging = pc >= 0 && batCharging();
  // no reading: faint grey, so it reads as "nothing to show" rather than a flat (red) battery
  uint16_t col = pc < 0 ? UI_TRACK : batteryColor(pc, charging, UI_INK);
  if (batAsPercent) {
    batteryPctLabel(s, sizeof(s), pc, charging);
    gfx->setTextColor(col);
    setCur(r.x + (r.w - textW(s, 2)) / 2, r.y + (r.h - 14) / 2);
    printT(s);
  } else {  // with no reading (pc < 0) an empty grey outline
    drawBatteryGlyph(r.x + (r.w - PILL_BAT_W - 4) / 2, r.y + (r.h - PILL_BAT_H) / 2, PILL_BAT_W, PILL_BAT_H,
                     pc < 0 ? 0 : pc, charging, col, UI_INK);
  }
}

void renderClock() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  // reseeded after a power-off and not set yet (clockprompt.h): ask, and show the time
  // it has in red, since that is only the last time saved before it went off
  bool asking = clockPrompt.asking(bedEdge.stale());
  const char *title = asking ? T(S_CLOCK_ASK) : T(S_SET_TIME);
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(title, 3), 44);
  printT(title);

  char t[8];
  snprintf(t, sizeof(t), "%02d:%02d", clockH, clockM);
  gfx->setTextColor(asking ? UI_BAR_BAD : UI_INK);
  setSize(7);
  // CX - 105 in Latin; in CJK only about centred (centerX ignores the text x offset)
  setCur(centerX(t, 7), 108);
  printT(t);

  drawClockBtn(104, 190, "-");  // hora -
  drawClockBtn(170, 190, "+");  // hora +
  drawClockBtn(252, 190, "-");  // min -
  drawClockBtn(318, 190, "+");  // min +
  setSize(2);
  gfx->setTextColor(UI_TRACK);
  setCur(120, 256);
  printT(T(S_HOUR));
  setCur(276, 256);
  printT(T(S_MIN));

  // sonido (izquierda de la fila): un altavoz y tres barras con el volumen, o una X si esta apagado
  gfx->fillRoundRect(SND_PILL_X, LANG_PILL_Y, SND_PILL_W, LANG_PILL_H, 8, UI_WHITE);
  gfx->drawRoundRect(SND_PILL_X, LANG_PILL_Y, SND_PILL_W, LANG_PILL_H, 8, UI_INK);
  {
    int sx = SND_PILL_X + 18, sy = LANG_PILL_Y + LANG_PILL_H / 2;
    gfx->fillRect(sx, sy - 4, 5, 8, UI_INK);                           // cuerpo del altavoz
    gfx->fillTriangle(sx + 4, sy, sx + 12, sy - 9, sx + 12, sy + 9, UI_INK);  // bocina
    if (audioEnabled()) {
      drawLevelBars(SND_PILL_X + 46, VOL_LEVELS - audioVolume());
    } else {
      int xx = SND_PILL_X + 58;
      for (int d = -1; d <= 1; d++) {  // X de 3 px de grueso
        gfx->drawLine(xx - 7 + d, sy - 7, xx + 7 + d, sy + 7, UI_BAR_BAD);
        gfx->drawLine(xx - 7 + d, sy + 7, xx + 7 + d, sy - 7, UI_BAR_BAD);
      }
    }
  }

  // nivel de brillo (centro de la fila): un sol y tres barras, las encendidas son el nivel
  gfx->fillRoundRect(BRI_PILL_X, LANG_PILL_Y, BRI_PILL_W, LANG_PILL_H, 8, UI_WHITE);
  gfx->drawRoundRect(BRI_PILL_X, LANG_PILL_Y, BRI_PILL_W, LANG_PILL_H, 8, UI_INK);
  {
    int sx = BRI_PILL_X + 26, sy = LANG_PILL_Y + LANG_PILL_H / 2;
    gfx->fillCircle(sx, sy, 5, UI_INK);
    static const int8_t RAY[8][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1} };
    for (int i = 0; i < 8; i++) {
      int k = i < 4 ? 8 : 6, e = i < 4 ? 11 : 9;  // las diagonales algo mas cortas
      gfx->drawLine(sx + RAY[i][0] * k, sy + RAY[i][1] * k, sx + RAY[i][0] * e, sy + RAY[i][1] * e, UI_INK);
    }
    drawLevelBars(BRI_PILL_X + 52, BRIGHT_LEVELS - userDim);
  }

  // selector de idioma: una pildora que cicla los 6 idiomas al tocar
  gfx->fillRoundRect(LANG_PILL_X, LANG_PILL_Y, LANG_PILL_W, LANG_PILL_H, 8, UI_WHITE);
  gfx->drawRoundRect(LANG_PILL_X, LANG_PILL_Y, LANG_PILL_W, LANG_PILL_H, 8, UI_INK);
  char lp[10];
  snprintf(lp, sizeof(lp), "%s >", LANG_CODES[gLang]);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(LANG_PILL_X + (LANG_PILL_W - textW(lp, 2)) / 2, LANG_PILL_Y + 8);
  printT(lp);

  drawPowerPills();

  gfx->fillRoundRect(OK_BTN.x, OK_BTN.y, OK_BTN.w, OK_BTN.h, 14, UI_BAR_OK);
  gfx->setTextColor(UI_BG_DAY);
  setSize(3);
  setCur(centerX("OK", 3), OK_BTN.y + 8);  // CX - 18 with the Latin font
  printT("OK");

  gfx->setTextColor(UI_TRACK);
  setSize(2);
  setCur(centerX(T(S_CLOCK_CANCEL), 2), 410);
  printT(T(S_CLOCK_CANCEL));

  // version del firmware (discreta, abajo del todo)
  char ver[20];
  snprintf(ver, sizeof(ver), "TamaPoke v%s", FW_VERSION);
  setSize(1);
  setCur(centerX(ver, 1), 436);
  printT(ver);
  flushUI();
}

void clockTap(int16_t x, int16_t y) {
  if (y >= 190 && y <= 248) {  // fila de botones +/-
    if (x >= 104 && x < 162) clockH = (clockH + 23) % 24;
    else if (x >= 170 && x < 228) clockH = (clockH + 1) % 24;
    else if (x >= 252 && x < 310) clockM = (clockM + 59) % 60;
    else if (x >= 318 && x < 376) clockM = (clockM + 1) % 60;
    return;
  }
  if (SND_PILL.contains(x, y)) {  // cicla sonido: alto, medio, bajo, apagado
    if (!audioEnabled()) {
      audioSetVolume(0);
      audioSetEnabled(true);
    } else if (audioVolume() + 1 < VOL_LEVELS) {
      audioSetVolume(audioVolume() + 1);
    } else {
      audioSetEnabled(false);
    }
    if (audioEnabled()) sfxPlay(SFX_TAP);    // se oye el nivel nuevo
    return;
  }
  if (BRI_PILL.contains(x, y)) {  // cicla brillo: alto, medio, bajo
    userDim = (userDim + 1) % BRIGHT_LEVELS;
    Preferences p;
    p.begin("tamapoke", false);
    p.putUChar("bri", userDim);
    p.end();
    sfxPlay(SFX_TAP);
    return;
  }
  if (LANG_PILL.contains(x, y)) {  // cicla idioma
    setLang((Lang)((gLang + 1) % LANG_COUNT));
    applyLangFont();  // la fuente cambia con el idioma
    sfxPlay(SFX_TAP);
    return;
  }
  PowerPill pill = powerPillAt(x, y);
  if (pill == PILL_TOUCH_LOCK) {
    touchLockMs = touchLockNext(touchLockMs);
    autoTouch.delayMs = touchLockMs;
    Preferences p;
    p.begin("tamapoke", false);
    p.putUChar("tlk", touchLockSel(touchLockMs));
    p.end();
    return;
  }
  if (pill == PILL_SCREEN_OFF) {
    powerOffMs = powerOffNext(powerOffMs);
    Preferences p;
    p.begin("tamapoke", false);
    p.putUChar("off", powerOffSel(powerOffMs));
    p.end();
    return;
  }
  if (pill == PILL_BATTERY) {
    batAsPercent = !batAsPercent;
    Preferences p;
    p.begin("tamapoke", false);
    p.putBool("bpct", batAsPercent);
    p.end();
    return;
  }
  if (OK_BTN.contains(x, y)) { applyClock(); return; }
}

// medallas en la ficha: badge con etiqueta, color si conseguida
void drawMedalBadge(int x, int y, int i) {
  bool got = pet.hasMedal(1 << i);
  gfx->fillRoundRect(x, y, 100, 24, 6, got ? UI_BAR_OK : UI_TRACK);
  if (!got) gfx->drawRoundRect(x, y, 100, 24, 6, UI_TRACK);
  gfx->setTextColor(got ? UI_BG_DAY : 0x9492);
  setSize(2);
  setCur(x + (100 - textW(medalLabel(i), 2)) / 2, y + 5);
  printT(medalLabel(i));
}

// pagina 0: perfil (retrato grande, identidad, racha, vinculo, baya)
void renderCardProfile() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  const char *nm = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
  char head[32];
  snprintf(head, sizeof(head), T(S_NAME_FMT), pet.shiny ? "*" : "", nm, pet.level());
  gfx->setTextColor(d.accent);
  // auto-encoge: a tamano 3 los nombres largos no caben en la franja estrecha de
  // arriba de la pantalla redonda, asi que se cortaban por el borde. El ancho se
  // MIDE con textW(): strlen() cuenta bytes, y en una fila UTF-8 no hay un byte
  // por caracter ni todos miden lo mismo. Con la fuente clasica textW() devuelve
  // strlen()*6*size, o sea exactamente la cuenta anterior: no mueve un pixel en
  // los seis idiomas latinos (11 caracteres a tamano 3 son esos 198 px).
  setSize(3);
  int hts = (textW(head, 3) <= 198) ? 3 : 2;
  setSize(hts);
  setCur(centerX(head, hts), hts == 3 ? 34 : 40);
  printT(head);
  if (pet.nick[0]) {  // especie real bajo el apodo
    const char *sp = dexName(pet.speciesId);
    char par[32];
    snprintf(par, sizeof(par), "(%s)", sp);
    gfx->setTextColor(UI_TRACK);
    setSize(2);
    setCur(centerX(par, 2), 64);   // medido, no contado en bytes
    printT(par);
  }

  // retrato grande animado
  if (pmd.loaded) drawPmdAct(PMD_IDLE, CX, 206, millis(), true, false, 4);

  // racha con llama
  int sx = 138, sy = 224;
  gfx->fillTriangle(sx + 8, sy, sx + 1, sy + 18, sx + 15, sy + 18, UI_BAR_BAD);
  gfx->fillTriangle(sx + 8, sy + 7, sx + 4, sy + 18, sx + 12, sy + 18, UI_BAR_WARN);
  char rl[40];  // en japones pierde cifras con 30 a partir de 100 dias
  snprintf(rl, sizeof(rl), T(S_STREAK_FMT), pet.streak, pet.bestStreak);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(sx + 24, sy + 2);
  printT(rl);

  drawCardStat(258, T(S_VIN), pet.bond, 100, C565(0xd4, 0x52, 0x7e));

  const char *berry = !pet.berryKnown ? T(S_BERRY_UNK)
                      : pet.lovesBerry(0) ? T(S_BERRY_RED)
                      : pet.lovesBerry(1) ? T(S_BERRY_BLUE)
                                          : T(S_BERRY_GREEN);
  char info[48];
  snprintf(info, sizeof(info), T(S_INFO_FMT), berry,
           (unsigned long)(pet.ageMinutes / 1440));
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(centerX(info, 2), 296);
  printT(info);

  gfx->setTextColor(UI_TRACK);
  setCur(centerX(T(S_RENAME_HINT), 2), 332);
  printT(T(S_RENAME_HINT));
}

// pagina 1: combate (4 barras + boton de entrenar)
void renderCardStats() {
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(T(S_BATTLE), 3), 48);
  printT(T(S_BATTLE));

  drawCardStat(118, T(S_STAT_ATK), pet.atkStat(), 260, UI_BAR_BAD);
  drawCardStat(160, T(S_STAT_DEF), pet.defStat(), 260, 0x4C98);
  drawCardStat(202, T(S_STAT_SPE), pet.speStat(), 260, UI_BAR_WARN);
  drawCardStat(244, T(S_STAT_WGT), pet.weight, 100, 0xB3C8);

  // boton: saco de entrenamiento de fuerza
  gfx->fillRoundRect(96, 300, 274, 40, 12, UI_BAR_BAD);
  gfx->setTextColor(UI_BG_DAY);
  setSize(2);
  setCur(centerX(T(S_TRAIN_STR), 2), 311);
  printT(T(S_TRAIN_STR));
}

// pagina 2: medallas con etiqueta descriptiva
void renderCardMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[24];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(head, 3), 48);
  printT(head);

  for (int i = 0; i < MED_COUNT; i++) {
    int x = 28 + (i % 2) * 206, y = 104 + (i / 2) * 54;
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, 196, 44, 10, g ? UI_BAR_OK : UI_TRACK);
    if (g) {  // marca de conseguida
      gfx->fillCircle(x + 22, y + 22, 11, UI_BG_DAY);
      gfx->setTextColor(UI_BAR_OK);
      setSize(2);
      setCur(x + 16, y + 13);
      printT("v");
    }
    gfx->setTextColor(g ? UI_BG_DAY : 0x8410);
    setSize(2);
    setCur(x + 44, y + 14);
    printT(medalDesc(i));
  }

  // records de los minijuegos de Play: icono y mejor puntuacion de cada uno
  for (uint8_t g = 0; g < GAME_COUNT; g++) {
    int x = 61 + g * 88, y = 322;
    gfx->fillRoundRect(x, y, 80, 40, 10, UI_TRACK);
    drawMap(GAME_ICON[g], 16, x + 4, y + 4, 2, false);
    char n[8];
    snprintf(n, sizeof(n), "%u", pet.gameBest(g));
    gfx->setTextColor(UI_INK);
    setSize(2);
    setCur(x + 38 + (40 - textW(n, 2)) / 2, y + 12);
    printT(n);
  }
}

// pagina 3: progreso (nivel, evolucion, descuidos) — saca a la luz mecanicas
// que antes eran invisibles (cuanto falta para subir/evolucionar y por que)
void renderCardProgress() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(T(S_PROGRESS), 3), 44);
  printT(T(S_PROGRESS));

  // nivel grande
  char lv[10];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), pet.level());
  setSize(5);
  setCur(centerX(lv, 5), 86);
  printT(lv);

  // barra de progreso al siguiente nivel (1 nivel = 60 min de juego)
  uint8_t into = pet.ageMinutes % MINUTES_PER_LEVEL;
  int bx = 93, bw = 280, by = 158, bh = 22;
  gfx->fillRoundRect(bx, by, bw, bh, 6, UI_TRACK);
  int fw = (bw - 4) * into / MINUTES_PER_LEVEL;
  if (fw > 0) gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 5, UI_BAR_OK);
  char nx[32];
  snprintf(nx, sizeof(nx), T(S_NEXT_LVL_FMT), MINUTES_PER_LEVEL - into, pet.level() + 1);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(centerX(nx, 2), by + 32);
  printT(nx);

  // estado de evolucion
  gfx->setTextColor(UI_TRACK);
  setCur(centerX(T(S_EVO_LABEL), 2), 230);
  printT(T(S_EVO_LABEL));
  char evoBuf[32];
  const char *evo;
  uint16_t evoCol = UI_INK;
  if (d.evolvesTo == 0) {
    evo = T(S_FINAL_FORM);
  } else {
    int needed = d.evolveLevel + pet.careMistakes;
    if (pet.level() >= needed) {
      if (pet.lowestStat() >= 40) { evo = T(S_EVO_READY); evoCol = UI_BAR_OK; }
      else { evo = T(S_EVO_BLOCKED); evoCol = UI_BAR_BAD; }
    } else {
      snprintf(evoBuf, sizeof(evoBuf), T(S_EVO_IN_FMT), needed - pet.level());
      evo = evoBuf;
    }
  }
  gfx->setTextColor(evoCol);
  setCur(centerX(evo, 2), 256);
  printT(evo);

  // descuidos (retrasan la evolucion)
  char ms[24];
  snprintf(ms, sizeof(ms), T(S_MISTAKES_FMT), pet.careMistakes);
  gfx->setTextColor(pet.careMistakes > 0 ? UI_BAR_BAD : UI_INK);
  setCur(centerX(ms, 2), 312);
  printT(ms);
}

// pagina 4: cuidados. Tres botones que ciclan un ajuste cada uno (se guardan al
// tocar): ritmo normal <-> relajado, hora de dormir apagado -> 21-07 -> 22-07 ->
// 23-08, y huevos sorpresa <-> elegir. 72 px de alto para que el tercero acabe en
// y=324, lejos de los puntos de pagina (y=374); ahi el circulo mide 424 px.
#define CARE_BTN_X 73
#define CARE_BTN_W 320
#define CARE_BTN_H 72
#define CARE_PACE_Y 92
#define CARE_BED_Y 172
#define CARE_EGG_Y 252
static const char *const BED_TXT[BED_PRESETS] = { "", "21:00-07:00", "22:00-07:00", "23:00-08:00" };

static void drawCareBtn(int y, const char *label, const char *value) {
  gfx->fillRoundRect(CARE_BTN_X, y, CARE_BTN_W, CARE_BTN_H, 16, UI_WHITE);
  gfx->drawRoundRect(CARE_BTN_X, y, CARE_BTN_W, CARE_BTN_H, 16, UI_INK);
  gfx->setTextColor(UI_TRACK);
  setSize(2);
  setCur(centerX(label, 2), y + 8);
  printT(label);
  gfx->setTextColor(UI_INK);
  setSize(4);
  setCur(centerX(value, 4), y + 32);
  printT(value);
}

void renderCardCare() {
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(T(S_CARE), 3), 44);
  printT(T(S_CARE));
  uint8_t bed = pet.bedPreset < BED_PRESETS ? pet.bedPreset : 0;
  drawCareBtn(CARE_PACE_Y, T(S_PACE), T(pet.pace == PACE_RELAXED ? S_PACE_RELAXED : S_PACE_NORMAL));
  drawCareBtn(CARE_BED_Y, T(S_BEDTIME), bed ? BED_TXT[bed] : T(S_BED_OFF));
  drawCareBtn(CARE_EGG_Y, T(S_EGGS), T(pet.eggPickMode == EGGPICK_ON ? S_EGG_CHOOSE : S_EGG_SURPRISE));
}

// toque en la pagina de cuidados: true si cayo en un boton (cicla, guarda y suena)
bool careTap(int16_t x, int16_t y) {
  if (x < CARE_BTN_X || x > CARE_BTN_X + CARE_BTN_W) return false;
  if (y >= CARE_PACE_Y && y <= CARE_PACE_Y + CARE_BTN_H) {
    pet.setPace(pet.pace == PACE_RELAXED ? PACE_NORMAL : PACE_RELAXED);
  } else if (y >= CARE_BED_Y && y <= CARE_BED_Y + CARE_BTN_H) {
    pet.setBedPreset((pet.bedPreset + 1) % BED_PRESETS);
    uint32_t e = rtcEpoch();
    if (e) bedSample(e);  // vuelve a tomar la referencia: cambiar el horario no duerme ni despierta por si solo
  } else if (y >= CARE_EGG_Y && y <= CARE_EGG_Y + CARE_BTN_H) {
    pet.setEggPickMode(pet.eggPickMode == EGGPICK_ON ? EGGPICK_OFF : EGGPICK_ON);  // vale para el proximo final
  } else {
    return false;
  }
  sfxPlay(SFX_TAP);
  return true;
}

void renderCard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (cardPage == 0) renderCardProfile();
  else if (cardPage == 1) renderCardStats();
  else if (cardPage == 2) renderCardMedals();
  else if (cardPage == 3) renderCardProgress();
  else renderCardCare();

  // indicador de 5 paginas + ayuda
  for (int i = 0; i <= CARD_LAST_PAGE; i++) {
    if (i == cardPage) gfx->fillCircle(181 + i * 26, 374, 5, UI_INK);
    else gfx->drawCircle(181 + i * 26, 374, 4, UI_INK);
  }
  gfx->setTextColor(UI_TRACK);
  setSize(2);
  setCur(centerX(T(S_BACK), 2), 398);
  printT(T(S_BACK));
  flushUI();
}

// ---------- teclado para renombrar ----------

static const char KB_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-";  // 28 + DEL + OK = 30
#define KB_COLS 6
#define KB_X 40
#define KB_Y 150
#define KB_W 64
#define KB_H 52

void openKeyboard() {
  kbOpen = true;
  strncpy(nameBuf, pet.nick, sizeof(nameBuf) - 1);
  nameBuf[sizeof(nameBuf) - 1] = 0;
  nameLen = strlen(nameBuf);
}

void renderKeyboard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(centerX(T(S_NAME), 2), 56);
  printT(T(S_NAME));
  // buffer actual
  gfx->fillRoundRect(83, 84, 300, 40, 8, UI_WHITE);
  gfx->drawRoundRect(83, 84, 300, 40, 8, UI_INK);
  setSize(3);
  setCur(95, 94);
  printT(nameLen ? nameBuf : "_");

  for (int i = 0; i < 30; i++) {
    int x = KB_X + (i % KB_COLS) * KB_W, y = KB_Y + (i / KB_COLS) * KB_H;
    bool special = (i >= 28);
    gfx->fillRoundRect(x, y, KB_W - 6, KB_H - 6, 6, special ? UI_BAR_WARN : UI_WHITE);
    gfx->drawRoundRect(x, y, KB_W - 6, KB_H - 6, 6, UI_INK);
    gfx->setTextColor(UI_INK);
    setSize(2);
    if (i < 28) {
      setCur(x + KB_W / 2 - 9, y + KB_H / 2 - 10);
      printT(KB_KEYS[i]);
    } else {
      const char *lab = (i == 28) ? "<-" : "OK";
      setCur(x + KB_W / 2 - 15, y + KB_H / 2 - 10);
      printT(lab);
    }
  }
  flushUI();
}

void keyboardTap(int16_t x, int16_t y) {
  // filtra ANTES de dividir: con x/y menores que KB_X/KB_Y la resta da negativo
  // y la division entera trunca hacia cero (no hacia -inf), asi que col/row
  // saldria 0 en vez de negativo y el filtro de abajo no lo detectaria
  if (x < KB_X || y < KB_Y) return;
  int col = (x - KB_X) / KB_W, row = (y - KB_Y) / KB_H;
  if (col >= KB_COLS || row >= 5) return;
  int i = row * KB_COLS + col;
  if (i >= 30) return;
  if (i == 28) {  // borrar
    if (nameLen) nameBuf[--nameLen] = 0;
  } else if (i == 29) {  // OK
    pet.rename(nameBuf);
    kbOpen = false;
  } else if (nameLen < sizeof(nameBuf) - 1) {
    nameBuf[nameLen++] = KB_KEYS[i];
    nameBuf[nameLen] = 0;
  }
}

// ---------- galeria pokedex ----------

#define GAL_X 73
#define GAL_Y 84
#define GAL_CELL 80

// dibuja una miniatura centrada en su celda; sil=true la pinta en tinta
void drawThumb(const uint8_t *b, int x, int y, int s, bool sil) {
  uint8_t w = b[0], h = b[1], n = b[2];
  const uint8_t *pal = b + 3;
  const uint8_t *d = pal + n * 2;
  int ox = x + (GAL_CELL - w * s) / 2;
  int oy = y + (GAL_CELL - h * s) / 2;
  for (int r = 0; r < h; r++) {
    const uint8_t *row = d + r * w;
    // thumbKey(): an index past the thumbnail's own palette (n entries) is transparent
    forRuns(w, [&](int c) { return thumbKey(row[c], n, sil); },
            [&](int c0, int len, int k) {
              uint16_t col = sil ? INK_K : (uint16_t)(pal[k * 2] | (pal[k * 2 + 1] << 8));
              gfx->fillRect(ox + c0 * s, oy + r * s, len * s, s, col);
            });
  }
}

void renderGallery() {
  if (galleryDetail) {  // vista detalle: se redibuja siempre (animada)
    gfx->fillScreen(RGB565_BLACK);
    gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
    const DexEntry &d = DEX_TBL[galleryDetail];
    bool reg = pet.isRegistered(galleryDetail);
    char head[32];
    snprintf(head, sizeof(head), "N.%03d %s%s", galleryDetail,
             pet.isShinyRegistered(galleryDetail) ? "*" : "", reg ? dexName(galleryDetail) : "???");
    gfx->setTextColor(reg ? d.accent : UI_INK);
    // auto-encoge nombres largos (no caben a t3), midiendo en vez de contar
    // bytes; 13 caracteres a tamano 3 son los 234 px de antes.
    setSize(3);
    int gts = (textW(head, 3) <= 234) ? 3 : 2;
    setSize(gts);
    setCur(centerX(head, gts), gts == 3 ? 56 : 60);
    printT(head);
    if (galleryPmd.loaded) {
      // animado y a color si esta registrado; silueta estatica si no (estilo "?")
      drawPmdActM(galleryPmd, PMD_IDLE, CX, 300, reg ? millis() : 0, true, !reg, 6);
    } else {
      const uint8_t *t = thumbs.get(galleryDetail);
      if (t) drawThumb(t, CX - GAL_CELL, 135, 4, !reg);
    }
    gfx->setTextColor(UI_INK);
    setSize(2);
    setCur(centerX(T(S_DETAIL_BACK), 2), 408);
    printT(T(S_DETAIL_BACK));
    flushUI();
    return;
  }

  if (!galleryDirty) return;  // la rejilla es estatica
  galleryDirty = false;

  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char head[24];
  snprintf(head, sizeof(head), T(S_POKEDEX_FMT), pet.registeredCount());
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(head, 3), 36);
  printT(head);

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int16_t dex = galleryPage * 16 + r * 4 + c + 1;
      if (dex > 151) break;
      int x = GAL_X + c * GAL_CELL, y = GAL_Y + r * GAL_CELL;
      const uint8_t *t = thumbs.get(dex);
      if (t) {
        drawThumb(t, x, y, 2, !pet.isRegistered(dex));
        if (pet.isShinyRegistered(dex)) {
          gfx->setTextColor(UI_BAR_WARN);
          setSize(2);
          setCur(x + 62, y + 4);
          printT("*");
        }
      } else {
        char num[6];
        snprintf(num, sizeof(num), "%d", dex);
        gfx->setTextColor(UI_TRACK);
        setSize(2);
        setCur(x + 24, y + 32);
        printT(num);
      }
    }
  }
  // puntos de pagina
  for (int i = 0; i < 10; i++) {
    if (i == galleryPage) gfx->fillCircle(170 + i * 14, 436, 4, UI_INK);
    else gfx->drawCircle(170 + i * 14, 436, 3, UI_INK);
  }
  flushUI();
}

void galleryTap(int16_t x, int16_t y) {
  if (galleryDetail) {  // volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  if (y < 72) {  // tocar la cabecera = salir
    galleryOpen = false;
    galleryPmd.unload();
    return;
  }
  // mismo motivo que en el teclado: filtrar antes de dividir
  if (x < GAL_X || y < GAL_Y) return;
  int c = (x - GAL_X) / GAL_CELL, r = (y - GAL_Y) / GAL_CELL;
  if (c > 3 || r > 3) return;
  int16_t dex = galleryPage * 16 + r * 4 + c + 1;
  if (dex > 151) return;
  galleryDetail = dex;
  galleryPmd.load(dex, pet.isShinyRegistered(dex));
}
