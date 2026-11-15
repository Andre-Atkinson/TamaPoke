#pragma once
#include <math.h>
#include <stdint.h>
#include "attention.h"  // ATTN_BUBBLE_Y: the play area's top, under the status line
#include "pet.h"        // PET_CHUBBY_WEIGHT
#include "sdmon.h"      // PMD_* (the sprite's actions)

// A life of its own (#22): what the pet does on the main screen while nobody touches it.
// Pure logic (no drawing, no clock or millis() reads, no random()) so test/test_idleplan.cpp
// can drive it. idlelife.cpp runs it from drawPetPMD() (scene.cpp), keeps the activity in
// PetBeh (scene.h) and draws the ball and the emotes.
//
// Every few seconds the next activity is picked by weighted chance (idlePick). The weights
// come from the needs, the hour and the weight (idleWeights), and an activity the species'
// sprite can't show is never picked. Two repertoires:
//   - full: the main screen is free and fully lit (petSceneFree() in TamaPoke.ino). Every
//     activity, the emotes over its head, and the small perks of a doze or a ball game;
//   - calm: the screen is dimmed, or a dialog is over the pet. What the pet always did
//     (stroll, a Pose/Nod/Breath where the species has one, or stand), no emotes, no perks.
// Any touch, PWR or BOOT (wakeUI) stops the activity (idleInterrupt): the pet stands and
// looks up for IDLE_ATTEND_MS, then picks again. Another screen, a dialog, a meal or sleep
// are opened and left by hand, and a call (#9) only reaches a sad pet, which shows HURT and
// runs the planner only after a dragging finger let go of it (scene.cpp), and then in the calm
// repertoire only, so all of them pass through a touch too.
//
// Idea from Mu-Muu/TamaPoke's behNext()/behEnd() and Pet::selfRest()/selfPlay() (MIT),
// re-implemented here offline, without the weather. Personalities (#25) can add weights.
//
// Sprite facts (the 151 shipped species, 2026-11-15): every one has IDLE, WALKL, WALKR,
// SLEEP, HURT, ATTACK and HOP; only 30 have POSE, NOD or BREATH. PMD_SIT shows the pet's
// back (#21 uses it for the huff), so "sit" is a rest in place on IDLE for every species.
// PMD_HOP is never drawn: its tall canvas makes drawPmdActM() shrink most pets mid-hop, so
// a hop is IDLE bounced (like #21's double tap).

enum IdleAct : uint8_t {
  IDLE_STAND = 0,  // its breathing IDLE loop
  IDLE_LOOK,       // looks one way, then the other (still WALKL and WALKR frames)
  IDLE_WALK,       // strolls to a spot
  IDLE_REST,       // stays put a while with "..." or, tired, a sweat drop ("sit")
  IDLE_DOZE,       // a daytime nap: the SLEEP pose, a small "z" bubble, any touch wakes it
  IDLE_HOP,        // happy hops: IDLE bounced
  IDLE_BALL,       // chases a ball and kicks it about
  IDLE_GESTURE,    // a POSE, NOD or BREATH, where the species has one
  IDLE_SEEK,       // hungry: walks to the food button and wishes for food there
  IDLE_ACTS
};

// for the simulator's "idle ACT" step (sim/script.cpp); the order of IdleAct
inline const char *const IDLE_ACT_NAMES[IDLE_ACTS] = { "stand", "look", "walk", "rest", "doze",
                                                       "hop", "ball", "gesture", "seek" };

// The emote in a small thought bubble beside its head, drawn as shapes (no font: the same
// in every language).
enum IdleEmote : uint8_t {
  EMO_NONE = 0,
  EMO_HEART,     // content
  EMO_NOTE,      // humming, hopping
  EMO_QUESTION,  // looking around
  EMO_DOTS,      // "..." resting
  EMO_SWEAT,     // resting while tired
  EMO_ZZZ,       // dozing
  EMO_FOOD,      // at the food button
  EMO_COUNT
};

// thresholds (needs are 0-100; under ATTN_LOW the pet is sad and gets only the calm repertoire)
#define IDLE_TIRED 50    // energy under this: dozes and rests more, plays less
#define IDLE_WEARY 35    // ...and under this, much more
#define IDLE_HAPPY 70    // joy over this, with energy left: hops and plays more
#define IDLE_HUNGRY 40   // fullness under this: goes to the food button
#define IDLE_CHUBBY PET_CHUBBY_WEIGHT  // weight over this (the "chubby" status): rests more, plays less
#define IDLE_NIGHT_FROM 20  // the night scene (render(): hour < 6 or >= 20): dozier
#define IDLE_NIGHT_TO 6

