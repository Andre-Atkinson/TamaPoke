// A life of its own (idleplan.h, #22): the idle planner's weights and picks, the perks' hourly
// cap, the ball and where the emote goes. Pure logic.
#include "framework.h"
#include "../idleplan.h"

static uint16_t bit(uint8_t a) { return (uint16_t)(1u << a); }
// what every shipped sprite has, and the 30 species with gestures on top
static const uint16_t BASE = bit(PMD_IDLE) | bit(PMD_WALKL) | bit(PMD_WALKR) | bit(PMD_SLEEP) | bit(PMD_HURT) |
                             bit(PMD_ATTACK) | bit(PMD_HOP);
static const uint16_t GESTURES = bit(PMD_POSE) | bit(PMD_NOD) | bit(PMD_BREATH);

static IdleIn needs(uint8_t food, uint8_t joy, uint8_t energy, uint8_t weight = 0, int8_t hour = 13,
                    uint16_t acts = BASE | GESTURES, bool calm = false) {
  IdleIn in;
  in.fullness = food;
  in.joy = joy;
  in.energy = energy;
  in.weight = weight;
  in.hour = hour;
  in.acts = acts;
  in.calm = calm;
  return in;
}

struct W {
  int v[IDLE_ACTS];
  int total() const {
    int t = 0;
    for (int x : v) t += x;
    return t;
  }
  // the share of `act`, in per mille
  int share(uint8_t act) const { return total() ? v[act] * 1000 / total() : 0; }
};
static W weights(const IdleIn &in) {
  W w;
  idleWeights(in, w.v);
  return w;
}

// ---------------------------------------------------------------- only what the sprite can show
TEST(IdlePlan, never_picks_an_action_the_sprite_lacks) {
  const int masks[] = {  // int: | promotes
    BASE | GESTURES,
    BASE,                                       // the common case: no Pose, Nod or Breath
    BASE & (uint16_t)~bit(PMD_WALKL),           // no walk to the left
    BASE & (uint16_t)~bit(PMD_WALKR),
    BASE & (uint16_t)~bit(PMD_SLEEP),           // no sleep pose
    bit(PMD_IDLE),                              // IDLE alone
    bit(PMD_IDLE) | bit(PMD_NOD),               // one gesture
    bit(PMD_IDLE) | bit(PMD_BREATH) | bit(PMD_SLEEP),
  };
  const uint8_t levels[] = { 26, 34, 45, 60, 90 };
  int checked = 0;
  for (int mi : masks)
    for (int calm = 0; calm < 2; calm++)
      for (uint8_t lv : levels)
        for (uint32_t seed = 1; seed <= 300; seed++) {
          uint16_t m = (uint16_t)mi;
          IdleIn in = needs(lv, (uint8_t)(116 - lv), lv, (uint8_t)(lv > 60 ? 80 : 0), (int8_t)(seed % 25) - 1, m, calm);
          IdlePick p = idlePick(in, seed * 2654435761u);
          bool walk = (m & bit(PMD_WALKL)) && (m & bit(PMD_WALKR));
          CHECK_MSG(idleAvailable(in, p.act), "picked " + std::string(IDLE_ACT_NAMES[p.act]));
          if (!walk) CHECK(p.act != IDLE_WALK && p.act != IDLE_LOOK && p.act != IDLE_BALL && p.act != IDLE_SEEK);
          if (!(m & bit(PMD_SLEEP))) CHECK(p.act != IDLE_DOZE);
          if (!(m & GESTURES)) CHECK(p.act != IDLE_GESTURE);
          if (p.act == IDLE_GESTURE) CHECK_MSG(m & bit(p.pmdAct), "gesture " + std::to_string(p.pmdAct));
          checked++;
        }
  CHECK_EQ(checked, 8 * 2 * 5 * 300);
}

