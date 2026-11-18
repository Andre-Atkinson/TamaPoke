// The berry garden (#115): a pot at the right-hand end of the ground where the player grows
// one berry plant at a time. Tapping the empty pot plants a berry (red, blue or green, a
// third each), tapping a growing plant waters it, and tapping a ripe plant picks a berry for
// the pet. Rain, storms and snow where the pet is water it too. The owner's choices of
// 2026-11-17 (recorded on #115): the pot is in every scene and stays through an ending; a
// growing plant left dry two days wilts (yellow leaves warn after one), a ripe plant never
// does, and the pet is never touched by it; a picked berry is a bigger treat than one from
// the Food strip; the berry planted is random.
//
// Pure logic, no hardware: host-tested in test/test_garden.cpp. The pet's save keeps it
// (Pet::load() and Pet::save(), NVS keys `potb`, `potg`, `potw`, `potd`, `pota`), the
// sketch moves it on with the clock and draws it (scene.cpp).
//
// Time: the plant grows only while watered, in watered seconds; water lasts GARDEN_WATER_S
// from a tap, or from the end of a rainy block. Only a trusted clock moves it on
// (sceneEpoch() in weather.h): while the clock is stale (after a power cut, until somebody
// sets it) the garden stands still, and after a clock set it starts again from the new
// time, so a power cut or a clock change never grows, dries or wilts it. The time it
// catches up in one go is capped at GARDEN_CATCHUP_S.
#pragma once
#include <stdint.h>
#include <string.h>
#include "weather.h"

// what is in the pot
enum GardenPlant : uint8_t { GARDEN_EMPTY = 0, GARDEN_GROWING, GARDEN_WITHERED, GARDEN_PLANTS };

// what it looks like: a growing plant's stage follows its watered time (gardenStage())
enum GardenStage : uint8_t {
  GST_EMPTY = 0, GST_SEED, GST_SPROUT, GST_LEAVES, GST_FLOWER, GST_RIPE, GST_WITHERED, GST_COUNT
};
// the words GARDEN? prints and the GARDEN serial command takes (serialcmd.cpp)
inline const char *const GARDEN_STAGE_KEYS[GST_COUNT] = { "empty",  "seed", "sprout",  "leaves",
                                                          "flower", "ripe", "withered" };
inline const char *const GARDEN_BERRY_KEYS[3] = { "red", "blue", "green" };  // feedBerry()'s colours

#define GARDEN_BERRY_COLORS 3
#define GARDEN_BERRIES 3                       // berries on a ripe plant, one per tap
#define GARDEN_SPROUT_S (4 * 3600UL)           // watered time to each stage
#define GARDEN_LEAVES_S (12 * 3600UL)
#define GARDEN_FLOWER_S (24 * 3600UL)
#define GARDEN_RIPE_S (40 * 3600UL)
#define GARDEN_WATER_S (12 * 3600UL)           // how long a watering (or the end of the rain) lasts
#define GARDEN_WET_MAX_S (WX_BLOCK_S + GARDEN_WATER_S)  // the most water it can hold: rain all block
#define GARDEN_WARN_S (24 * 3600UL)            // dry this long in a row: yellow leaves, "wilting soon"
#define GARDEN_WILT_S (48 * 3600UL)            // dry this long in a row: a growing plant wilts
#define GARDEN_CATCHUP_S (7 * 86400UL)         // the most time one settle() counts
// a picked berry's treat on top of the Food strip's (Pet::pickBerry)
#define GARDEN_PICK_JOY 10
#define GARDEN_PICK_FULL 10
#define GARDEN_PICK_BOND 1

// the watered time a stage starts at (a growing plant's stages only)
inline uint32_t gardenStageStart(uint8_t stage) {
  switch (stage) {
    case GST_SPROUT: return GARDEN_SPROUT_S;
    case GST_LEAVES: return GARDEN_LEAVES_S;
    case GST_FLOWER: return GARDEN_FLOWER_S;
    case GST_RIPE: return GARDEN_RIPE_S;
    default: return 0;
  }
}

