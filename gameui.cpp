// The minigames' screens and the calm exercises (gameui.h). Moved out of TamaPoke.ino
// (#41); the exercises replaced the strength-training sack (#78).
#include <Arduino.h>
#include "app.h"
#include "audio.h"
#include "blit.h"
#include "dex.h"
#include "exercise.h"
#include "gameui.h"
#include "i18n.h"
#include "minigames.h"
#include "scene.h"
#include "sdmon.h"
#include "species.h"
#include "trainingtouch.h"  // REPORT_GAP_MS (both boards), the 1.75C exit chip
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
static uint8_t gameSpeedGain = 0;    // the speed the game trained, for the result (#76)

// the calm exercises (#78; their rules are in exercise.h)
bool trainOpen = false;
bool trainPick = false;
static uint8_t trainKind = EX_PUNCH;
static RhythmPunch punch;
static StaminaHold hold;
static uint32_t trainOverUntil = 0;  // the result is on screen until this millis(); 0 while training
static uint8_t trainCount = 0, trainGain = 0;
static bool trainNewHi = false;
static uint32_t repAt = 0;           // when the last rep was done (the ring flashes)
#if !BOARD_AMOLED_175C
static bool trainHeld = false;       // the 1.75: a finger that didn't start on the top strip is on
static uint32_t trainHeldAt = 0;     // ...as of the last successful touch poll (exerciseHeld)
#endif

// ---------- minijuegos de Play (logica en minigames.h) ----------

#if HOST_SIM
static uint8_t forcedGame = GAME_COUNT;  // the simulator's "game" step; GAME_COUNT: the bag deals
void gameForceNext(uint8_t g) { forcedGame = g; }
#endif

void startGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  uint8_t fav = 0;
  while (fav < 2 && !pet.lovesBerry(fav)) fav++;
  games.start(millis(), (uint32_t)random(0x7FFFFFFF) ^ micros(), fav, pet.berryKnown);
#if HOST_SIM
  if (forcedGame < GAME_COUNT) games.begin(forcedGame, millis(), fav, pet.berryKnown);  // in place of the bag's pick
  forcedGame = GAME_COUNT;
#endif
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
  gameSpeedGain = pet.playGame(g, gameShownScore);
  Serial.printf("Play: %s score=%u speed=+%u\n", GAME_KEYS[g], gameShownScore, gameSpeedGain);
  if (!games.won()) sfxPlay(gameNewHi && gameShownScore > 0 ? SFX_MEDAL : SFX_LEVEL);
  gameOverUntil = millis() + 4000;
}

// The screen went dark with a minigame open (#94; MiniGames::endDark), or an ending is
// starting over it (#33, ending.h). The game ends here and the pet gets what its score so far
// earns, as the game's own end pays (Pet::playGame: joy, speed, the energy and food it costs,
// the weight it burns, bond and the day's care), but with no result screen to draw and no
// sound, and the game's screen closes: no 240 MHz with the screen off, and the screen comes
// back on to the creature (or to the ending). A game nobody touched pays nothing; the result
// screen only closes (paid at its end). A finger still on the glass does nothing as it lifts,
// as after the exit chip. `why` ends the serial line: " (screen off)" or " (ending)".
static void gameEndNow(const char *why) {
  if (!gameOpen) return;
  if (!games.over()) {  // over: on its result screen, paid by finishGame()
    uint8_t g = games.game();
    if (games.endDark()) {
      uint16_t score = games.score();
      uint8_t gain = pet.playGame(g, score);
      Serial.printf("Play: %s score=%u speed=+%u%s\n", GAME_KEYS[g], score, gain, why);
    } else {
      Serial.printf("Play: %s untouched%s\n", GAME_KEYS[g], why);
    }
  }
  gameOpen = false;
  gameOverUntil = 0;
  holdFired = true;
  swallowGesture = true;
}

