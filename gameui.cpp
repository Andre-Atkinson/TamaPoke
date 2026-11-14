// The minigames' screens and the strength-training sack (gameui.h). Moved out of
// TamaPoke.ino (#41).
#include <Arduino.h>
#include "app.h"
#include "audio.h"
#include "blit.h"
#include "dex.h"
#include "gameui.h"
#include "i18n.h"
#include "minigames.h"
#include "scene.h"
#include "sdmon.h"
#include "species.h"
#include "uitext.h"

// Used only in this file, and called above its definition (in TamaPoke.ino the
// prototype arduino-cli generates covered that).
static void gameSounds();

// minijuegos de Play (minigames.h): uno al azar de la bolsa cada vez
bool gameOpen = false;
static MiniGames games;
static uint32_t gameOverUntil = 0;   // pantalla de resultado hasta este millis
static uint16_t gameShownScore = 0;  // puntuacion de la partida que se ensena en el resultado
static bool gameNewHi = false;

// saco de entrenamiento (entrena la fuerza)
bool sackOpen = false;
uint32_t sackUntil = 0, sackOverUntil = 0;
uint16_t sackHits = 0;
float sackShake = 0;
static uint8_t sackGain = 0;
static bool sackNewHi = false;

// ---------- minijuegos de Play (logica en minigames.h) ----------

void startGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  uint8_t fav = 0;
  while (fav < 2 && !pet.lovesBerry(fav)) fav++;
  games.start(millis(), (uint32_t)random(0x7FFFFFFF) ^ micros(), fav, pet.berryKnown);
  gameOpen = true;
  gameOverUntil = 0;
  gameNewHi = false;
}

// Un informe del tactil con un juego abierto. El dedo que mantiene "salir" deja el
// juego sin premio, como antes tocar la cabecera; el resto del toque se traga para
// que al soltar no se resuelva como un toque en la pantalla principal.
void gameTouch(bool pressed, int16_t x, int16_t y) {
  bool start = pressed && !wasPressed;
  // Un toque que empieza con la pantalla apagada o atenuada solo la despierta
  // (como en la pantalla principal): el juego no lo ve, ni siquiera al soltar.
  bool dark = start && (screenOff || dimStage > 0);
  if (dark) screenOff = false;
  if (start) {
    holdFired = true;  // nada de este toque llega al gesto normal si el juego se cierra
    swallowGesture = true;
  }
  if (gameOverUntil) {  // pantalla de resultado: solo esperar
    if (start || (!pressed && wasPressed)) wakeUI();
    return;
  }
  bool exitHeld = games.touch(pressed, x, y, millis(), dark);
  // El dedo quieto informa cada 20 ms: solo cuenta (y despierta) al apoyar, al
  // soltar o al moverse de verdad (games.counted); si no, Berry Song no acabaria.
  if (start || (!pressed && wasPressed) || games.counted) wakeUI();
  if (exitHeld) {
    gameOpen = false;
    holdFired = true;
    swallowGesture = true;
    return;
  }
  gameSounds();  // la nota o el golpe suenan ya, no en el proximo frame
}

// sonidos que pide el juego tras un paso o un toque
static void gameSounds() {
  uint8_t note = games.takeNote();
  if (note < 4) sfxPlay(SFX_NOTE0 + note);
  uint8_t e = games.takeEvents();
  if (e & GEV_BAD) sfxPlay(SFX_DENY);
  if (e & GEV_WIN) sfxPlay(SFX_EVOLVE);  // las 12 notas: la fanfarria (finishGame no suena otra)
  else if (e & GEV_HEART) sfxPlay(SFX_HEART);
  else if ((e & GEV_GOOD) && games.game() != GAME_SONG) sfxPlay(games.game() == GAME_CATCH ? SFX_EAT : SFX_PLAY);
}

// fin de la partida (el juego acabo o nadie lo toco en 15 s): record y recompensa
static void finishGame() {
  uint8_t g = games.game();
  gameShownScore = games.score();
  gameNewHi = gameShownScore > pet.gameBest(g);
  pet.playGame(g, gameShownScore);
  if (!games.won()) sfxPlay(gameNewHi && gameShownScore > 0 ? SFX_MEDAL : SFX_LEVEL);
  gameOverUntil = millis() + 4000;
}

