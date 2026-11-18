// Accessories earned by care (#116): accessory.h (what earns each one, the special days' hats,
// where the head is), accfix.h (the hand-made nudges), the pet's wardrobe (pet.h: what it
// wears, the red dots, the reset at hatch, the save's checks), the banner that names one
// earned, and the Wardrobe page's layout and words (cardlayout.h, i18n.cpp).
#include "framework.h"
#include "shim/Arduino.h"
#include "../accessory.h"
#include "../accfix.h"
#include "../cardlayout.h"
#include "../i18n.h"
#include "../pet.h"
#include "../dex.h"
#include "../sdmon.h"  // PMD_*, pmdScaleFor
#include <dirent.h>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

static void advanceMin(Pet &p, uint32_t minutes) {
  for (uint32_t i = 0; i < minutes; i++) {
    mockAdvanceMillis(PET_TICK_MS);
    p.update(millis());
  }
}

// a freshly hatched easygoing pet with a clean NVS (as test_pet.cpp's makePet)
static void hatchedPet(Pet &p, int16_t dex) {
  mockNvsReset();
  mockSetMillis(0);
  p.begin();
  if (p.awaitingStarter()) p.chooseStarter(dex);
  p.eggTap();
  p.eggTap();
  p.eggTap();
  p.nature = NAT_EASYGOING;
}

// One minute tick past level `lv`: its level medal (and the accessory it earns) with it. A care
// mistake keeps IN SHAPE (and the Laurel with it) out of the way.
static void reachLevel(Pet &p, uint16_t lv) {
  p.careMistakes = 1;
  p.ageMinutes = (uint32_t)lv * MINUTES_PER_LEVEL - 1;
  p.fullness = p.joy = p.energy = p.hygiene = 90;
  advanceMin(p, 1);
}

static std::string bitsStr(uint16_t v) { return std::to_string(v); }

// ---------------------------------------------------------------- what earns each one
TEST(accessory, each_earned_one_comes_from_its_own_medal) {
  CHECK_EQ(accUnlockMask(MED_BERRY), (uint16_t)(1u << ACC_BOW));
  CHECK_EQ(accUnlockMask(MED_STREAK7), (uint16_t)(1u << ACC_FLOWERS));
  CHECK_EQ(accUnlockMask(MED_BOND), (uint16_t)(1u << ACC_BOBBLE));
  CHECK_EQ(accUnlockMask(MED_LV10), (uint16_t)(1u << ACC_CAP));
  CHECK_EQ(accUnlockMask(MED_LV25), (uint16_t)(1u << ACC_TOPHAT));
  CHECK_EQ(accUnlockMask(MED_LV50), (uint16_t)(1u << ACC_CROWN));
  CHECK_EQ(accUnlockMask(MED_ALL), ACC_EARNED_MASK);
  CHECK_EQ(accUnlockMask(0), (uint16_t)0);
}

// decision 1: the Crown is level 50 only. A final form earns nothing (25 species hatch in one).
TEST(accessory, a_final_form_earns_nothing) {
  CHECK_EQ(accUnlockMask(MED_FINAL), (uint16_t)0);
  CHECK_EQ(accUnlockMask(MED_ALL & ~MED_LV50), (uint16_t)(ACC_EARNED_MASK & ~(1u << ACC_CROWN)));
}

// The Laurel, by any training ribbon or by IN SHAPE
TEST(accessory, the_laurel_comes_from_any_ribbon_or_in_shape) {
  for (uint16_t m : { MED_FIT, MED_STR, MED_STA, MED_SPD })
    CHECK_MSG(accUnlockMask(m) == (1u << ACC_LAUREL), "medal " + bitsStr(m));
  CHECK_EQ(accEarnedBy(MED_STA, 1u << ACC_LAUREL), (uint8_t)ACC_LAUREL);
  CHECK_EQ(accEarnedBy(MED_LV10, 1u << ACC_LAUREL), (uint8_t)ACC_NONE);
}

TEST(accessory, the_serial_names_go_both_ways) {
  for (int i = 0; i < ACC_COUNT; i++) CHECK_EQ(accFromKey(ACC_KEYS[i]), i);
  CHECK_EQ(accFromKey("hat"), -1);
  CHECK_EQ(accFromKey(""), -1);
  CHECK_EQ(accFromKey("CAP"), -1);  // lower case only, as the other commands' words
}