TEST(IdlePlan, an_activity_the_sprite_lacks_weighs_nothing) {
  W none = weights(needs(30, 90, 30, 0, 23, bit(PMD_IDLE)));  // hungry, happy, tired, at night: IDLE only
  CHECK_EQ(none.v[IDLE_WALK], 0);
  CHECK_EQ(none.v[IDLE_LOOK], 0);
  CHECK_EQ(none.v[IDLE_BALL], 0);
  CHECK_EQ(none.v[IDLE_SEEK], 0);
  CHECK_EQ(none.v[IDLE_DOZE], 0);
  CHECK_EQ(none.v[IDLE_GESTURE], 0);
  CHECK(none.v[IDLE_STAND] > 0);
  CHECK(none.v[IDLE_REST] > 0);
  CHECK(none.v[IDLE_HOP] > 0);
}

// idleAvailable() (the simulator's forced picks) agrees with the weights: what it allows
// can be picked in that repertoire, what it refuses never is
TEST(IdlePlan, only_the_calm_activities_are_available_when_calm) {
  for (int calm = 0; calm < 2; calm++)
    for (int mi : { (int)(BASE | GESTURES), (int)BASE, (int)bit(PMD_IDLE) }) {
      IdleIn in = needs(30, 90, 30, 0, 23, (uint16_t)mi, calm);  // hungry, happy, weary, at night
      W w = weights(in);
      for (uint8_t a = 0; a < IDLE_ACTS; a++) {
        bool ok = idleAvailable(in, a);
        CHECK_MSG(ok == (w.v[a] > 0), std::string(IDLE_ACT_NAMES[a]) + (calm ? " calm" : " full"));
        if (calm && ok) CHECK(idleCalmAct(a));
      }
    }
  CHECK(!idleAvailable(needs(80, 80, 80), IDLE_ACTS));
}

TEST(IdlePlan, the_calm_repertoire_is_a_stroll_a_gesture_or_standing_with_no_emote) {
  int seen[IDLE_ACTS] = {};
  for (uint32_t seed = 1; seed <= 3000; seed++) {
    IdleIn in = needs(30, 90, 30, 80, 23, BASE | GESTURES, true);  // every full-mode term on
    IdlePick p = idlePick(in, seed * 40503u);
    seen[p.act]++;
    CHECK(idleCalmAct(p.act));
    CHECK_EQ(p.emote, (uint8_t)EMO_NONE);
  }
  CHECK(seen[IDLE_STAND] > 0 && seen[IDLE_WALK] > 0 && seen[IDLE_GESTURE] > 0);
  // what the pet did before #22: a stroll 35 %, a gesture 25 % (or standing), standing 40 %
  W c = weights(needs(80, 80, 80, 0, 13, BASE | GESTURES, true));
  CHECK_EQ(c.share(IDLE_WALK), 350);
  CHECK_EQ(c.share(IDLE_GESTURE), 250);
  CHECK_EQ(c.share(IDLE_STAND), 400);
  W n = weights(needs(80, 80, 80, 0, 13, BASE, true));
  CHECK_EQ(n.share(IDLE_WALK), 350);
  CHECK_EQ(n.share(IDLE_STAND), 650);
}

// ---------------------------------------------------------------- needs, hour and weight
TEST(IdlePlan, a_tired_pet_dozes_and_rests_more_and_plays_less) {
  W fresh = weights(needs(80, 80, 80));
  W tired = weights(needs(80, 80, 45));
  W weary = weights(needs(80, 80, 30));
  CHECK(tired.share(IDLE_DOZE) > fresh.share(IDLE_DOZE));
  CHECK(weary.share(IDLE_DOZE) > tired.share(IDLE_DOZE));
  CHECK(tired.share(IDLE_REST) > fresh.share(IDLE_REST));
  CHECK(tired.v[IDLE_HOP] < fresh.v[IDLE_HOP]);
  CHECK(tired.v[IDLE_BALL] < fresh.v[IDLE_BALL]);
  CHECK(weary.v[IDLE_WALK] < tired.v[IDLE_WALK]);
  // the edges: 50 is not tired, 49 is; 35 is not weary, 34 is
  CHECK_EQ(weights(needs(80, 80, 50)).v[IDLE_DOZE], fresh.v[IDLE_DOZE]);
  CHECK(weights(needs(80, 80, 49)).v[IDLE_DOZE] > fresh.v[IDLE_DOZE]);
  CHECK_EQ(weights(needs(80, 80, 35)).v[IDLE_DOZE], weights(needs(80, 80, 49)).v[IDLE_DOZE]);
  CHECK(weights(needs(80, 80, 34)).v[IDLE_DOZE] > weights(needs(80, 80, 35)).v[IDLE_DOZE]);
}

