#pragma once
#include "gamecore.h"
#include "game_keepy.h"
#include "game_catch.h"
#include "game_peek.h"
#include "game_song.h"

// The Play button's game session: picks the game, runs title -> play -> over,
// routes touches (and the exit chip) and reports the raw score. Pure logic; the
// sketch draws from the state and pays the reward (Pet::playGame).
struct MiniGames {
  GameRng rng;
  GameBag bag;
  GameFlow flow;
  GameTouchRouter input;
  KeepyGame keepy;
  CatchGame catcher;
  PeekGame peek;
  SongGame song;

  uint8_t game() const { return flow.game; }

  // Play pressed: deal the next game from the bag. `entropy` reseeds the dice;
  // favColour/favKnown are the pet's favourite berry for Berry Catch.
  void start(uint32_t now, uint32_t entropy, uint8_t favColour, bool favKnown) {
    rng.mix(entropy);
    begin(bag.next(rng), now, favColour, favKnown);
  }
  // a given game (start() and the tests)
  void begin(uint8_t g, uint32_t now, uint8_t favColour, bool favKnown) {
    if (g >= GAME_COUNT) g = GAME_KEEPY;
    uint32_t seed = rng.next();
    switch (g) {
      case GAME_CATCH: catcher.reset(seed, favColour, favKnown); break;
      case GAME_PEEK: peek.reset(seed); break;
      case GAME_SONG: song.reset(seed); break;
      default: keepy.reset(seed); break;
    }
    flow.begin(g, now);
    played = false;
    input.reset();
  }

  bool busy() const { return flow.game == GAME_SONG && song.busy(); }
  bool gameDone() const {
    switch (flow.game) {
      case GAME_CATCH: return catcher.done();
      case GAME_PEEK: return peek.done();
      case GAME_SONG: return song.done();
      default: return keepy.done();
    }
  }
  // Berry Song repeated all 12 notes (the sketch plays a fanfare instead of the usual end sound)
  bool won() const { return flow.game == GAME_SONG && song.won; }
  bool over() const { return flow.phase == GP_OVER; }
  bool playing() const { return flow.phase == GP_PLAY; }

  // One frame: advance the clock and the game; ends it when the game is done
  // or nobody touched it for GameFlow::IDLE_MS.
  void frame(uint32_t now) {
    uint32_t d = flow.frame(now, busy());
    if (flow.phase != GP_PLAY) return;
    if (d) {
      switch (flow.game) {
        case GAME_CATCH: catcher.step(d); break;
        case GAME_PEEK: peek.step(d); break;
        case GAME_SONG: song.step(d); break;
        default: keepy.step(d); break;
      }
    }
    if (gameDone() || flow.idleOut()) flow.phase = GP_OVER;
  }

  // One touch report. Returns true when the player held the exit chip: leave
  // now, without a reward. Touches before play starts (title card) and after
  // it ends do nothing. `dark`: the screen was off or dimmed when the report
  // came in; a touch that starts then is swallowed until it lifts (it only
  // wakes the screen) and never reaches the game.
  //
  // `counted` says whether this report was activity (it restarts the 15 s idle
  // end, and the sketch refreshes the screen timers): a press, or a slide of
  // more than MOVE_PX from the last counted point. A finger resting on the panel
  // reports GT_MOVE every 20 ms and must not count, or Berry Song would never end.
  static constexpr int MOVE_PX = 8;
  bool counted = false;
  bool played = false;  // any activity this game (see score())
  int lastX = 0, lastY = 0;

  bool touch(bool pressed, int x, int y, uint32_t now, bool dark = false) {
    counted = false;
    bool start = pressed && !input.down && !input.blocked && !dark;
    GameTouch k = input.update(pressed, x, y, now, dark);
    if (k == GT_EXIT) return true;
    if (flow.phase != GP_PLAY) return false;
    if (k == GT_NONE) {
      // A touch that starts on the exit chip can still hit a keepy-uppy ball
      // under it (the ball goes as high as y 28). Only the start: the hold goes on.
      if (start && input.onExit && flow.game == GAME_KEEPY && keepy.touchDown(x, y)) count(x, y);
      return false;
    }
    if (k == GT_DOWN) {
      count(x, y);
    } else if (k == GT_MOVE) {
      int dx = x - lastX, dy = y - lastY;
      if (dx * dx + dy * dy > MOVE_PX * MOVE_PX) count(x, y);
    } else {
      return false;
    }
    switch (flow.game) {
      case GAME_CATCH: catcher.touch(x); break;
      case GAME_PEEK: if (k == GT_DOWN) peek.touchDown(x, y); break;
      case GAME_SONG: if (k == GT_DOWN) song.touchDown(x, y); break;
      default: if (k == GT_DOWN) keepy.touchDown(x, y); break;
    }
    return false;
  }

  // The raw score. A game nobody touched scores 0: in Berry Catch the pet starts
  // in the middle and catches berries on its own, which is not the player's doing.
  uint16_t score() const {
    if (!played) return 0;
    switch (flow.game) {
      case GAME_CATCH: return catcher.result();
      case GAME_PEEK: return peek.result();
      case GAME_SONG: return song.result();
      default: return keepy.result();
    }
  }
  uint8_t takeEvents() {
    switch (flow.game) {
      case GAME_CATCH: return catcher.takeEvents();
      case GAME_PEEK: return peek.takeEvents();
      case GAME_SONG: return song.takeEvents();
      default: return keepy.takeEvents();
    }
  }
  void count(int x, int y) {
    counted = true;
    played = true;
    lastX = x;
    lastY = y;
    flow.touched();
  }
  uint8_t takeNote() { return flow.game == GAME_SONG ? song.takeNote() : SongGame::NO_PAD; }
};
