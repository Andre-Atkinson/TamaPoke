// Walks (#117): the QMI8658's step count turned into steps today, walk treats, stamina and the
// walking ribbon (walks.h), and what the pet does with them (pet.h, Pet::walkMinute and
// Pet::spendTreat). The owner's decisions of 2026-11-18: counting only awake and off the charger
// (so not at bedtime either); 1 treat per 250 steps, at most 40 earned a day and 40 banked; +1
// stamina per 1,000 steps, at most 5 a day; the walking ribbon at 5,000 steps in a day; a treat
// lifts the first need under 40 to 80, off the charger, one a minute; a bubble after a walk of
// 1,000 steps and 10 quiet minutes.
#include "framework.h"
#include "shim/Arduino.h"
#include "shim/Preferences.h"
#include "../pet.h"
#include "../bubble.h"
#include "../bedtime.h"
#include "../i18n.h"
#include <string>

// ================================================================ the ledger (walks.h)

static const uint32_t D0 = 20400;  // a local day (2025-11-17...): epoch / 86400, inside the clock's range

// One minute of the ledger with the chip at `chip`, counting open, on day D0
static WalkGain look(WalkLedger &w, uint32_t chip, bool open = true, uint32_t day = D0) {
  return w.minute(chip, open, day);
}

TEST(walks, the_chip_delta_copes_with_a_restart_and_the_24_bit_size) {
  CHECK_EQ(walkChipDelta(100, 160), 60u);
  CHECK_EQ(walkChipDelta(100, 100), 0u);
  CHECK_EQ(walkChipDelta(0, 0), 0u);
  // the count started over (the engine restarted): the steps are the count itself
  CHECK_EQ(walkChipDelta(5000, 42), 42u);
  // only 24 bits are the chip's: the high bits of a wider read are not steps
  CHECK_EQ(walkChipDelta(0xFFFFFF00u, 0xFFFFFF40u), 0x40u);
  CHECK_EQ(walkChipDelta(0x01000010u, 0x01000030u), 0x20u);
  // ...and the bits past 24 of the last look are not steps either
  CHECK_EQ(walkChipDelta(0xFF000010u, 0x20u), 0x10u);
  CHECK_EQ(walkChipDelta(0x00FFFFF0u, 0x01000010u), 0x10u);
  // the counter wrapping at 2^24: the count after it, which undercounts the few steps before it
  CHECK_EQ(walkChipDelta(0xFFFFF0u, 0x000010u), 0x10u);
}

TEST(walks, the_first_look_only_sets_the_baseline) {
  WalkLedger w;
  WalkGain g = look(w, 5000);  // a chip already at 5000 when the pet first looks: not 5000 steps today
  CHECK_EQ(g.steps, (uint16_t)0);
  CHECK_EQ(w.steps, 0u);
  g = look(w, 5060);
  CHECK_EQ(g.steps, (uint16_t)60);
  CHECK_EQ(w.steps, 60u);
}

TEST(walks, start_sets_the_baseline_without_a_look) {
  WalkLedger w;
  w.start(30);
  WalkGain g = look(w, 100);
  CHECK_EQ(g.steps, (uint16_t)70);
}

TEST(walks, a_minute_adds_at_most_the_cap) {
  WalkLedger w;
  w.start(0);
  WalkGain g = look(w, WALK_MINUTE_MAX);
  CHECK_EQ(g.steps, (uint16_t)WALK_MINUTE_MAX);
  g = look(w, WALK_MINUTE_MAX + WALK_MINUTE_MAX + 1);  // 301 more than a minute can hold
  CHECK_EQ(g.steps, (uint16_t)WALK_MINUTE_MAX);
  CHECK_EQ(w.steps, 2u * WALK_MINUTE_MAX);
  g = look(w, 0xFFFFFF);  // a read gone wild adds the cap, not thousands
  CHECK_EQ(g.steps, (uint16_t)WALK_MINUTE_MAX);
  CHECK_EQ(w.steps, 3u * WALK_MINUTE_MAX);
}

TEST(walks, closed_counting_discards_the_steps_and_moves_the_baseline) {
  WalkLedger w;
  w.start(0);
  look(w, 100);
  CHECK_EQ(w.steps, 100u);
  WalkGain g = look(w, 400, false);  // 300 steps while closed (on USB, at bedtime)
  CHECK_EQ(g.steps, (uint16_t)0);
  CHECK_EQ(w.steps, 100u);
  g = look(w, 450);  // open again: only what came after
  CHECK_EQ(g.steps, (uint16_t)50);
  CHECK_EQ(w.steps, 150u);
}

TEST(walks, a_restart_of_the_chip_mid_day_loses_nothing_but_the_gap) {
  WalkLedger w;
  w.start(0);
  look(w, 200);
  WalkGain g = look(w, 30);  // the engine restarted and counted 30 since
  CHECK_EQ(g.steps, (uint16_t)30);
  CHECK_EQ(w.steps, 230u);
}

// ---- the day
TEST(walks, a_new_day_starts_the_steps_over_and_keeps_the_bank) {
  WalkLedger w;
  w.start(0);
  look(w, 300);
  look(w, 600);
  CHECK_EQ(w.steps, 600u);
  CHECK_EQ(w.bank, (uint8_t)2);
  CHECK_EQ(w.earned, (uint8_t)2);
  WalkGain g = look(w, 700, true, D0 + 1);
  CHECK(g.rolled);
  CHECK_EQ(w.steps, 100u);       // the new day's steps: only this minute's
  CHECK_EQ(w.earned, (uint8_t)0);
  CHECK_EQ(w.stamina, (uint8_t)0);
  CHECK_EQ(w.day, D0 + 1);
  CHECK_EQ(w.bank, (uint8_t)2);  // the bank keeps overnight
}

TEST(walks, the_day_rolls_even_in_a_minute_with_no_steps_or_a_closed_counter) {
  WalkLedger w;
  w.start(0);
  look(w, 100);
  WalkGain g = look(w, 100, false, D0 + 1);  // midnight at bedtime: nothing counted, the day still turns
  CHECK(g.rolled);
  CHECK_EQ(w.steps, 0u);
  CHECK_EQ(w.day, D0 + 1);
}