// ---------------------------------------------------------------- special days
TEST(accessory, each_special_day_has_its_hat_and_the_birthday_wins) {
  CHECK_EQ(accForDay(HOL_NONE, false), (uint8_t)ACC_NONE);
  CHECK_EQ(accForDay(HOL_DECEMBER, false), (uint8_t)ACC_SANTA);
  CHECK_EQ(accForDay(HOL_CHRISTMAS, false), (uint8_t)ACC_SANTA);
  CHECK_EQ(accForDay(HOL_NEW_YEAR, false), (uint8_t)ACC_PARTY);
  CHECK_EQ(accForDay(HOL_VALENTINE, false), (uint8_t)ACC_HEART);
  CHECK_EQ(accForDay(HOL_EASTER, false), (uint8_t)ACC_BUNNY);
  CHECK_EQ(accForDay(HOL_HALLOWEEN, false), (uint8_t)ACC_WITCH);
  for (uint8_t h = 0; h < HOLIDAYS; h++) CHECK_EQ(accForDay(h, true), (uint8_t)ACC_PARTY);
  // every hat a day can give is a special day's (never one to earn)
  for (uint8_t h = 0; h < HOLIDAYS; h++) {
    uint8_t a = accForDay(h, false);
    CHECK_MSG(a == ACC_NONE || accIsHoliday(a), "day " + std::to_string(h));
  }
}

// The day's hat goes over what it wears, unless the player picked something that same day
TEST(accessory, the_days_hat_wins_unless_a_pick_was_made_that_day) {
  CHECK_EQ(accShown(ACC_CAP, ACC_NONE, 0, 500), (uint8_t)ACC_CAP);
  CHECK_EQ(accShown(ACC_CAP, ACC_SANTA, 0, 500), (uint8_t)ACC_SANTA);
  CHECK_EQ(accShown(ACC_CAP, ACC_SANTA, 500, 500), (uint8_t)ACC_CAP);
  CHECK_EQ(accShown(ACC_NONE, ACC_SANTA, 500, 500), (uint8_t)ACC_NONE);  // picked nothing
  CHECK_EQ(accShown(ACC_CAP, ACC_SANTA, 499, 500), (uint8_t)ACC_SANTA);  // yesterday's pick
  CHECK_EQ(accShown(ACC_CAP, ACC_SANTA, 0, 0), (uint8_t)ACC_SANTA);      // pickDay 0 is no pick
}

// ---------------------------------------------------------------- where the head is
// A frame drawn from text: '.' clear, anything else opaque (index 1)
static std::vector<uint8_t> frameOf(const std::vector<std::string> &rows) {
  std::vector<uint8_t> f;
  for (const std::string &r : rows)
    for (char c : r) f.push_back(c == '.' ? 0xFF : 1);
  return f;
}

TEST(accessory, the_head_is_the_first_row_half_as_wide_as_the_widest) {
  // a tail tip (row 0-1) above a head (rows 2-4) on a body (rows 5-7)
  std::vector<std::string> rows = {
    "..........#.", "..........#.", "...####.....", "..######....", "..######....",
    ".##########.", ".##########.", ".##########.",
  };
  std::vector<uint8_t> f = frameOf(rows);
  AccHead hd = accFindHead(f.data(), 12, 8, 0);
  CHECK_EQ(hd.crown, (uint8_t)3);  // 6 wide is half of 10; the tail's 1 and the 4 above are not
  CHECK_EQ(hd.x0, (uint8_t)2);     // the row under it: 6 wide from column 2
  CHECK_EQ(hd.w, (uint8_t)6);
  hd = accFindHead(f.data(), 12, 8, 5);  // skip: rows above 5 are not looked at
  CHECK_EQ(hd.crown, (uint8_t)5);
  CHECK_EQ(hd.x0, (uint8_t)1);
  CHECK_EQ(hd.w, (uint8_t)10);
  // a frame that is all tail above row 2: the widest counted is under skip's row only
  std::vector<uint8_t> g = frameOf({ "############", "#...........", "....##......", "...####....." });
  hd = accFindHead(g.data(), 12, 4, 2);
  CHECK_EQ(hd.crown, (uint8_t)2);  // 2 is half of 4, the widest from row 2 on
  CHECK_EQ(hd.x0, (uint8_t)3);
  CHECK_EQ(hd.w, (uint8_t)4);
}

