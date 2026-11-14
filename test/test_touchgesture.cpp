// Main-screen touch resolution and the pet gestures (#21): touchgesture.h, driven with
// synthetic touch traces sampled every 20 ms like handleTouch() polls the CST9217.
#include "framework.h"
#include "../touchgesture.h"
#include <vector>

using TG = TouchGesture;

namespace {

// One finger on the glass. Samples come every 20 ms; held() events are collected.
struct Finger {
  TG &g;
  uint32_t t;
  int x = 0, y = 0;
  bool longOk = true;  // the long-press may fire (main screen, landed in the pet zone)
  std::vector<TouchAct> acts;
  std::vector<uint32_t> strokeTimes;

  Finger(TG &gesture, uint32_t start = 10000) : g(gesture), t(start) {}

  void down(int px, int py, bool live = true) {
    x = px; y = py;
    g.down(t, (int16_t)x, (int16_t)y, live);
  }
  void sample() {
    t += 20;
    TouchAct a = g.held(t, (int16_t)x, (int16_t)y, longOk);
    if (a != TOUCH_NONE) acts.push_back(a);
    if (a == TOUCH_STROKE) strokeTimes.push_back(t);
  }
  // straight line to (px, py) in about ms, one sample per 20 ms
  void moveTo(int px, int py, uint32_t ms) {
    int n = (int)(ms / 20);
    if (n < 1) n = 1;
    int sx = x, sy = y;
    for (int i = 1; i <= n; i++) {
      x = sx + (px - sx) * i / n;
      y = sy + (py - sy) * i / n;
      sample();
    }
  }
  void rest(uint32_t ms) {
    for (uint32_t e = 0; e < ms; e += 20) sample();
  }
  TouchAct up(bool blocked = false) {
    t += 20;  // the poll that sees the finger gone
    return g.lift(t, blocked);
  }
  int count(TouchAct a) const {
    int n = 0;
    for (TouchAct b : acts) n += (b == a);
    return n;
  }
  // back and forth along x between x0 and x0+span, legs ms long, for `legs` legs
  void strokeX(int span, uint32_t legMs, int legs) {
    int a = x, b = x + span;
    for (int i = 0; i < legs; i++) moveTo(i % 2 == 0 ? b : a, y, legMs);
  }
};

// A quick tap on the pet: 80 ms down, then the lift. Returns what the lift resolved.
TouchAct quickTap(TG &g, uint32_t &t, int x = 233, int y = 200) {
  g.down(t, (int16_t)x, (int16_t)y, true);
  for (int i = 0; i < 4; i++) { t += 20; g.held(t, (int16_t)x, (int16_t)y, true); }
  t += 20;
  return g.lift(t, false);
}

}  // namespace

// ------------------------------------------------ what handleTouch() always did

TEST(TouchGesture, quick_swipes_resolve_as_before_on_and_off_the_pet) {
  for (bool live : {true, false}) {
    struct { int x1, y1; TouchAct want; } cases[] = {
      {340, 200, TOUCH_SWIPE_RIGHT}, {120, 200, TOUCH_SWIPE_LEFT},
      {233, 310, TOUCH_SWIPE_DOWN},  {233, 90, TOUCH_SWIPE_UP},
    };
    for (auto &c : cases) {
      TG g;
      Finger f(g);
      f.down(233, 200, live);
      f.moveTo(c.x1, c.y1, 200);
      CHECK_EQ(f.acts.size(), (size_t)0);
      CHECK_EQ(f.up(), c.want);
    }
  }
}

TEST(TouchGesture, a_tap_resolves_on_lift_where_the_finger_landed) {
  TG g;
  Finger f(g);
  f.down(200, 180);
  f.moveTo(230, 150, 100);  // a sloppy tap still inside TAP_SLOP
  CHECK_EQ(f.up(), TOUCH_TAP);
  CHECK_EQ(g.x0, (int16_t)200);
  CHECK_EQ(g.y0, (int16_t)180);
  // a press that lasts TAP_MS is no tap
  Finger s(g);
  s.down(200, 180);
  s.rest(TG::TAP_MS);
  CHECK_EQ(s.up(), TOUCH_NONE);
}

TEST(TouchGesture, the_long_press_fires_once_after_3s_and_the_lift_does_nothing) {
  TG g;
  Finger f(g);
  f.down(233, 200);
  f.rest(TG::LONG_PRESS_MS - 20);
  CHECK_EQ(f.count(TOUCH_LONG_PRESS), 0);
  f.rest(200);
  CHECK_EQ(f.count(TOUCH_LONG_PRESS), 1);
  f.rest(2000);
  CHECK_EQ(f.count(TOUCH_LONG_PRESS), 1);
  CHECK_EQ(f.up(), TOUCH_NONE);
}

