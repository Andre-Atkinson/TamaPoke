// The pet's own life on the main screen (#22, idlelife.h): the planner in idleplan.h run
// from drawPetPMD() (scene.cpp), with the ball and the emotes it draws over the pet.
#include <Arduino.h>
#include "app.h"
#include "bubble.h"
#include "caremenu.h"  // CARE_FOOD_BTN_X
#include "idlelife.h"
#include "idleplan.h"
#include "scene.h"
#include "species.h"

// A hungry pet wishes for food under the Food button (#66): every spot IDLE_SEEK picks keeps
// its centre on the button (BTN_HALF either side of CARE_FOOD_BTN_X).
static_assert(IDLE_SEEK_X >= CARE_FOOD_BTN_X - BTN_HALF && IDLE_SEEK_X + IDLE_SEEK_SPAN <= CARE_FOOD_BTN_X + BTN_HALF,
              "the hungry pet's spot must be under the Food button");

static IdlePerks perks;          // the hourly cap of the doze and ball perks (RAM: idleplan.h)
static IdleBall ball;            // on the ground while beh.activity is IDLE_BALL
static uint32_t ballLast = 0;    // when it last rolled
static uint8_t emote = EMO_NONE;
static uint32_t emoteUntil = 0;
static uint32_t lastFrame = 0;   // the last frame idleUpdate() ran (0 = none yet)
static bool lastFull = false;    // ...and whether that frame ran the full repertoire
#ifdef HOST_SIM
static int8_t forced = -1;       // idleForceNext()
#endif

static void showEmote(uint8_t e, uint32_t now, uint32_t ms) {
  emote = e;
  emoteUntil = now + ms;
}

static bool emoteShowing(uint32_t now) { return emote != EMO_NONE && (int32_t)(emoteUntil - now) > 0; }

static IdleIn idleInputs(bool full) {
  IdleIn in;
  in.fullness = pet.fullness;
  in.joy = pet.joy;
  in.energy = pet.energy;
  in.weight = pet.weight;
  in.stamina = pet.trDef;
  in.nature = pet.nature;  // #86: small biases, looks only (idleplan.h)
  in.poops = pet.poops;
  // the hour only from a trusted clock, as the quiet hours read it (attnLoop): no clock, or
  // one reseeded after a power-off and not set yet, is no hour at all
  in.hour = idleHour(pet.lastSeenEpoch, bedEdge.stale());
  for (uint8_t a = 0; a < PMD_NACTS; a++)
    if (pmd.has(a)) in.acts |= (uint16_t)(1u << a);
  in.calm = !full;
  return in;
}

static void startPick(const IdlePick &p, uint32_t now) {
  beh.activity = p.act;
  beh.t0 = now;
  beh.mode = 0;
  beh.until = now + p.ms;
  emote = EMO_NONE;
  switch (p.act) {
    case IDLE_WALK:
    case IDLE_SEEK:
      beh.mode = 1;
      beh.targetX = p.targetX;
      break;
    case IDLE_GESTURE:
      beh.mode = 2;
      beh.act = p.pmdAct;
      beh.until = now + pmdActTotalMs(pmd.acts[p.pmdAct]);
      break;
    case IDLE_BALL: {
      float bx = beh.x + (random(2) ? 60.0f : -60.0f);  // dropped beside it, either side
      ball.x = constrain(bx, (float)IDLE_BALL_MIN, (float)IDLE_BALL_MAX);
      ball.vx = 0;
      ballLast = now;
      beh.mode = 1;
      beh.targetX = idleBallSpot(ball.x, beh.x);
      break;
    }
    default:
      break;
  }
  if (p.emote) showEmote(p.emote, now, p.emoteMs);
  if (!beh.until) beh.until = 1;  // 0 means "pick now" (idleDue)
}

static void idleNext(uint32_t now, bool full) {
  startPick(idlePick(idleInputs(full), (uint32_t)random(0x7FFFFFFF)), now);
}

// The activity ran its course: a doze rests a little, a ball game cheers a little, within the
// hour's cap. Both only ever run on a free, lit screen (idleUpdate() swaps them for a calm
// activity on any other frame, and any touch stops them), so one that comes due ran its
// whole length there. Not the player's care: Pet::idlePerk() gives no streak, bond or heart.
static void idleFinish(uint32_t now) {
  uint8_t kind = beh.activity == IDLE_DOZE ? IDLE_PERK_ENERGY
               : beh.activity == IDLE_BALL ? IDLE_PERK_JOY
                                           : IDLE_PERK_KINDS;
  if (kind == IDLE_PERK_KINDS) return;
  uint8_t n = perks.grant(kind, now);
  // one line per finished doze or ball game: a handful an hour at most
  Serial.printf("idle: %s done, %s +%u%s\n", kind == IDLE_PERK_ENERGY ? "doze" : "ball",
                kind == IDLE_PERK_ENERGY ? "energy" : "joy", (unsigned)n, n ? "" : " (hourly cap)");
  if (!n) return;
  if (kind == IDLE_PERK_ENERGY) pet.idlePerk(n, 0);
  else pet.idlePerk(0, n);
}

