#pragma once
#include <stdint.h>

// Minigames behind the Play button: the parts every game shares. Pure logic (no
// drawing, no millis(), no Arduino random()) so it runs on the PC
// (test/test_minigames.cpp). The sketch feeds touches and time and draws from
// the state. Coordinates are the native 466x466 UI coordinates.
//
// Endless and skill-based (#127, owner's decisions 2026-11-28, as the exercises' #126). A game
// has no fixed length and no lives: once the player has made a first point, one miss ends it,
// and it still pays for everything done. Until that first point the game is a warm-up where a
// miss costs nothing and doesn't end it (a ball that drops is dealt again, a berry that lands
// is free, a pop-up that ducks or a wrong bush only rustles, a wrong note plays the tune
// again), so a start that takes a try or two never ends at 0. Every difficulty ramp rises with
// the points made (never with time), slower and slower, towards a floor past what anyone keeps
// up for long (test/test_minigames.cpp proves each game stays fair at every level). Each game
// also ends by itself at a storage cap no run gets near (GAME_REC_MAX), so the score and the
// records stay small. The pay per point rises with the run in steps, as the exercises' does.

enum GameId : uint8_t { GAME_KEEPY = 0, GAME_CATCH, GAME_PEEK, GAME_SONG, GAME_COUNT };
// each game's name in the serial log (the "Play:" lines) and in the simulator's "game" step
inline const char *const GAME_KEYS[GAME_COUNT] = { "keepy", "catch", "peek", "song" };

// ---------------------------------------------------------------- reward

// A "good game" in each one. At par every game pays the same reward: full JOY,
// +3 SPEED (gameSpeedPay) and a heart (Pet::playGame -> Pet::playReward). It also
// scales what a run costs the pet (gameNormScore).
constexpr uint8_t GAME_PAR[GAME_COUNT] = { 15, 20, 14, 6 };
// The score each game ends by itself at, and the highest record it can really produce (a
// saved record above it can only be a damaged or foreign save, so it loads as 0): keepy-uppy
// 255 hits, Berry Catch 200 berries caught (up to 400 points: a favourite berry the player has
// found counts 2), Peekaboo 255 hits, Berry Song a 100-note tune repeated (the win).
constexpr uint16_t GAME_REC_MAX[GAME_COUNT] = { 255, 400, 255, 100 };
constexpr uint8_t GAME_SCORE_CAP = 30;  // normalised score ceiling

// Points per pay step, about half each game's par, so a step takes about the same effort in
// every game and a game at par still pays today's +3 speed. A point is paid at its step: the
// first GAME_STEP points at step 1, the next ones at step 2...
constexpr uint8_t GAME_STEP[GAME_COUNT] = { 8, 10, 7, 3 };
// The most speed a game can pay: a training stat's top.
constexpr uint8_t GAME_PAY_MAX = 100;

// The step the next point is paid at, with `score` points so far (the screen's "x1", "x2"...).
inline uint8_t gameStep(uint8_t game, uint16_t score) {
  if (game >= GAME_COUNT) return 1;
  if (score > GAME_REC_MAX[game]) score = GAME_REC_MAX[game];
  return (uint8_t)(score / GAME_STEP[game] + 1);
}

// Speed for a run of `score` points: each point of step k pays k / GAME_STEP (a whole step k
// pays k), summed, rounded half up, never past GAME_PAY_MAX. At par every game pays 3 (keepy-uppy 15,
// Berry Catch 20, Peekaboo 14, Berry Song 6); keepy-uppy 30 pays 9, Berry Song 12 pays 10.
// In 32 bits, with room: the largest sum (keepy-uppy at its cap) is a few thousand.
inline uint8_t gameSpeedPay(uint8_t game, uint16_t score) {
  if (game >= GAME_COUNT) return 0;
  if (score > GAME_REC_MAX[game]) score = GAME_REC_MAX[game];
  uint32_t s = GAME_STEP[game], full = score / s, rest = score % s;
  uint32_t units = s * full * (full + 1) / 2 + rest * (full + 1);  // the sum, in 1/GAME_STEP points
  uint32_t pay = (2 * units + s) / (2 * s);                        // rounded half up
  return pay > GAME_PAY_MAX ? GAME_PAY_MAX : (uint8_t)pay;
}

