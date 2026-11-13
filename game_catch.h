#pragma once
#include "gamecore.h"

// Berry Catch: slide a finger and the pet walks to it along the ground,
// catching the berries that fall. Rotten berries cost points. 30 s.
struct CatchGame {
  static constexpr uint32_t DURATION_MS = 30000;
  static constexpr uint32_t CLEAN_START_MS = 5000;  // no rotten berries before this
  static constexpr int X_MIN = 100, X_MAX = 366;    // where berries fall and the pet walks
  static constexpr float SPAWN_Y = 120;
  static constexpr float CATCH_Y = 340;  // the pet's mouth line
  static constexpr float GONE_Y = 400;   // missed berries vanish here
  static constexpr int CATCH_DX = 55;    // how far from the pet's centre still catches
  static constexpr float PET_SPEED = 700;  // px/s
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
  uint32_t t = 0, nextSpawn = 0;
  uint32_t eatT = 0, hurtT = 0;  // game time of the last catch (0 = none yet)
  bool ate = false, hurt = false;
  uint8_t fav = RED;
  bool favKnown = false;
  uint8_t events = 0;
  GameRng rng;

  // ramps over the 30 s (t clamped to the game length)
  static uint32_t spawnGapAt(uint32_t t) { return 1100 - 450 * clampT(t) / DURATION_MS; }
  static float fallSpeedAt(uint32_t t) { return 150.0f + 110.0f * clampT(t) / DURATION_MS; }
  static uint8_t rottenPctAt(uint32_t t) {
    return t < CLEAN_START_MS ? 0 : (uint8_t)(10 + 20 * clampT(t) / DURATION_MS);
  }
  static uint32_t clampT(uint32_t t) { return t > DURATION_MS ? DURATION_MS : t; }

  // favColour: the pet's favourite berry (0 red, 1 blue, 2 green); it only
  // counts double once the player has discovered it.
  void reset(uint32_t seed, uint8_t favColour, bool known) {
    rng.seedWith(seed);
    for (Berry &b : berries) b = Berry();
    petX = 233;
    targetX = 233;
    score = 0;
    t = 0;
    nextSpawn = 400;
    eatT = hurtT = 0;
    ate = hurt = false;
    fav = favColour < ROTTEN ? favColour : RED;
    favKnown = known;
    events = 0;
  }
  bool done() const { return t >= DURATION_MS; }
  uint16_t result() const { return score > 0 ? (uint16_t)score : 0; }
  uint8_t takeEvents() { uint8_t e = events; events = 0; return e; }
  uint32_t timeLeft() const { return done() ? 0 : DURATION_MS - t; }

  void touch(int x) {
    targetX = (int16_t)(x < X_MIN ? X_MIN : x > X_MAX ? X_MAX : x);
  }

  void spawn() {
    for (Berry &b : berries) {
      if (b.live) continue;
      b.live = true;
      b.x = (int16_t)(X_MIN + rng.below(X_MAX - X_MIN + 1));
      b.y = SPAWN_Y;
      b.vy = fallSpeedAt(t);
      b.kind = rng.below(100) < rottenPctAt(t) ? ROTTEN : (uint8_t)rng.below(3);
      return;
    }
  }

  void step(uint32_t dtMs) {
    if (done()) return;
    t += dtMs;
    if (t > DURATION_MS) t = DURATION_MS;
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
          score = score > 2 ? score - 2 : 0;
          hurtT = t;
          hurt = true;
          events |= GEV_BAD;
        } else {
          bool loved = favKnown && b.kind == fav;
          score += loved ? 2 : 1;
          eatT = t;
          ate = true;
          events |= loved ? (GEV_GOOD | GEV_HEART) : GEV_GOOD;
        }
      } else if (b.y >= GONE_Y) {
        b.live = false;
      }
    }
    if (!done() && t >= nextSpawn) {
      spawn();
      nextSpawn += spawnGapAt(nextSpawn);
    }
  }

  bool eating() const { return ate && t - eatT < FLASH_MS; }
  bool hurting() const { return hurt && t - hurtT < FLASH_MS; }
};