// where it goes (x of the pet's centre); scene.cpp's walk and attnBubbleAt() use this range
#define IDLE_WALK_MIN 150
#define IDLE_WALK_MAX 325
#define IDLE_SEEK_X IDLE_WALK_MIN  // the food button (x 140) is at the walk's left end

// how long (ms)
#define IDLE_ATTEND_MS 1500   // after a touch it stands and looks up this long
#define IDLE_WALK_MS 15000    // a stroll gives up after this
#define IDLE_LOOK_MS 2600
#define IDLE_WISH_MS 2500     // at the food button, wishing
#define IDLE_EMOTE_MS 1600

struct IdleIn {
  uint8_t fullness = 80, joy = 80, energy = 80;  // the needs, 0-100
  uint8_t weight = 0;                            // 0-100
  int8_t hour = -1;     // 0-23; -1 = unknown (no clock, or stale after a power-off): no hour term
  uint16_t acts = 0;    // bit a = the sprite has PMD action a (PmdMon::has)
  bool calm = false;    // dimmed or a dialog open: the calm repertoire only
};

struct IdlePick {
  uint8_t act = IDLE_STAND;  // IdleAct
  uint8_t pmdAct = PMD_IDLE; // IDLE_GESTURE: PMD_POSE, PMD_NOD or PMD_BREATH
  uint32_t ms = 0;           // how long; 0 for a gesture (its animation's own length)
  int16_t targetX = 0;       // IDLE_WALK, IDLE_SEEK: where it goes
  uint8_t emote = EMO_NONE;  // shown from the start, for emoteMs
  uint16_t emoteMs = 0;
};