TEST(TouchGesture, the_long_press_waits_for_the_callers_ok) {
  TG g;
  Finger f(g);
  f.longOk = false;  // e.g. the gallery is open, or the finger landed off the pet
  f.down(233, 200);
  f.rest(4000);
  CHECK_EQ(f.acts.size(), (size_t)0);
}

TEST(TouchGesture, a_shaky_long_press_still_opens_the_dialog_and_never_strokes) {
  TG g;
  Finger f(g);
  f.down(233, 200);
  // wobble +-9 px on both axes for 3.5 s: well under STROKE_LEG
  for (int i = 0; i < 175; i++) {
    f.x = 233 + ((i / 3) % 2 ? 9 : -9);
    f.y = 200 + ((i / 5) % 2 ? 9 : -9);
    f.sample();
  }
  CHECK_EQ(f.count(TOUCH_STROKE), 0);
  CHECK_EQ(f.count(TOUCH_DRAG_START), 0);
  CHECK_EQ(f.count(TOUCH_LONG_PRESS), 1);
}

TEST(TouchGesture, a_blocked_or_unknown_contact_resolves_nothing) {
  TG g;
  Finger f(g);
  f.down(233, 200);
  CHECK_EQ(f.up(true), TOUCH_NONE);  // the touch only woke the screen
  // a lift with no down (the contact began in a minigame or before a lock): nothing
  CHECK_EQ(g.lift(f.t + 20, false), TOUCH_NONE);
  CHECK_EQ(g.held(f.t + 40, 233, 200, true), TOUCH_NONE);
}

// ------------------------------------------------ stroke

TEST(TouchGesture, back_and_forth_twice_is_a_stroke_that_claims_the_contact) {
  TG g;
  Finger f(g);
  f.down(180, 200);
  f.strokeX(100, 100, 3);  // right, left, right: ends 100 px right, 300 ms in
  CHECK_EQ(f.count(TOUCH_STROKE), 1);
  CHECK(g.claimed());
  // unclaimed, this lift would be a swipe right (the gallery); claimed, it is nothing
  CHECK_EQ(f.up(), TOUCH_NONE);
}

TEST(TouchGesture, one_turn_is_no_stroke_and_resolves_as_before) {
  TG g;
  Finger f(g);
  f.down(180, 200);
  f.strokeX(100, 100, 2);  // out and back: one turn, ends where it landed
  CHECK_EQ(f.count(TOUCH_STROKE), 0);
  CHECK_EQ(f.up(), TOUCH_TAP);
}

TEST(TouchGesture, an_up_and_down_stroke_counts_too) {
  TG g;
  Finger f(g);
  f.down(233, 150);
  f.moveTo(233, 250, 150);
  f.moveTo(233, 150, 150);
  f.moveTo(233, 250, 150);
  CHECK_EQ(f.count(TOUCH_STROKE), 1);
  CHECK_EQ(f.up(), TOUCH_NONE);  // not the card or the clock
}

TEST(TouchGesture, short_legs_never_stroke) {
  TG g;
  Finger f(g);
  f.down(200, 200);
  for (int i = 0; i < 40; i++) f.moveTo(i % 2 ? 200 : 200 + TG::STROKE_LEG - 1, 200, 60);
  CHECK_EQ(f.count(TOUCH_STROKE), 0);
}

TEST(TouchGesture, stroking_on_pays_one_pat_per_STROKE_EVERY_MS_at_most) {
  TG g;
  Finger f(g);
  f.down(160, 200);
  f.strokeX(120, 120, 50);  // 6 s of brisk stroking: a turn every 120 ms
  CHECK(f.strokeTimes.size() >= 4);  // it keeps rewarding a long stroke
  CHECK(f.strokeTimes.size() <= 6000 / TG::STROKE_EVERY_MS + 1);
  for (size_t i = 1; i < f.strokeTimes.size(); i++)
    CHECK(f.strokeTimes[i] - f.strokeTimes[i - 1] >= TG::STROKE_EVERY_MS);
  CHECK_EQ(f.up(), TOUCH_NONE);
}