TEST(IdlePlan, a_happy_pet_with_energy_hops_and_plays_ball_more) {
  W calm = weights(needs(80, 70, 80));
  W happy = weights(needs(80, 71, 80));
  CHECK(happy.v[IDLE_HOP] > calm.v[IDLE_HOP]);
  CHECK(happy.v[IDLE_BALL] > calm.v[IDLE_BALL]);
  W happyTired = weights(needs(80, 95, 49));  // happy but tired: no extra play
  CHECK(happyTired.v[IDLE_HOP] < happy.v[IDLE_HOP]);
  CHECK(happyTired.v[IDLE_BALL] < happy.v[IDLE_BALL]);
}

TEST(IdlePlan, a_hungry_pet_goes_to_the_food) {
  CHECK_EQ(weights(needs(40, 80, 80)).v[IDLE_SEEK], 0);
  CHECK(weights(needs(39, 80, 80)).v[IDLE_SEEK] > 0);
  bool sought = false;
  for (uint32_t seed = 1; seed <= 500; seed++) {
    IdlePick p = idlePick(needs(30, 80, 80), seed * 977u);
    if (p.act != IDLE_SEEK) continue;
    sought = true;
    CHECK_RANGE(p.targetX, (int16_t)IDLE_SEEK_X, (int16_t)(IDLE_SEEK_X + 10));  // the food button's end
  }
  CHECK(sought);
}

TEST(IdlePlan, the_night_is_dozier_and_an_unknown_hour_counts_as_day) {
  W day = weights(needs(80, 80, 80, 0, 13));
  for (int8_t h : { 20, 23, 0, 3, 5 }) {
    W night = weights(needs(80, 80, 80, 0, h));
    CHECK_MSG(night.v[IDLE_DOZE] > day.v[IDLE_DOZE], "hour " + std::to_string(h));
    CHECK(night.v[IDLE_BALL] < day.v[IDLE_BALL]);
  }
  for (int8_t h : { 6, 12, 19 }) CHECK_EQ(weights(needs(80, 80, 80, 0, h)).v[IDLE_DOZE], day.v[IDLE_DOZE]);
  W unknown = weights(needs(80, 80, 80, 0, -1));  // no clock, or stale: no hour term at all
  for (int a = 0; a < IDLE_ACTS; a++) CHECK_EQ(unknown.v[a], day.v[a]);
}

// idleInputs() (idlelife.cpp) reads the hour through idleHour(): no hour without a trusted clock
TEST(IdlePlan, the_hour_comes_only_from_a_trusted_clock) {
  const uint32_t at1030 = 1794652200u;  // 2026-11-14 10:30 UTC
  CHECK_EQ((int)idleHour(at1030, false), 10);
  CHECK_EQ((int)idleHour(at1030 + 13 * 3600u, false), 23);
  CHECK_EQ((int)idleHour(at1030 + 14 * 3600u, false), 0);  // wraps at midnight
  CHECK_EQ((int)idleHour(at1030, true), -1);   // stale: reseeded after a power-off, not set yet
  CHECK_EQ((int)idleHour(0, false), -1);       // never read
  CHECK_EQ((int)idleHour(0, true), -1);
  // a night-time clock only counts as night while trusted
  CHECK(idleNight(idleHour(at1030 + 12 * 3600u, false)));
  CHECK(!idleNight(idleHour(at1030 + 12 * 3600u, true)));
}

TEST(IdlePlan, a_chubby_pet_rests_more_and_plays_less) {
  // the same limit as the "chubby" status line (statusMsg in scene.cpp)
  W slim = weights(needs(80, 90, 80, PET_CHUBBY_WEIGHT));
  W chubby = weights(needs(80, 90, 80, PET_CHUBBY_WEIGHT + 1));
  CHECK(chubby.v[IDLE_REST] > slim.v[IDLE_REST]);
  CHECK(chubby.v[IDLE_HOP] < slim.v[IDLE_HOP]);
  CHECK(chubby.v[IDLE_BALL] < slim.v[IDLE_BALL]);
}