TEST(walks, a_stale_or_unknown_clock_never_rolls_the_day) {
  WalkLedger w;
  w.start(0);
  look(w, 100);
  CHECK_EQ(w.day, D0);
  WalkGain g = look(w, 200, true, 0);  // no trusted clock
  CHECK(!g.rolled);
  CHECK_EQ(w.steps, 200u);             // still counting into the stored day
  CHECK_EQ(w.day, D0);
  g = look(w, 300, true, 0);
  CHECK_EQ(w.steps, 300u);
  CHECK_EQ(w.day, D0);
}

TEST(walks, the_first_trusted_day_is_adopted_with_the_steps_so_far) {
  WalkLedger w;
  w.start(0);
  look(w, 120, true, 0);  // counted before any trusted clock: no day yet
  CHECK_EQ(w.day, 0u);
  WalkGain g = look(w, 150, true, D0);
  CHECK(!g.rolled);
  CHECK_EQ(w.day, D0);
  CHECK_EQ(w.steps, 150u);  // adopted, not thrown away
}

TEST(walks, a_clock_set_back_a_little_keeps_the_day_a_far_one_starts_it_afresh) {
  CHECK(!walkDayRolls(D0, D0));
  CHECK(!walkDayRolls(D0, D0 - 1));  // behind by a day or two: the streak's rule (registerCare)
  CHECK(!walkDayRolls(D0, D0 - 2));
  CHECK(walkDayRolls(D0, D0 - 3));   // the stored day was wrong (a far-future clock put right)
  CHECK(walkDayRolls(D0, D0 + 1));
  CHECK(walkDayRolls(D0, D0 + 400));
  CHECK(!walkDayRolls(D0, 0));       // no clock
  CHECK(!walkDayRolls(0, D0));       // no stored day: adopt
}

// ---- treats, stamina and the ribbon
TEST(walks, one_treat_per_250_steps) {
  WalkLedger w;
  w.start(0);
  WalkGain g = look(w, 249);
  CHECK_EQ(g.treats, (uint8_t)0);
  CHECK_EQ(w.bank, (uint8_t)0);
  g = look(w, 250);
  CHECK_EQ(g.treats, (uint8_t)1);
  CHECK_EQ(w.bank, (uint8_t)1);
  g = look(w, 500);
  CHECK_EQ(g.treats, (uint8_t)1);
  CHECK_EQ(w.bank, (uint8_t)2);
  CHECK_EQ(w.earned, (uint8_t)2);
  g = look(w, 749);
  CHECK_EQ(g.treats, (uint8_t)0);
  g = look(w, 750);
  CHECK_EQ(g.treats, (uint8_t)1);
  CHECK_EQ(w.bank, (uint8_t)3);
}

TEST(walks, a_minute_can_earn_more_than_one_treat) {
  WalkLedger w;
  w.start(0);
  look(w, 200);
  WalkGain g = look(w, 500);  // 300 steps: 200 -> 500 crosses 250 and 500
  CHECK_EQ(g.treats, (uint8_t)2);
  CHECK_EQ(w.bank, (uint8_t)2);
}

// Walks `steps` in minutes of the cap, one after another
static void walkSteps(WalkLedger &w, uint32_t &chip, uint32_t steps, uint32_t day = D0) {
  while (steps) {
    uint32_t n = steps > WALK_MINUTE_MAX ? WALK_MINUTE_MAX : steps;
    chip += n;
    w.minute(chip, true, day);
    steps -= n;
  }
}

TEST(walks, at_most_40_treats_are_earned_a_day) {
  WalkLedger w;
  uint32_t chip = 0;
  w.start(0);
  walkSteps(w, chip, 40 * 250 - 1);
  CHECK_EQ(w.earned, (uint8_t)39);
  walkSteps(w, chip, 1);
  CHECK_EQ(w.earned, (uint8_t)40);
  CHECK_EQ(w.bank, (uint8_t)40);
  w.bank = 0;  // spent them all: the day's cap still holds
  walkSteps(w, chip, 5000);
  CHECK_EQ(w.earned, (uint8_t)WALK_TREATS_DAY_MAX);
  CHECK_EQ(w.bank, (uint8_t)0);
  // the next day earns again
  walkSteps(w, chip, 250, D0 + 1);
  CHECK_EQ(w.earned, (uint8_t)1);
  CHECK_EQ(w.bank, (uint8_t)1);
}

TEST(walks, the_bank_holds_at_most_40_and_the_excess_is_lost) {
  WalkLedger w;
  uint32_t chip = 0;
  w.start(0);
  walkSteps(w, chip, 40 * 250);
  CHECK_EQ(w.bank, (uint8_t)40);
  WalkGain g;
  chip += 250;
  g = w.minute(chip, true, D0 + 1);  // a new day, a full bank: the treat has nowhere to go
  CHECK_EQ(g.treats, (uint8_t)0);
  CHECK_EQ(w.bank, (uint8_t)40);
  CHECK_EQ(w.earned, (uint8_t)1);    // but the day counts it earned
  // a bank with room takes only what fits
  WalkLedger v;
  uint32_t c2 = 0;
  v.start(0);
  v.bank = 39;
  walkSteps(v, c2, 500);
  CHECK_EQ(v.bank, (uint8_t)40);
  CHECK_EQ(v.earned, (uint8_t)2);
}

TEST(walks, spend_takes_one_from_the_bank) {
  WalkLedger w;
  CHECK(!w.spend());
  w.bank = 2;
  CHECK(w.spend());
  CHECK_EQ(w.bank, (uint8_t)1);
  CHECK(w.spend());
  CHECK(!w.spend());
  CHECK_EQ(w.bank, (uint8_t)0);
}