// band: a bird's head between wings that are wider at its height
TEST(accessory, a_band_looks_only_near_the_middle) {
  std::vector<std::string> rows = {
    "###.......###", "###...#...###", "###..###..###", ".##.#####.##.", "#############",
  };
  std::vector<uint8_t> f = frameOf(rows);
  AccHead hd = accFindHead(f.data(), 13, 5, 0, 20);
  CHECK_EQ(hd.crown, (uint8_t)0);  // a wingtip: 3 is 23% of 13
  CHECK_EQ(hd.x0, (uint8_t)0);
  hd = accFindHead(f.data(), 13, 5, 0, 20, 3);  // columns 3 to 8 only
  CHECK_EQ(hd.crown, (uint8_t)2);  // 3 of the band's widest 6 (row 1's 1 is under 20%)
  CHECK_EQ(hd.x0, (uint8_t)4);     // the row under it, in the canvas's columns
  CHECK_EQ(hd.w, (uint8_t)5);
  hd = accFindHead(f.data(), 13, 5, 0, 20, 2, -4);  // columns 0 to 3: the left wing
  CHECK_EQ(hd.crown, (uint8_t)0);
  CHECK_EQ(hd.x0, (uint8_t)0);
  CHECK_EQ(hd.w, (uint8_t)3);
  CHECK_EQ(accFindHead(f.data(), 13, 5, 0, 20, 3, 20).w, (uint8_t)0);  // a band off the canvas
}

TEST(accessory, a_frame_with_nothing_has_no_head) {
  std::vector<uint8_t> f(6 * 4, 0xFF);
  CHECK_EQ(accFindHead(f.data(), 6, 4, 0).w, (uint8_t)0);
  std::vector<uint8_t> g = frameOf({ "..##..", "......", "......", "......" });
  CHECK_EQ(accFindHead(g.data(), 6, 4, 1).w, (uint8_t)0);  // all of it above skip
  AccHead hd = accFindHead(g.data(), 6, 4, 0);  // nothing under the crown: its own row
  CHECK_EQ(hd.crown, (uint8_t)0);
  CHECK_EQ(hd.x0, (uint8_t)2);
  CHECK_EQ(hd.w, (uint8_t)2);
}

// A gap in a row: the widest stretch counts, not the row's ends
TEST(accessory, a_row_with_a_gap_counts_its_widest_stretch) {
  std::vector<uint8_t> f = frameOf({ "##..####", "########" });
  uint8_t st;
  CHECK_EQ(accRowRun(f.data(), 8, st), (uint8_t)4);
  CHECK_EQ(st, (uint8_t)4);
  CHECK_EQ(accRowRun(f.data() + 8, 8, st), (uint8_t)8);
  CHECK_EQ(st, (uint8_t)0);
}

// Centred on the head, half a pixel rounding right; sunk; nudged
TEST(accessory, the_art_sits_centred_on_the_head) {
  AccHead hd;
  hd.crown = 20;
  hd.x0 = 10;
  hd.w = 8;  // columns 10-17: centre 14
  const AccArt &cap = ACC_ART[ACC_CAP];  // 13 wide, 8 tall, sinks 2
  int col, row;
  accPlace(hd, cap, 0, 0, col, row);
  CHECK_EQ(col, 8);  // 14 - 6.5 = 7.5: rounds to 8
  CHECK_EQ(row, 20 + 2 - 8);
  accPlace(hd, cap, -3, 2, col, row);
  CHECK_EQ(col, 5);
  CHECK_EQ(row, 16);
  hd.x0 = 0;
  hd.w = 2;  // a head narrower than the art at the canvas's edge: left of column 0
  accPlace(hd, cap, 0, 0, col, row);
  CHECK_EQ(col, -5);  // 1 - 6.5 = -5.5: rounds to -5
  hd.w = 1;
  accPlace(hd, cap, 0, 0, col, row);
  CHECK_EQ(col, -6);  // 0.5 - 6.5 = -6
}

// ---------------------------------------------------------------- the art
TEST(accessory, every_art_map_is_the_size_it_says_in_known_colours) {
  CHECK_EQ(ACC_ART[ACC_NONE].w, (uint8_t)0);
  for (uint8_t id = ACC_BOW; id < ACC_COUNT; id++) {
    const AccArt &a = ACC_ART[id];
    std::string where = ACC_KEYS[id];
    CHECK_MSG(a.rows != nullptr && a.w > 0 && a.h > 0, where);
    CHECK_MSG(a.w <= ACC_ART_MAX_W && a.h <= ACC_ART_MAX_H, where + ": within the largest");
    CHECK_MSG(a.sink < a.h, where + ": something stays above the head");
    if (!a.rows) continue;
    for (uint8_t r = 0; r < a.h; r++) {
      CHECK_MSG(strlen(a.rows[r]) == a.w, where + " row " + std::to_string(r));
      for (uint8_t c = 0; c < a.w && a.rows[r][c]; c++) {
        char ch = a.rows[r][c];
        CHECK_MSG(ch == '.' || accColor(ch) != 0 || ch == 'K', where + ": colour " + std::string(1, ch));
      }
    }
  }
}