// ---------------------------------------------------------------- the pick
TEST(IdlePlan, the_same_seed_gives_the_same_pick) {
  IdleIn in = needs(35, 75, 45, 30, 22);
  bool differs = false;
  IdlePick first = idlePick(in, 1);
  for (uint32_t seed = 1; seed <= 200; seed++) {
    IdlePick a = idlePick(in, seed), b = idlePick(in, seed);
    CHECK_EQ(a.act, b.act);
    CHECK_EQ(a.pmdAct, b.pmdAct);
    CHECK_EQ(a.ms, b.ms);
    CHECK_EQ(a.targetX, b.targetX);
    CHECK_EQ(a.emote, b.emote);
    CHECK_EQ(a.emoteMs, b.emoteMs);
    if (a.act != first.act || a.ms != first.ms) differs = true;
  }
  CHECK(differs);  // and the seed matters
}

TEST(IdlePlan, picks_follow_the_weights) {
  const IdleIn ins[] = { needs(80, 80, 80), needs(30, 90, 30, 80, 23), needs(60, 75, 60, 0, -1, BASE) };
  for (const IdleIn &in : ins) {
    W w = weights(in);
    int seen[IDLE_ACTS] = {};
    const int N = 40000;
    for (uint32_t seed = 1; seed <= (uint32_t)N; seed++) seen[idlePick(in, seed * 2246822519u).act]++;
    for (int a = 0; a < IDLE_ACTS; a++) {
      int expect = w.v[a] * N / w.total();
      if (!w.v[a]) CHECK_EQ(seen[a], 0);
      else CHECK_MSG(seen[a] > expect * 85 / 100 && seen[a] < expect * 115 / 100,
                     std::string(IDLE_ACT_NAMES[a]) + " " + std::to_string(seen[a]) + " vs " + std::to_string(expect));
    }
  }
}

TEST(IdlePlan, each_activity_lasts_a_few_seconds_and_walks_stay_in_range) {
  IdleIn in = needs(30, 90, 30, 0, 13);
  for (uint8_t act = 0; act < IDLE_ACTS; act++)
    for (uint32_t seed = 1; seed <= 300; seed++) {
      IdlePick p = idlePickAct(in, act, seed * 7919u);
      CHECK_EQ(p.act, act);
      switch (act) {
        case IDLE_STAND: CHECK_RANGE(p.ms, 2000u, 4999u); break;
        case IDLE_LOOK: CHECK_EQ(p.ms, (uint32_t)IDLE_LOOK_MS); break;
        case IDLE_WALK:
          CHECK_EQ(p.ms, (uint32_t)IDLE_WALK_MS);
          CHECK_RANGE(p.targetX, (int16_t)IDLE_WALK_MIN, (int16_t)IDLE_WALK_MAX);
          break;
        case IDLE_REST: CHECK_RANGE(p.ms, 5000u, 9999u); CHECK_EQ(p.emote, (uint8_t)EMO_SWEAT); break;
        case IDLE_DOZE:
          CHECK_RANGE(p.ms, 8000u, 15000u);
          CHECK_EQ(p.emote, (uint8_t)EMO_ZZZ);
          CHECK_EQ((uint32_t)p.emoteMs, p.ms);  // the z's for the whole nap
          break;
        case IDLE_HOP: CHECK_RANGE(p.ms, 1800u, 2799u); CHECK_EQ(p.emote, (uint8_t)EMO_NOTE); break;
        case IDLE_BALL: CHECK_RANGE(p.ms, 9000u, 13999u); break;
        case IDLE_GESTURE:
          CHECK_EQ(p.ms, 0u);  // its animation's own length
          CHECK(p.pmdAct == PMD_POSE || p.pmdAct == PMD_NOD || p.pmdAct == PMD_BREATH);
          break;
        case IDLE_SEEK: CHECK_EQ(p.ms, (uint32_t)IDLE_WALK_MS); break;
      }
      if (p.emote) CHECK(p.emoteMs > 0);
    }
  // a rested pet rests with "...", a tired one with a sweat drop
  CHECK_EQ(idlePickAct(needs(80, 80, 80), IDLE_REST, 5).emote, (uint8_t)EMO_DOTS);
}