TEST(walks, stamina_is_one_point_per_1000_steps_at_most_5_a_day) {
  WalkLedger w;
  uint32_t chip = 0;
  w.start(0);
  walkSteps(w, chip, 999);
  CHECK_EQ(w.stamina, (uint8_t)0);
  walkSteps(w, chip, 1);
  CHECK_EQ(w.stamina, (uint8_t)1);
  WalkLedger v;
  uint32_t c2 = 0;
  v.start(0);
  int total = 0;
  for (int i = 0; i < 40; i++) {  // 12,000 steps
    uint32_t before = c2;
    c2 += 300;
    total += v.minute(c2, true, D0).stamina;
    (void)before;
  }
  CHECK_EQ(total, 5);
  CHECK_EQ(v.stamina, (uint8_t)WALK_STAMINA_DAY_MAX);
  c2 += 300;
  total += v.minute(c2, true, D0 + 1).stamina;  // a new day: counted from nothing again
  CHECK_EQ(total, 5);
  CHECK_EQ(v.stamina, (uint8_t)0);
  while (v.steps < 1000) { c2 += 300; total += v.minute(c2, true, D0 + 1).stamina; }
  CHECK_EQ(total, 6);
}

TEST(walks, the_ribbon_is_5000_steps_in_one_day) {
  WalkLedger w;
  uint32_t chip = 0;
  w.start(0);
  walkSteps(w, chip, WALK_RIBBON_STEPS - 1);
  CHECK(!w.ribbon());
  walkSteps(w, chip, 1);
  CHECK(w.ribbon());
  // another day: the day's count is what counts (the pet's medal, once earned, is kept by Pet)
  walkSteps(w, chip, 100, D0 + 1);
  CHECK(!w.ribbon());
}

// ---- the speech bubble
static void quietMinutes(WalkBubble &b, int n) {
  for (int i = 0; i < n; i++) b.minute(0);
}

TEST(walks, the_bubble_comes_after_1000_steps_and_10_quiet_minutes) {
  WalkBubble b;
  for (int i = 0; i < 4; i++) b.minute(250);  // 1,000 steps
  quietMinutes(b, WALK_SAY_QUIET_MIN - 1);
  CHECK(!b.due());
  quietMinutes(b, 1);
  CHECK(b.due());
  b.said();
  CHECK(!b.due());
  quietMinutes(b, 30);
  CHECK_MSG(!b.due(), "once said, it says nothing more until another walk");
}

TEST(walks, a_walk_under_1000_steps_says_nothing) {
  WalkBubble b;
  b.minute(999);
  quietMinutes(b, 60);
  CHECK(!b.due());
  // and it is forgotten: 999 now does not add to the next walk's
  b.minute(500);
  quietMinutes(b, 60);
  CHECK(!b.due());
}

TEST(walks, steps_in_the_quiet_stretch_keep_the_walk_going) {
  WalkBubble b;
  b.minute(600);
  quietMinutes(b, WALK_SAY_QUIET_MIN - 1);
  b.minute(10);  // a few steps round the house: the quiet count starts again
  quietMinutes(b, WALK_SAY_QUIET_MIN - 1);
  CHECK(!b.due());
  b.minute(400);  // the same walk: 600 + 10 + 400
  quietMinutes(b, WALK_SAY_QUIET_MIN - 1);
  CHECK(!b.due());
  quietMinutes(b, 1);
  CHECK(b.due());
}

TEST(walks, the_next_walk_starts_from_nothing_and_a_pending_line_waits_to_be_said) {
  WalkBubble b;
  b.minute(1000);
  quietMinutes(b, WALK_SAY_QUIET_MIN);
  CHECK(b.due());
  b.minute(300);  // walking again before the line was said: it still waits
  CHECK(b.due());
  b.said();
  quietMinutes(b, 60);
  CHECK_MSG(!b.due(), "the 300 of the new walk are not a walk of 1,000");
}

TEST(walks, the_ledgers_minutes_feed_the_bubble_and_a_closed_counter_is_quiet) {
  WalkLedger w;
  uint32_t chip = 0;
  w.start(0);
  walkSteps(w, chip, 1200);
  CHECK(!w.say.due());
  for (int i = 0; i < WALK_SAY_QUIET_MIN - 1; i++) w.minute(chip, true, D0);
  CHECK(!w.say.due());
  // walking while closed (on the charger) is thrown away: it neither extends the walk nor
  // counts as steps, so the walk ends there
  chip += 100;
  w.minute(chip, false, D0);
  CHECK(w.say.due());
}

// ---- NVS range checks (WalkLedger::load)
TEST(walks, load_keeps_values_in_range) {
  WalkLedger w;
  w.load(3000, D0, 25, 12, 3, 20089, 47482);
  CHECK_EQ(w.steps, 3000u);
  CHECK_EQ(w.day, D0);
  CHECK_EQ(w.bank, (uint8_t)25);
  CHECK_EQ(w.earned, (uint8_t)12);
  CHECK_EQ(w.stamina, (uint8_t)3);
}

TEST(walks, load_reads_out_of_range_values_as_nothing) {
  WalkLedger w;
  w.load(WALK_DAY_STEPS_MAX + 1, D0, 0, 0, 0, 20089, 47482);
  CHECK_EQ(w.steps, 0u);  // more than a day can hold
  w.load(WALK_DAY_STEPS_MAX, D0, 0, 0, 0, 20089, 47482);
  CHECK_EQ(w.steps, WALK_DAY_STEPS_MAX);
  w.load(0xFFFFFFFFu, D0, 0, 0, 0, 20089, 47482);
  CHECK_EQ(w.steps, 0u);
  w.load(100, 20088, 0, 0, 0, 20089, 47482);  // a day before the clock's range
  CHECK_EQ(w.day, 0u);
  w.load(100, 47482, 0, 0, 0, 20089, 47482);  // the first day past it
  CHECK_EQ(w.day, 0u);
  w.load(100, 20089, 0, 0, 0, 20089, 47482);
  CHECK_EQ(w.day, 20089u);
  w.load(100, D0, WALK_BANK_MAX + 1, 0, 0, 20089, 47482);
  CHECK_EQ(w.bank, (uint8_t)0);
  w.load(100, D0, 255, 0, 0, 20089, 47482);
  CHECK_EQ(w.bank, (uint8_t)0);
  w.load(100, D0, WALK_BANK_MAX, 0, 0, 20089, 47482);
  CHECK_EQ(w.bank, (uint8_t)WALK_BANK_MAX);
}