// ---------- the calm exercises (#78): the rhythm punch and the stamina hold ----------
// The stat card's TRAIN button opens a choice on the card (trainPick, renderTrainPick);
// a tap there starts one (startExercise). Touches reach them through exerciseTap() and,
// for the hold, the board's holding state (exerciseHolding). Leaving early by the exit
// (exerciseExit) ends the session and pays for what was done; with nothing done the
// screen just closes and nothing is spent.

static bool gameNight();           // prototipos (definidas mas abajo)
static uint16_t gameInk();
static void drawGameScene(bool words);
static void drawGameBar(uint32_t left, uint32_t total);
static void drawGamePet(uint8_t act, int cx, int groundY, uint8_t maxS);

#define TRAIN_GROUND 372            // the pet's feet on both exercise screens
#define TRAIN_PET_S 4               // ...and its largest scale (a minigame's is 3)
#define STAMINA_COL 0x4C98          // the stamina colour: the stat card's DEF bar
#define STAMINA_COL_NIGHT C565(0x9c, 0xcc, 0xf4)  // ...lighter, on the night sky

void startExercise(uint8_t kind) {
  if (pet.isEgg() || pet.sleeping || pet.ceremony || (kind != EX_PUNCH && kind != EX_HOLD)) return;
#if BOARD_AMOLED_175C
  if (!trainingCaptureReady) { showControlToast("Touch capture unavailable"); return; }
  trainingCapture.reset();  // opened by a tap, which resolves on the lift: no finger is down
#else
  trainHeld = false;
#endif
  uint32_t now = millis();
  trainKind = kind;
  punch.begin(now);
  hold.begin(now);
  trainOverUntil = 0;
  trainCount = trainGain = 0;
  trainNewHi = false;
  repAt = 0;
  trainOpen = true;
}

// Pays the pet for what the session did (Pet::trainStrength or trainStamina) and logs it,
// `why` at the end of the line. False when nothing was done: nothing paid, nothing spent.
static bool payExercise(const char *why) {
  bool p = trainKind == EX_PUNCH;
  trainCount = p ? punch.punches : hold.reps;
  if (!trainCount) return false;
  trainNewHi = p && trainCount > pet.punchHi;
  trainGain = p ? pet.trainStrength(trainCount) : pet.trainStamina(trainCount);
#if BOARD_AMOLED_175C
  Serial.printf("Training: %s count=%u gain=%u input_drops=%lu%s\n", p ? "punch" : "hold", trainCount,
                trainGain, (unsigned long)trainingCapture.dropped(), why);
#else
  Serial.printf("Training: %s count=%u gain=%u%s\n", p ? "punch" : "hold", trainCount, trainGain, why);
#endif
  return true;
}

// The session is over (its end, or the exit): pay for what was done and show the result
// for 3.5 s. With nothing done, back to the pet at once: nothing paid, nothing spent.
static void finishExercise(uint32_t now) {
  if (trainOverUntil) return;
  if (!payExercise("")) {
    leaveTraining();
    return;
  }
  sfxPlay(trainNewHi ? SFX_MEDAL : SFX_PLAY);
  trainOverUntil = now + 3500;
  if (!trainOverUntil) trainOverUntil = 1;  // 0 means "still training"
}

// The screen went dark during an exercise (#88), or an ending is starting over it (#33).
// The session ends here and pays for what was done, as the exit does, but with no result
// screen to draw and no sound to go with it, and the training screen closes: no 240 MHz and
// no touch capture with the screen off, and the screen comes back on to the creature (or to
// the ending). The result screen only closes (paid already). `why` ends the serial line.
static void exerciseEndNow(const char *why) {
  if (!trainOpen) return;
  if (!trainOverUntil) payExercise(why);
  leaveTraining();
}

void playEndNow(const char *why) {
  gameEndNow(why);
  exerciseEndNow(why);
}

void playScreenOff() { playEndNow(" (screen off)"); }

void exerciseTap(uint32_t at) {
  if (!trainOpen || trainOverUntil || trainKind != EX_PUNCH) return;
  if (punch.tap(at)) sfxPlay(SFX_PLAY);
}