// Small seeded generator: a given seed always gives the same pick (xorshift32).
struct IdleRng {
  uint32_t s;
  explicit IdleRng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

inline bool idleHas(const IdleIn &in, uint8_t pmdAct) { return pmdAct < 16 && (in.acts >> pmdAct) & 1u; }
inline bool idleCanWalk(const IdleIn &in) { return idleHas(in, PMD_WALKL) && idleHas(in, PMD_WALKR); }
// The hour of day the planner may use, from the clock's last reading (epoch, 0 = never read)
// and whether it is stale (a reseed after a power-off, not set yet): -1, no hour, without a
// trusted clock. The same hour as sceneHour() (scene.cpp) otherwise.
inline int8_t idleHour(uint32_t epoch, bool stale) {
  return (!epoch || stale) ? (int8_t)-1 : (int8_t)((epoch / 3600) % 24);
}
inline bool idleNight(int8_t hour) {
  return hour >= 0 && (hour >= IDLE_NIGHT_FROM || hour < IDLE_NIGHT_TO);
}

// The gestures the sprite has, in a fixed order; returns how many.
inline uint8_t idleGestures(const IdleIn &in, uint8_t out[3]) {
  static const uint8_t G[3] = { PMD_POSE, PMD_NOD, PMD_BREATH };
  uint8_t n = 0;
  for (uint8_t g : G)
    if (idleHas(in, g)) out[n++] = g;
  return n;
}

// The calm repertoire: what the pet did before #22, and what it still does when dimmed.
inline bool idleCalmAct(uint8_t act) { return act == IDLE_STAND || act == IDLE_WALK || act == IDLE_GESTURE; }

// The chance of each activity, out of their sum. Zero for anything the sprite can't show.
inline void idleWeights(const IdleIn &in, int w[IDLE_ACTS]) {
  for (int i = 0; i < IDLE_ACTS; i++) w[i] = 0;
  bool walk = idleCanWalk(in);
  bool doze = idleHas(in, PMD_SLEEP);
  uint8_t g[3];
  bool gest = idleGestures(in, g) > 0;
  if (in.calm) {  // the old behNext(): stroll 35 %, a gesture 25 % (else stand), stand 40 %
    w[IDLE_STAND] = gest ? 40 : 65;
    w[IDLE_WALK] = walk ? 35 : 0;
    w[IDLE_GESTURE] = gest ? 25 : 0;
    return;
  }
  w[IDLE_STAND] = 14;
  w[IDLE_LOOK] = walk ? 10 : 0;
  w[IDLE_WALK] = walk ? 20 : 0;
  w[IDLE_GESTURE] = gest ? 14 : 0;
  w[IDLE_HOP] = 7;
  w[IDLE_BALL] = walk ? 6 : 0;
  w[IDLE_DOZE] = doze ? 2 : 0;
  w[IDLE_REST] = 4;
  if (in.energy < IDLE_TIRED) {
    if (doze) w[IDLE_DOZE] += 10;
    w[IDLE_REST] += 5;
    w[IDLE_HOP] /= 2;
    w[IDLE_BALL] /= 3;
  }
  if (in.energy < IDLE_WEARY) {
    if (doze) w[IDLE_DOZE] += 20;
    w[IDLE_WALK] /= 2;
  }
  if (in.joy > IDLE_HAPPY && in.energy >= IDLE_TIRED) {
    w[IDLE_HOP] += 6;
    if (walk) w[IDLE_BALL] += 6;
  }
  if (in.fullness < IDLE_HUNGRY && walk) w[IDLE_SEEK] += 15;
  if (idleNight(in.hour)) {
    if (doze) w[IDLE_DOZE] += 8;
    w[IDLE_BALL] /= 2;
  }
  if (in.weight > IDLE_CHUBBY) {
    w[IDLE_REST] += 6;
    w[IDLE_HOP] /= 2;
    w[IDLE_BALL] /= 2;
  }
}

// Whether the sprite can show `act` in this repertoire (its weight can be above zero).
inline bool idleAvailable(const IdleIn &in, uint8_t act) {
  if (act >= IDLE_ACTS) return false;
  uint8_t g[3];
  switch (act) {
    case IDLE_STAND: return true;
    case IDLE_LOOK:
    case IDLE_WALK:
    case IDLE_BALL:
    case IDLE_SEEK: return idleCanWalk(in) && (!in.calm || act == IDLE_WALK);
    case IDLE_DOZE: return idleHas(in, PMD_SLEEP) && !in.calm;
    case IDLE_GESTURE: return idleGestures(in, g) > 0;
    default: return !in.calm;  // rest and hop: IDLE only
  }
}

// The details of `act` (which should be idleAvailable): how long, where to, which gesture,
// and the emote it starts with (none in the calm repertoire).
inline IdlePick idlePickAct(const IdleIn &in, uint8_t act, uint32_t seed) {
  IdleRng r(seed);
  IdlePick p;
  p.act = act < IDLE_ACTS ? act : (uint8_t)IDLE_STAND;
  switch (p.act) {
    case IDLE_LOOK:
      p.ms = IDLE_LOOK_MS;
      if (r.below(2) == 0) p.emote = EMO_QUESTION;
      break;
    case IDLE_WALK:
      p.ms = IDLE_WALK_MS;
      p.targetX = (int16_t)(IDLE_WALK_MIN + r.below(IDLE_WALK_MAX - IDLE_WALK_MIN + 1));
      break;
    case IDLE_REST:
      p.ms = 5000 + r.below(5000);
      p.emote = in.energy < IDLE_TIRED ? EMO_SWEAT : EMO_DOTS;
      p.emoteMs = 2000;
      break;
    case IDLE_DOZE:
      p.ms = 8000 + r.below(7001);  // 8-15 s
      p.emote = EMO_ZZZ;
      break;
    case IDLE_HOP:
      p.ms = 1800 + r.below(1000);
      p.emote = EMO_NOTE;
      break;
    case IDLE_BALL:
      p.ms = 9000 + r.below(5000);
      break;
    case IDLE_GESTURE: {
      uint8_t g[3], n = idleGestures(in, g);
      p.pmdAct = n ? g[r.below(n)] : (uint8_t)PMD_IDLE;
      if (r.below(3) == 0) p.emote = EMO_HEART;
      break;
    }
    case IDLE_SEEK:
      p.ms = IDLE_WALK_MS;  // the walk there; then IDLE_WISH_MS wishing (idlelife.cpp)
      p.targetX = (int16_t)(IDLE_SEEK_X + r.below(11));
      break;
    default:  // IDLE_STAND: now and then it says how it feels
      p.ms = 2000 + r.below(3000);
      if (r.below(3) == 0) p.emote = in.joy > IDLE_HAPPY ? EMO_HEART : EMO_NOTE;
      break;
  }
  if (in.calm) p.emote = EMO_NONE;
  if (p.emote && !p.emoteMs) p.emoteMs = p.emote == EMO_ZZZ ? (uint16_t)p.ms : IDLE_EMOTE_MS;
  return p;
}

// The next activity, by weighted chance. Deterministic for a given seed.
inline IdlePick idlePick(const IdleIn &in, uint32_t seed) {
  int w[IDLE_ACTS];
  idleWeights(in, w);
  int total = 0;
  for (int v : w) total += v;
  IdleRng r(seed);
  int roll = (int)r.below(total > 0 ? (uint32_t)total : 1u);
  uint8_t act = 0;
  while (act < IDLE_ACTS - 1 && roll >= w[act]) roll -= w[act++];
  if (total <= 0 || w[act] <= 0) act = IDLE_STAND;
  return idlePickAct(in, act, r.next());
}

// The activity that runs until `until` is over. Wrap-safe (millis() turns over at about
// 49.7 days); until 0 means "pick now".
inline bool idleDue(uint32_t now, uint32_t until) { return until == 0 || (int32_t)(now - until) >= 0; }

// LOOK: which still frame it holds with `leftMs` to go (one way, then the other).
inline uint8_t idleLookAct(uint32_t leftMs) { return leftMs > IDLE_LOOK_MS / 2 ? PMD_WALKL : PMD_WALKR; }

// HOP: how far up it is (px, 0 or negative), `elapsed` ms into the hop.
#define IDLE_HOP_MS 450  // one hop
#define IDLE_HOP_PX 20   // its height: the tallest pet's head stays under the status line
inline int idleHopDy(uint32_t elapsed) {
  return -(int)(IDLE_HOP_PX * sinf((float)(elapsed % IDLE_HOP_MS) * 3.14159265f / IDLE_HOP_MS));
}

// ---------------------------------------------------------------- the perks
// A finished doze gives a little energy, a finished ball game a little joy, at most
// IDLE_PERK_CAP of each in an hour, so they never replace care: awake, energy and joy each
// drain 60 an hour (30 on the relaxed pace). Nothing lowers a stat. The planner runs only
// on a lit, free main screen, so in practice only in the minute or so after a touch.
//
// The cap lives in RAM: a reboot gives one fresh hour's allowance (+6 each at most), and
// the save is untouched (no new NVS key). Self-care while charging (#24) needs 10 untouched
// minutes, when the screen has long been off and the planner is not running: the two never
// overlap, and self-care stops at half anyway.
#define IDLE_PERK 3
#define IDLE_PERK_CAP 6
#define IDLE_PERK_WINDOW_MS 3600000UL
enum : uint8_t { IDLE_PERK_ENERGY = 0, IDLE_PERK_JOY, IDLE_PERK_KINDS };

struct IdlePerks {
  uint8_t given[IDLE_PERK_KINDS] = { 0, 0 };
  uint32_t from[IDLE_PERK_KINDS] = { 0, 0 };
  bool open[IDLE_PERK_KINDS] = { false, false };