// ---------------------------------------------------------------- accfix.h
// The sprite pack (tools/sdcard/mons, TPK2, as PmdMon::load reads it): each action's canvas and
// frames, and where its feet are (PmdAct::base)
struct AccSprite {
  std::vector<uint8_t> blob;
  struct Act {
    uint8_t w = 0, h = 0, n = 0, base = 0;
    const uint8_t *data = nullptr;
  } acts[PMD_NACTS];
};
static bool readSprite(int dex, AccSprite &t) {
  char path[64];
  snprintf(path, sizeof(path), "../tools/sdcard/mons/p%03d.bin", dex);
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  t.blob.resize(size > 0 ? (size_t)size : 0);
  bool ok = size > 7 && fread(t.blob.data(), 1, t.blob.size(), f) == t.blob.size();
  fclose(f);
  if (!ok || memcmp(t.blob.data(), "TPK2", 4) != 0) return false;
  const uint8_t *b = t.blob.data();
  size_t p = 7 + (size_t)(b[5] | b[6] << 8) * 2, end = t.blob.size();
  for (uint8_t i = 0; i < b[4]; i++) {
    if (p + 4 > end || b[p] >= PMD_NACTS) return false;
    AccSprite::Act &a = t.acts[b[p]];
    a.w = b[p + 1];
    a.h = b[p + 2];
    a.n = b[p + 3];
    p += 4 + 2 * (size_t)a.n;
    a.data = b + p;
    p += (size_t)a.w * a.h * a.n;
    if (p > end) return false;
    for (uint8_t fi = 0; fi < a.n; fi++)
      for (int r = a.h - 1; r >= 0; r--) {
        bool any = false;
        for (int c = 0; c < a.w && !any; c++) any = a.data[(size_t)fi * a.w * a.h + r * a.w + c] != 0xFF;
        if (any) {
          if (r + 1 > a.base) a.base = (uint8_t)(r + 1);
          break;
        }
      }
  }
  return true;
}

// The actions the main screen draws the pet in (drawPetPMD): not sleep (put away), nor
// ATTACK and HOP, which it never draws there
static const uint8_t SHOWN_ACTS[] = { PMD_IDLE, PMD_WALKL, PMD_WALKR, PMD_EAT, PMD_HURT,
                                      PMD_POSE, PMD_NOD, PMD_BREATH, PMD_SIT };

TEST(accfix, every_row_is_a_species_and_a_pose_that_shows_one) {
  std::set<std::pair<int, int>> seen;
  for (unsigned i = 0; i < ACC_FIX_COUNT; i++) {
    const AccFix &f = ACC_FIXES[i];
    std::string where = "row " + std::to_string(i) + " (#" + std::to_string(f.dex) + " pose " + std::to_string(f.act) + ")";
    if (f.dex == 0) {  // the placeholder that keeps an empty table compiling: nothing else in it
      CHECK_MSG(ACC_FIX_COUNT == 1 && f.act == 0 && !f.skip && !f.pct && !f.band && !f.mid && !f.dx && !f.dy && !f.hide,
                where);
      continue;
    }
    CHECK_MSG(f.dex >= 1 && f.dex <= DEX_COUNT, where + ": a species");
    bool shown = false;
    for (uint8_t a : SHOWN_ACTS) shown = shown || (a == f.act && a != PMD_WALKR);
    CHECK_MSG(shown, where + ": a pose the main screen shows (walking right uses walking left's row)");
    CHECK_MSG(seen.insert({ f.dex, f.act }).second, where + ": that pose twice");
    CHECK_MSG(f.dx >= -12 && f.dx <= 12 && f.dy >= -12 && f.dy <= 12, where + ": a nudge, not a move");
    CHECK_MSG(!(f.hide && (f.skip || f.pct || f.band || f.mid || f.dx || f.dy)), where + ": hidden, so nothing else");
    CHECK_MSG(f.band || !f.mid, where + ": a band's middle only with a band");
    CHECK_MSG(f.pct == 0 || (f.pct >= 10 && f.pct < ACC_HEAD_PCT) || (f.pct > ACC_HEAD_PCT && f.pct <= 90),
              where + ": a share between 10 and 90 per cent, other than the rule's own");
    AccSprite t;
    if (!readSprite(f.dex, t)) continue;  // the species' sprite is checked below
    const AccSprite::Act &a = t.acts[f.act];
    CHECK_MSG(a.n > 0, where + ": the sprite has that pose");
    CHECK_MSG(f.skip < a.h, where + ": skip inside the canvas");
    CHECK_MSG(f.band == 0 || (f.band >= 3 && 2 * f.band < a.w && a.w / 2 + f.mid - f.band >= 0 &&
                                a.w / 2 + f.mid + f.band <= a.w),
              where + ": a band inside the canvas");
  }
}

