// The minigames' screens and the training exercises (gameui.h). Moved out of TamaPoke.ino
// (#41); the exercises replaced the strength-training sack (#78), endless since #126.
#include <Arduino.h>
#include "app.h"
#include "audio.h"
#include "blit.h"
#include "cardlayout.h"  // FIT_STA_COLOR
#include "dex.h"
#include "exercise.h"
#include "gameui.h"
#include "i18n.h"
#include "minigames.h"
#include "punchlunge.h"
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

// the exercises (#78, endless since #126; their rules are in exercise.h)
bool trainOpen = false;
bool trainPick = false;
static uint8_t trainKind = EX_PUNCH;
static RhythmPunch punch;
static PushUps pushups;
static PunchLunge punchLunge;      // the last landed punch's lunge (#90)
static uint32_t trainOverUntil = 0;  // the result is on screen until this millis(); 0 while training
static uint8_t trainCount = 0, trainGain = 0;
static bool trainNewHi = false;
// How the session ended, for the result and its serial line (#126): by itself or the exit,
// or by the miss (the punch's, or push-ups let go too early or too late).
enum : uint8_t { TRAIN_ENDED = 0, TRAIN_MISSED, TRAIN_EARLY, TRAIN_LATE };
static uint8_t trainMiss = TRAIN_ENDED;
static const char *const TRAIN_MISS_LOG[] = { "", " miss", " miss=early", " miss=late" };

// ---------- minijuegos de Play (logica en minigames.h) ----------

#if HOST_SIM
static uint8_t forcedGame = GAME_COUNT;  // the simulator's "game" step; GAME_COUNT: the bag deals
void gameForceNext(uint8_t g) { forcedGame = g; }

// The simulator's "gameplay" step (gameui.h): what a player would touch now for the open
// game's next point (or its miss), from the game's own state.
uint8_t gameSimAim(bool point, int16_t &x, int16_t &y) {
  if (!gameOpen || gameOverUntil || !games.playing()) return GAMEAIM_WAIT;
  switch (games.game()) {
    case GAME_KEEPY: {
      if (!point) return GAMEAIM_IDLE;  // left alone, the ball drops
      const KeepyGame &k = games.keepy;
      x = (int16_t)k.ballX;
      y = (int16_t)k.ballY;
      return GAMEAIM_TAP;
    }
    case GAME_CATCH: {
      // The berry that reaches the mouth line first (the lowest of the live ones, whatever its
      // speed). A good one: the finger goes to where it falls (or away from it, for a miss). A
      // rotten one: the finger holds still, where the pet is (the spawner keeps a rotten berry
      // out of reach of a pet standing where it caught the last good one, game_catch.h).
      const CatchGame &c = games.catcher;
      const CatchGame::Berry *next = nullptr;
      for (const CatchGame::Berry &b : c.berries)
        if (b.live && b.y < CatchGame::CATCH_Y && (!next || b.y > next->y)) next = &b;
      if (!next) return GAMEAIM_WAIT;
      y = 350;
      if (next->kind == CatchGame::ROTTEN) x = (int16_t)(c.petX + 0.5f);
      else x = point ? next->x : (int16_t)(next->x < 233 ? CatchGame::X_MAX : CatchGame::X_MIN);  // away from it
      return GAMEAIM_HOLD;
    }
    case GAME_PEEK: {
      const PeekGame &p = games.peek;
      if (point) {
        if (!p.up() || p.hit) return GAMEAIM_WAIT;
        x = PeekGame::BX[p.bush];
        y = (int16_t)(PeekGame::BY[p.bush] - 60);
        return GAMEAIM_TAP;
      }
      // a bush that isn't the one up (and whose centre is out of its hit area), or any bush while none is
      for (uint8_t b = 0; b < PeekGame::BUSHES; b++) {
        if (p.up() && (b == p.bush || PeekGame::inBush(p.bush, PeekGame::BX[b], PeekGame::BY[b]))) continue;
        x = PeekGame::BX[b];
        y = PeekGame::BY[b];
        return GAMEAIM_TAP;
      }
      return GAMEAIM_WAIT;
    }
    default: {
      const SongGame &s = games.song;
      if (!s.playersTurn()) return GAMEAIM_WAIT;
      uint8_t pad = point ? s.seq[s.k] : (uint8_t)((s.seq[s.k] + 1) % SongGame::PADS);
      x = SongGame::PX[pad];
      y = SongGame::PY[pad];
      return GAMEAIM_TAP;
    }
  }
}
uint16_t gameSimScore() { return gameOpen ? games.score() : 0; }
bool gameSimOpen() { return gameOpen && !gameOverUntil && !games.over(); }
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
  if (e & GEV_WIN) sfxPlay(SFX_EVOLVE);  // la melodia de 100 notas: la fanfarria (finishGame no suena otra)
  else if (e & GEV_HEART) sfxPlay(SFX_HEART);
  else if ((e & GEV_GOOD) && games.game() != GAME_SONG) sfxPlay(games.game() == GAME_CATCH ? SFX_EAT : SFX_PLAY);
}

