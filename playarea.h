#pragma once
#include <math.h>
#include <stdint.h>

// The main screen's play area (#66): where the pet's centre may stand, the column that sends
// its bubbles left or right, and the shape of its hops. One copy of each, shared by the idle
// planner (idleplan.h), the finger drag (scene.cpp), the "!" bubble (attention.h), the speech
// bubble (bubble.h) and the double-tap hop (reactOffset(), TamaPoke.ino). Pure numbers and a
// helper, so the host tests read them too.

// Where the pet's centre may be: the idle planner walks it anywhere in this range (its
// IDLE_WALK_MIN..MAX), a dragging finger leads it no further, and the bubbles beside its head
// place themselves for a pet in it.
#define PET_WALK_MIN 150
#define PET_WALK_MAX 325

// The screen's centre column (app.h's CX; scene.cpp checks they agree): a pet at it or left of
// it has its bubbles on its right, the side with more room.
#define PLAY_MID_X 233

// A hop's height off the ground (px, 0 or negative), `elapsed` ms in, for hops `hopMs` long
// and `heightPx` tall, one after another: half a sine, so it rises fast, slows at the top and
// lands at the end of each hop. The idle hop and the plush's (idleHopDy, idleplan.h) and the
// double-tap hop (PET_TAP_HOP_*, touchgesture.h) differ only in their numbers.
inline int hopDy(uint32_t elapsed, uint32_t hopMs, int heightPx) {
  return -(int)(heightPx * sinf((float)(elapsed % hopMs) * 3.14159265f / hopMs));
}