bool exerciseExit() {
  if (!trainOpen) return true;
  if (trainOverUntil) return false;  // the result screen closes by itself
  finishExercise(millis());
  return !trainOpen;
}

#if !BOARD_AMOLED_175C
void exerciseHeld(bool on) {
  trainHeld = on;
  trainHeldAt = millis();
}
#endif

// A finger is on for the stamina hold (the 1.75C asks the capture, which the loop owns
// here: renderExercise() reads it before its capture frame starts the worker). The 1.75
// holds only while its last poll is fresh: handleTouch() returns early when the chip
// doesn't answer, so without this a stalled stream would keep counting reps, where the
// 1.75C pauses on the same gap.
static bool exerciseHolding(uint32_t now) {
#if BOARD_AMOLED_175C
  return !screenIsLocked() && trainingCapture.holding(now);
#else
  return trainHeld && now - trainHeldAt <= TrainingTouch::REPORT_GAP_MS;
#endif
}

#define TRAIN_HINT_Y 46  // the hint's top, the same on both screens
#if !BOARD_AMOLED_175C
// The 1.75's exit label "tap: back" (size 2, 16 px tall) sits at the very top of the round
// panel, inside the exit strip and clear of the hint.
#define TRAIN_BACK_Y 20
static_assert(TRAIN_BACK_Y + 16 <= TRAIN_EXIT_STRIP_H, "the exit label is inside the exit strip");
static_assert(TRAIN_EXIT_STRIP_H <= TRAIN_HINT_Y, "the exit strip ends above the hint");
#endif

// the hint at the top and the count under it, the same on both screens
static void drawTrainHead(const char *hint, uint8_t count, uint16_t ink) {
  gfx->setTextColor(ink);
  setSize(2);
  setCur(centerX(hint, 2), TRAIN_HINT_Y);
  printT(hint);
  char b[4];
  snprintf(b, sizeof(b), "%u", count);
  setSize(5);
  setCur(centerX(b, 5), 72);
  printT(b);
}

// The punch: the pet on the left, the bag hanging from the right and swinging in to it.
#define BAG_PIVOT_X 372
#define BAG_PIVOT_Y 76
#define BAG_ROPE 190
#define BAG_W 64
#define BAG_H 88
#define BAG_OUT 0.04f    // the rope's angle all the way out (rad, + = right)
#define BAG_IN (-0.62f)  // ...and at the pet: its near side reaches the pet's
#define PUNCH_PET_X 186
#define PUNCH_KNOCK_MS 400  // a punch knocks the bag back out for this long
#define PUNCH_LUNGE_MS 250  // ...and the pet leans into it