TEST(IdlePlan, due_is_wrap_safe) {
  CHECK(idleDue(5, 0));                       // 0: pick now
  CHECK(idleDue(0x90000000u, 0));             // even past 24.8 days of uptime
  CHECK(!idleDue(1000, 1001));
  CHECK(idleDue(1001, 1001));
  CHECK(idleDue(1002, 1001));
  CHECK(!idleDue(0xFFFFFF00u, 0x00000100u));  // set just before millis() turns over: not yet
  CHECK(idleDue(0x00000200u, 0x00000100u));
  CHECK(idleDue(0x00000010u, 0xFFFFFF00u));   // set just before, now just after: over
}

TEST(IdlePlan, look_turns_one_way_then_the_other_and_a_hop_stays_low) {
  CHECK_EQ(idleLookAct(IDLE_LOOK_MS), (uint8_t)PMD_WALKL);
  CHECK_EQ(idleLookAct(IDLE_LOOK_MS / 2 + 1), (uint8_t)PMD_WALKL);
  CHECK_EQ(idleLookAct(IDLE_LOOK_MS / 2), (uint8_t)PMD_WALKR);
  CHECK_EQ(idleLookAct(0), (uint8_t)PMD_WALKR);
  int lowest = 0;
  for (uint32_t e = 0; e < 3000; e += 7) {
    int dy = idleHopDy(e);
    CHECK_RANGE(dy, -IDLE_HOP_PX, 0);
    if (dy < lowest) lowest = dy;
  }
  CHECK(lowest <= -IDLE_HOP_PX + 1);  // it does leave the ground
  CHECK_EQ(idleHopDy(0), 0);
  CHECK_EQ(idleHopDy(IDLE_HOP_MS), 0);  // and lands between hops
}

// ---------------------------------------------------------------- the perks' cap
TEST(IdlePerks, at_most_six_of_each_in_an_hour) {
  IdlePerks p;
  uint32_t t = 1000;
  CHECK_EQ(p.grant(IDLE_PERK_ENERGY, t), (uint8_t)3);
  CHECK_EQ(p.grant(IDLE_PERK_ENERGY, t + 60000), (uint8_t)3);
  CHECK_EQ(p.grant(IDLE_PERK_ENERGY, t + 120000), (uint8_t)0);  // the cap: 6
  CHECK_EQ(p.grant(IDLE_PERK_ENERGY, t + IDLE_PERK_WINDOW_MS - 1), (uint8_t)0);
  CHECK_EQ(p.grant(IDLE_PERK_ENERGY, t + IDLE_PERK_WINDOW_MS), (uint8_t)3);  // a new hour
  // the two kinds are counted apart
  CHECK_EQ(p.grant(IDLE_PERK_JOY, t + 10), (uint8_t)3);
  CHECK_EQ(p.grant(IDLE_PERK_JOY, t + 20), (uint8_t)3);
  CHECK_EQ(p.grant(IDLE_PERK_JOY, t + 30), (uint8_t)0);
  CHECK_EQ(p.grant(IDLE_PERK_KINDS, t), (uint8_t)0);  // not a kind
  // in a whole day of non-stop dozing and ball games: 24 x 6 of each, no more
  IdlePerks d;
  int e = 0, j = 0;
  for (uint32_t m = 0; m < 24 * 60; m++) {
    e += d.grant(IDLE_PERK_ENERGY, m * 60000u);
    j += d.grant(IDLE_PERK_JOY, m * 60000u + 30000u);
  }
  CHECK_EQ(e, 24 * IDLE_PERK_CAP);
  CHECK_EQ(j, 24 * IDLE_PERK_CAP);
}