TEST(walks, load_never_lets_earned_or_stamina_exceed_what_the_steps_allow) {
  WalkLedger w;
  w.load(600, D0, 0, 200, 0, 20089, 47482);   // 600 steps earned 2 treats at most
  CHECK_EQ(w.earned, (uint8_t)2);
  w.load(600, D0, 0, 3, 0, 20089, 47482);
  CHECK_EQ(w.earned, (uint8_t)2);
  w.load(600, D0, 0, 1, 0, 20089, 47482);     // fewer than allowed is as saved
  CHECK_EQ(w.earned, (uint8_t)1);
  w.load(WALK_DAY_STEPS_MAX, D0, 0, 255, 0, 20089, 47482);
  CHECK_EQ(w.earned, (uint8_t)WALK_TREATS_DAY_MAX);  // never past the day's cap
  w.load(2500, D0, 0, 0, 9, 20089, 47482);    // 2,500 steps earned 2 stamina points at most
  CHECK_EQ(w.stamina, (uint8_t)2);
  w.load(WALK_DAY_STEPS_MAX, D0, 0, 0, 255, 20089, 47482);
  CHECK_EQ(w.stamina, (uint8_t)WALK_STAMINA_DAY_MAX);
  w.load(0, D0, 0, 5, 5, 20089, 47482);       // no steps: nothing earned
  CHECK_EQ(w.earned, (uint8_t)0);
  CHECK_EQ(w.stamina, (uint8_t)0);
}

// ================================================================ the bubble's line (bubble.h)

static BubbleIn calmIn() {
  BubbleIn in;
  in.needs = attnLevels(80, 80, 80, 80);
  in.clockKnown = false;
  in.hour = 13;
  in.named = false;
  in.last = BUB_NONE;
  return in;
}

TEST(walkbubble, the_walk_line_is_said_when_due_whatever_the_roll) {
  BubbleIn in = calmIn();
  in.walk = true;
  for (uint32_t r = 0; r < 1000; r += 3) CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_WALK);
  in.walk = false;
  for (uint32_t r = 0; r < 1000; r += 3) CHECK(bubblePick(in, r) != BUB_WALK);
}

TEST(walkbubble, a_low_need_or_sickness_or_a_greeting_comes_first_then_the_walk_beats_the_milk_hint) {
  BubbleIn in = calmIn();
  in.walk = true;
  in.needs = attnLevels(24, 80, 80, 80);
  CHECK_EQ(bubblePick(in, 5), (uint8_t)BUB_HUNGRY);
  in = calmIn();
  in.walk = true;
  in.sick = true;
  CHECK_EQ(bubblePick(in, 5), (uint8_t)BUB_SICK);
  in = calmIn();
  in.walk = true;
  in.greet = BUB_CHRISTMAS;
  CHECK_EQ(bubblePick(in, 5), (uint8_t)BUB_CHRISTMAS);
  in = calmIn();
  in.walk = true;
  in.milkHint = true;
  CHECK_EQ(bubblePick(in, 5), (uint8_t)BUB_WALK);
  in.hatchLine = true;  // the nature's naming line after a hatch goes first
  CHECK_EQ(bubblePick(in, 5), (uint8_t)bubbleNatureLine(in.nature, 0));
}

TEST(walkbubble, the_line_has_its_key_and_a_string_in_both_languages) {
  CHECK_EQ(std::string(BUBBLE_KEYS[BUB_WALK]), std::string("walk"));
  CHECK_EQ((int)S_SAY_HUNGRY + (int)BUB_WALK, (int)S_SAY_WALK);
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    CHECK(T(S_SAY_WALK)[0]);
  }
  gLang = LANG_EN;
  CHECK_EQ(std::string(T(S_SAY_WALK)), std::string("That was a lovely walk!"));
  gLang = LANG_DEFAULT;
}

// ================================================================ the pet (pet.h, pet.cpp)

static const uint32_t LONG_IDLE_MS = 60UL * 60 * 1000;

static uint32_t gChip = 0;
static bool gChipOk = true;
static bool chipSource(uint32_t *c) {
  if (!gChipOk) return false;
  *c = gChip;
  return true;
}

static uint32_t noonOf(uint32_t day) { return day * 86400UL + 12UL * 3600; }
static uint32_t atHour(uint32_t day, uint32_t hour) { return day * 86400UL + hour * 3600UL; }

// what the sketch tells the pet on every pass
struct Env {
  bool pmu = true, usb = false, clockSet = true;
  uint32_t idle = 0;
};
static Env gEnv;

static void pass(Pet &p) { p.setCharger(gEnv.pmu, gEnv.usb, gEnv.idle, gEnv.clockSet); }

// A hatched pet with the step counter at 0, noon of day D0, awake and off the charger. No
// random poops (random(100) < 15 never holds).
static void walker(Pet &p) {
  mockNvsReset();
  mockSetMillis(0);
  mockForceRandom(99);
  gChip = 0;
  gChipOk = true;
  gEnv = Env();
  p.begin();
  if (p.awaitingStarter()) p.chooseStarter(4);
  p.eggTap();
  p.eggTap();
  p.eggTap();
  p.nature = NAT_EASYGOING;
  p.lastSeenEpoch = noonOf(D0);
  p.stepSource = chipSource;
  p.walkStart();
  pass(p);
}

static void done() { mockClearForcedRandom(); }

static void setNeeds(Pet &p, uint8_t f, uint8_t j, uint8_t e, uint8_t h) {
  p.fullness = f;
  p.joy = j;
  p.energy = e;
  p.hygiene = h;
}

// One minute with `steps` more on the chip; the needs put back at 100 first, unless keep
static void minute(Pet &p, uint32_t steps, bool topUp = true) {
  gChip += steps;
  if (topUp) setNeeds(p, 100, 100, 100, 100);
  pass(p);
  mockAdvanceMillis(PET_TICK_MS);
  p.update(millis());
}

