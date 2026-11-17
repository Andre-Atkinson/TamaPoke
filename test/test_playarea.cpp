// The play area (playarea.h, #66): the "!" bubble, the speech bubble and the idle emote place
// themselves for a pet inside one walk range and turn at one centre column, and the idle hop
// and the double tap's hop share one shape. Pure logic.
#include "framework.h"
#include "../attention.h"
#include "../bubble.h"
#include "../idleplan.h"
#include "../playarea.h"
#include "../touchgesture.h"
#include <string>

static int intoWalk(int x) { return x < PET_WALK_MIN ? PET_WALK_MIN : x > PET_WALK_MAX ? PET_WALK_MAX : x; }

// A pet outside the walk range (a finger let go of it mid-step, say) gets the bubbles of the
// nearest spot inside it, and each of the three is on the pet's right up to the centre column
// and on its left after it.
TEST(PlayArea, the_bubbles_and_the_emote_share_the_walk_range_and_the_middle) {
  for (int petX = 0; petX <= 466; petX++) {
    int in = intoWalk(petX);
    bool right = in <= PLAY_MID_X;
    std::string at = " at x " + std::to_string(petX);
    for (int top : { 120, 200, 262 }) {
      AttnBubble a = attnBubbleAt(petX, top, false), ai = attnBubbleAt(in, top, false);
      CHECK_MSG(a.x == ai.x && a.y == ai.y && a.right == ai.right, "\"!\" bubble outside the walk" + at);
      CHECK_MSG(a.right == right, "\"!\" bubble's side" + at);
      // a narrow line, so the box never has to slide past the pet to stay on the panel
      BubbleBox b = bubbleBoxAt(petX, top, 40, 1, false, false), bi = bubbleBoxAt(in, top, 40, 1, false, false);
      CHECK_MSG(b.x == bi.x && b.y == bi.y && b.tipX == bi.tipX, "speech bubble outside the walk" + at);
      CHECK_MSG((b.x > in) == right, "speech bubble's side" + at);
      IdleSpot e = idleEmoteAt(petX, top), ei = idleEmoteAt(in, top);
      CHECK_MSG(e.x == ei.x && e.right == ei.right, "emote outside the walk" + at);
      CHECK_MSG(e.right == right, "emote's side" + at);
    }
  }
}

// Both hops stay between the ground and their height, reach it mid-hop, land at the end of
// each hop and repeat exactly, and the idle hop is the shared shape with its own numbers.
TEST(PlayArea, the_idle_and_the_double_tap_hops_share_one_shape) {
  for (uint32_t e = 0; e < 3 * IDLE_HOP_MS; e++) CHECK_EQ(idleHopDy(e), hopDy(e, IDLE_HOP_MS, IDLE_HOP_PX));
  struct Hop { uint32_t ms; int px; } hops[] = { { IDLE_HOP_MS, IDLE_HOP_PX }, { PET_TAP_HOP_MS, PET_TAP_HOP_PX } };
  for (const Hop &h : hops) {
    int lowest = 0;
    for (uint32_t e = 0; e < 2 * h.ms; e++) {
      int dy = hopDy(e, h.ms, h.px);
      CHECK_RANGE(dy, -h.px, 0);
      if (dy < lowest) lowest = dy;
      CHECK_EQ(hopDy(e + h.ms, h.ms, h.px), dy);  // every hop the same, one straight after another
    }
    CHECK_EQ(lowest, -h.px);
    CHECK_EQ(hopDy(h.ms / 2, h.ms, h.px), -h.px);  // the top, mid-hop
    CHECK_EQ(hopDy(0, h.ms, h.px), 0);
    CHECK_EQ(hopDy(h.ms, h.ms, h.px), 0);  // lands, and the next hop starts from the ground
  }
}

// The shape is half a sine, not any symmetric hump: a triangle (or sin squared) has the same
// range, peak, landing and repeat, so those checks above pass it. Literals worked out by hand
// as -(int)(px * sin(pi * e / ms)) at an eighth, a quarter and three quarters of each hop
// (a triangle gives -4/-9/-10 and -5/-11/-12 there, sin squared -2/-9/-10 and -3/-11/-12).
TEST(PlayArea, the_hops_are_half_a_sine) {
  // idle hop, 450 ms / 20 px
  CHECK_EQ(IDLE_HOP_MS, 450);
  CHECK_EQ(IDLE_HOP_PX, 20);
  CHECK_EQ(hopDy(56, 450, 20), -7);    // 20 * sin(pi * 56 / 450)  = 7.62
  CHECK_EQ(hopDy(112, 450, 20), -14);  // 20 * sin(pi * 112 / 450) = 14.09
  CHECK_EQ(hopDy(337, 450, 20), -14);  // 20 * sin(pi * 337 / 450) = 14.19
  // double-tap hop, 350 ms / 24 px
  CHECK_EQ(PET_TAP_HOP_MS, 350);
  CHECK_EQ(PET_TAP_HOP_PX, 24);
  CHECK_EQ(hopDy(43, 350, 24), -9);    // 24 * sin(pi * 43 / 350)  = 9.05
  CHECK_EQ(hopDy(87, 350, 24), -16);   // 24 * sin(pi * 87 / 350)  = 16.88
  CHECK_EQ(hopDy(262, 350, 24), -17);  // 24 * sin(pi * 262 / 350) = 17.04
}