// The ball rolls; once it has nearly stopped beside the pet, a kick. The pet heads for the
// spot beside it (the walk in drawPetPMD() takes it there).
static void ballTick(uint32_t now) {
  uint32_t dt = now - ballLast;
  ballLast = now;
  idleBallRoll(ball, dt / 1000.0f);
  if (idleBallKickable(ball, beh.x)) {
    idleBallKick(ball, beh.x, (uint32_t)random(0x7FFFFFFF), pet.trAtk);
    if (random(3) == 0) showEmote(EMO_NOTE, now, 900);
  }
  beh.mode = 1;
  beh.targetX = idleBallSpot(ball.x, beh.x);
}

void idleInterrupt(uint32_t now) {
  emote = EMO_NONE;
  beh.activity = IDLE_STAND;
  if (beh.fast) return;  // walking after a finger (#21): it gets there, then picks again
  beh.mode = 0;
  beh.t0 = now;
  beh.until = now + IDLE_ATTEND_MS;
  if (!beh.until) beh.until = 1;
}

void idleReset() {
  emote = EMO_NONE;
  lastFrame = 0;
  lastFull = false;
  beh.activity = IDLE_STAND;
}

void idleUpdate(uint32_t now, bool full) {
  lastFrame = now ? now : 1;
  // dimmed, or a dialog over the pet: back to the calm repertoire at once, and what was cut
  // short gives nothing. (Coming back to a free, lit screen always takes a touch, PWR or
  // BOOT, and wakeUI() has already stopped the activity: another screen, sleep or a meal
  // is left by hand, and a sad pet, the only one a call reaches, is cheered up by hand. A sad
  // pet shows HURT and gets here only after a dragging finger let go of it, with full false.)
  if (!full && !idleCalmAct(beh.activity)) idleNext(now, false);
  lastFull = full;
#ifdef HOST_SIM
  if (forced >= 0 && full) {
    IdleIn in = idleInputs(true);
    if (idleAvailable(in, (uint8_t)forced)) startPick(idlePickAct(in, (uint8_t)forced, (uint32_t)random(0x7FFFFFFF)), now);
    forced = -1;
    return;
  }
#endif
  if (beh.activity == IDLE_BALL) ballTick(now);
  if (idleDue(now, beh.until)) {
    idleFinish(now);
    idleNext(now, full);
  }
}

void idleArrived(uint32_t now) {
  if (beh.activity == IDLE_BALL) return;  // beside the ball: it waits for a kick
  if (beh.activity == IDLE_SEEK) {        // at the food button: wishes for food
    beh.mode = 0;
    beh.t0 = now;
    beh.until = now + IDLE_WISH_MS;
    if (!beh.until) beh.until = 1;
    showEmote(EMO_FOOD, now, IDLE_WISH_MS);
    return;
  }
  idleNext(now, lastFull);
}

float idleWalkSpeed() { return idleWalkPace(beh.activity == IDLE_BALL, pet.trSpe); }

uint8_t idleFrameAct(uint32_t now, int &dy, uint32_t &t0, bool &loop) {
  t0 = beh.t0;
  if (beh.mode == 2) {  // a one-shot gesture (or the pose after a bath)
    loop = false;
    return beh.act;
  }
  loop = true;
  switch (beh.activity) {
    case IDLE_LOOK:
      t0 = now;  // a still frame: it turns its head, it doesn't walk
      return idleLookAct(idleDue(now, beh.until) ? 0 : beh.until - now);
    case IDLE_DOZE:
      return PMD_SLEEP;
    case IDLE_HOP:
      dy += idleHopDy(now - beh.t0);
      return PMD_IDLE;
    default:
      return PMD_IDLE;
  }
}

// Something else is by its head or over the pet: a speech bubble (#23: it goes first, and
// draws in the same band beside the head), the heart of a pat, a medal or streak banner, the
// evolve, farewell or run-away button.
static bool emoteAllowed() {
  return !bubbleShowing() && !pet.showHeart() && !pet.showMedal() && !pet.showMilestone() &&
         !pet.wantEvolveButton() && !pet.canRunawayNow() && !pet.wantFarewellButton();
}