// one minute with the needs as they are
static void minuteAsIs(Pet &p, uint32_t steps = 0) { minute(p, steps, false); }

TEST(petwalk, steps_off_the_charger_count_and_a_failed_read_counts_nothing) {
  Pet p;
  walker(p);
  minute(p, 120);
  CHECK_EQ(p.walk.steps, 120u);
  CHECK_EQ(p.walk.day, D0);
  gChipOk = false;
  minute(p, 200);  // the chip moved on, the read failed: nothing, and the baseline stays
  CHECK_EQ(p.walk.steps, 120u);
  gChipOk = true;
  minute(p, 0);    // the next good read has the whole 200
  CHECK_EQ(p.walk.steps, 320u);
  done();
}

TEST(petwalk, no_step_source_leaves_everything_alone) {  // the original 1.75: no counter
  Pet p;
  walker(p);
  p.stepSource = nullptr;
  minute(p, 500);
  CHECK_EQ(p.walk.steps, 0u);
  CHECK_EQ(p.walk.day, 0u);
  CHECK_EQ(p.walk.bank, (uint8_t)0);
  CHECK(!p.hasMedal(MED_WALK));
  done();
}

TEST(petwalk, on_the_charger_nothing_counts_and_the_steps_are_not_saved_up) {
  Pet p;
  walker(p);
  minute(p, 100);
  gEnv.usb = true;
  minute(p, 250);
  minute(p, 250);
  CHECK_EQ(p.walk.steps, 100u);
  CHECK(!p.walkCounting());
  gEnv.usb = false;
  minute(p, 40);
  CHECK_MSG(p.walk.steps == 140u, "only the 40 after the cable came out: " + std::to_string(p.walk.steps));
  CHECK(p.walkCounting());
  done();
}

// STEPS? prints walkCounting(): with no step counter (the original 1.75, or a 1.75C whose sensor
// did not start) nothing is counted, so it must not say counting is open.
TEST(petwalk, with_no_step_source_counting_is_not_open) {
  Pet p;
  walker(p);
  CHECK(p.walkCounting());
  p.stepSource = nullptr;
  CHECK(!p.walkCounting());
  minute(p, 250);
  CHECK_EQ(p.walk.steps, 0u);
  done();
}

TEST(petwalk, a_pmu_that_never_answered_counts_as_unplugged) {  // as self-care (usbPresent() reads true)
  Pet p;
  walker(p);
  gEnv.pmu = false;
  gEnv.usb = true;
  minute(p, 90);
  CHECK_EQ(p.walk.steps, 90u);
  done();
}

TEST(petwalk, bedtime_closes_the_counter_and_no_bedtime_or_a_stale_clock_leaves_it_open) {
  Pet p;
  walker(p);
  p.setBedPreset(1);  // 21:00 - 07:00
  p.lastSeenEpoch = atHour(D0, 12);
  minute(p, 50);
  CHECK_EQ(p.walk.steps, 50u);
  p.lastSeenEpoch = atHour(D0, 22);
  minute(p, 80);
  CHECK_EQ(p.walk.steps, 50u);
  CHECK(!p.walkCounting());
  p.lastSeenEpoch = atHour(D0 + 1, 6);  // still the night (and the next day: the day rolls, nothing counts)
  minute(p, 80);
  CHECK_EQ(p.walk.steps, 0u);
  p.lastSeenEpoch = atHour(D0 + 1, 7);  // 07:00: the window has ended
  minute(p, 30);
  CHECK_EQ(p.walk.steps, 30u);
  // the preset off: no window, so open at 22:00
  p.setBedPreset(0);
  p.lastSeenEpoch = atHour(D0 + 1, 22);
  minute(p, 20);
  CHECK_EQ(p.walk.steps, 50u);
  // a stale clock has no bedtime: open all day, at 22:00 with the schedule on
  p.setBedPreset(1);
  gEnv.clockSet = false;
  minute(p, 10);
  CHECK_EQ(p.walk.steps, 60u);
  CHECK(p.walkCounting());
  done();
}

TEST(petwalk, the_day_rolls_with_the_clock_and_a_stale_clock_never_rolls) {
  Pet p;
  walker(p);
  minute(p, 100);
  p.lastSeenEpoch = noonOf(D0 + 1);
  minute(p, 30);
  CHECK_EQ(p.walk.day, D0 + 1);
  CHECK_EQ(p.walk.steps, 30u);
  gEnv.clockSet = false;
  p.lastSeenEpoch = noonOf(D0 + 5);  // a reseeded clock running ahead: not trusted
  minute(p, 30);
  CHECK_EQ(p.walk.day, D0 + 1);
  CHECK_EQ(p.walk.steps, 60u);
  gEnv.clockSet = true;  // the player sets the clock: today is a new day
  minute(p, 30);
  CHECK_EQ(p.walk.day, D0 + 5);
  CHECK_EQ(p.walk.steps, 30u);
  done();
}

TEST(petwalk, the_day_is_the_one_the_streak_counts_by) {
  Pet p;
  walker(p);
  p.caress();  // the day's care: lastCareDay
  minute(p, 10);
  CHECK_EQ(p.lastCareDay, p.walk.day);
  done();
}

// (all inside 29 minutes: STAMINA_CARE_MIN good minutes of steady care would add a point of their own)
TEST(petwalk, stamina_training_comes_from_steps_up_to_5_a_day_and_never_past_100) {
  Pet p;
  walker(p);
  CHECK_EQ(p.trDef, (uint8_t)0);
  for (int i = 0; i < 3; i++) minute(p, 300);
  CHECK_EQ(p.trDef, (uint8_t)0);  // 900 steps
  minute(p, 100);
  CHECK_EQ(p.trDef, (uint8_t)1);  // 1,000
  for (int i = 0; i < 13; i++) minute(p, 300);
  CHECK_EQ(p.walk.steps, 4900u);
  CHECK_EQ(p.trDef, (uint8_t)4);
  minute(p, 100);
  CHECK_EQ(p.trDef, (uint8_t)5);  // 5,000: five points
  for (int i = 0; i < 10; i++) minute(p, 300);  // 3,000 more: the day's cap
  CHECK_EQ(p.trDef, (uint8_t)5);
  CHECK_EQ(p.walk.stamina, (uint8_t)WALK_STAMINA_DAY_MAX);
  // a new day's five points stop at 100
  p.lastSeenEpoch = noonOf(D0 + 1);
  p.trDef = 98;
  for (int i = 0; i < 17; i++) minute(p, 300);
  CHECK_EQ(p.trDef, (uint8_t)100);
  done();
}