// What accfix.h's rows give, with the rule: on every frame of every pose the main screen shows,
// for all 151, a head is found unless the pose is hidden, and the art is drawn on the panel
// (none goes above its top edge). "No species is left without a place for a hat."
TEST(accfix, every_species_wears_one_on_every_frame_it_shows) {
  int species = 0, frames = 0, minTop = 1000, minDex = 0;
  for (int dex = 1; dex <= DEX_COUNT; dex++) {
    AccSprite t;
    if (!readSprite(dex, t)) {
      CHECK_MSG(false, "the sprite of #" + std::to_string(dex) + " is in tools/sdcard/mons");
      continue;
    }
    species++;
    for (uint8_t act : SHOWN_ACTS) {
      const AccSprite::Act &a = t.acts[act];
      if (!a.n) continue;
      bool mirrored;
      const AccFix *fix = accFixFor(dex, act, mirrored);
      if (fix && fix->hide) continue;
      uint8_t s = pmdScaleFor(t.acts[PMD_IDLE].h, a.h, 5);
      for (uint8_t fi = 0; fi < a.n; fi++, frames++) {
        AccHead hd = accFindHead(a.data + (size_t)fi * a.w * a.h, a.w, a.h, fix ? fix->skip : 0,
                                 fix && fix->pct ? fix->pct : ACC_HEAD_PCT, fix ? fix->band : 0,
                                 fix ? (mirrored ? -fix->mid : fix->mid) : 0);
        std::string where = "#" + std::to_string(dex) + " act " + std::to_string(act) + " frame " + std::to_string(fi);
        CHECK_MSG(hd.w > 0, where + ": a head");
        int col, row;
        int dx = fix ? (mirrored ? -fix->dx : fix->dx) : 0, dy = fix ? fix->dy : 0;
        for (uint8_t id = ACC_BOW; id < ACC_COUNT; id++) {
          accPlace(hd, ACC_ART[id], dx, dy, col, row);
          int top = 304 /* PET_GROUND, scene.h */ - a.base * s + row * s;
          if (top < minTop) {
            minTop = top;
            minDex = dex;
          }
          CHECK_MSG(top >= 0, where + " " + ACC_KEYS[id] + ": on the panel");
        }
      }
    }
  }
  CHECK_EQ(species, (int)DEX_COUNT);
  CHECK_MSG(frames > 151 * 4, "every frame looked at: " + std::to_string(frames));
  CHECK_MSG(minTop >= 0, "the highest art's top: y " + std::to_string(minTop) + " (#" + std::to_string(minDex) + ")");
}

// ---------------------------------------------------------------- the pet's wardrobe
// decision 2: each pet's own; the new pet hatches wearing nothing, with nothing earned
TEST(wardrobe, a_new_pet_hatches_wearing_nothing) {
  Pet p;
  hatchedPet(p, 4);
  CHECK_EQ(p.acc, (uint8_t)ACC_NONE);
  CHECK_EQ(p.accUnlocked(), (uint16_t)0);
  reachLevel(p, 10);
  CHECK_EQ(p.acc, (uint8_t)ACC_CAP);
  p.startFarewell();
  p.update(millis() + CEREMONY_MS + 1);
  p.newEgg();
  CHECK_EQ(p.accUnlocked(), (uint16_t)0);  // an egg wears and has nothing
  p.eggTap();
  p.eggTap();
  p.eggTap();
  CHECK(!p.isEgg());
  CHECK_EQ(p.acc, (uint8_t)ACC_NONE);
  CHECK_EQ(p.accSeen, (uint16_t)0);
  CHECK_EQ(p.accUnlocked(), (uint16_t)0);
}

// A pet that has earned none puts the first one on by itself (the owner's call); later ones
// wait on the Wardrobe page
TEST(wardrobe, the_first_one_earned_goes_on_by_itself_and_later_ones_wait) {
  Pet p;
  hatchedPet(p, 4);
  reachLevel(p, 10);
  CHECK_EQ(p.acc, (uint8_t)ACC_CAP);
  reachLevel(p, 25);
  CHECK_MSG(p.accUnlocked() & (1u << ACC_TOPHAT), "the top hat earned");
  CHECK_EQ(p.acc, (uint8_t)ACC_CAP);  // still the cap
}