// fin de la partida (el juego acabo o nadie lo toco en 15 s): record y recompensa
static void finishGame() {
  uint8_t g = games.game();
  gameShownScore = games.score();
  gameNewHi = gameShownScore > pet.gameBest(g);
  gameSpeedGain = pet.playGame(g, gameShownScore, games.played);  // a game nobody touched pays little
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

// ---------- the exercises (#78, #126): the rhythm punch and the push-ups ----------
// The stat card's TRAIN button opens a choice on the card (trainPick, renderTrainPick);
// a tap there starts one (startExercise). Touches reach them through exerciseTap() (a
// touch-down) and exerciseRelease() (a confirmed lift, timed when the finger left). A
// session runs until its miss (or nobody there for 15 s) and pays for what was done, as
// leaving early by the exit (exerciseExit) does; with nothing done the screen just closes
// and nothing is spent.

static bool gameNight();           // prototipos (definidas mas abajo)
static uint16_t gameInk();
static void drawGameScene(bool words);
static void drawGamePet(uint8_t act, int cx, int groundY, uint8_t maxS);
static void exerciseHitSound(uint8_t done, uint8_t hit);
static void planLunge();

#define TRAIN_GROUND 372            // the pet's feet on both exercise screens
#define TRAIN_PET_S 4               // ...and its largest scale (a minigame's is 3)
#define STAMINA_COL_NIGHT C565(0x9c, 0xcc, 0xf4)  // the stamina blue (FIT_STA_COLOR), lighter at night

void startExercise(uint8_t kind) {
  if (pet.isEgg() || pet.sleeping || pet.ceremony || (kind != EX_PUNCH && kind != EX_HOLD)) return;
#if BOARD_AMOLED_175C
  if (!trainingCaptureReady) { showControlToast("Touch capture unavailable"); return; }
  trainingCapture.reset();  // opened by a tap, which resolves on the lift: no finger is down
#endif
  uint32_t now = millis();
  trainKind = kind;
  if (kind == EX_PUNCH) planLunge();  // once per session (punchlunge.h)
  punch.begin(now);
  pushups.begin(now, (uint32_t)random(0x7FFFFFFF) ^ micros());
  trainOverUntil = 0;
  trainCount = trainGain = 0;
  trainNewHi = false;
  trainMiss = TRAIN_ENDED;
  trainOpen = true;
}

// Pays the pet for what the session did (Pet::trainStrength or trainStamina) and logs it,
// `why` at the end of the line. False when nothing was done: nothing paid, nothing spent.
static bool payExercise(const char *why) {
  bool p = trainKind == EX_PUNCH;
  trainCount = p ? punch.punches : pushups.reps;
  trainMiss = p ? (punch.missed ? TRAIN_MISSED : TRAIN_ENDED)
                : pushups.miss == PushUps::MISS_EARLY ? TRAIN_EARLY
                : pushups.miss == PushUps::MISS_LATE  ? TRAIN_LATE : TRAIN_ENDED;
  if (!trainCount) return false;
  trainNewHi = trainCount > (p ? pet.punchHi : pet.pushHi);
  trainGain = p ? pet.trainStrength(trainCount) : pet.trainStamina(trainCount);
#if BOARD_AMOLED_175C
  Serial.printf("Training: %s count=%u gain=%u%s%s input_drops=%lu%s\n", p ? "punch" : "pushup", trainCount,
                trainGain, TRAIN_MISS_LOG[trainMiss], trainNewHi ? " record" : "",
                (unsigned long)trainingCapture.dropped(), why);
#else
  Serial.printf("Training: %s count=%u gain=%u%s%s%s\n", p ? "punch" : "pushup", trainCount, trainGain,
                TRAIN_MISS_LOG[trainMiss], trainNewHi ? " record" : "", why);
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
  sfxPlay(trainNewHi ? SFX_MEDAL : trainMiss != TRAIN_ENDED ? SFX_DENY : SFX_PLAY);
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

// The punch session plans its lunge from the sprite on screen (punchlunge.h, #90): its
// ATTACK at IDLE's scale on the punch screen. It scans every frame, so it is done once, when
// the session starts (startExercise) and again by drawPunch if the sprite on screen changed
// (ensureMon() reloaded it). No PMD sprite, or no ATTACK: scale 0, a plan that isn't ok and
// doesn't scan, but still records the sprite it looked at (isFor) so it isn't redone every
// frame, and the pet stays on IDLE as before.
static void planLunge() {
  const PmdAct &a = pmd.acts[PMD_ATTACK];
  const PmdAct &idle = pmd.acts[PMD_IDLE];
  bool can = pmd.has(PMD_ATTACK) && pmd.has(PMD_IDLE);
  punchLunge.plan(a.data, a.w, a.h, a.frames, can ? pmdScaleFor(idle.h, idle.h, TRAIN_PET_S) : 0);
}

// ensureMon() reloaded the sprite (#90): forget the lunge's plan, so drawPunch plans it again
// from the new one. isFor() alone can't tell a reload that lands at the same address with
// the same ATTACK size (a PUT of an updated sprite, or SPEC, mid-session).
void exerciseSpriteChanged() { punchLunge = PunchLunge(); }

// A hit's sound: the punch's, or the push-ups' notes up the scale; the hit that raises the
// pay step chimes instead (#126). `done`: hits so far, this one included.
static void exerciseHitSound(uint8_t done, uint8_t hit) {
  sfxPlay(done % EX_TIER_HITS == 0 ? (uint8_t)SFX_LEVEL : hit);
}

void exerciseTap(uint32_t at) {
  if (!trainOpen || trainOverUntil) return;
  if (trainKind == EX_PUNCH) {
    if (punch.tap(at) == RhythmPunch::TAP_LANDED) exerciseHitSound(punch.punches, SFX_PLAY);
  } else {
    pushups.press(at);
  }
}

void exerciseRelease(uint32_t at) {
  if (!trainOpen || trainOverUntil || trainKind != EX_HOLD) return;
  uint8_t r = pushups.release(at);
  if (r == PushUps::PU_REP) exerciseHitSound(pushups.reps, (uint8_t)(SFX_NOTE0 + (pushups.reps - 1) % 4));
  else if (r == PushUps::PU_RETRY) sfxPlay(SFX_DENY);  // the warm-up: try again
}

#if HOST_SIM
int32_t exerciseMsToLift(uint32_t now, bool edge) {
  if (!trainOpen || trainOverUntil || trainKind != EX_HOLD || !pushups.down()) return -1;
  // The middle of the band, or SIM_EDGE_MS before its end: the touch poll sees the lift up to
  // 20 ms late, so it is still judged inside the band (even the narrowest, 100 ms), but a
  // judgement at the lift's confirmation, 40 ms after that, would be too late.
  const uint32_t SIM_EDGE_MS = 25;
  uint32_t at = pushups.pressAt + (edge ? pushups.bandHi - SIM_EDGE_MS : (pushups.bandLo + pushups.bandHi) / 2);
  return exAtOrAfter(now, at) ? 0 : (int32_t)(at - now);
}
#endif

bool exerciseExit() {
  if (!trainOpen) return true;
  if (trainOverUntil) return false;  // the result screen closes by itself
  finishExercise(millis());
  return !trainOpen;
}

#define TRAIN_HINT_Y 46  // the hint's top, the same on both screens
#define TRAIN_COUNT_Y 72  // ...and the count's (size 5: 40 px tall)
#if !BOARD_AMOLED_175C
// The 1.75's exit label "tap: back" (size 2, 16 px tall) sits at the very top of the round
// panel, inside the exit strip and clear of the hint.
#define TRAIN_BACK_Y 20
static_assert(TRAIN_BACK_Y + 16 <= TRAIN_EXIT_STRIP_H, "the exit label is inside the exit strip");
static_assert(TRAIN_EXIT_STRIP_H <= TRAIN_HINT_Y, "the exit strip ends above the hint");
#endif

// the hint at the top (in `hintInk`) and the count under it, the same on both screens
static void drawTrainHead(const char *hint, uint8_t count, uint16_t ink, uint16_t hintInk) {
  gfx->setTextColor(hintInk);
  setSize(2);
  setCur(centerX(hint, 2), TRAIN_HINT_Y);
  printT(hint);
  char b[4];  // the count is at most EX_MAX_COUNT: three digits
  snprintf(b, sizeof(b), "%u", count);
  gfx->setTextColor(ink);
  setSize(5);
  setCur(centerX(b, 5), TRAIN_COUNT_Y);
  printT(b);
}

// The pay step the next hit is paid at (#126), "x1", "x2"..., under the count on both screens.
#define TRAIN_TIER_Y 120
#define TRAIN_TIER_H 16  // size 2
static_assert(TRAIN_TIER_Y >= TRAIN_COUNT_Y + 40, "the pay step is under the count");
static void drawTier(uint8_t done) {
  char b[6];
  snprintf(b, sizeof(b), "x%u", exerciseTier(done));
  gfx->setTextColor(UI_BAR_WARN);
  setSize(2);
  setCur(centerX(b, 2), TRAIN_TIER_Y);
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
#define PUNCH_LEAN_MS 250   // ...and the pet leans into it
#define PUNCH_LEAN_PX 16    // ...this far, at most
// The lunge's room (punchlunge.h) on this screen: the tallest frame stays under the pay step
// (which is under the count), with a small gap, and the room's four corners, the lean
// included, are on the round panel (so all of it is).
static_assert(TRAIN_GROUND - PunchLunge::ROOM_UP >= TRAIN_TIER_Y + TRAIN_TIER_H + 4, "the lunge stays under the pay step");
static constexpr bool onRoundPanel(int x, int y) { return (x - CX) * (x - CX) + (y - CY) * (y - CY) <= CX * CX; }
static_assert(onRoundPanel(PUNCH_PET_X - PunchLunge::ROOM_SIDE, TRAIN_GROUND - PunchLunge::ROOM_UP) &&
                  onRoundPanel(PUNCH_PET_X - PunchLunge::ROOM_SIDE, TRAIN_GROUND) &&
                  onRoundPanel(PUNCH_PET_X + PUNCH_LEAN_PX + PunchLunge::ROOM_SIDE, TRAIN_GROUND - PunchLunge::ROOM_UP) &&
                  onRoundPanel(PUNCH_PET_X + PUNCH_LEAN_PX + PunchLunge::ROOM_SIDE, TRAIN_GROUND),
              "the lunge stays on the round panel");
// every shipped sprite's lunge is checked up to scale 5 (test_trainingtouch.cpp)
static_assert(TRAIN_PET_S <= 5, "the lunge's sprite test covers this scale");

static void drawPunch(uint32_t now, uint16_t ink) {
  drawTrainHead(T(S_PUNCH_HINT), punch.punches, ink, ink);
  drawTier(punch.punches);

  uint32_t since = now - punch.lastPunchAt;
  // The knock, the lean and the burst are over before the next window opens: the swings get
  // quicker as the run goes on (#126), and the bag is in its true place for the "now!".
  uint32_t knockMs = PUNCH_KNOCK_MS;
  if (punch.punches && punch.nextOpen() - punch.lastPunchAt < knockMs) knockMs = punch.nextOpen() - punch.lastPunchAt;
  if (knockMs < 1) knockMs = 1;
  uint32_t leanMs = PUNCH_LEAN_MS < knockMs ? PUNCH_LEAN_MS : knockMs;
  bool knocked = punch.punches && since < knockMs;
  float ang = BAG_OUT + (BAG_IN - BAG_OUT) * punch.reach(now);
  if (knocked) ang += 0.3f * (1.0f - (float)since / (float)knockMs);
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

  int lean = punch.punches && since < leanMs
                 ? (int)(PUNCH_LEAN_PX * sinf((float)since * 3.14159265f / (float)leanMs)) : 0;
  // A landed punch: the pet's own ATTACK, from its strike, on its feet at IDLE's size
  // (punchlunge.h, #90). The plan was made when the session started; a sprite that changed
  // since (ensureMon() reloaded it) is planned again here, once (the plan records it either way).
  const PmdAct &atk = pmd.acts[PMD_ATTACK];
  if (!punchLunge.isFor(atk.data, atk.w, atk.h, atk.frames)) planLunge();
  int fi = pmd.loaded ? punchLunge.frameAt(punch, now, atk.ms) : -1;
  if (fi >= 0) drawPmdFrame(pmd, PMD_ATTACK, (uint8_t)fi, punchLunge.feet[fi], PUNCH_PET_X + lean, TRAIN_GROUND, punchLunge.s);
  else drawGamePet(PMD_IDLE, PUNCH_PET_X + lean, TRAIN_GROUND, TRAIN_PET_S);
  if (knocked && since < 300) {  // the hit: a burst on the bag's near side (knockMs at most)
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

// The push-ups (#126): the bar with its green band under the count, and the pet in a ring,
// going down as the bar fills and pushing up at a rep.
#define HOLD_RING_Y 276
#define HOLD_RING_R 100
#define HOLD_LIFT_PX 26     // a rep: the pet pushes up this far...
#define PUSH_UP_MS 300      // ...over this long
#define PUSH_DOWN_PX 16     // the pet goes down this far at a full bar
#define PUSH_BAR_Y 148
#define PUSH_BAR_W 300
#define PUSH_BAR_H 22
#define PUSH_RETRY_MS 1200  // a warm-up try let go at the wrong moment: the hint says which way
static_assert(PUSH_BAR_Y >= TRAIN_TIER_Y + TRAIN_TIER_H + 8, "the bar is under the pay step");
static_assert(PUSH_BAR_Y + PUSH_BAR_H + 4 <= HOLD_RING_Y - HOLD_RING_R, "the bar is above the ring");

static void drawPushUps(uint32_t now, uint16_t ink) {
  const char *hint = T(pushups.down() ? S_HOLD_KEEP : S_HOLD_HINT);
  uint16_t hintInk = ink;
  if (pushups.retry != PushUps::MISS_NONE && now - pushups.retryAt < PUSH_RETRY_MS) {
    hint = T(pushups.retry == PushUps::MISS_EARLY ? S_TOO_EARLY : S_TOO_LATE);
    hintInk = UI_BAR_BAD;
  }
  drawTrainHead(hint, pushups.reps, ink, hintInk);
  drawTier(pushups.reps);

  // the bar: the green band where letting go is a rep, and the fill with its marker
  const int bx = CX - PUSH_BAR_W / 2;
  bool in = pushups.inBand(now);
  float f = pushups.fill(now);
  gfx->fillRoundRect(bx, PUSH_BAR_Y, PUSH_BAR_W, PUSH_BAR_H, 8, UI_TRACK);
  int lo = (int)((uint32_t)PUSH_BAR_W * pushups.bandLo / pushups.fillMs);
  int hi = (int)((uint32_t)PUSH_BAR_W * pushups.bandHi / pushups.fillMs);
  gfx->fillRect(bx + lo, PUSH_BAR_Y, hi - lo, PUSH_BAR_H, UI_BAR_OK);
  int fx = (int)(PUSH_BAR_W * f);
  if (fx > 0) gfx->fillRect(bx, PUSH_BAR_Y + 7, fx, PUSH_BAR_H - 14, UI_BAR_WARN);
  if (pushups.down()) gfx->fillRect(bx + fx - 2, PUSH_BAR_Y - 5, 5, PUSH_BAR_H + 10, in ? UI_WHITE : ink);
  gfx->drawRoundRect(bx, PUSH_BAR_Y, PUSH_BAR_W, PUSH_BAR_H, 8, ink);

  uint32_t sinceRep = now - pushups.lastRepAt;
  bool up = pushups.reps && sinceRep < PUSH_UP_MS;
  uint16_t ring = up ? UI_WHITE : in ? UI_BAR_OK : pushups.down() ? UI_BAR_WARN : UI_TRACK;
  for (int r = 0; r < 3; r++) gfx->drawCircle(CX, HOLD_RING_Y, HOLD_RING_R - r, ring);
  int dy = up ? -(int)(HOLD_LIFT_PX * sinf((float)sinceRep * 3.14159265f / PUSH_UP_MS)) : (int)(PUSH_DOWN_PX * f);
  drawGamePet(PMD_IDLE, CX, TRAIN_GROUND + dy, TRAIN_PET_S);
}

// the result (3.5 s): how it ended (the miss), what was done, what it trained, and the record
static void drawExerciseResult(uint16_t ink) {
  bool p = trainKind == EX_PUNCH;
  if (trainMiss != TRAIN_ENDED) {
    const char *m = T(trainMiss == TRAIN_EARLY ? S_TOO_EARLY : trainMiss == TRAIN_LATE ? S_TOO_LATE : S_MISSED);
    gfx->setTextColor(UI_BAR_BAD);
    setSize(3);
    setCur(centerX(m, 3), 100);
    printT(m);
  }
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
  gfx->setTextColor(p ? UI_BAR_BAD : gameNight() ? STAMINA_COL_NIGHT : FIT_STA_COLOR);
  setSize(3);
  setCur(centerX(g, 3), 210);
  printT(g);
  setSize(2);
  if (trainNewHi) {
    gfx->setTextColor(UI_BAR_WARN);
    setCur(centerX(T(S_NEW_RECORD), 2), 256);
    printT(T(S_NEW_RECORD));
  } else {
    char r[18];
    snprintf(r, sizeof(r), T(S_RECORD_FMT), p ? pet.punchHi : pet.pushHi);
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

  if (trainKind == EX_HOLD) {
    if (pushups.update(now) == PushUps::PU_RETRY) sfxPlay(SFX_DENY);  // the warm-up, held too long
  } else {
    punch.update(now);
  }
  // A session ends by itself (its miss, or 15 s with nobody there), and a still finger or a
  // player timing the bag isn't touching: keep it bright until it ends.
  wakeUI();
  if (trainKind == EX_PUNCH ? punch.over() : pushups.over()) {
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
  else drawPushUps(now, ink);
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
    gfx->fillRoundRect(PICK_X, y, PICK_W, PICK_H, 14, k == EX_HOLD ? FIT_STA_COLOR : UI_BAR_BAD);
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
  bool night = skyNightHour(hh);
  uint16_t top, bot;
  skyColors(hh, night, top, bot);  // the main scene's sky (scene.cpp)
  int hor = 376;
  for (int y = 0; y < hor; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, hor));
  if (night && sceneStarsOut())  // not under cloud, rain, snow or fog (#60)
    for (auto &st : STARS)  // out of the HUD and the words (#84)
      if (gameStarShown(st[1], words)) gfx->fillRect(st[0], st[1], SKY_STAR_SIZE, SKY_STAR_SIZE, UI_WHITE);
  uint16_t soil = soilColor(scenePlace(), night);  // where the pet is today (#60), not always its home
  gfx->fillRect(0, hor, 466, 466 - hor, soil);
}

// ---------- dibujo de los minijuegos ----------

static bool gameNight() { return skyNightHour(sceneHour()); }
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

// The HUD under the exit chip: the score, the record and the pay step the next point is paid at
// (#127, "x1", "x2"..., as the exercises' drawTier), where a bar or dots used to be. The games'
// own scenes start under it (the keepy ball is drawn over it).
#define GAME_SCORE_Y 68
#define GAME_REC_Y 106
#define GAME_STEP_Y 128
#define GAME_STEP_H 16  // size 2
static_assert(GAME_REC_Y >= GAME_SCORE_Y + 32, "the record is under the score (size 4)");
static_assert(GAME_STEP_Y >= GAME_REC_Y + 16, "the pay step is under the record (size 2)");
static_assert(GAME_STEP_Y + GAME_STEP_H <= GAME_HUD_BOTTOM, "the HUD ends where the stars may start (skylayout.h)");

static void drawGameRec(uint16_t ink) {
  // Las tallas de los buffers de texto las vigila TestBuffersDeTexto en
  // test/test_tools.py.
  char rec[24];
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.gameBest(games.game()));
  gfx->setTextColor(ink);
  setSize(2);
  setCur(centerX(rec, 2), GAME_REC_Y);
  printT(rec);
}

static void drawGameStep(uint16_t score) {
  char b[6];
  snprintf(b, sizeof(b), "x%u", gameStep(games.game(), score));
  gfx->setTextColor(UI_BAR_WARN);
  setSize(2);
  setCur(centerX(b, 2), GAME_STEP_Y);
  printT(b);
}

// marcador, record y escalon de este juego, debajo del boton de salir
static void drawGameHud(uint16_t ink, uint16_t score) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", score);
  gfx->setTextColor(ink);
  setSize(4);
  setCur(centerX(buf, 4), GAME_SCORE_Y);
  printT(buf);
  drawGameRec(ink);
  drawGameStep(score);
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
  drawGameRec(ink);
  drawGameStep(s.result());
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