// Where it is on the main screen (scene.cpp draws it, the sketch's onTap() tests the tap):
// the right-hand end of the ground, mirroring the poops' box (x 36-192, y 244-308). The
// garden box holds the pot and the tallest plant, below the lowest bubble (BUBBLE_TIP_LOW,
// attention.h) and right of where most sprites reach. The tap box is a little larger, clear
// of the pet's touch zone (inPetZone, x 111-355), the buttons over the pet (they end at y 236)
// and the care buttons; test_garden.cpp checks it.
#define GARDEN_BOX_X0 396
#define GARDEN_BOX_X1 452
#define GARDEN_BOX_Y0 242
#define GARDEN_BOX_Y1 308
#define GARDEN_TAP_X0 360
#define GARDEN_TAP_X1 458
#define GARDEN_TAP_Y0 237
#define GARDEN_TAP_Y1 310
#define GARDEN_POT_X 424  // the pot's centre column
inline bool gardenHit(int x, int y) {
  return x >= GARDEN_TAP_X0 && x <= GARDEN_TAP_X1 && y >= GARDEN_TAP_Y0 && y <= GARDEN_TAP_Y1;
}

// rain, a storm or snow waters the pot (the snowfield rains in 0-5 % of its blocks and the
// mountain never in winter: snow counts so the pot there isn't left dry)
inline bool gardenWeatherWaters(uint8_t kind) { return kind == WX_RAIN || kind == WX_STORM || kind == WX_SNOW; }

// What a tap on the pot did (Garden::tap())
enum GardenTap : uint8_t {
  GTAP_PLANTED,  // the empty pot: a seed, watered
  GTAP_WATERED,  // a growing plant that isn't ripe
  GTAP_PICKED,   // a ripe plant: one berry for the pet (Garden::tap's `picked`)
  GTAP_CLEARED,  // the wilted stalk is gone: the pot is empty
  GTAP_WAIT,     // a ripe plant, but the pet can't eat now (an egg, asleep, eating): nothing
};

// What the pet may say about it (BubbleIn::garden, bubble.h)
enum GardenSay : uint8_t { GSAY_NONE = 0, GSAY_THIRSTY, GSAY_WILTING, GSAY_RIPE };

struct Garden {
  uint8_t plant = GARDEN_EMPTY;
  uint8_t berry = 0;   // 0 red, 1 blue, 2 green: what was planted
  uint8_t left = 0;    // berries left on a ripe plant (1 to GARDEN_BERRIES), else 0
  uint32_t grown = 0;  // watered seconds so far, up to GARDEN_RIPE_S (a growing plant)
  uint32_t wet = 0;    // seconds of water left, up to GARDEN_WET_MAX_S (a growing plant)
  uint32_t dry = 0;    // seconds dry in a row, under GARDEN_WILT_S (a growing plant)
  uint32_t at = 0;     // the trusted clock it was last moved on to; 0: none yet, start afresh

  bool growing() const { return plant == GARDEN_GROWING; }
  bool ripe() const { return growing() && grown >= GARDEN_RIPE_S; }
  bool watered() const { return growing() && wet > 0; }
  // dry a day in a row and still growing: yellow leaves warn it will wilt
  bool wiltingSoon() const { return growing() && !ripe() && wet == 0 && dry >= GARDEN_WARN_S; }

  uint8_t stage() const {
    if (plant == GARDEN_WITHERED) return GST_WITHERED;
    if (!growing()) return GST_EMPTY;
    if (grown >= GARDEN_RIPE_S) return GST_RIPE;
    if (grown >= GARDEN_FLOWER_S) return GST_FLOWER;
    if (grown >= GARDEN_LEAVES_S) return GST_LEAVES;
    if (grown >= GARDEN_SPROUT_S) return GST_SPROUT;
    return GST_SEED;
  }

  void clear() {
    plant = GARDEN_EMPTY;
    berry = left = 0;
    grown = wet = dry = 0;
  }

  // water for `s` seconds from now, unless it already has more
  void water(uint32_t s) {
    if (!growing()) return;
    if (s > GARDEN_WET_MAX_S) s = GARDEN_WET_MAX_S;
    if (s > wet) wet = s;
    dry = 0;
  }

  // `s` seconds go by with the water it has: growth while watered, then drying
  void pass(uint32_t s) {
    if (!growing() || !s) return;
    uint32_t w = s < wet ? s : wet;
    wet -= w;
    if (ripe()) return;  // a ripe plant waits to be picked: it never wilts
    grown += w;
    if (grown >= GARDEN_RIPE_S) {
      grown = GARDEN_RIPE_S;
      left = GARDEN_BERRIES;
      dry = 0;
      return;
    }
    dry += s - w;  // wet > 0 meant dry == 0 (water() clears it), so this is the dry run
    if (dry >= GARDEN_WILT_S) {
      clear();
      plant = GARDEN_WITHERED;
    }
  }