// Raw score -> the 0..30 score Pet::playReward spends the run's cost by (energy, food, weight)
// and the result screen's joy line reads. Keepy-uppy (par 15) keeps its raw score up to the cap.
inline uint8_t gameNormScore(uint8_t game, uint16_t raw) {
  if (game >= GAME_COUNT) return 0;
  uint32_t n = (uint32_t)raw * 15 / GAME_PAR[game];
  return n > GAME_SCORE_CAP ? GAME_SCORE_CAP : (uint8_t)n;
}

// A record read from NVS: kept if the game could have produced it, else 0.
inline uint16_t gameRecordOk(uint8_t game, uint32_t v) {
  return game < GAME_COUNT && v <= GAME_REC_MAX[game] ? (uint16_t)v : 0;
}

// What a game asks the sketch to play or show after a step or a touch.
enum : uint8_t { GEV_GOOD = 1, GEV_BAD = 2, GEV_HEART = 4, GEV_WIN = 8 };

// ---------------------------------------------------------------- randomness

// xorshift32: small, deterministic with a seed, and good enough for spawns.
struct GameRng {
  uint32_t s = 0x9E3779B9u;
  GameRng() = default;
  explicit GameRng(uint32_t seed) { seedWith(seed); }
  void seedWith(uint32_t seed) { s = seed ? seed : 0x9E3779B9u; }
  void mix(uint32_t entropy) { seedWith(s ^ entropy); next(); }
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  uint32_t below(uint32_t n) { return n ? next() % n : 0; }  // 0..n-1
};

// Shuffle bag: deals every game once in a random order, then reshuffles. The
// first game of a new round is never the one just played.
template <uint8_t N>
struct ShuffleBag {
  uint8_t order[N] = {};
  uint8_t pos = N;   // N = reshuffle before the next deal
  uint8_t last = N;  // N = nothing dealt yet
  uint8_t next(GameRng &rng) {
    if (pos >= N) {
      for (uint8_t i = 0; i < N; i++) order[i] = i;
      for (uint8_t i = N - 1; i > 0; i--) {  // Fisher-Yates
        uint8_t j = (uint8_t)rng.below(i + 1u);
        uint8_t tmp = order[i]; order[i] = order[j]; order[j] = tmp;
      }
      if (N > 1 && order[0] == last) {
        uint8_t j = (uint8_t)(1 + rng.below(N - 1u));
        uint8_t tmp = order[0]; order[0] = order[j]; order[j] = tmp;
      }
      pos = 0;
    }
    last = order[pos++];
    return last;
  }
};
using GameBag = ShuffleBag<GAME_COUNT>;

// ---------------------------------------------------------------- touch

// Leaving a game: a "Hold: exit" chip at the top centre, held for 1 s, like the
// exercises' exit on the 1.75C. The hold has to start on the chip; sliding
// onto it never arms it, and the touch that exits is swallowed until the finger
// lifts. A finger report only counts while reports keep coming (<= 250 ms apart,
// GameClock::MAX_STEP_MS: a game frame can take 100 ms or more and reports are
// only read between frames), so a stalled touch stream can't complete a hold.
// A touch that starts while the screen is dark (screen off or dimmed) only wakes
// it: it is swallowed until it lifts and never reaches the game.
//
// Every other touch goes to the game: DOWN when it starts, MOVE while it stays
// down, UP when it lifts.
enum GameTouch : uint8_t { GT_NONE = 0, GT_DOWN, GT_MOVE, GT_UP, GT_EXIT };

struct GameTouchRouter {
  static constexpr int EXIT_X = 153, EXIT_Y = 20, EXIT_W = 160, EXIT_H = 40;
  static constexpr uint32_t EXIT_HOLD_MS = 1000;
  static constexpr uint32_t REPORT_GAP_MS = 250;  // == GameClock::MAX_STEP_MS (checked below)

  static constexpr bool inExit(int x, int y) {
    return x >= EXIT_X && x < EXIT_X + EXIT_W && y >= EXIT_Y && y < EXIT_Y + EXIT_H;
  }