TEST(IdlePerks, the_hour_survives_millis_turning_over) {
  IdlePerks p;
  uint32_t t = 0xFFFFF000u;
  CHECK_EQ(p.grant(IDLE_PERK_JOY, t), (uint8_t)3);
  CHECK_EQ(p.grant(IDLE_PERK_JOY, t + 0x2000u), (uint8_t)3);  // after the wrap, same hour
  CHECK_EQ(p.grant(IDLE_PERK_JOY, t + 0x3000u), (uint8_t)0);
  CHECK_EQ(p.grant(IDLE_PERK_JOY, t + IDLE_PERK_WINDOW_MS), (uint8_t)3);
}

// ---------------------------------------------------------------- the ball
TEST(IdleBall, it_slows_bounces_and_never_leaves_its_range) {
  IdleBall b;
  b.x = 300;
  b.vx = 190;  // the hardest kick
  float lo = b.x, hi = b.x;
  for (int i = 0; i < 400; i++) {
    idleBallRoll(b, 0.1f);
    CHECK_RANGE(b.x, (float)IDLE_BALL_MIN, (float)IDLE_BALL_MAX);
    if (b.x < lo) lo = b.x;
    if (b.x > hi) hi = b.x;
  }
  CHECK_EQ(hi, (float)IDLE_BALL_MAX);       // it reached the end...
  CHECK(b.x < (float)IDLE_BALL_MAX - 20);   // ...and came back off it
  CHECK_EQ(lo, 300.0f);
  CHECK_EQ(b.vx, 0.0f);                // and stopped on the grass
  // a long frame counts as 0.3 s, and a bad state is pulled back into range
  IdleBall c;
  c.x = 200;
  c.vx = 150;
  idleBallRoll(c, 5.0f);
  CHECK(c.x <= 200 + 150 * 0.3f + 0.01f);
  IdleBall d;
  d.x = -1000;
  d.vx = NAN;
  idleBallRoll(d, 0.1f);
  CHECK_EQ(d.x, (float)IDLE_BALL_MIN);
  CHECK_EQ(d.vx, 0.0f);
  IdleBall e;
  e.x = NAN;
  idleBallRoll(e, 0.1f);
  CHECK_EQ(e.x, (float)IDLE_BALL_MIN);
  IdleBall f;  // an infinite speed would bounce forever: it stops
  f.x = 200;
  f.vx = INFINITY;
  idleBallRoll(f, 0.1f);
  CHECK_EQ(f.x, 200.0f);
  CHECK_EQ(f.vx, 0.0f);
}

TEST(IdleBall, the_pet_stands_beside_it_and_kicks_it_away) {
  CHECK_EQ(idleBallSpot(280, 200), 280.0f - IDLE_BALL_REACH);  // ball to its right: stands left of it
  CHECK_EQ(idleBallSpot(200, 280), 200.0f + IDLE_BALL_REACH);
  CHECK_EQ(idleBallSpot(230, 200), 200.0f);  // already within reach: it never backs off
  CHECK_EQ(idleBallSpot(200 + IDLE_BALL_KICK_AT, 200), 200.0f);
  CHECK_EQ(idleBallSpot(201 + IDLE_BALL_KICK_AT, 200), 201.0f + IDLE_BALL_KICK_AT - IDLE_BALL_REACH);
  CHECK_EQ(idleBallSpot(IDLE_BALL_MIN + 10, 100), (float)IDLE_WALK_MIN);  // never past its walk range
  CHECK_EQ(idleBallSpot(IDLE_BALL_MAX - 10, 400), (float)IDLE_WALK_MAX);
  // every spot in the ball's range can be kicked from where the pet may stand
  for (int x = IDLE_BALL_MIN; x <= IDLE_BALL_MAX; x++) {
    IdleBall b;
    b.x = (float)x;
    for (float pet : { 160.0f, 300.0f }) {
      float s = idleBallSpot(b.x, pet);
      CHECK_MSG(idleBallKickable(b, s), "ball at " + std::to_string(x));
      CHECK_RANGE(s, (float)IDLE_WALK_MIN, (float)IDLE_WALK_MAX);
    }
  }
  IdleBall b;
  b.x = 250;
  b.vx = IDLE_BALL_SLOW;  // still rolling: not yet
  CHECK(!idleBallKickable(b, 250 - IDLE_BALL_REACH));
  b.vx = 0;
  CHECK(!idleBallKickable(b, 250 - IDLE_BALL_REACH - 5));  // out of reach
  idleBallKick(b, 214, 0, 0);
  CHECK_EQ(b.vx, (float)IDLE_BALL_KICK_MIN);  // away from the pet: to the right
  idleBallKick(b, 280, IDLE_BALL_KICK_SPAN - 1, 0);
  CHECK_EQ(b.vx, -(float)(IDLE_BALL_KICK_MIN + IDLE_BALL_KICK_SPAN - 1));
}