// ---------- saco de entrenamiento (entrena la fuerza) ----------

void startSack() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
#if BOARD_AMOLED_175C
  if (!trainingCaptureReady) { showControlToast("Touch capture unavailable"); return; }
  trainingCapture.reset();
#endif
  sackOpen = true;
  sackUntil = millis() + 10000;
  sackOverUntil = 0;
  sackHits = 0;
  sackShake = 0;
  sackNewHi = false;
}

void sackTap() {
  if (!timeLeft(sackUntil)) return;  // ya termino el tiempo
  sackHits++;
  sackShake = 16;  // sacude el saco
}

static void drawGameScene();  // prototipo (definida mas abajo)

void renderSack() {
  uint32_t now = millis();
  drawGameScene();  // fondo del habitat
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  // pantalla de resultado
  if (sackOverUntil) {
    if (!timeLeft(sackOverUntil)) {
#if BOARD_AMOLED_175C
      leaveSack();
#else
      sackOpen = false;
#endif
      return;
    }
    char b[20];
    snprintf(b, sizeof(b), T(S_HITS_FMT), sackHits);
    gfx->setTextColor(ink);
    setSize(4);
    setCur(centerX(b, 4), 150);
    printT(b);
    char g[18];
    snprintf(g, sizeof(g), T(S_STR_GAIN_FMT), sackGain);
    gfx->setTextColor(UI_BAR_BAD);
    setSize(3);
    setCur(centerX(g, 3), 210);
    printT(g);
    setSize(2);
    if (sackNewHi && sackHits > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      setCur(centerX(T(S_NEW_RECORD), 2), 256);
      printT(T(S_NEW_RECORD));
    } else {
      char r[18];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.strHi);
      gfx->setTextColor(ink);
      setCur(centerX(r, 2), 256);
      printT(r);
    }
    flushUI();
    return;
  }

  // se acabaron los 10 s: aplicar entrenamiento
  if (!timeLeft(sackUntil)) {
    sackNewHi = (sackHits > pet.strHi);
    sackGain = pet.trainStrength(sackHits);
#if BOARD_AMOLED_175C
    Serial.printf("Strength training: hits=%u input_drops=%lu poll=20ms exit_hold=1000ms\n",
                  sackHits, (unsigned long)trainingCapture.dropped());
#endif
    sfxPlay(sackNewHi ? SFX_MEDAL : SFX_PLAY);
    sackOverUntil = now + 3500;
    flushUI();
    return;
  }

#if BOARD_AMOLED_175C
  // Everything below is I2C-free. Snapshot policy state before starting the
  // worker; RAII joins it on return, including the blocking QSPI flush.
  uint32_t exitHeld = trainingCapture.exitHeldMs(now);
  TrainingCapture::Frame captureFrame(trainingCapture, !screenIsLocked());
#endif
  // aporreo activo
  sackShake *= 0.84f;
  int off = (int)(sackShake * sinf(now * 0.05f));
  int sx = CX + off, top = 86, sy = 150;
  gfx->fillRect(CX - 3, 56, 6, top - 56, ink);          // gancho/cuerda
  gfx->fillRect(sx - 4, top - 30, 8, 34, ink);          // cadena
  gfx->fillRoundRect(sx - 42, top, 84, 150, 26, C565(0xb5, 0x3a, 0x3a));  // saco
  gfx->fillRoundRect(sx - 42, top, 84, 22, 18, C565(0x7e, 0x28, 0x28));   // tapa
  gfx->drawRoundRect(sx - 42, top, 84, 150, 26, ink);
  gfx->fillRect(sx - 42, top + 70, 84, 4, C565(0x7e, 0x28, 0x28));        // costura

  // contador de golpes
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", sackHits);
  gfx->setTextColor(ink);
  setSize(6);
  setCur(centerX(buf, 6), 268);
  printT(buf);

  setSize(2);
  setCur(centerX(T(S_HIT_FAST), 2), 322);
  printT(T(S_HIT_FAST));

  // barra de tiempo
  uint32_t left = sackUntil - now;
  int bw = 280, fw = (int)((uint32_t)bw * left / 10000);
  gfx->fillRoundRect(CX - bw / 2, 350, bw, 16, 5, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, 350, fw, 16, 5, UI_BAR_OK);