TEST(TouchGesture, the_stroke_limit_holds_across_contacts) {
  TG g;
  Finger a(g);
  a.down(160, 200);
  a.strokeX(100, 100, 3);
  CHECK_EQ(a.count(TOUCH_STROKE), 1);
  a.up();
  // lift and stroke again at once: a stroke (no swipe, no tap), but no pat before the limit
  Finger b(g, a.t + 40);
  b.down(160, 200);
  b.strokeX(100, 100, 3);
  CHECK_EQ(b.count(TOUCH_STROKE), 0);
  CHECK(g.claimed());
  b.strokeX(100, 100, 12);  // keep going past the limit
  CHECK_EQ(b.count(TOUCH_STROKE), 1);
  CHECK(b.strokeTimes[0] - a.strokeTimes[0] >= TG::STROKE_EVERY_MS);
  CHECK_EQ(b.up(), TOUCH_NONE);
}

TEST(TouchGesture, no_stroke_off_the_pet_or_with_gestures_off) {
  TG g;
  Finger f(g);
  f.down(180, 200, false);
  f.strokeX(100, 100, 3);
  CHECK_EQ(f.count(TOUCH_STROKE), 0);
  CHECK_EQ(f.up(), TOUCH_SWIPE_RIGHT);  // what it always was
}

// ------------------------------------------------ hold then drag

TEST(TouchGesture, rest_then_slide_sideways_drags_and_the_pet_follows) {
  TG g;
  Finger f(g);
  f.down(200, 220);
  f.rest(TG::DRAG_REST_MS + 40);
  f.moveTo(320, 225, 600);
  CHECK_EQ(f.count(TOUCH_DRAG_START), 1);
  CHECK(f.count(TOUCH_DRAG_MOVE) > 10);
  CHECK_EQ(g.dragX, (int16_t)320);
  CHECK(g.dragging(f.t));
  f.moveTo(150, 225, 600);  // and back the other way
  CHECK_EQ(f.count(TOUCH_DRAG_START), 1);  // one pat per contact
  CHECK_EQ(g.dragX, (int16_t)150);
  CHECK_EQ(f.up(), TOUCH_NONE);
  CHECK(!g.dragging(f.t));
}

TEST(TouchGesture, the_drag_starts_at_DRAG_START_px_not_before) {
  TG g;
  Finger f(g);
  f.down(200, 220);
  f.rest(TG::DRAG_REST_MS + 40);
  f.moveTo(200 + TG::DRAG_START - 1, 220, 200);
  CHECK_EQ(f.count(TOUCH_DRAG_START), 0);
  CHECK_EQ(f.up(), TOUCH_TAP);  // still a tap: drags never take one
  Finger h(g, f.t + 1000);
  h.down(200, 220);
  h.rest(TG::DRAG_REST_MS + 40);
  h.moveTo(200 + TG::DRAG_START, 220, 200);
  CHECK_EQ(h.count(TOUCH_DRAG_START), 1);
}

TEST(TouchGesture, a_swipe_never_drags) {
  TG g;
  Finger f(g);
  f.down(200, 220);
  f.moveTo(320, 220, 500);  // no rest: moving at once
  CHECK_EQ(f.count(TOUCH_DRAG_START), 0);
  CHECK_EQ(f.up(), TOUCH_SWIPE_RIGHT);
}

TEST(TouchGesture, moving_during_the_rest_rules_the_drag_out) {
  TG g;
  Finger f(g);
  f.down(200, 220);
  f.moveTo(200 + TG::DRAG_REST_SLOP + 2, 220, 100);  // too far, too soon
  f.rest(400);
  f.moveTo(300, 220, 300);
  CHECK_EQ(f.count(TOUCH_DRAG_START), 0);
}

TEST(TouchGesture, the_rest_must_last_DRAG_REST_MS) {
  TG g;
  Finger f(g);
  f.down(200, 220);
  f.rest(TG::DRAG_REST_MS - 60);
  f.x = 200 + TG::DRAG_START + 10;  // a jump before the rest is over
  f.sample();
  CHECK_EQ(f.count(TOUCH_DRAG_START), 0);
}

TEST(TouchGesture, a_vertical_slide_after_a_rest_is_no_drag) {
  TG g;
  Finger f(g);
  f.down(233, 150);
  f.rest(TG::DRAG_REST_MS + 40);
  f.moveTo(233 + TG::DRAG_START + 10, 260, 200);  // past DRAG_START sideways, but mostly down
  CHECK_EQ(f.count(TOUCH_DRAG_START), 0);
  CHECK_EQ(f.up(), TOUCH_SWIPE_DOWN);  // as before: under SWIPE_MS in all
}

