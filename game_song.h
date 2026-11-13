#pragma once
#include "gamecore.h"

// Berry Song: the pet plays a tune on four berry pads; the player repeats it.
// Every round adds a note to the same tune. Three wrong answers end it, and so
// does repeating all 12 notes. No timer while it's the player's turn, and the
// pads light up, so it works with the sound off.
struct SongGame {
  static constexpr uint8_t PADS = 4;
  static constexpr uint8_t MAX_LEN = 12, START_LEN = 2, TRIES = 3;
  // pads along the lower arc: red berry, blue berry, green berry, candy
  static constexpr int16_t PX[PADS] = { 105, 185, 281, 361 };
  static constexpr int16_t PY[PADS] = { 310, 372, 372, 310 };
  static constexpr int PAD_R = 46, HIT_R = 48;  // neighbouring pads are 96+ px apart: no overlap
  static constexpr uint32_t LEAD_MS = 800;    // "Listen..." before the tune
  static constexpr uint32_t GAP_MS = 150;     // silence between notes
  static constexpr uint32_t NEXT_MS = 700;    // pause after a round is repeated
  static constexpr uint32_t WRONG_MS = 1000;  // pause before the tune plays again
  static constexpr uint32_t PRESS_MS = 250;   // a pressed pad stays lit
  static constexpr uint32_t NEVER = 0xFFFFFFFFu;
  static constexpr uint8_t NO_PAD = 0xFF;
  enum : uint8_t { LEAD = 0, SHOW, TURN, PAUSE, WRONG, DONE };

  uint8_t seq[MAX_LEN] = {};
  uint8_t len = START_LEN;  // notes in the current tune
  uint8_t shown = 0;        // note being shown (SHOW)
  uint8_t k = 0;            // next note the player must press (TURN)
  uint8_t tries = TRIES;
  uint8_t best = 0;         // longest tune repeated: the score
  uint8_t phase = LEAD;
  bool won = false;
  uint32_t t = 0, phaseEnd = 0;
  uint8_t lit = NO_PAD;     // pad drawn lit
  uint32_t litUntil = 0;
  uint8_t note = NO_PAD;    // tone to play now (taken by the sketch)
  uint8_t events = 0;
  GameRng rng;

  // a note's length: 500 ms for short tunes, down to 350 ms from 8 notes
  static uint32_t noteMsFor(uint8_t n) {
    if (n <= START_LEN) return 500;
    if (n >= 8) return 350;
    return 500 - 25u * (n - START_LEN);
  }
  static uint8_t padAt(int x, int y) {
    for (uint8_t p = 0; p < PADS; p++) {
      int dx = x - PX[p], dy = y - PY[p];
      if (dx * dx + dy * dy <= HIT_R * HIT_R) return p;
    }
    return NO_PAD;
  }

  void reset(uint32_t seed) {
    rng.seedWith(seed);
    for (uint8_t &n : seq) n = (uint8_t)rng.below(PADS);
    len = START_LEN;
    shown = k = 0;
    tries = TRIES;
    best = 0;
    won = false;
    t = 0;
    phase = LEAD;
    phaseEnd = LEAD_MS;
    lit = note = NO_PAD;
    litUntil = 0;
    events = 0;
  }
  bool done() const { return phase == DONE; }
  uint16_t result() const { return best; }
  uint8_t takeEvents() { uint8_t e = events; events = 0; return e; }
  uint8_t takeNote() { uint8_t n = note; note = NO_PAD; return n; }
  bool playersTurn() const { return phase == TURN; }
  // the tune is playing or about to: not the player's idle time
  bool busy() const { return phase != TURN && phase != DONE; }
  uint8_t litPad() const { return lit != NO_PAD && t < litUntil ? lit : NO_PAD; }

  void showNote(uint32_t at) {
    uint32_t ms = noteMsFor(len);
    lit = note = seq[shown];
    litUntil = at + ms;
    phaseEnd = at + ms + GAP_MS;
  }
  void startTune(uint32_t at) {
    phase = SHOW;
    shown = 0;
    showNote(at);
  }
  void advance() {
    uint32_t at = phaseEnd;
    switch (phase) {
      case LEAD:
      case WRONG:
        startTune(at);
        break;
      case PAUSE:
        len++;
        startTune(at);
        break;
      case SHOW:
        if (shown + 1 < len) {
          shown++;
          showNote(at);
        } else {
          phase = TURN;
          k = 0;
          phaseEnd = NEVER;
        }
        break;
      default:
        phaseEnd = NEVER;
        break;
    }
  }

  void step(uint32_t dtMs) {
    if (done()) return;
    t += dtMs;
    while (phase != DONE && phaseEnd != NEVER && t >= phaseEnd) advance();
  }

  void touchDown(int x, int y) {
    if (phase != TURN) return;
    uint8_t p = padAt(x, y);
    if (p == NO_PAD) return;
    lit = p;
    litUntil = t + PRESS_MS;
    if (p == seq[k]) {
      note = p;
      if (++k < len) return;
      best = len;
      events |= GEV_GOOD;
      if (len >= MAX_LEN) {
        won = true;
        phase = DONE;
        events |= GEV_WIN;
      } else {
        phase = PAUSE;
        phaseEnd = t + NEXT_MS;
      }
    } else {
      events |= GEV_BAD;
      if (--tries == 0) {
        phase = DONE;
      } else {
        phase = WRONG;
        phaseEnd = t + WRONG_MS;
      }
    }
  }
};