#if BOARD_AMOLED_175C
  // Visible, separated from the bag/count/time bar, fully inside the circle.
  const int ex = TrainingTouch::EXIT_X, ey = TrainingTouch::EXIT_Y;
  const int ew = TrainingTouch::EXIT_W, eh = TrainingTouch::EXIT_H;
  gfx->fillRoundRect(ex, ey, ew, eh, 8, UI_TRACK);
  int progress = (int)(ew * (exitHeld > 1000 ? 1000 : exitHeld) / 1000);
  if (progress > 2) gfx->fillRoundRect(ex, ey, progress, eh, 8, UI_BAR_WARN);
  gfx->drawRoundRect(ex, ey, ew, eh, 8, ink);
  gfx->setTextColor(ink);
  setSize(2);
  setCur(centerX(T(S_HOLD_EXIT), 2), ey + 16);
  printT(T(S_HOLD_EXIT));
#endif

  flushUI();
}

// fondo del minijuego: hatibat del bicho (cielo por hora + suelo del bioma)
static void drawGameScene() {
  int hh = sceneHour();
  bool night = hh < 6 || hh >= 20;
  uint16_t top, bot;
  if (night)       { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (hh < 8) { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }
  else if (hh < 18){ top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }
  else             { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }
  int hor = 376;
  for (int y = 0; y < hor; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, hor));
  if (night)
    for (auto &st : STARS) gfx->fillRect(st[0], st[1], 4, 4, UI_WHITE);
  uint8_t bio = pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome;
  uint16_t soil = BIOME_SOIL[bio < 6 ? bio : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  gfx->fillRect(0, hor, 466, 466 - hor, soil);
}

// ---------- dibujo de los minijuegos ----------

static bool gameNight() {
  int h = sceneHour();
  return h < 6 || h >= 20;
}


// el bicho dentro de un juego: su sprite PMD (sin esa accion, IDLE), si no el sprite
// antiguo de la SD, y si tampoco hay, la Poke Ball
static void drawGamePet(uint8_t act, int cx, int groundY, uint8_t maxS) {
  if (pmd.loaded) {
    if (!pmd.has(act)) act = PMD_IDLE;
    drawPmdAct(act, cx, groundY, millis(), true, false, maxS);
  } else if (mon.loaded && mon.frames) {
    int s = (mon.h * 2 > 130) ? 1 : 2;
    int w = mon.w * s, h = mon.h * s;
    uint16_t fm = mon.frameMs ? mon.frameMs : 100;
    uint16_t fi = (millis() / fm) % mon.frames;
    const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
    int px = cx - w / 2, py = groundY - h;
    for (int r = 0; r < mon.h; r++) {
      const uint8_t *row = fr + r * mon.w;
      forRuns(mon.w, [&](int c) { return indexedKey(row[c], false); },
              [&](int c0, int len, int k) { gfx->fillRect(px + c0 * s, py + r * s, len * s, s, mon.pal[k]); });
    }
  } else {
    drawMap(SPR_ICON_PLAY, 16, cx - 24, groundY - 48, 3, false);
  }
}

// marcador y record de este juego, debajo del boton de salir
static void drawGameHud(uint16_t ink, uint16_t score) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", score);
  gfx->setTextColor(ink);
  setSize(4);
  setCur(centerX(buf, 4), 68);
  printT(buf);
  // 24 y no 12: en japones "きろく %u" son 10 bytes antes de la cifra y el
  // record se quedaba en su primer digito ("きろく 2" con 219). Las tallas de
  // los buffers de texto las vigila TestBuffersDeTexto en test/test_tools.py.
  char rec[24];
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.gameBest(games.game()));
  setSize(2);
  setCur(centerX(rec, 2), 106);
  printT(rec);
}