// a "z" `s` px across with its top-left at (x, y), 2 px strokes
static void drawZ(int x, int y, int s, uint16_t col) {
  gfx->fillRect(x, y, s, 2, col);
  for (int d = 0; d < 2; d++) gfx->drawLine(x + s - 1 - d, y + 2, x - d + 1, y + s - 2, col);
  gfx->fillRect(x, y + s - 1, s, 2, col);
}

static void drawEmote(uint8_t e, const IdleSpot &at, uint32_t now) {
  const int x = at.x, y = at.y, r = IDLE_EMOTE_R;
  uint16_t ink = inkColor();
  gfx->fillCircle(x, y, r, UI_WHITE);
  gfx->drawCircle(x, y, r, ink);
  int side = at.right ? -1 : 1;  // the tail dots run down towards the pet's head
  gfx->fillCircle(x + side * 11, y + r + 3, 3, UI_WHITE);
  gfx->drawCircle(x + side * 11, y + r + 3, 3, ink);
  gfx->fillCircle(x + side * 17, y + r + 9, 2, UI_WHITE);
  gfx->drawCircle(x + side * 17, y + r + 9, 2, ink);
  switch (e) {
    case EMO_HEART: {
      uint16_t c = C565(0xe8, 0x40, 0x68);
      gfx->fillCircle(x - 4, y - 3, 5, c);
      gfx->fillCircle(x + 4, y - 3, 5, c);
      gfx->fillTriangle(x - 9, y - 1, x + 9, y - 1, x, y + 9, c);
      break;
    }
    case EMO_NOTE:
      gfx->fillCircle(x - 3, y + 5, 4, UI_INK);
      gfx->fillRect(x, y - 9, 2, 14, UI_INK);
      gfx->fillTriangle(x + 2, y - 9, x + 8, y - 5, x + 2, y - 3, UI_INK);
      break;
    case EMO_QUESTION: {
      uint16_t c = C565(0x3c, 0x78, 0xc8);
      gfx->fillCircle(x, y - 4, 7, c);            // the hook: a ring...
      gfx->fillCircle(x, y - 4, 3, UI_WHITE);
      gfx->fillRect(x - 7, y - 4, 7, 8, UI_WHITE);  // ...open at its lower left
      gfx->fillRect(x - 1, y + 1, 3, 4, c);       // the stem
      gfx->fillCircle(x, y + 9, 2, c);            // the dot
      break;
    }
    case EMO_DOTS:
      for (int k = -1; k <= 1; k++) gfx->fillCircle(x + k * 7, y + 1, 2, UI_INK);
      break;
    case EMO_SWEAT: {
      uint16_t c = C565(0x60, 0xb0, 0xf0);
      gfx->fillCircle(x, y + 3, 6, c);
      gfx->fillTriangle(x - 5, y + 1, x + 5, y + 1, x, y - 11, c);
      break;
    }
    case EMO_ZZZ: {  // two z's bobbing gently: not the big "Zz" of bedtime
      uint16_t c = C565(0x50, 0x70, 0xc0);
      int bob = ((now / 500) % 2) ? -1 : 0;
      drawZ(x - 10, y - 2 + bob, 11, c);
      drawZ(x + 3, y - 11 - bob, 7, c);
      break;
    }
    case EMO_FOOD:
      drawMap(SPR_ICON_FOOD, 16, x - 8, y - 8, 1, false);  // the food button's own icon
      break;
    default:
      break;
  }
}

// the planner ran on this frame, with the full repertoire
static bool ranFull(uint32_t now) { return lastFrame == (now ? now : 1) && lastFull; }

bool idleEmoteDue(uint32_t now) { return ranFull(now) && emoteShowing(now) && emoteAllowed(); }

void idleDrawOver(uint32_t now, int petX, int headTop) {
  if (!ranFull(now)) return;  // the planner didn't run this frame, or ran calm
  if (beh.activity == IDLE_BALL)
    drawMap(SPR_ICON_PLAY, 16, (int)ball.x - 16, PET_GROUND - 32, 2, false);  // the Play button's ball
  if (idleEmoteDue(now)) drawEmote(emote, idleEmoteAt(petX, headTop), now);
}

#ifdef HOST_SIM
void idleForceNext(uint8_t act) { forced = act < IDLE_ACTS ? (int8_t)act : (int8_t)-1; }
#endif
