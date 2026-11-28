#pragma once
#include <math.h>
#include "gamecore.h"

// Keepy-uppy: tap the Poke Ball to keep it in the air. Endless (#127): until the first hit a
// drop only deals the ball again (the warm-up), and from then on one drop ends the game. The
// pace never stops rising: up to 31 hits the physics is exactly the one the sketch used before
// the minigames were split out (stepGame/gameTap/respawnBall, tuned to an 85 ms step; gravity
// rises with the score and stops at 31), and past 31 hits the whole game plays faster instead
// (paceAt): the ball's arcs keep their shape and the pet's chase keeps up, only on a quicker
// clock, so no hit's arc gets shorter than about a second (test/test_minigames.cpp). The
// game ends by itself at 255 hits. Only the input changed from the original: a hit counts
// when the finger touches.
struct KeepyGame {
  static constexpr float ARENA_CX = 233, ARENA_CY = 233;
  static constexpr float WALL_R = 205;   // round wall the ball bounces off
  static constexpr float FLOOR_Y = 384;  // below this the ball is dropped
  static constexpr float HIT_R = 74;     // tap distance that hits the ball
  static constexpr uint32_t STEP_MS = 85;
  static constexpr uint8_t MAX_HITS = 255;  // the game ends by itself here (GAME_REC_MAX)
  static constexpr uint8_t PACE_FROM = 31;  // past this many hits the whole game speeds up
  static_assert(MAX_HITS == GAME_REC_MAX[GAME_KEEPY], "the cap is the record's top");

  float ballX = 233, ballY = 96, ballVX = 0, ballVY = 0, petX = 233;
  uint8_t score = 0;
  bool dropped = false;  // the ball hit the floor after the first hit: over
  float hitX = 0, hitY = 0;  // last hit (impact ring)
  uint32_t hitT = 0;         // game time of the last hit
  bool hasHit = false;
  uint32_t t = 0;
  uint8_t events = 0;
  GameRng rng;

  void reset(uint32_t seed) {
    rng.seedWith(seed);
    score = 0;
    dropped = false;
    petX = 233;
    t = hitT = 0;
    hasHit = false;
    events = 0;
    respawn();
  }
  void respawn() {
    ballX = 150 + (float)rng.below(166);
    ballY = 96;
    // Livelier as the score grows: 1.6 + 0.05 per hit, up to 4. Since #127 a drop after the first
    // hit ends the game and only a drop in the warm-up (score 0) deals a new ball, so this only
    // ever runs at score 0, where it is 1.6; the formula is left as it was.
    float sp = 1.6f + score * 0.05f;
    if (sp > 4.0f) sp = 4.0f;
    ballVX = rng.below(2) ? sp : -sp;
    ballVY = 0;
  }
  bool done() const { return dropped || score >= MAX_HITS; }
  // The game's pace, 1 for the first 31 hits (the physics as it always was) and then rising
  // with every hit, ever more slowly: 1 + 1.5 * (s - 31) / (s - 31 + 60). 1.75 at 91 hits, 2 at 151,
  // about 2.18 at 255, never 2.5. It multiplies the physics step, so one frame plays that much game time.
  static float paceAt(uint16_t s) {
    if (s <= PACE_FROM) return 1.0f;
    float e = (float)(s - PACE_FROM);
    return 1.0f + 1.5f * e / (e + 60.0f);
  }
  uint16_t result() const { return score; }
  uint8_t takeEvents() { uint8_t e = events; events = 0; return e; }

  // true when the touch hit the ball
  bool touchDown(int x, int y) {
    if (done()) return false;
    float dx = ballX - x, dy = ballY - y;
    if (dx * dx + dy * dy >= HIT_R * HIT_R) return false;
    score++;  // done() at MAX_HITS, so never past it
    events |= GEV_GOOD;
    // a soft hit: a moderate lift that grows slowly with the score
    float lift = 6.6f + (score > 16 ? 3.5f : score * 0.22f);
    ballVY = -lift;
    ballVX += dx * 0.12f;
    if (ballVX > 6.5f) ballVX = 6.5f;
    if (ballVX < -6.5f) ballVX = -6.5f;
    hitX = ballX;
    hitY = ballY;
    hitT = t;
    hasHit = true;
    return true;
  }

  // One frame. The physics runs in sub-steps of at most STEP_MS of game time (the faster pace
  // never integrates in bigger steps than the 85 ms one the physics was tuned to).
  void step(uint32_t dtMs) {
    if (done()) return;
    t += dtMs;
    float k = dtMs / (float)STEP_MS;  // physics per 85 ms, whatever the frame took
    if (k > 3.0f) k = 3.0f;
    k *= paceAt(score);
    int n = (int)ceilf(k);
    if (n < 1) n = 1;
    for (int i = 0; i < n && !done(); i++) physics(k / n);
  }

 private:
  void physics(float k) {
    float grav = 0.40f + score * 0.013f;  // falls a little faster each time
    if (grav > 0.80f) grav = 0.80f;
    ballVY += grav * k;
    ballX += ballVX * k;
    ballY += ballVY * k;
    float dx = ballX - ARENA_CX, dy = ballY - ARENA_CY;  // bounce off the round wall
    float d = sqrtf(dx * dx + dy * dy);
    if (d > WALL_R) {
      float nx = dx / d, ny = dy / d;
      float dot = ballVX * nx + ballVY * ny;
      if (dot > 0) {
        ballVX = (ballVX - 2 * dot * nx) * 0.85f;
        ballVY = (ballVY - 2 * dot * ny) * 0.85f;
      }
      ballX = ARENA_CX + nx * WALL_R;
      ballY = ARENA_CY + ny * WALL_R;
    }
    if (ballY > FLOOR_Y) {  // a drop: before the first hit the ball comes back, after it that is the end
      if (score == 0) respawn();
      else dropped = true;
    }
    float chase = (ballX - petX) * 0.12f;  // the pet follows it along the ground
    if (chase > 7) chase = 7;
    if (chase < -7) chase = -7;
    petX += chase * k;
  }
};