// barra fina de lo que queda de partida (Berry Catch: tiempo; Peekaboo: apariciones)
static void drawGameBar(uint32_t left, uint32_t total) {
  const int bw = 200, by = 130;
  int fw = total ? (int)((uint32_t)bw * left / total) : 0;
  gfx->fillRoundRect(CX - bw / 2, by, bw, 10, 4, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, by, fw, 10, 4, UI_BAR_OK);
}

// "Mantener: salir", arriba en todos los juegos (como el del saco en la 1.75C). El
// boton es claro de dia y de noche, asi que su texto va siempre en tinta oscura.
static void drawGameExit() {
  const int ex = GameTouchRouter::EXIT_X, ey = GameTouchRouter::EXIT_Y;
  const int ew = GameTouchRouter::EXIT_W, eh = GameTouchRouter::EXIT_H;
  const uint32_t hold = GameTouchRouter::EXIT_HOLD_MS;
  uint32_t held = games.input.heldMs(millis());
  gfx->fillRoundRect(ex, ey, ew, eh, 8, UI_TRACK);
  int progress = (int)(ew * (held > hold ? hold : held) / hold);
  if (progress > 2) gfx->fillRoundRect(ex, ey, progress, eh, 8, UI_BAR_WARN);
  gfx->drawRoundRect(ex, ey, ew, eh, 8, UI_INK);
  gfx->setTextColor(UI_INK);
  setSize(2);
  setCur(centerX(T(S_HOLD_EXIT), 2), ey + 12);
  printT(T(S_HOLD_EXIT));
}

// portada (~1,2 s): icono, nombre y pista
static void drawGameTitle(uint16_t ink) {
  uint8_t g = games.game();
  drawMap(GAME_ICON[g], 16, CX - 32, 112, 4, false);
  const char *name = T((StrId)(S_GAME_KEEPY + g));
  setSize(4);  // textW() mide con el tamano puesto (fuentes CJK)
  uint8_t sz = textW(name, 4) <= 380 ? 4 : 3;
  setSize(sz);
  gfx->setTextColor(ink);
  setCur(centerX(name, sz), 200);
  printT(name);
  const char *hint = T((StrId)(S_HINT_KEEPY + g));
  setSize(2);
  setCur(centerX(hint, 2), 256);
  printT(hint);
}

static void drawKeepy(uint16_t ink) {
  const KeepyGame &k = games.keepy;
  drawGameHud(ink, k.score);
  for (int i = 0; i < KeepyGame::LIVES; i++) {  // vidas
    if (i < KeepyGame::LIVES - k.misses) gfx->fillCircle(205 + i * 28, 132, 6, UI_BAR_BAD);
    else gfx->drawCircle(205 + i * 28, 132, 6, UI_TRACK);
  }
  uint8_t act = (k.ballX > k.petX + 4) ? PMD_WALKR : (k.ballX < k.petX - 4) ? PMD_WALKL : PMD_IDLE;
  drawGamePet(act, (int)k.petX, 394, 3);
  // anillo de impacto que se expande y desvanece (feedback suave del golpe)
  uint32_t ht = k.t - k.hitT;
  if (k.hasHit && ht < 260) {
    int rad = 22 + (int)(ht / 6);
    gfx->drawCircle((int)k.hitX, (int)k.hitY, rad, C565(0xff, 0xe7, 0x9f));
    gfx->drawCircle((int)k.hitX, (int)k.hitY, rad - 2, C565(0xff, 0xd9, 0x8a));
  }
  drawMap(SPR_ICON_PLAY, 16, (int)k.ballX - 24, (int)k.ballY - 24, 3, false);  // la Poke Ball
}

static const char *const *const CATCH_ICON[4] = { SPR_ICON_FOOD, SPR_ICON_BERRY_B, SPR_ICON_BERRY_G,
                                                  SPR_ICON_ROTTEN };

