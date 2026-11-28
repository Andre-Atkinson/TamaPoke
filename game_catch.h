#pragma once
#include "gamecore.h"

// Berry Catch: slide a finger and the pet walks to it along the ground, catching the berries
// that fall. Endless (#127): there is no timer. The pet starts in the middle and the first
// berries only teach the game: until the first berry caught after the player first steered
// (CatchGame::touch) the game is a warm-up where a berry that lands is free, and berries the
// standing pet catches before the first steer are eaten (shown) and count for nothing. From
// then on one good berry reaching the ground uncaught, or catching a rotten one, ends the game
// (it pays for what was caught). Everything ramps with the berries caught (a count, not the
// points; a favourite the player has found scores 2 but is one berry): berries come more often
// (spawnGapFor), fall faster (fallSpeedFor) and a growing share is rotten (rottenPctFor, none
// before CLEAN_CATCHES). The game ends by itself at MAX_CATCHES berries.
//
// The spawner keeps a plan so the game is fair at every level. It follows the player's best
// play: a position `planX` the pet is standing at (within CATCH_DX of it) at time `planT`, the
// moment the last planned berry arrives on the mouth line. A good berry is placed where a pet
// walking at PLAN_SPEED (half of its real speed, so a slow frame or a late start still makes
// it) can reach from there by its arrival, and becomes the new plan; a rotten berry is placed
// at least AVOID away from it, so standing still misses it, and moves only the time on. So from
// wherever the player caught the last good berry, standing still never catches a rotten berry
// and the next good one is always reachable. Arrivals come in spawn order (the ramps guarantee
// it; test/test_minigames.cpp checks).
struct CatchGame {
  static constexpr uint16_t MAX_CATCHES = 200;  // the game ends by itself at this many berries
  static constexpr uint16_t CLEAN_CATCHES = 5;  // no rotten berry before this many are caught
  static constexpr int X_MIN = 100, X_MAX = 366;    // where berries fall and the pet walks
  static constexpr float SPAWN_Y = 120;
  static constexpr float CATCH_Y = 340;  // the pet's mouth line
  static constexpr float GONE_Y = 400;   // a good berry that reaches it uncaught ends the game
  static constexpr int CATCH_DX = 55;    // how far from the pet's centre still catches
  static constexpr float PET_SPEED = 700;   // px/s
  static constexpr float PLAN_SPEED = 350;  // px/s: how far the plan expects the pet to walk
  // A rotten berry falls at least this far from the plan: the pet stands within CATCH_DX of it
  // and the berry must be out of its reach (CATCH_DX each side) with a margin.
  static constexpr int AVOID = 2 * CATCH_DX + 10;
  static_assert(X_MAX - X_MIN >= 2 * AVOID, "every plan position has room for a rotten berry on one side");
  static_assert(2 * PLAN_SPEED <= PET_SPEED, "the plan asks for at most half the pet's speed");
  static_assert(MAX_CATCHES * 2 == GAME_REC_MAX[GAME_CATCH], "the cap is the record's top (a favourite scores 2)");
  static constexpr uint8_t MAX_BERRIES = 8;
  static constexpr uint32_t FLASH_MS = 400;  // eat / hurt reaction
  enum : uint8_t { RED = 0, BLUE, GREEN, ROTTEN };

  struct Berry {
    float y = 0, vy = 0;
    int16_t x = 0;
    uint8_t kind = RED;
    bool live = false;
  };
  Berry berries[MAX_BERRIES];
  float petX = 233;
  int16_t targetX = 233;
  int16_t score = 0;
  uint16_t caught = 0;     // berries caught since the warm-up ended
  bool steered = false;    // the player has moved the target (touch())
  bool warm = true;        // no berry caught since the first steer: a miss costs nothing
  bool lost = false;       // over: a good berry reached the ground, or a rotten one was caught
  uint32_t t = 0, nextSpawn = 0;
  uint32_t eatT = 0, hurtT = 0;  // game time of the last catch (0 = none yet)
  bool ate = false, hurt = false;
  bool loved = false;  // the last berry caught was its favourite (heart)
  uint8_t fav = RED;
  bool favKnown = false;
  uint8_t events = 0;
  int16_t planX = 233;   // the plan (see the top)
  float planT = 0;       // ms of game time
  GameRng rng;

  // The ramps, by berries caught c. Integer maths (the host tests and the board agree).
  // Spawn gap in ms: 1100 at 0 caught, about 650 at 25, 567 at 50, 514 at 100.
  static uint32_t spawnGapFor(uint16_t c) { return 450u + 7150u / (11u + c); }
  // Fall speed in px/s: 150 at 0, about 260 at 25, 307 at 50, 348 at 100.
  static uint32_t fallSpeedFor(uint16_t c) { return 420u - 9720u / (36u + c); }
  // Rotten share in percent: 0 below CLEAN_CATCHES, then 10 rising towards 35 (never reaching it).
  static uint8_t rottenPctFor(uint16_t c) {
    if (c < CLEAN_CATCHES) return 0;
    uint32_t e = c - CLEAN_CATCHES;
    return (uint8_t)(10u + 25u * e / (e + 20u));
  }

