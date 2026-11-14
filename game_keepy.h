#pragma once
#include <math.h>
#include "gamecore.h"

// Keepy-uppy: tap the Poke Ball to keep it in the air; three drops end it.
// The physics and every constant are the ones the sketch used before the
// minigames were split out (stepGame/gameTap/respawnBall), tuned to an 85 ms
// step. Only the input changed: a hit counts when the finger touches.
struct KeepyGame {
  static constexpr float ARENA_CX = 233, ARENA_CY = 233;
  static constexpr float WALL_R = 205;   // round wall the ball bounces off
  static constexpr float FLOOR_Y = 384;  // below this the ball is dropped
  static constexpr float HIT_R = 74;     // tap distance that hits the ball
  static constexpr uint8_t LIVES = 3;
  static constexpr uint32_t STEP_MS = 85;

  float ballX = 233, ballY = 96, ballVX = 0, ballVY = 0, petX = 233;
  uint8_t score = 0, misses = 0;
  float hitX = 0, hitY = 0;  // last hit (impact ring)
  uint32_t hitT = 0;         // game time of the last hit
  bool hasHit = false;
  uint32_t t = 0;
  uint8_t events = 0;
  GameRng rng;

  void reset(uint32_t seed) {
    rng.seedWith(seed);
    score = misses = 0;
    petX = 233;
    t = hitT = 0;
    hasHit = false;
    events = 0;
    respawn();
  }
  void respawn() {
    ballX = 150 + (float)rng.below(166);
    ballY = 96;
    float sp = 1.6f + score * 0.05f;  // livelier as the score grows
    if (sp > 4.0f) sp = 4.0f;
    ballVX = rng.below(2) ? sp : -sp;
    ballVY = 0;
  }
  bool done() const { return misses >= LIVES; }
  uint16_t result() const { return score; }
  uint8_t takeEvents() { uint8_t e = events; events = 0; return e; }

  // true when the touch hit the ball
  bool touchDown(int x, int y) {
    if (done()) return false;
    float dx = ballX - x, dy = ballY - y;
    if (dx * dx + dy * dy >= HIT_R * HIT_R) return false;
    if (score < 255) score++;
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

  void step(uint32_t dtMs) {
    if (done()) return;
    t += dtMs;
    float k = dtMs / (float)STEP_MS;  // physics per 85 ms, whatever the frame took
    if (k > 3.0f) k = 3.0f;
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
    if (ballY > FLOOR_Y) {
      misses++;
      if (!done()) respawn();
    }
    float chase = (ballX - petX) * 0.12f;  // the pet follows it along the ground
    if (chase > 7) chase = 7;
    if (chase < -7) chase = -7;
    petX += chase * k;
  }
};