static void drawPunch(uint32_t now, uint16_t ink) {
  drawTrainHead(T(S_PUNCH_HINT), punch.punches, ink);
  uint32_t e = punch.elapsed(now);
  drawGameBar(e < RhythmPunch::TOTAL_MS ? RhythmPunch::TOTAL_MS - e : 0, RhythmPunch::TOTAL_MS);

  uint32_t since = now - punch.lastPunchAt;
  bool knocked = punch.punches && since < PUNCH_KNOCK_MS;
  float ang = BAG_OUT + (BAG_IN - BAG_OUT) * punch.reach(now);
  if (knocked) ang += 0.3f * (1.0f - (float)since / PUNCH_KNOCK_MS);
  int bx = BAG_PIVOT_X + (int)(BAG_ROPE * sinf(ang));
  int by = BAG_PIVOT_Y + (int)(BAG_ROPE * cosf(ang));
  gfx->fillRect(BAG_PIVOT_X - 20, BAG_PIVOT_Y - 8, 40, 8, ink);  // the hook
  for (int d = -1; d <= 1; d++) gfx->drawLine(BAG_PIVOT_X + d, BAG_PIVOT_Y, bx + d, by, ink);  // rope
  int x0 = bx - BAG_W / 2;
  gfx->fillRoundRect(x0, by, BAG_W, BAG_H, 20, C565(0xb5, 0x3a, 0x3a));      // the bag
  gfx->fillRoundRect(x0, by, BAG_W, 18, 14, C565(0x7e, 0x28, 0x28));         // its lid
  gfx->fillRect(x0, by + BAG_H / 2, BAG_W, 4, C565(0x7e, 0x28, 0x28));       // seam
  gfx->drawRoundRect(x0, by, BAG_W, BAG_H, 20, ink);
  if (punch.open(now)) {  // "now!": a punch would land
    gfx->drawRoundRect(x0 - 5, by - 5, BAG_W + 10, BAG_H + 10, 24, UI_BAR_WARN);
    gfx->drawRoundRect(x0 - 6, by - 6, BAG_W + 12, BAG_H + 12, 25, UI_BAR_WARN);
    gfx->drawRoundRect(x0 - 7, by - 7, BAG_W + 14, BAG_H + 14, 26, UI_BAR_WARN);
  }

  int lunge = punch.punches && since < PUNCH_LUNGE_MS
                  ? (int)(16.0f * sinf((float)since * 3.14159265f / PUNCH_LUNGE_MS)) : 0;
  drawGamePet(PMD_IDLE, PUNCH_PET_X + lunge, TRAIN_GROUND, TRAIN_PET_S);
  if (knocked && since < 300) {  // the hit: a burst on the bag's near side
    int cx = x0 + 6, cy = by + BAG_H / 2;
    for (int i = 0; i < 8; i++) {
      float a = (float)i * 0.785398f;
      int r0 = 12, r1 = 26 + (int)(since / 30);
      for (int d = -1; d <= 1; d++)
        gfx->drawLine(cx + (int)(r0 * cosf(a)) + d, cy + (int)(r0 * sinf(a)), cx + (int)(r1 * cosf(a)) + d,
                      cy + (int)(r1 * sinf(a)), UI_BAR_WARN);
    }
  }
}

// The hold: the pet in a ring, lifting itself once a rep while a finger is on.
#define HOLD_RING_Y 276
#define HOLD_RING_R 100
#define HOLD_LIFT_PX 26

static void drawHold(uint32_t now, uint16_t ink, bool holding) {
  drawTrainHead(T(holding ? S_HOLD_KEEP : S_HOLD_HINT), hold.reps, ink);
  drawGameBar(hold.into, StaminaHold::REP_MS);  // the rep under way: stops while lifted
  uint16_t ring = repAt && now - repAt < 250 ? UI_WHITE : holding ? UI_BAR_OK : UI_TRACK;
  for (int r = 0; r < 3; r++) gfx->drawCircle(CX, HOLD_RING_Y, HOLD_RING_R - r, ring);
  int dy = -(int)(HOLD_LIFT_PX * sinf(hold.progress() * 3.14159265f));  // still while paused
  drawGamePet(PMD_IDLE, CX, TRAIN_GROUND + dy, TRAIN_PET_S);
}

// the result (3.5 s): what was done, what it trained, and the punch record
static void drawExerciseResult(uint16_t ink) {
  bool p = trainKind == EX_PUNCH;
  char b[24];  // buffer sizes: TestBuffersDeTexto in test/test_tools.py
  if (trainCount == 1) snprintf(b, sizeof(b), "%s", T(p ? S_ONE_PUNCH : S_ONE_REP));
  else snprintf(b, sizeof(b), T(p ? S_PUNCHES_FMT : S_REPS_FMT), trainCount);
  gfx->setTextColor(ink);
  uint8_t sz = textW(b, 4) <= 400 ? 4 : 3;
  setSize(sz);
  setCur(centerX(b, sz), 150);
  printT(b);
  char g[24];
  snprintf(g, sizeof(g), T(p ? S_STR_GAIN_FMT : S_STA_GAIN_FMT), trainGain);
  gfx->setTextColor(p ? UI_BAR_BAD : gameNight() ? STAMINA_COL_NIGHT : STAMINA_COL);
  setSize(3);
  setCur(centerX(g, 3), 210);
  printT(g);
  if (!p) return;
  setSize(2);
  if (trainNewHi) {
    gfx->setTextColor(UI_BAR_WARN);
    setCur(centerX(T(S_NEW_RECORD), 2), 256);
    printT(T(S_NEW_RECORD));
  } else {
    char r[18];
    snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.punchHi);
    gfx->setTextColor(ink);
    setCur(centerX(r, 2), 256);
    printT(r);
  }
}