// Taken off by the player: a later one doesn't go on by itself (it isn't the first)
TEST(wardrobe, taken_off_it_stays_off_when_the_next_comes) {
  Pet p;
  hatchedPet(p, 4);
  reachLevel(p, 10);
  CHECK(p.wearAcc(ACC_NONE, ACC_NONE, 0));
  reachLevel(p, 25);
  CHECK_EQ(p.acc, (uint8_t)ACC_NONE);
}

// The streak runs across pets (decision kept on 2026-11-17): a running 7-day streak gives the
// new pet its medal at hatch, so the Flowers are its first and go straight on
TEST(wardrobe, a_running_streak_gives_a_new_pet_its_flowers_at_hatch) {
  Pet p;
  hatchedPet(p, 4);
  p.streak = 9;
  p.startFarewell();
  p.update(millis() + CEREMONY_MS + 1);
  p.newEgg();
  p.eggTap();
  p.eggTap();
  p.eggTap();
  CHECK(p.hasMedal(MED_STREAK7));
  CHECK_EQ(p.acc, (uint8_t)ACC_FLOWERS);
}

TEST(wardrobe, only_what_it_has_can_be_worn) {
  Pet p;
  hatchedPet(p, 4);
  CHECK(!p.wearAcc(ACC_CROWN, ACC_NONE, 0));
  CHECK(!p.wearAcc(ACC_SANTA, ACC_NONE, 0));  // a special day's hat, not today
  CHECK(!p.wearAcc(ACC_COUNT, ACC_NONE, 0));
  CHECK(!p.wearAcc(200, ACC_NONE, 0));
  CHECK_EQ(p.acc, (uint8_t)ACC_NONE);
  p.medals |= MED_LV50;
  p.saveNow();
  CHECK(p.wearAcc(ACC_CROWN, ACC_NONE, 0));
  CHECK_EQ(p.acc, (uint8_t)ACC_CROWN);
  Pet q;  // saved at once in its own key
  q.begin();
  CHECK_EQ(q.acc, (uint8_t)ACC_CROWN);
}

TEST(wardrobe, nothing_is_worn_in_an_ending) {
  Pet p;
  hatchedPet(p, 4);
  p.medals |= MED_LV10;
  p.release();
  CHECK(!p.wearAcc(ACC_CAP, ACC_NONE, 0));
  CHECK_EQ(p.acc, (uint8_t)ACC_NONE);
}

// decision 3: the day's hat goes on by itself; a pick that day wins until midnight, in RAM
TEST(wardrobe, a_pick_on_a_special_day_wins_until_midnight) {
  Pet p;
  hatchedPet(p, 4);
  p.medals |= MED_LV10 | MED_LV25;
  CHECK(p.wearAcc(ACC_CAP, ACC_NONE, 0));
  const uint32_t day = 20000;
  CHECK_EQ(p.accNow(ACC_SANTA, day), (uint8_t)ACC_SANTA);
  CHECK(p.wearAcc(ACC_TOPHAT, ACC_SANTA, day));
  CHECK_EQ(p.accNow(ACC_SANTA, day), (uint8_t)ACC_TOPHAT);
  CHECK_EQ(p.accNow(ACC_SANTA, day + 1), (uint8_t)ACC_SANTA);  // the next day: the hat again
  CHECK(p.wearAcc(ACC_SANTA, ACC_SANTA, day));  // the day's tile: the hat again, today
  CHECK_EQ(p.accNow(ACC_SANTA, day), (uint8_t)ACC_SANTA);
  CHECK_EQ(p.acc, (uint8_t)ACC_TOPHAT);  // what it wears on other days is unchanged
  CHECK(p.wearAcc(ACC_NONE, ACC_SANTA, day));  // nothing, that day
  CHECK_EQ(p.accNow(ACC_SANTA, day), (uint8_t)ACC_NONE);
  Pet q;  // a restart forgets the pick (RAM only)
  q.begin();
  CHECK_EQ(q.accNow(ACC_SANTA, day), (uint8_t)ACC_SANTA);
  CHECK_EQ(q.accNow(ACC_NONE, day), (uint8_t)ACC_NONE);
}