// ---------------------------------------------------------------- training shows (#76)
// Strength kicks further: up to IDLE_BALL_KICK_TRAIN px/s more at 100, and even the hardest
// kick, from anywhere in the ball's range, stops inside that range after at most one bounce.
TEST(IdleTraining, strength_kicks_further_and_the_ball_stays_in_range) {
  for (uint32_t roll = 0; roll < IDLE_BALL_KICK_SPAN; roll++) {
    IdleBall a, b;
    a.x = b.x = 300;
    idleBallKick(a, 250, roll, 0);
    idleBallKick(b, 250, roll, 100);
    CHECK_EQ(b.vx - a.vx, (float)IDLE_BALL_KICK_TRAIN);
    CHECK_RANGE(a.vx, (float)IDLE_BALL_KICK_MIN, (float)(IDLE_BALL_KICK_MIN + IDLE_BALL_KICK_SPAN - 1));
    CHECK_RANGE(b.vx, (float)IDLE_BALL_KICK_MIN, (float)IDLE_BALL_KICK_MAX);
    IdleBall c;
    c.x = 300;
    idleBallKick(c, 250, roll, 50);
    CHECK_EQ(c.vx - a.vx, (float)(IDLE_BALL_KICK_TRAIN / 2));
    IdleBall d;  // a damaged training value counts as 100
    d.x = 300;
    idleBallKick(d, 250, roll, 255);
    CHECK_EQ(d.vx, b.vx);
  }
  for (uint8_t strength : { (uint8_t)0, (uint8_t)100 }) {
    for (int x = IDLE_BALL_MIN; x <= IDLE_BALL_MAX; x += 5) {
      for (float side : { -1.0f, 1.0f }) {  // the pet on either side of the ball
        IdleBall b;
        b.x = (float)x;
        idleBallKick(b, b.x - side * IDLE_BALL_REACH, IDLE_BALL_KICK_SPAN - 1, strength);
        int bounces = 0;
        float lastDir = b.vx;
        for (int f = 0; f < 400 && b.vx != 0.0f; f++) {  // 25 fps, up to 16 s
          idleBallRoll(b, 0.04f);
          if (b.vx != 0.0f && (b.vx > 0) != (lastDir > 0)) bounces++;
          if (b.vx != 0.0f) lastDir = b.vx;
        }
        std::string where = "strength " + std::to_string(strength) + " from x " + std::to_string(x);
        CHECK_MSG(b.vx == 0.0f, where + ": stopped");
        CHECK_MSG(b.x >= IDLE_BALL_MIN && b.x <= IDLE_BALL_MAX, where + ": inside the ball's range");
        CHECK_MSG(bounces <= 1, where + ": at most one bounce");
      }
    }
  }
}

// Speed quickens the stroll and the chase after the ball, by IDLE_SPEED_BONUS % at 100.
TEST(IdleTraining, speed_quickens_the_walk_and_the_chase) {
  CHECK_EQ(idleWalkPace(false, 0), IDLE_WALK_PX_S);
  CHECK_EQ(idleWalkPace(true, 0), IDLE_BALL_PX_S);
  CHECK_EQ(idleWalkPace(false, 100), IDLE_WALK_PX_S * 1.3f);
  CHECK_EQ(idleWalkPace(true, 100), IDLE_BALL_PX_S * 1.3f);
  CHECK_EQ(idleWalkPace(false, 50), IDLE_WALK_PX_S * 1.15f);
  CHECK_EQ(idleWalkPace(true, 255), idleWalkPace(true, 100));  // a damaged value counts as 100
  float last = 0;
  for (int sp = 0; sp <= 100; sp++) {
    float v = idleWalkPace(false, (uint8_t)sp);
    CHECK(v >= last);
    last = v;
  }
}