static void drawCatch(uint16_t ink) {
  const CatchGame &c = games.catcher;
  drawGameHud(ink, c.result());
  drawGameBar(c.timeLeft(), CatchGame::DURATION_MS);
  uint8_t act = c.hurting() ? PMD_HURT
                : c.eating() ? PMD_EAT
                : c.targetX > c.petX + 4 ? PMD_WALKR
                : c.targetX < c.petX - 4 ? PMD_WALKL : PMD_IDLE;
  drawGamePet(act, (int)c.petX, 394, 3);
  if (c.eating() && c.loved) drawMap(SPR_HEART, 32, (int)c.petX + 10, 250, 2, false);
  for (const CatchGame::Berry &b : c.berries)
    if (b.live) drawMap(CATCH_ICON[b.kind < 4 ? b.kind : 0], 16, b.x - 24, (int)b.y - 24, 3, false);
}

// arbusto de Peekaboo: la copa va detras del bicho y el borde de delante encima
static void drawBush(int x, int y, bool front, bool night) {
  uint16_t light = night ? C565(0x2c, 0x5c, 0x34) : C565(0x58, 0xb8, 0x68);
  uint16_t dark = night ? C565(0x1e, 0x45, 0x26) : C565(0x3c, 0x8a, 0x4c);
  if (!front) {
    gfx->fillCircle(x, y - 14, 40, light);
    gfx->fillCircle(x - 34, y + 4, 30, light);
    gfx->fillCircle(x + 34, y + 4, 30, light);
  } else {
    gfx->fillCircle(x - 30, y + 18, 26, dark);
    gfx->fillCircle(x, y + 22, 28, dark);
    gfx->fillCircle(x + 30, y + 18, 26, dark);
    gfx->fillRect(x - 56, y + 18, 112, 22, dark);
  }
}

static void drawPeek(uint16_t ink) {
  const PeekGame &p = games.peek;
  bool night = gameNight();
  drawGameHud(ink, p.score);
  drawGameBar(PeekGame::POPS - p.pops, PeekGame::POPS);
  for (uint8_t b = 0; b < PeekGame::BUSHES; b++) {  // fila de arriba primero: la de abajo tapa
    int shake = p.rustling(b) ? ((p.t / 40) % 2 ? 4 : -4) : 0;
    int x = PeekGame::BX[b] + shake, y = PeekGame::BY[b];
    drawBush(x, y, false, night);
    if (p.up() && p.bush == b) {
      // sale y se esconde en 150 ms: los pies suben desde detras del borde
      uint32_t in = p.t - p.phaseStart, out = p.phaseEnd > p.t ? p.phaseEnd - p.t : 0;
      uint32_t edge = in < out ? in : out;
      int sink = edge >= 150 ? 0 : (int)(40 * (150 - edge) / 150);
      drawGamePet(p.hit ? PMD_NOD : PMD_POSE, PeekGame::BX[b], y + 4 + sink, 3);
    }
    drawBush(x, y, true, night);
  }
}

static const uint16_t SONG_COL[SongGame::PADS] = { C565(0xe8, 0x50, 0x3a), C565(0x4f, 0x93, 0xc4),
                                                   C565(0x58, 0xb8, 0x68), C565(0xf0, 0x8a, 0xa4) };
static const char *const *const SONG_ICON[SongGame::PADS] = { SPR_ICON_FOOD, SPR_ICON_BERRY_B,
                                                              SPR_ICON_BERRY_G, SPR_ICON_CANDY };