TEST(petwalk, the_walking_ribbon_comes_at_5000_steps_and_is_kept) {
  Pet p;
  walker(p);
  for (int i = 0; i < 16; i++) minute(p, 300);  // 4,800
  minute(p, 199);
  CHECK_EQ(p.walk.steps, 4999u);
  CHECK(!p.hasMedal(MED_WALK));
  uint16_t total = p.totalMedals;
  minute(p, 1);
  CHECK(p.hasMedal(MED_WALK));
  CHECK_EQ(p.totalMedals, (uint16_t)(total + 1));
  p.lastSeenEpoch = noonOf(D0 + 1);
  minute(p, 10);
  CHECK_MSG(p.hasMedal(MED_WALK), "a medal once earned stays");
  CHECK_MSG(!(MED_BY_TIME & MED_WALK), "it is the player's doing, not time's");
  CHECK_EQ(accUnlockMask(MED_WALK), (uint16_t)0);  // earns no accessory
  done();
}

TEST(petwalk, the_treat_bank_fills_and_a_new_pet_starts_with_none) {
  Pet p;
  walker(p);
  for (int i = 0; i < 4; i++) minute(p, 300);  // 1,200 steps: 4 treats
  CHECK_EQ(p.walk.bank, (uint8_t)4);
  CHECK_EQ(p.walk.earned, (uint8_t)4);
  p.newEgg();
  CHECK_EQ(p.walk.bank, (uint8_t)0);
  CHECK_MSG(p.walk.steps == 1200u, "the day's steps are the player's: they stay");
  done();
}

TEST(petwalk, steps_stamina_and_treats_are_saved_and_a_walk_asks_for_a_save) {
  Pet p;
  walker(p);
  p.saveNow();
  CHECK(!p.savePending());
  minute(p, 100);
  CHECK_MSG(!p.savePending(), "steps alone wait for the periodic save, they don't ask for one");
  minute(p, 250);
  CHECK_MSG(p.savePending(), "a treat earned asks for the next quiet save");
  p.saveNow();
  Pet q;
  q.begin();
  CHECK_EQ(q.walk.steps, p.walk.steps);
  CHECK_EQ(q.walk.day, (uint32_t)D0);
  CHECK_EQ(q.walk.bank, (uint8_t)1);
  CHECK_EQ(q.walk.earned, (uint8_t)1);
  CHECK_EQ(q.walk.stamina, (uint8_t)0);
  done();
}

TEST(petwalk, a_save_from_before_walks_loads_with_a_day_of_nothing) {
  Pet a;
  walker(a);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.remove("wsteps");
    raw.remove("wday");
    raw.remove("wbank");
    raw.remove("wearn");
    raw.remove("wsta");
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_EQ(b.walk.steps, 0u);
  CHECK_EQ(b.walk.day, 0u);
  CHECK_EQ(b.walk.bank, (uint8_t)0);
  CHECK_EQ(b.walk.earned, (uint8_t)0);
  CHECK_EQ(b.walk.stamina, (uint8_t)0);
  done();
}

// every key is range-checked as it loads (Pet::load)
TEST(petwalk, every_nvs_key_is_range_checked_on_load) {
  Pet a;
  walker(a);
  auto put = [](uint32_t steps, uint32_t day, uint8_t bank, uint8_t earn, uint8_t sta) {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putUInt("wsteps", steps);
    raw.putUInt("wday", day);
    raw.putUChar("wbank", bank);
    raw.putUChar("wearn", earn);
    raw.putUChar("wsta", sta);
    raw.end();
  };
  put(3000, D0, 25, 12, 3);  // all good
  {
    Pet b;
    b.begin();
    CHECK_EQ(b.walk.steps, 3000u);
    CHECK_EQ(b.walk.day, (uint32_t)D0);
    CHECK_EQ(b.walk.bank, (uint8_t)25);
    CHECK_EQ(b.walk.earned, (uint8_t)12);
    CHECK_EQ(b.walk.stamina, (uint8_t)3);
  }
  put(0xFFFFFFFFu, 5, 200, 255, 255);  // damaged: every value out of range
  {
    Pet b;
    b.begin();
    CHECK_EQ(b.walk.steps, 0u);
    CHECK_EQ(b.walk.day, 0u);
    CHECK_EQ(b.walk.bank, (uint8_t)0);
    CHECK_EQ(b.walk.earned, (uint8_t)0);
    CHECK_EQ(b.walk.stamina, (uint8_t)0);
  }
  put(600, 0xFFFFFFFFu, WALK_BANK_MAX + 1, 99, 99);  // a day past the clock's range; counts past the steps
  {
    Pet b;
    b.begin();
    CHECK_EQ(b.walk.steps, 600u);
    CHECK_EQ(b.walk.day, 0u);
    CHECK_EQ(b.walk.bank, (uint8_t)0);
    CHECK_EQ(b.walk.earned, (uint8_t)2);
    CHECK_EQ(b.walk.stamina, (uint8_t)0);
  }
  done();
}

// ---- spending (Pet::spendTreat, tick)
TEST(treat, a_need_under_40_goes_up_to_80_and_costs_one_treat) {
  Pet p;
  walker(p);
  p.walk.bank = 3;
  setNeeds(p, 41, 80, 80, 100);  // food 41 -> 39 this minute
  minuteAsIs(p);
  CHECK_EQ(p.fullness, (uint8_t)80);  // the level itself, not the macro: 80 (the owner, 2026-11-18)
  CHECK_EQ(p.joy, (uint8_t)79);  // the others only drained
  CHECK_EQ(p.walk.bank, (uint8_t)2);
  CHECK(p.savePending());
  done();
}