  // Moves the garden on to `now`, the trusted clock (sceneEpoch(): 0 while it is stale or
  // unknown), with the rain at the pet's place block by block (`home`, the pet's home:
  // travelPlace() picks the day's place from it; an egg's is the meadow). With no trusted
  // clock it stands still and forgets where it was, so the first trusted settle starts
  // afresh; a clock that went back starts afresh too. Catches up at most GARDEN_CATCHUP_S
  // (the latest part).
  void settle(uint32_t now, uint8_t home) {
    if (!now) {
      at = 0;
      return;
    }
    if (!at || now <= at) {
      at = now;
      return;
    }
    uint32_t t = now - at > GARDEN_CATCHUP_S ? now - GARDEN_CATCHUP_S : at;
    at = now;
    while (t < now && growing()) {
      uint32_t block = t / WX_BLOCK_S;
      uint32_t end = (block + 1) * WX_BLOCK_S;
      if (end > now) end = now;
      // it rains through the block, and the water lasts GARDEN_WATER_S after it ends
      if (gardenWeatherWaters(wxKind(travelPlace(home, t), block)))
        water((block + 1) * WX_BLOCK_S - t + GARDEN_WATER_S);
      pass(end - t);
      t = end;
    }
  }

  // The weather on screen waters it too (scene.cpp, while the main scene shows rain, a storm
  // or snow): the place on screen can be yesterday's for a moment after 06:00 (SceneLatch),
  // and the SCENE serial command can force the weather. `now`: the trusted clock (0: none,
  // nothing happens, as the garden stands still).
  void rainSeen(uint32_t now) {
    if (!now) return;
    water(WX_BLOCK_S - now % WX_BLOCK_S + GARDEN_WATER_S);
  }

  // A tap on the pot. `roll`: any random number (picks the berry planted). `canEat`: the pet
  // can eat a berry now (hatched, awake, not in an ending, not already eating). A pick sets
  // `picked` to the berry's colour; the caller feeds it to the pet.
  GardenTap tap(uint32_t roll, bool canEat, uint8_t &picked) {
    if (plant == GARDEN_WITHERED) {
      clear();
      return GTAP_CLEARED;
    }
    if (!growing()) {
      clear();
      plant = GARDEN_GROWING;
      berry = (uint8_t)(roll % GARDEN_BERRY_COLORS);
      water(GARDEN_WATER_S);
      return GTAP_PLANTED;
    }
    if (!ripe()) {
      water(GARDEN_WATER_S);
      return GTAP_WATERED;
    }
    if (!canEat) return GTAP_WAIT;
    picked = berry;
    if (left > 1) left--;
    else clear();
    return GTAP_PICKED;
  }

  // what the pet may say about it now (bubble.h): ripe, or a growing plant with no water
  uint8_t say() const {
    if (ripe()) return GSAY_RIPE;
    if (wiltingSoon()) return GSAY_WILTING;
    if (growing() && wet == 0) return GSAY_THIRSTY;
    return GSAY_NONE;
  }

  // The serial GARDEN command (a test hook): the pot as `stage` (GST_*), with water
  // (`moist` 0 dry, 1 watered for GARDEN_WATER_S, 2 dry a day: wilting soon) and `color`
  // (0-2, or 0xFF to keep the one planted). The clock reference is left as it is.
  void force(uint8_t stage, uint8_t moist, uint8_t color) {
    uint8_t b = color < GARDEN_BERRY_COLORS ? color : berry;
    clear();
    if (stage == GST_WITHERED) plant = GARDEN_WITHERED;
    if (stage == GST_EMPTY || stage >= GST_WITHERED) return;
    plant = GARDEN_GROWING;
    berry = b;
    grown = gardenStageStart(stage);
    if (stage == GST_RIPE) left = GARDEN_BERRIES;
    if (moist == 1) water(GARDEN_WATER_S);
    else if (moist == 2 && stage != GST_RIPE) dry = GARDEN_WARN_S;
  }

  // ---- the save (Pet::save / Pet::load): `potb` packs plant, berry and berries left ----
  uint8_t packed() const { return (uint8_t)(plant | berry << 2 | left << 4); }