static void drawSong(uint16_t ink) {
  const SongGame &s = games.song;
  bool night = gameNight();
  const char *msg = s.playersTurn() ? T(S_SONG_TURN) : T(S_SONG_LISTEN);
  gfx->setTextColor(ink);
  setSize(4);  // grande si cabe (la fuente CJK divide el tamano entre 2), si no a 3
  uint8_t sz = textW(msg, 4) <= 300 ? 4 : 3;
  setSize(sz);
  setCur(centerX(msg, sz), 68);
  printT(msg);
  char rec[24];  // ver drawGameHud
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.gameBest(GAME_SONG));
  setSize(2);
  setCur(centerX(rec, 2), 104);
  printT(rec);
  for (int i = 0; i < SongGame::TRIES; i++) {  // intentos
    if (i < s.tries) gfx->fillCircle(205 + i * 28, 132, 6, UI_BAR_BAD);
    else gfx->drawCircle(205 + i * 28, 132, 6, UI_TRACK);
  }
  uint8_t lit = s.litPad();
  uint8_t act = s.won ? PMD_POSE
                : s.phase == SongGame::WRONG ? PMD_SIT
                : (s.busy() && lit != SongGame::NO_PAD) ? PMD_NOD : PMD_IDLE;  // "canta" cada nota
  drawGamePet(act, CX, 262, 3);
  for (uint8_t i = 0; i < SongGame::PADS; i++) {
    int x = SongGame::PX[i], y = SongGame::PY[i];
    bool on = lit == i;
    int r = SongGame::PAD_R + (on ? 4 : 0);
    // apagado: el mismo color mas palido (o mas oscuro de noche); encendido: vivo y con aro blanco
    uint16_t col = on ? SONG_COL[i] : lerp565(SONG_COL[i], night ? UI_BG_NIGHT : UI_TRACK, 9, 16);
    gfx->fillCircle(x, y, r, col);
    gfx->drawCircle(x, y, r, on ? UI_WHITE : ink);
    if (on) gfx->drawCircle(x, y, r + 2, UI_WHITE);
    drawMap(SONG_ICON[i], 16, x - 24, y - 24, 3, false);
  }
}

// resultado (4 s): puntuacion, record y premio
static void drawGameResult(uint16_t ink) {
  uint8_t g = games.game();
  const char *name = T((StrId)(S_GAME_KEEPY + g));
  gfx->setTextColor(ink);
  setSize(2);
  setCur(centerX(name, 2), 120);
  printT(name);
  char buf[22];
  snprintf(buf, sizeof(buf), T(S_SCORE_FMT), gameShownScore);
  setSize(4);
  setCur(centerX(buf, 4), 160);
  printT(buf);
  setSize(2);
  if (gameNewHi && gameShownScore > 0) {
    gfx->setTextColor(UI_BAR_WARN);
    setCur(centerX(T(S_NEW_RECORD), 2), 214);
    printT(T(S_NEW_RECORD));
  } else {
    char rec[20];
    snprintf(rec, sizeof(rec), T(S_RECORD_FMT), pet.gameBest(g));
    gfx->setTextColor(ink);
    setCur(centerX(rec, 2), 214);
    printT(rec);
  }
  const char *msg = gameNormScore(g, gameShownScore) >= 10 ? T(S_GREAT_JOY) : T(S_PLUS_JOY);
  gfx->setTextColor(ink);
  setCur(centerX(msg, 2), 250);
  printT(msg);
}

void renderGame() {
  // sin fillScreen(NEGRO): drawGameScene cubre los 466x466 completos. Si el
  // DMA del flush anterior aun lee el buffer, vera contenido valido (no negro
  // a medio pintar), que era el parpadeo a 25 fps.
  uint16_t ink = gameNight() ? UI_INK_NIGHT : UI_INK;
  if (!gameOverUntil) {
    games.frame(millis());
    if (games.busy()) wakeUI();  // la melodia suena: el tiempo no es inactividad ni apaga la pantalla
    gameSounds();
    if (games.over()) finishGame();
  }
  drawGameScene();
  if (gameOverUntil) {
    if (!timeLeft(gameOverUntil)) {
      gameOpen = false;
      return;
    }
    drawGameResult(ink);
    flushUI();
    return;
  }
  drawGameExit();  // antes del juego: la bola de keepy-uppy sube hasta y=28 y va por encima
  if (games.flow.phase == GP_TITLE) {
    drawGameTitle(ink);
  } else {
    switch (games.game()) {
      case GAME_CATCH: drawCatch(ink); break;
      case GAME_PEEK: drawPeek(ink); break;
      case GAME_SONG: drawSong(ink); break;
      default: drawKeepy(ink); break;
    }
  }
  flushUI();
}