// The red dots: earned ones not seen before the page opened; seen once it has, and saved
TEST(wardrobe, the_page_marks_what_it_showed_as_seen) {
  Pet p;
  hatchedPet(p, 4);
  reachLevel(p, 10);
  CHECK_EQ(p.accPageOpened(), (uint16_t)(1u << ACC_CAP));
  CHECK_EQ(p.accPageOpened(), (uint16_t)0);
  reachLevel(p, 25);
  CHECK_EQ(p.accPageOpened(), (uint16_t)(1u << ACC_TOPHAT));
  Pet q;
  q.begin();
  CHECK_EQ(q.accSeen, (uint16_t)((1u << ACC_CAP) | (1u << ACC_TOPHAT)));
  CHECK_EQ(q.accPageOpened(), (uint16_t)0);
}

// An old save (no "acc" or "accseen" keys): wearing nothing, each earned one with a red dot
TEST(wardrobe, an_old_save_wears_nothing_with_its_earned_ones_new) {
  Pet p;
  hatchedPet(p, 4);
  p.medals |= MED_LV10 | MED_BOND;
  p.saveNow();
  Preferences raw;
  raw.begin("tamapoke", false);
  raw.remove("acc");
  raw.remove("accseen");
  raw.end();
  Pet q;
  q.begin();
  CHECK_EQ(q.acc, (uint8_t)ACC_NONE);
  CHECK_EQ(q.accSeen, (uint16_t)0);
  CHECK_EQ(q.accUnlocked(), (uint16_t)((1u << ACC_CAP) | (1u << ACC_BOBBLE)));
}

// Range checks on load: a value no firmware writes, or one this pet hasn't earned, reads as nothing
TEST(wardrobe, the_save_is_checked_when_it_loads) {
  Pet p;
  hatchedPet(p, 4);
  p.medals |= MED_LV10;
  p.saveNow();
  struct Case {
    uint8_t acc;
    uint8_t want;
  } cases[] = { { ACC_CAP, ACC_CAP }, { ACC_CROWN, ACC_NONE }, { ACC_SANTA, ACC_NONE },
                { ACC_COUNT, ACC_NONE }, { 255, ACC_NONE } };
  for (const Case &c : cases) {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putUChar("acc", c.acc);
    raw.putUShort("accseen", 0xFFFF);
    raw.end();
    Pet q;
    q.begin();
    CHECK_MSG(q.acc == c.want, "acc " + std::to_string(c.acc) + " loads as " + std::to_string(q.acc));
    CHECK_EQ(q.accSeen, (uint16_t)(1u << ACC_CAP));  // only what it has
  }
}

// ---------------------------------------------------------------- the banner
TEST(wardrobe, an_accessory_banner_follows_its_medal) {
  BannerQueue q;
  q.add(MED_LV10 | MED_LV25, false, 1000, (1u << ACC_CAP) | (1u << ACC_TOPHAT));
  uint32_t t = 1000;
  const int8_t want[] = { 0, BANNER_ACC + ACC_CAP, 1, BANNER_ACC + ACC_TOPHAT };
  for (int8_t b : want) {
    CHECK_MSG(q.shown(t) == b, "banner " + std::to_string(b) + " (got " + std::to_string(q.shown(t)) + ")");
    t += MEDAL_BANNER_MS;
  }
  CHECK_EQ((int)q.shown(t), (int)BANNER_NONE);
}

// An accessory whose medal's banner already went (two of the Laurel's medals at once) comes after
// the medals left; the queue stays bounded: everything at once is each banner once
TEST(wardrobe, the_queue_with_accessories_is_each_once_then_none) {
  BannerQueue q;
  q.add(MED_ALL, true, 0, ACC_EARNED_MASK);
  std::set<int> seen;
  uint32_t t = 0;
  int n = 0;
  while (q.shown(t) != BANNER_NONE && n < 40) {
    int8_t b = q.shown(t);
    CHECK_MSG(seen.insert(b).second, "banner " + std::to_string(b) + " twice");
    t += BannerQueue::length(b);
    n++;
  }
  CHECK_EQ(n, MED_COUNT + 1 + ACC_EARNED);
  CHECK(q.shown(t) == BANNER_NONE);
}

