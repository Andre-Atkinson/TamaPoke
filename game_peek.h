#pragma once
#include "gamecore.h"

// Peekaboo: the pet pops up from one of five bushes for a moment; tap it while
// it's up. A tap on an empty bush only rustles it. 20 pop-ups (~30 s).
struct PeekGame {
  static constexpr uint8_t BUSHES = 5;
  static constexpr uint8_t POPS = 20;
  // bush centres: two above, three below, all well inside the round panel
  static constexpr int16_t BX[BUSHES] = { 150, 316, 100, 233, 366 };
  static constexpr int16_t BY[BUSHES] = { 200, 200, 310, 310, 310 };
  // hit area: the bush plus the pet's height above it
  static constexpr int HIT_HALF_W = 60, HIT_ABOVE = 120, HIT_BELOW = 40;
  static constexpr uint32_t FIRST_GAP_MS = 600;
  static constexpr uint32_t DUCK_AFTER_HIT_MS = 400;  // a happy nod, then it ducks
  static constexpr uint32_t RUSTLE_MS = 300;
  enum : uint8_t { WAIT = 0, UP, DONE };

  uint8_t phase = WAIT;
  uint8_t bush = 0, lastBush = BUSHES;  // BUSHES = none yet
  uint8_t pops = 0, score = 0;
  bool hit = false;                    // this pop-up was already tapped
  uint32_t t = 0, phaseStart = 0, phaseEnd = 0;
  uint8_t rustleBush = BUSHES;
  uint32_t rustleT = 0;
  uint8_t events = 0;
  GameRng rng;

  // how long the pet stays up / hides before the next pop-up, by pop-up number
  static uint32_t upMsFor(uint8_t pop) {
    if (pop >= POPS - 1) return 750;
    return 1500 - 750u * pop / (POPS - 1);
  }
  static uint32_t gapMsFor(uint8_t pop) {
    if (pop >= POPS - 1) return 300;
    return 500 - 200u * pop / (POPS - 1);
  }
  static bool inBush(uint8_t b, int x, int y) {
    if (b >= BUSHES) return false;
    int dx = x - BX[b];
    return dx >= -HIT_HALF_W && dx <= HIT_HALF_W && y >= BY[b] - HIT_ABOVE && y <= BY[b] + HIT_BELOW;
  }

  void reset(uint32_t seed) {
    rng.seedWith(seed);
    phase = WAIT;
    bush = 0;
    lastBush = BUSHES;
    pops = score = 0;
    hit = false;
    t = phaseStart = 0;
    phaseEnd = FIRST_GAP_MS;
    rustleBush = BUSHES;
    rustleT = 0;
    events = 0;
  }
  bool done() const { return phase == DONE; }
  uint16_t result() const { return score; }
  uint8_t takeEvents() { uint8_t e = events; events = 0; return e; }
  bool up() const { return phase == UP; }

  void advance() {
    if (phase == WAIT) {
      uint8_t b;
      if (lastBush >= BUSHES) {
        b = (uint8_t)rng.below(BUSHES);
      } else {  // any bush but the last one, all equally likely
        b = (uint8_t)rng.below(BUSHES - 1);
        if (b >= lastBush) b++;
      }
      bush = b;
      hit = false;
      phase = UP;
      phaseStart = phaseEnd;
      phaseEnd += upMsFor(pops);
    } else if (phase == UP) {
      lastBush = bush;
      pops++;
      phaseStart = phaseEnd;
      if (pops >= POPS) {
        phase = DONE;
      } else {
        phase = WAIT;
        phaseEnd += gapMsFor(pops);
      }
    }
  }

  void step(uint32_t dtMs) {
    if (done()) return;
    t += dtMs;
    while (phase != DONE && t >= phaseEnd) advance();
  }

  void touchDown(int x, int y) {
    if (done()) return;
    if (phase == UP && !hit && inBush(bush, x, y)) {
      hit = true;
      score++;
      events |= GEV_GOOD;
      if (phaseEnd > t + DUCK_AFTER_HIT_MS) phaseEnd = t + DUCK_AFTER_HIT_MS;
      return;
    }
    for (uint8_t b = 0; b < BUSHES; b++) {
      if (inBush(b, x, y) && !(phase == UP && b == bush)) {
        rustleBush = b;
        rustleT = t;
        return;
      }
    }
  }
  bool rustling(uint8_t b) const { return b == rustleBush && t - rustleT < RUSTLE_MS; }
};
