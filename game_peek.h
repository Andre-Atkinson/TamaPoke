#pragma once
#include "gamecore.h"

// Peekaboo: the pet pops up from one of five bushes for a moment; tap it while it's up.
// Endless (#127): there is no pop-up limit. Until the first hit the game is a warm-up where a
// pop-up that ducks back untapped, or a tap on a bush other than the one up (or on any bush
// while none is up), only rustles. From then on either of those ends the game, and it pays for
// the hits. A tap outside every bush does nothing, the duck after a hit is not a miss, and a
// second tap on the pet just hit is never one: inside the hit bush's own area, whatever other
// bush's area it also lies in (the areas overlap), and for BOUNCE_MS after the hit even once
// it has ducked (a double tap). The time up keeps shortening with the hits
// (upMsFor), down to a floor, and the pause between pop-ups to its own (gapMsFor); both
// go by hits, so the warm-up's misses don't speed the game up. It ends by itself at 255 hits.
//
// Taps and frames, on both boards (loop() in TamaPoke.ino, renderGame in gameui.cpp): each
// pass of the loop reads the touch first (handleTouch -> gameTouch, polled every 20 ms) and
// steps the game afterwards, by the real time since the last frame (MiniGames::frame). A tap
// is therefore judged against the game's clock as of the last frame, never later than when it
// was made, except for a tap made between a pass's touch read and its frame (the rest of the
// loop's work, a few ms, up to the 20 ms poll gap): that one is read in the next pass, after
// a frame that may already be past phaseEnd. So a pop-up that is untapped at phaseEnd ducks
// (the picture) but stays tappable for SETTLE_MS more (a hit still counts), and the untapped
// miss ends the game only once that has passed, as RhythmPunch::SETTLE_MS does (exercise.h).
// The next pop-up's schedule still counts from phaseEnd, so the settle slows nothing down.
// The settle is far over the longest loop work between a touch read and the frame.
struct PeekGame {
  static constexpr uint8_t BUSHES = 5;
  static constexpr uint8_t MAX_HITS = 255;  // the game ends by itself here (GAME_REC_MAX)
  static_assert(MAX_HITS == GAME_REC_MAX[GAME_PEEK], "the cap is the record's top");
  // bush centres: two above, three below, all well inside the round panel
  static constexpr int16_t BX[BUSHES] = { 150, 316, 100, 233, 366 };
  static constexpr int16_t BY[BUSHES] = { 200, 200, 310, 310, 310 };
  // hit area: the bush plus the pet standing on it (most sprites are under 130 px tall)
  static constexpr int HIT_HALF_W = 60, HIT_ABOVE = 140, HIT_BELOW = 40;
  static constexpr uint32_t FIRST_GAP_MS = 600;
  static constexpr uint32_t DUCK_AFTER_HIT_MS = 400;  // a happy nod, then it ducks
  static constexpr uint32_t RUSTLE_MS = 300;
  static constexpr uint32_t SETTLE_MS = 60;   // an untapped pop-up is a miss this long after phaseEnd
  static constexpr uint32_t BOUNCE_MS = 150;  // a second tap this soon after a hit is the same tap
  enum : uint8_t { WAIT = 0, UP, DONE };

  uint8_t phase = WAIT;
  uint8_t bush = 0, lastBush = BUSHES;  // BUSHES = none yet
  uint8_t score = 0;
  bool missed = false;                 // over: a pop-up ducked untapped, or a wrong tap (after the first hit)
  bool hit = false;                    // this pop-up was already tapped
  uint8_t hitBush = BUSHES;            // the bush last hit (BUSHES: none yet) and when
  uint32_t hitAt = 0;
  uint32_t t = 0, phaseStart = 0, phaseEnd = 0;
  uint8_t rustleBush = BUSHES;
  uint32_t rustleT = 0;
  uint8_t events = 0;
  GameRng rng;

  // How long the pet stays up / hides before the next pop-up, by the hits so far. The up time
  // goes 1500 -> 750 ms over the first 19 hits, as it always did, then on towards a floor:
  // 650 ms at 39 hits, 590 at 99, 566 at 254, never under 550. The pet takes 150 ms to rise and
  // 150 to hide (drawPeek), so even at the floor it is fully up for about 250 ms, and taps
  // count through the whole time up. The pause goes 500 -> 300 ms by 19 hits and stays there.
  static constexpr uint8_t RAMP_HITS = 19;
  static uint32_t upMsFor(uint8_t hits) {
    if (hits <= RAMP_HITS) return 1500 - 750u * hits / RAMP_HITS;
    return 550 + 4000u / (hits + 1u);
  }
  static uint32_t gapMsFor(uint8_t hits) {
    if (hits > RAMP_HITS) hits = RAMP_HITS;
    return 500 - 200u * hits / RAMP_HITS;
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
    score = 0;
    missed = false;
    hit = false;
    hitBush = BUSHES;
    hitAt = 0;
    t = phaseStart = 0;
    phaseEnd = FIRST_GAP_MS;
    rustleBush = BUSHES;
    rustleT = 0;
    events = 0;
  }
  bool done() const { return phase == DONE; }
  bool warm() const { return score == 0; }  // before the first hit: a miss costs nothing
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
      phaseEnd += upMsFor(score);
    } else if (phase == UP) {
      lastBush = bush;
      phaseStart = phaseEnd;
      if (!hit && !warm()) {  // ducked back untapped: the miss that ends the game
        missed = true;
        phase = DONE;
        events |= GEV_BAD;
      } else {
        phase = WAIT;
        phaseEnd += gapMsFor(score);
      }
    }
  }

  void step(uint32_t dtMs) {
    if (done()) return;
    t += dtMs;
    // an untapped pop-up is judged SETTLE_MS after it ducks (see the top)
    while (phase != DONE && t >= phaseEnd + (phase == UP && !hit ? SETTLE_MS : 0)) advance();
  }

  void touchDown(int x, int y) {
    if (done()) return;
    if (phase == UP && !hit && inBush(bush, x, y)) {
      hit = true;
      hitBush = bush;
      hitAt = t;
      score++;
      events |= GEV_GOOD;
      if (score >= MAX_HITS) {
        phase = DONE;
      } else if (phaseEnd > t + DUCK_AFTER_HIT_MS || t >= phaseEnd) {  // (a hit in the settle)
        phaseEnd = t + DUCK_AFTER_HIT_MS;
      }
      return;
    }
    // The pet just hit is never a miss: the bush up and hit (whichever other bush's area the
    // tap is also in), or the bush hit a moment ago (a double tap, even once it has ducked).
    if (phase == UP && hit && inBush(bush, x, y)) return;
    if (hitBush < BUSHES && t - hitAt < BOUNCE_MS && inBush(hitBush, x, y)) return;
    for (uint8_t b = 0; b < BUSHES; b++) {
      if (inBush(b, x, y)) {
        if (warm()) {
          rustleBush = b;
          rustleT = t;
        } else {
          missed = true;
          phase = DONE;
          events |= GEV_BAD;
        }
        return;
      }
    }
  }
  bool rustling(uint8_t b) const { return b == rustleBush && t - rustleT < RUSTLE_MS; }
};