  bool down = false;     // finger down, as of the last report
  bool onExit = false;   // this touch started on the chip
  bool armed = false;    // ... and has stayed on it
  bool blocked = false;  // exited: ignore this touch until it lifts
  uint32_t start = 0, lastReport = 0;

  void reset() { down = onExit = armed = blocked = false; start = lastReport = 0; }

  // How long the chip has been held, for its progress bar (0 when not held).
  uint32_t heldMs(uint32_t now) const {
    return armed && now - lastReport <= REPORT_GAP_MS ? now - start : 0;
  }

  // One touch report from the panel.
  // `dark`: the screen was off or dimmed when this report came in. A touch that
  // starts then is swallowed until it lifts (it only wakes the screen).
  GameTouch update(bool pressed, int x, int y, uint32_t now, bool dark = false) {
    if (!blocked && pressed && !down && dark) {
      blocked = true;
      onExit = armed = false;
    }
    if (blocked) {
      down = pressed;
      if (!pressed) blocked = false;
      return GT_NONE;
    }
    bool wasDown = down;
    if (wasDown && armed && now - lastReport > REPORT_GAP_MS) armed = false;
    down = pressed;
    lastReport = now;
    if (!pressed) {
      bool gameTouch = wasDown && !onExit;
      onExit = armed = false;
      return gameTouch ? GT_UP : GT_NONE;
    }
    if (!wasDown) {
      onExit = armed = inExit(x, y);
      start = now;
      return onExit ? GT_NONE : GT_DOWN;
    }
    if (!onExit) return GT_MOVE;
    if (!inExit(x, y)) armed = false;
    if (armed && now - start >= EXIT_HOLD_MS) {
      onExit = armed = false;
      blocked = true;
      return GT_EXIT;
    }
    return GT_NONE;
  }
};

// ---------------------------------------------------------------- time

// A game's own clock: the sum of frame steps, each capped. If the screen goes
// off (PWR, pocket lock) no frames are drawn and the game simply waits, instead
// of its timer running out unseen.
struct GameClock {
  static constexpr uint32_t MAX_STEP_MS = 250;
  uint32_t t = 0, last = 0;
  void start(uint32_t now) { t = 0; last = now; }
  uint32_t tick(uint32_t now) {
    uint32_t d = now - last;  // unsigned: safe across the millis() wrap
    last = now;
    if (d > MAX_STEP_MS) d = MAX_STEP_MS;
    t += d;
    return d;
  }
};
static_assert(GameTouchRouter::REPORT_GAP_MS == GameClock::MAX_STEP_MS,
              "the exit hold must survive the slowest frame the game clock accepts");

// Title card -> play -> over. A game keeps the screen on, so a game nobody
// touches for 15 s ends (and pays for what was done). Time the game spends
// showing something (Berry Song playing its tune) doesn't count as idle.
enum GamePhase : uint8_t { GP_TITLE = 0, GP_PLAY, GP_OVER };

struct GameFlow {
  static constexpr uint32_t TITLE_MS = 1200;
  static constexpr uint32_t IDLE_MS = 15000;
  uint8_t game = GAME_KEEPY;
  uint8_t phase = GP_TITLE;
  GameClock clock;
  uint32_t lastTouch = 0;

  void begin(uint8_t g, uint32_t now) {
    game = g;
    phase = GP_TITLE;
    clock.start(now);
    lastTouch = 0;
  }
  // One frame. Returns the ms the game should advance (0 outside play).
  uint32_t frame(uint32_t now, bool gameBusy) {
    uint32_t d = clock.tick(now);
    if (phase == GP_TITLE) {
      if (clock.t >= TITLE_MS) {
        phase = GP_PLAY;
        lastTouch = clock.t;
      }
      return 0;
    }
    if (phase != GP_PLAY) return 0;
    if (gameBusy) lastTouch = clock.t;
    return d;
  }
  void touched() { lastTouch = clock.t; }
  bool idleOut() const { return phase == GP_PLAY && clock.t - lastTouch >= IDLE_MS; }
};