void renderExercise() {
  uint32_t now = millis();
  drawGameScene(trainOverUntil != 0);  // fondo del habitat; the result's has no stars (#84)
  uint16_t ink = gameInk();

  if (trainOverUntil) {  // pantalla de resultado
    if (!timeLeft(trainOverUntil)) {
      leaveTraining();
      return;
    }
    drawExerciseResult(ink);
    flushUI();
    return;
  }

  bool holding = false;
  if (trainKind == EX_HOLD) {
    holding = exerciseHolding(now);
    if (hold.step(now, holding)) {
      repAt = now;
      sfxPlay(SFX_NOTE0 + (hold.reps - 1) % 4);  // each rep a note up the scale
    }
    if (holding) wakeUI();  // a still finger is someone there: no dimming mid-hold
  } else {
    // A punch session outlasts the game's keep-awake (18 s against POWER_GAME_AWAKE_MS) and
    // a player who only watches never touches: keep it bright until it ends.
    wakeUI();
  }
  if (trainKind == EX_PUNCH ? punch.over(now) : hold.over(now)) {
    finishExercise(now);
    if (!trainOpen) return;
    // The result's first frame: the sky again, without the stars (#84). The pass that ends the
    // session has painted the game's background above; this repaints it, one extra fill once a session.
    drawGameScene(true);
    drawExerciseResult(ink);
    flushUI();
    return;
  }

#if BOARD_AMOLED_175C
  // Everything below is I2C-free. Snapshot policy state before starting the
  // worker; RAII joins it on return, including the blocking QSPI flush.
  uint32_t exitHeld = trainingCapture.exitHeldMs(now);
  TrainingCapture::Frame captureFrame(trainingCapture, !screenIsLocked());
#endif
  if (trainKind == EX_PUNCH) drawPunch(now, ink);
  else drawHold(now, ink, holding);
#if BOARD_AMOLED_175C
  // Visible, clear of the pet and the bag, fully inside the circle.
  const int ex = TrainingTouch::EXIT_X, ey = TrainingTouch::EXIT_Y;
  const int ew = TrainingTouch::EXIT_W, eh = TrainingTouch::EXIT_H;
  gfx->fillRoundRect(ex, ey, ew, eh, 8, UI_TRACK);
  int progress = (int)(ew * (exitHeld > 1000 ? 1000 : exitHeld) / 1000);
  if (progress > 2) gfx->fillRoundRect(ex, ey, progress, eh, 8, UI_BAR_WARN);
  gfx->drawRoundRect(ex, ey, ew, eh, 8, UI_INK);
  gfx->setTextColor(UI_INK);  // the chip is light by day and by night: dark ink on it
  setSize(2);
  setCur(centerX(T(S_HOLD_EXIT), 2), ey + 16);
  printT(T(S_HOLD_EXIT));
#else
  gfx->setTextColor(ink);  // the 1.75's exit: a tap on the strip this label is in
  setSize(2);
  setCur(centerX(T(S_BACK), 2), TRAIN_BACK_Y);
  printT(T(S_BACK));
#endif
  flushUI();
}

// The exercise choice (#78), drawn on the stat card in place of the stats while
// trainPick is set: two big buttons. TamaPoke.ino's onTap asks trainPickAt().
#define PICK_X 73
#define PICK_W 320
#define PICK_H 84
static const int PICK_Y[2] = { 98, 198 };