// Stamina keeps a happy pet hopping a little longer, and changes nothing else: no weight,
// and no other activity's length (the doze and the ball game pay a perk when they finish).
TEST(IdleTraining, stamina_lengthens_the_hops_only) {
  IdleIn lo = needs(90, 90, 90), hi = needs(90, 90, 90);
  hi.stamina = 100;
  for (uint32_t seed = 1; seed < 200; seed++) {
    for (uint8_t act = 0; act < IDLE_ACTS; act++) {
      if (!idleAvailable(lo, act)) continue;
      IdlePick a = idlePickAct(lo, act, seed), b = idlePickAct(hi, act, seed);
      uint32_t more = act == IDLE_HOP ? IDLE_HOP_STAMINA_MS : 0;
      CHECK_MSG(b.ms == a.ms + more, std::string("act ") + IDLE_ACT_NAMES[act]);
      CHECK_EQ(b.targetX, a.targetX);
      CHECK_EQ(b.emote, a.emote);
    }
    CHECK_EQ(idlePick(lo, seed).act, idlePick(hi, seed).act);  // the same choices
  }
  int wl[IDLE_ACTS], wh[IDLE_ACTS];
  idleWeights(lo, wl);
  idleWeights(hi, wh);
  for (int i = 0; i < IDLE_ACTS; i++) CHECK_EQ(wl[i], wh[i]);
  IdleIn bad = needs(90, 90, 90);
  bad.stamina = 255;  // a damaged value counts as 100
  CHECK_EQ(idlePickAct(bad, IDLE_HOP, 7).ms, idlePickAct(hi, IDLE_HOP, 7).ms);
}

// ---------------------------------------------------------------- where the emote goes
TEST(IdleEmote, it_stays_in_the_play_area_beside_the_head) {
  for (int petX = 100; petX <= 380; petX += 5)
    for (int top = 60; top <= 300; top += 5) {
      IdleSpot s = idleEmoteAt(petX, top);
      int r = IDLE_EMOTE_R;
      CHECK(s.y - r >= ATTN_BUBBLE_Y_MIN);  // under the status line (ends near y 106)
      CHECK(s.y + r + 11 < 312);        // the tail dots too stay above the bars' panel
      // inside the round panel, tail dots included (they reach 19 px across and 27 px down)
      int far = s.right ? s.x + r : s.x - r;
      int dx = far - 233, dy = s.y - 233;
      CHECK(dx * dx + dy * dy < 225 * 225);
      int tx = s.x + (s.right ? -19 : 19) - 233, ty = s.y + r + 11 - 233;
      CHECK(tx * tx + ty * ty < 225 * 225);
      // on the side of the screen with more room, never over the clock badge (x 52-100)
      CHECK_EQ(s.right, (petX < IDLE_WALK_MIN ? IDLE_WALK_MIN : petX > IDLE_WALK_MAX ? IDLE_WALK_MAX : petX) <= 233);
      CHECK(s.x - r > 100);
    }
  IdleSpot tall = idleEmoteAt(233, 169);  // the tallest pet: just over its head
  CHECK_EQ(tall.y, (int16_t)(169 - IDLE_EMOTE_UP));
  CHECK_EQ(tall.x, (int16_t)(233 + IDLE_EMOTE_DX));
  IdleSpot small = idleEmoteAt(233, 266);  // the lowest (a lying Snorlax)
  CHECK_EQ(small.y, (int16_t)(266 - IDLE_EMOTE_UP));
  CHECK_EQ(idleEmoteAt(300, 100).y, (int16_t)IDLE_EMOTE_YMIN);  // a hop can't lift it into the header
  CHECK_EQ(idleEmoteAt(300, 100).x, (int16_t)(300 - IDLE_EMOTE_DX));
  // pmdHeight() finds nothing (0): the head "top" is the ground line, and the bubble still stops
  CHECK_EQ(idleEmoteAt(233, 304).y, (int16_t)IDLE_EMOTE_YMAX);  // above the bars
}