// The poops go with a bath (liftNeed, the same as self-care's and the player's clean()): a
// treat spent on hygiene leaves none on the ground, a treat spent on anything else leaves them.
// Not the player's care either way: no streak, bond or heart (is_not_the_players_care below).
TEST(treat, a_treat_spent_on_hygiene_clears_the_poops_and_the_other_needs_leave_them) {
  {
    Pet p;
    walker(p);
    p.walk.bank = 2;
    setNeeds(p, 80, 80, 80, 41);  // hygiene 41 -> 36 with a poop on the ground: the first need under 40
    p.poops = 1;
    minuteAsIs(p);
    CHECK_EQ(p.hygiene, (uint8_t)80);
    CHECK_EQ(p.poops, (uint8_t)0);
    CHECK_EQ(p.walk.bank, (uint8_t)1);
    CHECK(p.savePending());
    CHECK(!p.showHeart());  // a bath that wakes nothing: no heart, no care
  }
  for (int need = 0; need < 3; need++) {  // food, energy, joy
    Pet p;
    walker(p);
    p.walk.bank = 2;
    p.poops = 2;
    uint8_t f = 80, j = 80, e = 80;
    if (need == 0) f = 20;
    if (need == 1) e = 20;
    if (need == 2) j = 20;
    setNeeds(p, f, j, e, 100);  // hygiene stays well over 40 after the drain
    minuteAsIs(p);
    CHECK_MSG(p.walk.bank == 1, "the treat was spent, need " + std::to_string(need));
    CHECK_MSG(p.poops == 2, "the poops stay, need " + std::to_string(need));
  }
  done();
}

// Self-care on the charger is the same job at its own level: 50, and its bath clears the poops
// as it always has (hygiene lifted by it, not by a treat).
TEST(treat, self_care_still_lifts_to_50_and_its_bath_still_clears_the_poops) {
  Pet p;
  walker(p);
  p.walk.bank = 3;
  gEnv.usb = true;
  gEnv.idle = LONG_IDLE_MS;
  setNeeds(p, 80, 80, 80, 41);
  p.poops = 1;
  minuteAsIs(p);
  CHECK_EQ(p.hygiene, (uint8_t)50);
  CHECK_EQ(p.poops, (uint8_t)0);
  CHECK_EQ(p.walk.bank, (uint8_t)3);  // no treat spent
  done();
}

TEST(treat, a_need_at_40_or_more_is_left_alone_and_costs_nothing) {
  Pet p;
  walker(p);
  p.walk.bank = 3;
  setNeeds(p, 42, 41, 41, 41);  // all exactly 40 after the drain
  minuteAsIs(p);
  CHECK_EQ(p.fullness, (uint8_t)40);
  CHECK_EQ(p.walk.bank, (uint8_t)3);
  CHECK(!p.treatHeld());
  done();
}

TEST(treat, no_treats_banked_no_lift) {
  Pet p;
  walker(p);
  setNeeds(p, 20, 80, 80, 100);
  minuteAsIs(p);
  CHECK(p.fullness < 40);
  done();
}

TEST(treat, food_then_hygiene_then_energy_then_joy_one_a_minute) {
  Pet p;
  walker(p);
  p.walk.bank = 10;
  setNeeds(p, 20, 20, 20, 20);
  p.poops = 2;
  minuteAsIs(p);
  CHECK_EQ(p.fullness, (uint8_t)80);
  CHECK(p.hygiene < 40 && p.energy < 40 && p.joy < 40);
  CHECK_EQ(p.walk.bank, (uint8_t)9);
  minuteAsIs(p);
  CHECK_EQ(p.hygiene, (uint8_t)80);
  CHECK_EQ(p.poops, (uint8_t)0);  // a bath clears them, as self-care's
  CHECK(p.energy < 40 && p.joy < 40);
  minuteAsIs(p);
  CHECK_EQ(p.energy, (uint8_t)80);
  CHECK(p.joy < 40);
  minuteAsIs(p);
  CHECK_EQ(p.joy, (uint8_t)80);
  CHECK_EQ(p.walk.bank, (uint8_t)6);
  done();
}

TEST(treat, not_on_the_charger_where_self_care_does_the_job) {
  Pet p;
  walker(p);
  p.walk.bank = 5;
  gEnv.usb = true;
  gEnv.idle = 0;  // plugged in but touched: self-care waits, and so do the treats
  setNeeds(p, 20, 80, 80, 100);
  minuteAsIs(p);
  CHECK(p.fullness < 40);
  CHECK_EQ(p.walk.bank, (uint8_t)5);
  gEnv.idle = LONG_IDLE_MS;  // untouched: self-care lifts it, the bank still isn't touched
  setNeeds(p, 20, 80, 80, 100);
  minuteAsIs(p);
  CHECK_EQ(p.fullness, (uint8_t)50);  // self-care keeps its 50: a treat is the only thing that goes to 80
  CHECK_EQ(p.walk.bank, (uint8_t)5);
  CHECK_MSG(!p.treatHeld(), "self-care is not a treat");
  done();
}

TEST(treat, a_pmu_that_never_answered_counts_as_unplugged_so_a_treat_is_spent) {
  Pet p;
  walker(p);
  p.walk.bank = 2;
  gEnv.pmu = false;
  gEnv.usb = true;
  gEnv.idle = LONG_IDLE_MS;
  setNeeds(p, 20, 80, 80, 100);
  minuteAsIs(p);
  CHECK_EQ(p.fullness, (uint8_t)WALK_TREAT_LEVEL);
  CHECK_EQ(p.walk.bank, (uint8_t)1);
  done();
}

TEST(treat, never_asleep_and_it_waits_for_the_morning) {
  Pet p;
  walker(p);
  p.walk.bank = 4;
  p.toggleLight();  // asleep
  setNeeds(p, 20, 80, 80, 100);  // under the sleep floor: stays 20
  for (int i = 0; i < 5; i++) minuteAsIs(p);
  CHECK_EQ(p.fullness, (uint8_t)20);
  CHECK_EQ(p.walk.bank, (uint8_t)4);
  CHECK(p.autoWake());
  minuteAsIs(p);
  CHECK_EQ(p.fullness, (uint8_t)WALK_TREAT_LEVEL);
  CHECK_EQ(p.walk.bank, (uint8_t)3);
  done();
}

