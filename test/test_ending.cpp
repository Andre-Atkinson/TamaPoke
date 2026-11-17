// Starting an ending (ending.h, #33): everything over the scene closes first, then the
// farewell, run-away or release starts. A pet that cannot end is left with what is open.
// The sketch's closeForEnding() (what closes) runs in the simulator, not here: these tests
// stand a recorder in for it.
#include "framework.h"
#include "shim/Arduino.h"
#include "../ending.h"
#include "../gamecore.h"  // GAME_KEEPY, GAME_PAR

static Pet *gPet = nullptr;
static int closes = 0;
static uint8_t ceremonyAtClose = 0xFF;  // the pet's ceremony when the overlays closed
static uint8_t speedPaid = 0;

static void closeRecord() {
  closes++;
  ceremonyAtClose = gPet->ceremony;
}

// a minigame open over the scene: closing it pays its score (gameEndNow in gameui.cpp)
static void closeAMinigame() {
  closeRecord();
  speedPaid = gPet->playGame(GAME_KEEPY, GAME_PAR[GAME_KEEPY]);
}

static void watch(Pet &p) {
  gPet = &p;
  closes = 0;
  ceremonyAtClose = 0xFF;
  speedPaid = 0;
}

// a freshly hatched easygoing Charmander with clean NVS (test_pet.cpp's makePet)
static void hatched(Pet &p) {
  mockNvsReset();
  mockSetMillis(0);
  p.begin();
  if (p.awaitingStarter()) p.chooseStarter(4);
  p.eggTap();
  p.eggTap();
  p.eggTap();
  p.nature = NAT_EASYGOING;
  watch(p);
}

static const uint8_t KINDS[3] = { CER_FAREWELL, CER_RUNAWAY, CER_RELEASE };
static std::string kindName(uint8_t k) {
  return k == CER_FAREWELL ? "farewell" : k == CER_RUNAWAY ? "run-away" : "release";
}

TEST(ending, closes_everything_before_the_ceremony_starts) {
  for (uint8_t k : KINDS) {
    Pet p;
    hatched(p);
    CHECK_MSG(startEnding(p, k, closeRecord), kindName(k) + ": started");
    CHECK_MSG(closes == 1, kindName(k) + ": closed once, not " + std::to_string(closes));
    CHECK_MSG(ceremonyAtClose == CER_NONE,
              kindName(k) + ": closed with the ceremony already on (" + std::to_string(ceremonyAtClose) + ")");
    CHECK_MSG(p.ceremony == k, kindName(k) + ": the ceremony on screen");
  }
}

// The reason for the order: the pet takes nothing once a ceremony is on, so a minigame
// closed after it started would pay nothing and keep no record.
TEST(ending, a_minigame_it_closes_keeps_its_score) {
  Pet p;
  hatched(p);
  CHECK(startEnding(p, CER_RUNAWAY, closeAMinigame));
  CHECK_MSG(speedPaid > 0, "the minigame's speed was paid");
  CHECK_EQ(p.gameHi, (uint16_t)GAME_PAR[GAME_KEEPY]);  // and its record kept
}

TEST(ending, each_kind_starts_its_own_ending) {
  for (uint8_t k : KINDS) {
    Pet p;
    hatched(p);
    CHECK(startEnding(p, k, closeRecord));
    CHECK_MSG(p.ceremony == k, kindName(k) + ": ceremony " + std::to_string(p.ceremony));
    CHECK_MSG(p.lastEnd == k, kindName(k) + ": lastEnd " + std::to_string(p.lastEnd));
  }
}

// BYE or RUN on an egg (a starter or egg choice is one too) starts nothing, so it closes
// nothing either: a gallery open over the egg stays open
TEST(ending, an_egg_is_left_with_its_overlays) {
  Pet p;
  mockNvsReset();
  mockSetMillis(0);
  p.begin();  // the starter choice
  watch(p);
  for (uint8_t k : KINDS) CHECK_MSG(!startEnding(p, k, closeRecord), kindName(k) + " on the starter choice");
  p.chooseStarter(4);  // an egg about to hatch
  for (uint8_t k : KINDS) CHECK_MSG(!startEnding(p, k, closeRecord), kindName(k) + " on an egg");
  CHECK_EQ(closes, 0);
  CHECK_EQ(p.ceremony, (uint8_t)CER_NONE);
}

TEST(ending, a_pet_already_ending_is_left_with_its_overlays) {
  Pet p;
  hatched(p);
  CHECK(startEnding(p, CER_FAREWELL, closeRecord));
  closes = 0;
  for (uint8_t k : KINDS) CHECK_MSG(!startEnding(p, k, closeRecord), kindName(k) + " during a farewell");
  CHECK_EQ(closes, 0);
  CHECK_EQ(p.ceremony, (uint8_t)CER_FAREWELL);
  CHECK_EQ(p.lastEnd, (uint8_t)CER_FAREWELL);
}

TEST(ending, an_unknown_kind_starts_nothing_and_closes_nothing) {
  Pet p;
  hatched(p);
  for (uint8_t k : { (uint8_t)CER_NONE, (uint8_t)(CER_RELEASE + 1), (uint8_t)0xFF })
    CHECK_MSG(!startEnding(p, k, closeRecord), "kind " + std::to_string(k));
  CHECK_EQ(closes, 0);
  CHECK_EQ(p.ceremony, (uint8_t)CER_NONE);
}