void renderTrainPick() {
  gfx->setTextColor(UI_INK);
  setSize(3);
  setCur(centerX(T(S_EX_TITLE), 3), 48);
  printT(T(S_EX_TITLE));
  for (uint8_t k = 0; k < 2; k++) {
    int y = PICK_Y[k];
    gfx->fillRoundRect(PICK_X, y, PICK_W, PICK_H, 14, k == EX_HOLD ? STAMINA_COL : UI_BAR_BAD);
    gfx->setTextColor(UI_BG_DAY);
    const char *name = T(k == EX_HOLD ? S_EX_HOLD : S_EX_PUNCH);
    const char *what = T(k == EX_HOLD ? S_EX_STA : S_EX_STR);
    setSize(3);
    setCur(centerX(name, 3), y + 16);
    printT(name);
    setSize(2);
    setCur(centerX(what, 2), y + 54);
    printT(what);
  }
}

uint8_t trainPickAt(int16_t x, int16_t y) {
  if (x < PICK_X - 8 || x > PICK_X + PICK_W + 8) return EX_NONE;
  for (uint8_t k = 0; k < 2; k++)
    if (y >= PICK_Y[k] - 6 && y <= PICK_Y[k] + PICK_H + 6) return k;
  return EX_NONE;
}

// fondo del minijuego: habitat del bicho (cielo por hora + suelo del bioma). `words`: a title
// card or a result is over it, so its night sky has no stars (gameStarShown, skylayout.h)
static void drawGameScene(bool words) {
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
  if (night && sceneStarsOut())  // not under cloud, rain, snow or fog (#60)
    for (auto &st : STARS)  // out of the HUD and the words (#84)
      if (gameStarShown(st[1], words)) gfx->fillRect(st[0], st[1], SKY_STAR_SIZE, SKY_STAR_SIZE, UI_WHITE);
  uint8_t bio = scenePlace();  // where the pet is today (#60), not always its home
  uint16_t soil = BIOME_SOIL[bio < 6 ? bio : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  gfx->fillRect(0, hor, 466, 466 - hor, soil);
}

// ---------- dibujo de los minijuegos ----------

static bool gameNight() {
  int h = sceneHour();
  return h < 6 || h >= 20;
}
static uint16_t gameInk() { return gameNight() ? UI_INK_NIGHT : UI_INK; }


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
  // Las tallas de los buffers de texto las vigila TestBuffersDeTexto en
  // test/test_tools.py.
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

// "Mantener: salir", arriba en todos los juegos (como el de los ejercicios en la 1.75C). El
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
  uint8_t sz = textW(msg, 4) <= 300 ? 4 : 3;  // grande si cabe, si no a 3
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

// resultado (4 s): puntuacion, lo que entreno (#76: "SPEED +3", as the punch shows its
// strength), record y premio
static void drawGameResult(uint16_t ink) {
  uint8_t g = games.game();
  const char *name = T((StrId)(S_GAME_KEEPY + g));
  gfx->setTextColor(ink);
  setSize(2);
  setCur(centerX(name, 2), 112);
  printT(name);
  char buf[22];
  snprintf(buf, sizeof(buf), T(S_SCORE_FMT), gameShownScore);
  setSize(4);
  setCur(centerX(buf, 4), 148);
  printT(buf);
  char sp[18];
  snprintf(sp, sizeof(sp), T(S_SPD_GAIN_FMT), gameSpeedGain);
  gfx->setTextColor(ink);
  setSize(3);
  setCur(centerX(sp, 3), 198);
  printT(sp);
  setSize(2);
  if (gameNewHi && gameShownScore > 0) {
    gfx->setTextColor(UI_BAR_WARN);
    setCur(centerX(T(S_NEW_RECORD), 2), 242);
    printT(T(S_NEW_RECORD));
  } else {
    char rec[20];
    snprintf(rec, sizeof(rec), T(S_RECORD_FMT), pet.gameBest(g));
    gfx->setTextColor(ink);
    setCur(centerX(rec, 2), 242);
    printT(rec);
  }
  const char *msg = gameNormScore(g, gameShownScore) >= 10 ? T(S_GREAT_JOY) : T(S_PLUS_JOY);
  gfx->setTextColor(ink);
  setCur(centerX(msg, 2), 274);
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
  drawGameScene(gameOverUntil || games.flow.phase == GP_TITLE);  // words in the sky: no stars (#84)
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