TEST(treat, never_as_an_egg) {
  mockNvsReset();
  mockSetMillis(0);
  Pet p;
  p.begin();
  p.chooseStarter(4);  // an egg that hatches at 3 minutes
  p.walk.bank = 4;
  setNeeds(p, 20, 20, 20, 20);
  pass(p);
  for (int i = 0; i < 2; i++) { mockAdvanceMillis(PET_TICK_MS); p.update(millis()); }
  CHECK(p.isEgg());
  CHECK_EQ(p.fullness, (uint8_t)20);
  CHECK_EQ(p.walk.bank, (uint8_t)4);
}

TEST(treat, never_during_a_ceremony) {
  Pet p;
  walker(p);
  p.walk.bank = 4;
  setNeeds(p, 20, 20, 20, 20);
  mockAdvanceMillis(PET_TICK_MS - 5);
  p.update(millis());
  p.release();
  mockAdvanceMillis(5);
  p.update(millis());
  CHECK(p.ceremony != CER_NONE);
  CHECK_EQ(p.fullness, (uint8_t)20);
  CHECK_EQ(p.walk.bank, (uint8_t)4);
  done();
}

TEST(treat, never_in_the_powered_off_catch_up) {
  Pet p;
  walker(p);
  p.walk.bank = 4;
  setNeeds(p, 80, 80, 80, 100);
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 600 * 60);  // 600 minutes powered off
  CHECK_EQ(p.fullness, (uint8_t)15);      // the catch-up's floor, untouched
  CHECK_EQ(p.walk.bank, (uint8_t)4);
  minuteAsIs(p);                          // the first live minute spends one
  CHECK_EQ(p.fullness, (uint8_t)WALK_TREAT_LEVEL);
  CHECK_EQ(p.walk.bank, (uint8_t)3);
  done();
}

TEST(treat, a_need_it_lifts_this_minute_is_not_a_slip_up) {
  Pet p;
  walker(p);
  p.careMistakes = 2;
  p.walk.bank = 2;
  setNeeds(p, 11, 80, 80, 100);  // food 11 -> 9 this minute: a slip-up without a treat
  minuteAsIs(p);
  CHECK_EQ(p.fullness, (uint8_t)WALK_TREAT_LEVEL);
  CHECK_MSG(p.careMistakes == 2, "no new slip-up");
  // control: with the bank empty it is one
  Pet q;
  walker(q);
  q.careMistakes = 2;
  setNeeds(q, 11, 80, 80, 100);
  minuteAsIs(q);
  CHECK_EQ(q.careMistakes, (uint8_t)3);
  done();
}

TEST(treat, is_not_the_players_care) {
  Pet p;
  walker(p);
  p.walk.bank = 2;
  p.bond = 50;
  uint16_t streak = p.streak;
  uint32_t care = p.lastCareDay;
  uint16_t medals = p.medals;
  setNeeds(p, 20, 80, 80, 100);
  minuteAsIs(p);
  CHECK_EQ(p.fullness, (uint8_t)WALK_TREAT_LEVEL);
  CHECK_EQ(p.bond, (uint8_t)50);
  CHECK_EQ(p.streak, streak);
  CHECK_EQ(p.lastCareDay, care);
  CHECK_EQ(p.medals, medals);
  CHECK(!p.showHeart());
  CHECK(!p.eating());
  done();
}

// ---- the stamina pause (Pet::treatHeld)
TEST(treat, minutes_a_treat_holds_up_do_not_build_stamina_until_the_next_real_care) {
  Pet p;
  walker(p);
  p.walk.bank = 1;
  setNeeds(p, 38, 100, 100, 100);  // food 38 -> 36: a treat lifts it
  minuteAsIs(p);
  CHECK(p.treatHeld());
  CHECK_EQ(p.walk.bank, (uint8_t)0);
  for (int m = 0; m < 2 * STAMINA_CARE_MIN; m++) minute(p, 0);  // good minutes, all needs full
  CHECK_MSG(p.trDef == 0, "the count pauses while a treat's hold lasts");
  p.caress();  // the player looks after it
  CHECK(!p.treatHeld());
  for (int m = 0; m < STAMINA_CARE_MIN; m++) minute(p, 0);
  CHECK_MSG(p.trDef == 1, "and counts again after: " + std::to_string(p.trDef));
  done();
}

TEST(treat, every_real_care_ends_the_hold) {
  // registerCare() is behind each of these
  for (int care = 0; care < 6; care++) {
    Pet p;
    walker(p);
    p.walk.bank = 1;
    setNeeds(p, 38, 100, 100, 100);
    minuteAsIs(p);
    CHECK(p.treatHeld());
    switch (care) {
      case 0: p.feedBerry(0); break;
      case 1: p.feedCandy(); break;
      case 2: p.clean(); break;
      case 3: p.caress(); break;
      case 4: p.playToy(); break;
      default: p.playGame(GAME_KEEPY, 5); break;
    }
    CHECK_MSG(!p.treatHeld(), "care " + std::to_string(care));
  }
  done();
}

TEST(treat, the_hold_is_not_saved_a_restart_forgets_it) {
  Pet p;
  walker(p);
  p.walk.bank = 1;
  setNeeds(p, 38, 100, 100, 100);
  minuteAsIs(p);
  CHECK(p.treatHeld());
  p.saveNow();
  Pet q;
  q.begin();
  CHECK(!q.treatHeld());
  done();
}

// a new pet starts with no hold, as with no bank
TEST(treat, a_new_pet_has_no_hold) {
  Pet p;
  walker(p);
  p.walk.bank = 1;
  setNeeds(p, 38, 100, 100, 100);
  minuteAsIs(p);
  CHECK(p.treatHeld());
  p.newEgg();
  CHECK(!p.treatHeld());
  done();
}