  // Loads the saved values and range-checks them: anything this firmware can't have written
  // (a damaged page, another build) gives an empty pot, never a plant that grew or wilted on
  // its own. Returns false when it had to empty the pot. `savedAt` past the clock's range
  // (`clockEnd`) reads as none.
  bool load(uint8_t b, uint32_t g, uint32_t w, uint32_t d, uint32_t savedAt, uint32_t clockEnd) {
    clear();
    at = savedAt < clockEnd ? savedAt : 0;
    uint8_t p = b & 3, c = (b >> 2) & 3, l = (b >> 4) & 0x0F;
    bool ok = (b >> 6) == 0 && p < GARDEN_PLANTS && c < GARDEN_BERRY_COLORS;
    if (ok && p == GARDEN_GROWING) {
      bool isRipe = g >= GARDEN_RIPE_S;
      ok = g <= GARDEN_RIPE_S && w <= GARDEN_WET_MAX_S && d < GARDEN_WILT_S && !(w && d) &&
           (isRipe ? (l >= 1 && l <= GARDEN_BERRIES && d == 0) : l == 0);
    } else if (ok) {
      ok = l == 0 && g == 0 && w == 0 && d == 0;  // empty or wilted: nothing else is kept
    }
    if (!ok) return false;
    plant = p;
    if (p != GARDEN_GROWING) return true;
    berry = c;
    left = l;
    grown = g;
    wet = w;
    dry = d;
    return true;
  }
};

// ---- the serial GARDEN command (serialcmd.cpp) ----
// "GARDEN?" reads; "GARDEN <stage> [wet|dry|wilting] [red|blue|green]" forces the pot (a test
// hook, for the simulator and screenshots), with the words in that order. A growing stage
// with no water word is watered. Anything else is GCMD_BAD. Pure, so it is tested on the PC.
enum GardenCmd : uint8_t { GCMD_NONE = 0, GCMD_READ, GCMD_SET, GCMD_BAD };
struct GardenCmdArgs {
  uint8_t stage = GST_EMPTY;
  uint8_t moist = 1;      // 0 dry, 1 wet, 2 wilting soon
  uint8_t color = 0xFF;   // 0xFF: keep the berry planted
};
inline GardenCmd gardenCmdParse(const char *line, GardenCmdArgs &a) {
  a = GardenCmdArgs();
  if (strncmp(line, "GARDEN", 6) != 0) return GCMD_NONE;
  const char *s = line + 6;
  if (strcmp(s, "?") == 0) return GCMD_READ;
  if (*s != ' ') return *s ? GCMD_NONE : GCMD_BAD;
  // up to three words
  char w[3][12];
  int n = 0;
  while (*s) {
    while (*s == ' ') s++;
    if (!*s) break;
    if (n == 3) return GCMD_BAD;
    int k = 0;
    while (*s && *s != ' ') {
      if (k == 11) return GCMD_BAD;
      w[n][k++] = *s++;
    }
    w[n++][k] = 0;
  }
  if (!n) return GCMD_BAD;
  int st = -1;
  for (int i = 0; i < GST_COUNT; i++)
    if (strcmp(w[0], GARDEN_STAGE_KEYS[i]) == 0) st = i;
  if (st < 0) return GCMD_BAD;
  a.stage = (uint8_t)st;
  int i = 1;
  if (i < n) {
    if (strcmp(w[i], "dry") == 0) { a.moist = 0; i++; }
    else if (strcmp(w[i], "wet") == 0) { a.moist = 1; i++; }
    else if (strcmp(w[i], "wilting") == 0) { a.moist = 2; i++; }
  }
  if (i < n) {
    int c = -1;
    for (int k = 0; k < GARDEN_BERRY_COLORS; k++)
      if (strcmp(w[i], GARDEN_BERRY_KEYS[k]) == 0) c = k;
    if (c < 0) return GCMD_BAD;
    a.color = (uint8_t)c;
    i++;
  }
  if (i < n) return GCMD_BAD;
  // the empty pot and the wilted stalk take no water or colour words
  if ((st == GST_EMPTY || st == GST_WITHERED) && n > 1) return GCMD_BAD;
  // a ripe plant never wilts
  if (st == GST_RIPE && a.moist == 2) return GCMD_BAD;
  return GCMD_SET;
}