TEST(TouchGesture, a_drag_never_becomes_a_long_press_a_swipe_or_a_tap) {
  TG g;
  Finger f(g);
  f.down(200, 220);
  f.rest(TG::DRAG_REST_MS + 20);
  f.moveTo(260, 220, 200);
  CHECK_EQ(f.count(TOUCH_DRAG_START), 1);
  f.moveTo(205, 220, 200);  // back by the landing point, then still for 4 s
  f.rest(4000);
  CHECK_EQ(f.count(TOUCH_LONG_PRESS), 0);
  CHECK_EQ(f.up(), TOUCH_NONE);
  // and a quick one: rest, then 100 px inside SWIPE_MS
  Finger q(g, f.t + 1000);
  q.down(180, 220);
  q.rest(TG::DRAG_REST_MS + 20);
  q.moveTo(300, 220, 200);
  CHECK_EQ(q.count(TOUCH_DRAG_START), 1);
  CHECK_EQ(q.up(), TOUCH_NONE);
}

TEST(TouchGesture, a_long_press_that_fired_first_keeps_the_contact) {
  TG g;
  Finger f(g);
  f.down(233, 200);
  f.rest(TG::LONG_PRESS_MS + 100);
  CHECK_EQ(f.count(TOUCH_LONG_PRESS), 1);
  f.moveTo(320, 200, 300);
  f.strokeX(-100, 100, 4);
  CHECK_EQ(f.count(TOUCH_DRAG_START), 0);
  CHECK_EQ(f.count(TOUCH_STROKE), 0);
  CHECK_EQ(f.up(), TOUCH_NONE);
}

TEST(TouchGesture, a_stroke_and_a_drag_never_share_a_contact) {
  TG g;
  Finger f(g);
  f.down(200, 220);
  f.rest(TG::DRAG_REST_MS + 20);
  f.moveTo(280, 220, 200);
  CHECK_EQ(f.count(TOUCH_DRAG_START), 1);
  f.strokeX(-80, 100, 6);
  CHECK_EQ(f.count(TOUCH_STROKE), 0);
  // and the other way round: rest (so a drag could start), a small stroke near the landing
  // point, then a slide
  Finger s(g, f.t + 5000);
  s.down(200, 200);
  s.rest(TG::DRAG_REST_MS + 20);
  s.moveTo(230, 200, 100);
  s.moveTo(205, 200, 100);
  s.moveTo(230, 200, 100);
  CHECK_EQ(s.count(TOUCH_STROKE), 1);
  CHECK_EQ(s.count(TOUCH_DRAG_START), 0);
  s.moveTo(330, 200, 300);
  CHECK_EQ(s.count(TOUCH_DRAG_START), 0);
  CHECK_EQ(s.up(), TOUCH_NONE);
}

TEST(TouchGesture, no_drag_off_the_pet_or_with_gestures_off) {
  TG g;
  Finger f(g);
  f.down(200, 220, false);
  f.rest(TG::DRAG_REST_MS + 40);
  f.moveTo(320, 220, 300);
  CHECK_EQ(f.count(TOUCH_DRAG_START), 0);
  CHECK(!g.dragging(f.t));
}

TEST(TouchGesture, a_drag_without_samples_goes_stale) {
  TG g;
  Finger f(g);
  f.down(200, 220);
  f.rest(TG::DRAG_REST_MS + 20);
  f.moveTo(280, 220, 200);
  CHECK(g.dragging(f.t));
  // a lock takes the touch chip mid-drag: no more samples and no lift
  CHECK(g.dragging(f.t + TG::DRAG_STALE_MS - 1));
  CHECK(!g.dragging(f.t + TG::DRAG_STALE_MS));
}

// ------------------------------------------------ taps on the pet

TEST(TouchGesture, a_single_tap_is_a_pat_and_a_slow_second_one_too) {
  TG g;
  uint32_t t = 5000;
  CHECK_EQ(quickTap(g, t), TOUCH_TAP);
  CHECK_EQ(g.petTap(t), PET_TAP_PAT);
  t += TG::QUICK_TAP_MS + 1 - 100;  // the next lift lands QUICK_TAP_MS + 1 after this one
  CHECK_EQ(quickTap(g, t), TOUCH_TAP);
  CHECK_EQ(g.petTap(t), PET_TAP_PAT);
}