  // How much the stat may go up now (IDLE_PERK, less near the cap, 0 once the hour's cap is
  // spent), and counts it. The hour starts at the first perk of that kind.
  uint8_t grant(uint8_t kind, uint32_t now) {
    if (kind >= IDLE_PERK_KINDS) return 0;
    if (!open[kind] || now - from[kind] >= IDLE_PERK_WINDOW_MS) {
      open[kind] = true;
      from[kind] = now;
      given[kind] = 0;
    }
    uint8_t room = given[kind] < IDLE_PERK_CAP ? (uint8_t)(IDLE_PERK_CAP - given[kind]) : 0;
    uint8_t g = room < IDLE_PERK ? room : (uint8_t)IDLE_PERK;
    given[kind] += g;
    return g;
  }
};

// ---------------------------------------------------------------- the ball
// It rolls on the ground in front of the pet, slows on the grass and bounces off the ends
// of its range. The pet walks up beside it and kicks it away once it has nearly stopped.
#define IDLE_BALL_MIN (IDLE_WALK_MIN - 30)  // within kicking reach of the walk's ends
#define IDLE_BALL_MAX (IDLE_WALK_MAX + 30)
#define IDLE_BALL_REACH 46     // px from the pet's centre: where it stands to kick (by its feet)
#define IDLE_BALL_SLOW 25.0f   // px/s: slow enough to kick again
#define IDLE_BALL_DRAG 110.0f  // px/s each second the grass takes off
#define IDLE_BALL_BOUNCE 0.6f  // speed kept off an end
#define IDLE_BALL_PX_S 50.0f   // the pet's pace after the ball (a stroll is 30 px/s)
#define IDLE_BALL_KICK_MIN 110 // a kick's speed, px/s: rolls about 55-160 px
#define IDLE_BALL_KICK_SPAN 80

struct IdleBall {
  float x = 233, vx = 0;
};

// Rolls the ball for dt seconds (a long frame counts as at most 0.3 s). It always ends inside
// [IDLE_BALL_MIN, IDLE_BALL_MAX], even from a bad x.
inline void idleBallRoll(IdleBall &b, float dt) {
  if (!(dt > 0.0f)) dt = 0.0f;
  if (dt > 0.3f) dt = 0.3f;
  if (!(fabsf(b.vx) < 1000.0f)) b.vx = 0.0f;  // also catches NaN
  b.x += b.vx * dt;
  float sp = fabsf(b.vx) - IDLE_BALL_DRAG * dt;
  b.vx = sp > 0.0f ? (b.vx > 0.0f ? sp : -sp) : 0.0f;
  if (!(b.x >= (float)IDLE_BALL_MIN)) {
    b.x = IDLE_BALL_MIN;
    b.vx = fabsf(b.vx) * IDLE_BALL_BOUNCE;
  } else if (b.x > (float)IDLE_BALL_MAX) {
    b.x = IDLE_BALL_MAX;
    b.vx = -fabsf(b.vx) * IDLE_BALL_BOUNCE;
  }
}

#define IDLE_BALL_KICK_AT (IDLE_BALL_REACH + 4)  // within this, it can kick

// The spot beside the ball is always inside the walk range, so the pet can reach every ball.
static_assert(IDLE_BALL_MIN + IDLE_BALL_REACH >= IDLE_WALK_MIN && IDLE_BALL_MAX - IDLE_BALL_REACH <= IDLE_WALK_MAX,
              "the pet must be able to stand beside the ball anywhere in its range");
static_assert(IDLE_WALK_MIN - IDLE_BALL_MIN <= IDLE_BALL_KICK_AT && IDLE_BALL_MAX - IDLE_WALK_MAX <= IDLE_BALL_KICK_AT,
              "a ball at either end must be within kicking reach of the walk's end");

// Where the pet heads in a ball game: beside the ball on the side it is already on (inside
// its walk range: the asserts above). Already within kicking reach, it stays where it is
// (it never backs off).
inline float idleBallSpot(float ballX, float petX) {
  if (petX < IDLE_WALK_MIN) petX = IDLE_WALK_MIN;
  if (petX > IDLE_WALK_MAX) petX = IDLE_WALK_MAX;
  if (fabsf(ballX - petX) <= IDLE_BALL_KICK_AT) return petX;
  return ballX >= petX ? ballX - IDLE_BALL_REACH : ballX + IDLE_BALL_REACH;
}

// Within reach and nearly stopped: time for a kick.
inline bool idleBallKickable(const IdleBall &b, float petX) {
  return fabsf(b.x - petX) <= IDLE_BALL_KICK_AT && fabsf(b.vx) < IDLE_BALL_SLOW;
}

// Kicks it away from the pet; roll picks the strength.
inline void idleBallKick(IdleBall &b, float petX, uint32_t roll) {
  float sp = (float)(IDLE_BALL_KICK_MIN + roll % IDLE_BALL_KICK_SPAN);
  b.vx = b.x >= petX ? sp : -sp;
}

// ---------------------------------------------------------------- where the emote goes
// A round thought bubble over the pet's head, a little to the side with more room (as the
// "!" bubble, attnBubbleAt), its tail dots running down to the head. Never above the status
// line (ends near y 106) nor down at the bars.
#define IDLE_EMOTE_R 16
#define IDLE_EMOTE_DX 24  // from the pet's centre to the bubble's, sideways
#define IDLE_EMOTE_UP 26  // from the top of its head up to the bubble's centre
#define IDLE_EMOTE_YMIN (ATTN_BUBBLE_Y + IDLE_EMOTE_R)
#define IDLE_EMOTE_YMAX 250  // its lowest point stays well above the bars' panel (y 312)

struct IdleSpot {
  int16_t x, y;
  bool right;  // the bubble is right of the pet's centre (its tail dots go down-left)
};

// petX: the pet's centre; headTop: the top of its sprite on screen.
inline IdleSpot idleEmoteAt(int petX, int headTop) {
  if (petX < IDLE_WALK_MIN) petX = IDLE_WALK_MIN;
  if (petX > IDLE_WALK_MAX) petX = IDLE_WALK_MAX;
  IdleSpot s;
  s.right = petX <= 233;  // the screen's centre column
  s.x = (int16_t)(s.right ? petX + IDLE_EMOTE_DX : petX - IDLE_EMOTE_DX);
  int y = headTop - IDLE_EMOTE_UP;
  if (y < IDLE_EMOTE_YMIN) y = IDLE_EMOTE_YMIN;
  if (y > IDLE_EMOTE_YMAX) y = IDLE_EMOTE_YMAX;
  s.y = (int16_t)y;
  return s;
}
