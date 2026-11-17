// The rhythm punch's lunge (#90): a landed punch plays the pet's own ATTACK, from its strike
// to its end, standing on its feet at IDLE's size, with the punch's lean toward the bag on
// top (gameui.cpp). Only the plan and the timing live here, with no drawing and no sprite
// loading, so the host tests (test/test_trainingtouch.cpp) share them with gameui.cpp.
//
// Why ATTACK isn't drawn like the other actions (drawPmdActM). Sprite facts (the shipped
// pack, 151 species and their shinies, 2026-11-17):
// - Every ATTACK is the sheet's front-facing row (tools/pack_pmd.py), so its strike moves
//   16-31 sprite px toward the viewer, down its canvas. Stood on its lowest pixel in any
//   frame (PmdAct::base), the frames before and after the strike would float 50-120 px
//   above the ground: the pet would leap up on every punch.
// - Its canvas is 80-88 px tall to make room for that move, so drawPmdActM() steps the scale
//   down for 86 of the 151 species at the punch's cap (pmdScaleFor in sdmon.h). The art
//   itself is drawn at IDLE's pixel size in every action.
// So each frame stands on its own feet (its lowest opaque row) and is drawn at IDLE's scale.
// The strike to the end runs 70-840 ms at the sprite's pace (median 350); the wind-up before
// it, 280-1050 ms, is skipped so the pose never comes after the bag was hit.
//
// Owner's decisions (2026-11-17): every species lunges, a frame that doesn't fit the room
// keeps today's look (the pet on IDLE); the lunge starts at the strike and is back to IDLE by
// MAX_MS; a tap that doesn't land shows nothing (exercise.h: it does nothing and costs
// nothing); the plan is worked out from the sprite on screen, here when a punch lands
// (exerciseTap), not stored with the sprite or in a table.
#pragma once
#include <stdint.h>
#include "exercise.h"

struct PunchLunge {
  // Back to IDLE by then. A punch lands at most WINDOW_MS after its swing's contact and the
  // next window opens WINDOW_MS before the next contact, so the pet is idle again well
  // before the next "now!": the lunge never sits on the cue the player times against.
  static constexpr uint32_t MAX_MS = 700;
  static constexpr uint8_t MAX_FRAMES = 24;  // PmdAct::ms[]: a TPK2 action has at most 24
  // The room on the punch screen, from the pet's feet and its centre (gameui.cpp checks it
  // against the layout): the tallest frame stays under the punch count, and neither side
  // reaches past the round panel's edge.
  static constexpr int ROOM_UP = 252;
  static constexpr int ROOM_SIDE = 120;

  bool ok = false;     // a landed punch lunges with this plan
  uint8_t s = 0;       // the scale it is drawn at: IDLE's
  uint8_t strike = 0;  // the first frame at the strike's deepest point (the lowest feet)
  uint8_t feet[MAX_FRAMES] = {};  // each frame's lowest opaque row + 1 (0: an empty frame)
  // the frames it was planned from: gameui.cpp draws it only while they are still on screen
  const uint8_t *data = nullptr;
  uint8_t w = 0, h = 0, frames = 0;

  // Plans from `n` frames of w x h palette indices at `d` (0xFF transparent), drawn at
  // scale `scale`. Not ok when there is nothing to plan from, when a frame from the strike
  // to the end is empty (the pet would blink out) or when one doesn't fit the room.
  void plan(const uint8_t *d, uint8_t w_, uint8_t h_, uint8_t n, uint8_t scale) {
    ok = false;
    data = d;
    w = w_;
    h = h_;
    frames = n;
    s = scale;
    strike = 0;
    if (!d || !w || !h || !n || n > MAX_FRAMES || !scale) return;
    uint8_t top[MAX_FRAMES], lo[MAX_FRAMES], hi[MAX_FRAMES];
    for (uint8_t f = 0; f < n; f++) {
      const uint8_t *fr = d + (uint32_t)f * w * h;
      feet[f] = 0;
      top[f] = h;
      lo[f] = w;
      hi[f] = 0;
      for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
          if (fr[r * w + c] != 0xFF) {
            if (top[f] == h) top[f] = (uint8_t)r;
            feet[f] = (uint8_t)(r + 1);
            if (c < lo[f]) lo[f] = (uint8_t)c;
            if (c + 1 > hi[f]) hi[f] = (uint8_t)(c + 1);
          }
      if (feet[f] > feet[strike]) strike = f;
    }
    int mid = (int)w * s / 2;  // the canvas centre, as drawPmdFrame() places it
    for (uint8_t f = strike; f < n; f++) {
      if (!feet[f]) return;
      if ((feet[f] - top[f]) * s > ROOM_UP) return;
      if (mid - lo[f] * s > ROOM_SIDE || hi[f] * s - mid > ROOM_SIDE) return;
    }
    ok = true;
  }
  bool isFor(const uint8_t *d, uint8_t w_, uint8_t h_, uint8_t n) const {
    return d == data && w_ == w && h_ == h && n == frames;
  }
  // The frame to draw at `now` (its durations in `ms`), or -1 for the pet on IDLE: only
  // after a landed punch, from the strike at the sprite's own pace, never past MAX_MS.
  int frameAt(const RhythmPunch &p, uint32_t now, const uint16_t *ms) const {
    if (!ok || !p.punches) return -1;
    uint32_t t = now - p.lastPunchAt;
    if (t >= MAX_MS) return -1;
    for (uint8_t f = strike; f < frames; f++) {
      if (t < ms[f]) return f;
      t -= ms[f];
    }
    return -1;
  }
};

static_assert(PunchLunge::MAX_MS + 100 <= RhythmPunch::SWING_MS - 2 * RhythmPunch::WINDOW_MS,
              "the lunge is over at least 100 ms before the next window can open");
