// Natures (#25): each pet gets one of five at hatch, shown on the stat card's profile
// page. The idea comes from Mu-Muu/TamaPoke (MIT; NAT_* in pet.h and their uses in
// pet.cpp), rewritten here without their network features.
//
//   easygoing  no quirks
//   greedy     food drains a little faster, berries fill more;    strength trains a little faster
//   playful    joy drains a little faster, play cheers it more;   speed trains a little faster
//   tidy       hygiene drains a little faster, a bath lifts joy;  stamina trains a little faster
//   lazy       energy drains a little faster, sleep restores more
//
// Pure logic (no hardware, no clock reads), tested on the PC (test/test_pet.cpp). Pet
// (pet.cpp) applies the drains and the care boosts; every place that grows a trained
// stat passes its gain through natureTrainGain().
#pragma once
#include <stdint.h>

// The order is saved (NVS key "nature") and is the order of the names (S_NAT_EASYGOING...
// in i18n.h): only ever add at the end. A pet saved before natures loads as easygoing.
enum : uint8_t { NAT_EASYGOING = 0, NAT_GREEDY, NAT_PLAYFUL, NAT_TIDY, NAT_LAZY, NAT_COUNT };
// each nature's name in the serial log (STATS) and in the simulator's "nature" step
inline const char *const NATURE_KEYS[NAT_COUNT] = { "easygoing", "greedy", "playful", "tidy", "lazy" };

// ---- the drain: one extra point off the nature's need every NATURE_DRAIN_EVERY drains ----
// Awake only (asleep the needs barely fall and stop at a floor). The relaxed pace drains on
// even minutes only, so its extra point comes every 2 * NATURE_DRAIN_EVERY minutes: the
// same share of the drain in both paces. Per minute awake, at the normal pace:
//   greedy  food     2 -> 2.25  (+12.5%)
//   playful joy      1 -> 1.25  (+25%; the -2s for low food or hygiene are unchanged)
//   tidy    hygiene  1 -> 1.25  (+25%; the -4 per poop is unchanged)
//   lazy    energy   1 -> 1.25  (+25%; 2 -> 2.25 when overweight)
// The powered-off catch-up (Pet::syncClock) takes the same extra point every
// NATURE_DRAIN_EVERY minutes, with its floor of 15 like every other drain there.
#define NATURE_DRAIN_EVERY 4
inline bool natureDrainMinute(uint32_t minute, bool relaxed) {
  return minute % (relaxed ? 2 * NATURE_DRAIN_EVERY : NATURE_DRAIN_EVERY) == 0;
}

// ---- the care boosts ----
#define NAT_BERRY_EXTRA 5     // greedy: every berry fills 5 more (25 -> 30, the favourite 35 -> 40)
#define NAT_PLAY_JOY_EXTRA 5  // playful: the toy and every Play minigame cheer it 5 more
#define NAT_BATH_JOY 10       // tidy: a bath also lifts joy by 10
#define NAT_SLEEP_ENERGY 8    // lazy: asleep, energy comes back 8 a minute instead of 6

// ---- the training bonus (#25 "Stat growth") ----
// The three trained stats: strength (trAtk), stamina (trDef) and speed (trSpe).
enum : uint8_t { TRAIN_STRENGTH = 0, TRAIN_STAMINA, TRAIN_SPEED, TRAIN_NONE };

// The stat a nature trains a little faster; TRAIN_NONE for easygoing and lazy (and for a
// value out of range).
inline uint8_t natureTrainStat(uint8_t nature) {
  switch (nature) {
    case NAT_GREEDY: return TRAIN_STRENGTH;
    case NAT_TIDY: return TRAIN_STAMINA;
    case NAT_PLAYFUL: return TRAIN_SPEED;
    default: return TRAIN_NONE;
  }
}

// A gain of `base` points of `stat`, with the nature's bonus: +25% rounded up, so a gain of
// 1 still earns its extra point (1 -> 2, 4 -> 5, 18 -> 23). A bonus only: any other
// nature or stat, or a gain of 0, comes back unchanged. The caller still caps the stat.
inline uint8_t natureTrainGain(uint8_t nature, uint8_t stat, uint8_t base) {
  if (stat == TRAIN_NONE || natureTrainStat(nature) != stat) return base;
  unsigned g = base + (base + 3u) / 4u;
  return g > 255u ? (uint8_t)255 : (uint8_t)g;
}