TEST(TouchGesture, the_second_quick_tap_is_a_double_tap) {
  TG g;
  uint32_t t = 5000;
  quickTap(g, t);
  CHECK_EQ(g.petTap(t), PET_TAP_PAT);  // instant: the first tap is a pat at once
  t += TG::QUICK_TAP_MS - 100;         // lift to lift: exactly QUICK_TAP_MS
  quickTap(g, t);
  CHECK_EQ(g.petTap(t), PET_TAP_DOUBLE);
  CHECK(petTapEarnsPat(PET_TAP_DOUBLE));
}

TEST(TouchGesture, quick_taps_go_pat_double_pats_tickle_then_huff) {
  TG g;
  uint32_t t = 5000;
  PetTap want[] = {PET_TAP_PAT,    PET_TAP_DOUBLE, PET_TAP_PAT,    PET_TAP_PAT,
                   PET_TAP_PAT,    PET_TAP_TICKLE, PET_TAP_TICKLE, PET_TAP_TICKLE,
                   PET_TAP_TICKLE, PET_TAP_TICKLE, PET_TAP_TICKLE, PET_TAP_HUFF,
                   PET_TAP_HUFF,   PET_TAP_HUFF};
  int pats = 0, n = 0;
  for (PetTap w : want) {
    if (n++) t += 300;
    quickTap(g, t);
    PetTap got = g.petTap(t);
    CHECK_EQ(got, w);
    pats += petTapEarnsPat(got);
  }
  CHECK_EQ(pats, TG::HUFF_TAPS - 1);  // the huff earns nothing
  CHECK(!petTapEarnsPat(PET_TAP_HUFF));
  // a pause ends the huff: the next tap is a plain pat again
  t += TG::QUICK_TAP_MS + 1;
  quickTap(g, t);
  CHECK_EQ(g.petTap(t), PET_TAP_PAT);
}

TEST(TouchGesture, six_taps_at_the_simulators_pace_tickle) {
  // tools/sim.sh's tap: 80 ms down, 400 ms settle, so lifts land 480 ms apart
  TG g;
  uint32_t t = 5000;
  PetTap last = PET_TAP_PAT;
  for (int i = 0; i < TG::TICKLE_TAPS; i++) {
    if (i) t += 480 - 100;
    quickTap(g, t);
    last = g.petTap(t);
  }
  CHECK_EQ(last, PET_TAP_TICKLE);
}

TEST(TouchGesture, a_slow_tap_restarts_the_count) {
  TG g;
  uint32_t t = 5000;
  for (int i = 0; i < 5; i++) {
    if (i) t += 300;
    quickTap(g, t);
    g.petTap(t);
  }
  t += TG::QUICK_TAP_MS + 1;
  quickTap(g, t);
  CHECK_EQ(g.petTap(t), PET_TAP_PAT);  // not the tickle the 6th quick tap would be
  t += 300;
  quickTap(g, t);
  CHECK_EQ(g.petTap(t), PET_TAP_DOUBLE);
}

TEST(TouchGesture, any_other_contact_breaks_a_run_of_taps) {
  TG g;
  uint32_t t = 5000;
  quickTap(g, t);
  g.petTap(t);
  // a tap that was not on the pet (a button): onTap never calls petTap
  t += 100;
  quickTap(g, t);
  t += 100;
  quickTap(g, t);
  CHECK_EQ(g.petTap(t), PET_TAP_PAT);  // within QUICK_TAP_MS, but no double tap
  // a swipe in between does the same
  t += 50;
  Finger f(g, t);
  f.down(200, 200);
  f.moveTo(320, 200, 120);
  CHECK_EQ(f.up(), TOUCH_SWIPE_RIGHT);
  t = f.t + 60;
  quickTap(g, t);
  CHECK_EQ(g.petTap(t), PET_TAP_PAT);
}

// ------------------------------------------------ when the gestures are live

TEST(TouchGesture, every_gate_turns_the_gestures_off) {
  PetGestureGate open;
  CHECK(petGesturesLive(open));
  bool PetGestureGate::*flags[] = {
    &PetGestureGate::dark,     &PetGestureGate::egg,      &PetGestureGate::ceremony,
    &PetGestureGate::sleeping, &PetGestureGate::evolving, &PetGestureGate::eating,
    &PetGestureGate::bathing,  &PetGestureGate::choosing, &PetGestureGate::otherScreen,
    &PetGestureGate::dialog,
  };
  for (auto f : flags) {
    PetGestureGate g;
    g.*f = true;
    CHECK(!petGesturesLive(g));
  }
}