TEST(wardrobe, the_banner_names_what_was_earned) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (uint8_t id = ACC_BOW; id <= ACC_LAUREL; id++) {
      const char *l1 = nullptr, *l2 = nullptr;
      bannerLines(BANNER_ACC + id, 7, &l1, &l2);
      char want[32];
      snprintf(want, sizeof(want), T(S_ACC_NEW_FMT), accName(id));
      std::string where = "[lang " + std::to_string(lang) + "] " + ACC_KEYS[id];
      CHECK_MSG(!strcmp(l1, want), where + ": " + l1);
      CHECK_MSG(!strcmp(l2, T(S_ACC_SWIPE)), where + ": the second line");
      // the banner is 320 px: size 3 is 18 px a letter, size 2 is 12; a letter's room clear each end
      CHECK_MSG(strlen(l1) * 18 <= 320 - 2 * 10, where + ": the first line fits");
      CHECK_MSG(strlen(l2) * CARD_LETTER_W <= 320 - 2 * 10, where + ": the second line fits");
    }
  }
  gLang = LANG_DEFAULT;
}

// ---------------------------------------------------------------- the Wardrobe page
TEST(wardrobe, the_tiles_sit_on_the_disc_above_the_lines) {
  for (uint8_t i = 0; i < WARD_TILES; i++) {
    CardRect r = wardTile(i);
    for (int cx : { (int)r.x, r.x + r.w }) {
      for (int cy : { (int)r.y, r.y + r.h }) {
        double d = std::hypot(cx - CARD_C, cy - CARD_C);
        CHECK_MSG(d <= CARD_R, "tile " + std::to_string(i) + " corner on the disc: " + std::to_string(d));
      }
    }
    CHECK(r.y + r.h < WARD_LINE_Y);
    CHECK_EQ(wardTileAt(r.x + r.w / 2, r.y + r.h / 2), (int)i);
    CHECK_EQ(wardTileAt(r.x, r.y), (int)i);
    CHECK_EQ(wardTileAt(r.x + r.w - 1, r.y + r.h - 1), (int)i);
  }
  CHECK_EQ(wardTileAt(WARD_GRID_X + WARD_TILE_W + 2, WARD_GRID_Y + 10), -1);  // a gap
  CHECK_EQ(wardTileAt(CARD_C, WARD_LINE_Y + 4), -1);
  CHECK(WARD_GRID_Y > WARD_TITLE_Y + 21);                 // under the size-3 title
  CHECK(WARD_LINE_Y + WARD_LINE_H + 14 < 369);            // the second line above the page dots
  CHECK(ACC_ART_MAX_W * WARD_ART_S <= WARD_TILE_W - 8);   // every art inside its tile
  CHECK(ACC_ART_MAX_H * WARD_ART_S <= WARD_TILE_H - 8);
  CHECK(WARD_TILES == ACC_LAUREL + 2);                    // Nothing, the seven, today's
  // the lines: WARD_LINE_MAX letters fit across the disc at the second line's bottom
  int y = WARD_LINE_Y + WARD_LINE_H + 14;
  CHECK(WARD_LINE_MAX * CARD_LETTER_W <= 2 * (int)std::sqrt((double)(CARD_R * CARD_R - (y - CARD_C) * (y - CARD_C))) - 24);
}

TEST(wardrobe, its_words_fit_in_every_language) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    std::string where = "[lang " + std::to_string(lang) + "] ";
    char buf[64];
    snprintf(buf, sizeof(buf), T(S_WARDROBE_FMT), ACC_EARNED, ACC_EARNED);
    CHECK_MSG(strlen(buf) * 18 <= 2 * 150, where + "the title: " + buf);
    CHECK_MSG((int)strlen(accName(ACC_NONE)) * CARD_LETTER_W <= WARD_TILE_W - 8, where + "the Nothing tile");
    for (uint8_t id = ACC_BOW; id < ACC_COUNT; id++) {
      const char *n = accName(id);
      CHECK_MSG(n && n[0], where + ACC_KEYS[id] + ": a name");
      snprintf(buf, sizeof(buf), T(S_ACC_WEARING_FMT), n);
      CHECK_MSG(strlen(buf) <= WARD_LINE_MAX, where + buf);
      snprintf(buf, sizeof(buf), T(S_ACC_TODAY_FMT), n);
      CHECK_MSG(strlen(buf) <= WARD_LINE_MAX, where + buf);
      if (accIsEarned(id)) {
        CHECK_MSG(accEarnText(id)[0], where + ACC_KEYS[id] + ": what earns it");
        snprintf(buf, sizeof(buf), T(S_ACC_HINT_FMT), n, accEarnText(id));
        CHECK_MSG(strlen(buf) <= WARD_LINE_MAX, where + buf);
      } else {
        CHECK_MSG(!accEarnText(id)[0], where + ACC_KEYS[id] + ": not earned");
      }
    }
  }
  gLang = LANG_DEFAULT;
}