  // favColour: the pet's favourite berry (0 red, 1 blue, 2 green); it only
  // counts double once the player has discovered it.
  void reset(uint32_t seed, uint8_t favColour, bool known) {
    rng.seedWith(seed);
    for (Berry &b : berries) b = Berry();
    petX = 233;
    targetX = 233;
    score = 0;
    caught = 0;
    steered = false;
    warm = true;
    lost = false;
    t = 0;
    nextSpawn = 400;
    eatT = hurtT = 0;
    ate = hurt = loved = false;
    fav = favColour < ROTTEN ? favColour : (uint8_t)RED;
    favKnown = known;
    events = 0;
    planX = 233;
    planT = 0;
  }
  bool done() const { return lost || caught >= MAX_CATCHES; }
  uint16_t result() const { return score > 0 ? (uint16_t)score : 0; }
  uint8_t takeEvents() { uint8_t e = events; events = 0; return e; }

  void touch(int x) {
    steered = true;
    targetX = (int16_t)(x < X_MIN ? X_MIN : x > X_MAX ? X_MAX : x);
  }

  // A new berry: when it reaches the mouth line, and where it falls by the plan.
  void spawn() {
    for (Berry &b : berries) {
      if (b.live) continue;
      uint32_t v = fallSpeedFor(caught);
      float arrive = (float)t + (CATCH_Y - SPAWN_Y) * 1000.0f / (float)v;
      b.live = true;
      b.y = SPAWN_Y;
      b.vy = (float)v;
      bool rotten = rng.below(100) < rottenPctFor(caught);
      b.kind = rotten ? (uint8_t)ROTTEN : (uint8_t)rng.below(3);
      if (rotten) {
        // out of the plan's reach on one side of it; the plan stays, only its time moves on
        int left = planX - AVOID - X_MIN + 1, right = X_MAX - (planX + AVOID) + 1;
        if (left < 0) left = 0;
        if (right < 0) right = 0;
        uint32_t u = rng.below((uint32_t)(left + right));
        b.x = (int16_t)(u < (uint32_t)left ? X_MIN + (int)u : planX + AVOID + (int)(u - left));
      } else {
        // within what a pet walking at PLAN_SPEED reaches from the plan by the arrival
        float room = PLAN_SPEED * (arrive - planT) / 1000.0f;
        int r = room < 0 ? 0 : room > 1000 ? 1000 : (int)room;
        int lo = planX - r < X_MIN ? X_MIN : planX - r;
        int hi = planX + r > X_MAX ? X_MAX : planX + r;
        b.x = (int16_t)(lo + (int)rng.below((uint32_t)(hi - lo + 1)));
        planX = b.x;
      }
      planT = arrive;
      return;
    }
  }

  void step(uint32_t dtMs) {
    if (done()) return;
    t += dtMs;
    float maxMove = PET_SPEED * dtMs / 1000.0f;
    float d = targetX - petX;
    petX += d > maxMove ? maxMove : d < -maxMove ? -maxMove : d;
    for (Berry &b : berries) {
      if (!b.live) continue;
      float prev = b.y;
      b.y += b.vy * dtMs / 1000.0f;
      // crossing the mouth line (no tunnelling through it on a slow frame)
      int dx = b.x - (int)(petX + 0.5f);
      if (prev < CATCH_Y && b.y >= CATCH_Y && dx >= -CATCH_DX && dx <= CATCH_DX) {
        b.live = false;
        if (b.kind == ROTTEN) {
          lost = true;  // no rotten berry spawns in the warm-up (none before CLEAN_CATCHES)
          hurtT = t;
          hurt = true;
          events |= GEV_BAD;
        } else if (!steered) {  // the standing pet's: eaten, but not the player's doing
          loved = false;
          eatT = t;
          ate = true;
          events |= GEV_GOOD;
        } else {
          loved = favKnown && b.kind == fav;
          score += loved ? 2 : 1;
          caught++;
          warm = false;
          eatT = t;
          ate = true;
          events |= loved ? (GEV_GOOD | GEV_HEART) : GEV_GOOD;
        }
      } else if (b.y >= GONE_Y) {
        b.live = false;
        if (b.kind != ROTTEN && !warm) lost = true;  // a good berry that nobody caught
      }
    }
    if (!done() && t >= nextSpawn) {
      spawn();
      nextSpawn += spawnGapFor(caught);
    }
  }

  bool eating() const { return ate && t - eatT < FLASH_MS; }
  bool hurting() const { return hurt && t - hurtT < FLASH_MS; }
};
