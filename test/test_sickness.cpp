// Sickness and medicine (#15, SICK_* in pet.h): the owner's choices of 2026-11-17. Off unless
// the Care page's setting is on; 6 h in the red (awake, outside quiet hours, either pace) and it
// falls sick; a slip-up then and every hour it stays sick; better with the medicine or after
// 12 h out of the red; left sick 24 h it runs away (the sketch starts it). Never from the
// powered-off catch-up.
#include "framework.h"
#include "shim/Arduino.h"
#include "../pet.h"
#include "../dex.h"
#include <string>

// a freshly hatched easygoing pet (as test_pet.cpp's makePet), with a clean NVS
static void hatchPet(Pet &p, int16_t dex) {
  mockNvsReset();
  mockSetMillis(0);
  p.begin();
  if (p.awaitingStarter()) p.chooseStarter(dex);
  p.eggTap();
  p.eggTap();
  p.eggTap();
  p.nature = NAT_EASYGOING;
}

// `minutes` ticks, each with food, joy and energy put back at 80, no poops and hygiene at
// `hyg` first, so no need slips up (a need at 10 or under is a slip-up of its own): only
// sickness can. RED is in the hygiene bar's red but over 10 after the minute's drain.
#define RED 20
#define CLEAN 100
static void minutesAt(Pet &p, uint32_t minutes, uint8_t hyg) {
  for (uint32_t i = 0; i < minutes; i++) {
    p.fullness = p.joy = p.energy = 80;
    p.poops = 0;
    p.hygiene = hyg;
    mockAdvanceMillis(PET_TICK_MS);
    p.update(millis());
  }
}

static void makeSick(Pet &p) {
  p.setSickMode(SICK_ON);
  minutesAt(p, SICK_AFTER_MIN, RED);
}

// ---------------------------------------------------------------- falling sick
TEST(sick, off_by_default_and_nothing_counts_while_off) {
  Pet p;
  hatchPet(p, 4);
  CHECK_EQ(p.sickMode, (uint8_t)SICK_OFF);
  minutesAt(p, 3 * SICK_AFTER_MIN, RED);
  CHECK_MSG(!p.isSick(), "with the setting off, mess never makes it sick");
  CHECK_EQ(p.careMistakes, (uint8_t)0);
}

TEST(sick, six_hours_in_the_red_make_it_sick_and_that_is_a_slip_up) {
  Pet p;
  hatchPet(p, 4);
  p.setSickMode(SICK_ON);
  p.bond = 50;
  minutesAt(p, SICK_AFTER_MIN - 1, RED);
  CHECK(!p.isSick());
  CHECK_EQ(p.careMistakes, (uint8_t)0);
  minutesAt(p, 1, RED);
  CHECK(p.isSick());
  CHECK_EQ(p.careMistakes, (uint8_t)1);
  CHECK_EQ(p.bond, (uint8_t)49);
  CHECK_EQ(p.sickMinutes(), (uint16_t)0);
}

TEST(sick, a_clean_starts_the_count_again) {
  Pet p;
  hatchPet(p, 4);
  p.setSickMode(SICK_ON);
  minutesAt(p, SICK_AFTER_MIN - 1, RED);
  minutesAt(p, 1, CLEAN);  // one minute out of the red
  minutesAt(p, SICK_AFTER_MIN - 1, RED);
  CHECK_MSG(!p.isSick(), "the 6 h are in a row: a clean starts them again");
  minutesAt(p, 1, RED);
  CHECK(p.isSick());
}

TEST(sick, asleep_or_in_quiet_hours_the_count_waits) {
  Pet p;
  hatchPet(p, 4);
  p.setSickMode(SICK_ON);
  minutesAt(p, 300, RED);
  p.quietHours = true;  // also a stale or unknown clock (attnQuietAt)
  minutesAt(p, 600, RED);
  CHECK_MSG(!p.isSick(), "quiet hours: it couldn't call, so it doesn't count");
  p.quietHours = false;
  p.toggleLight();  // to bed, still in the red
  minutesAt(p, 600, RED);
  CHECK_MSG(!p.isSick(), "asleep it doesn't count");
  p.toggleLight();
  minutesAt(p, SICK_AFTER_MIN - 300 - 1, RED);
  CHECK_MSG(!p.isSick(), "the count waited: it was not started again");
  minutesAt(p, 1, RED);
  CHECK(p.isSick());
}

TEST(sick, the_relaxed_pace_counts_the_same_six_hours) {
  Pet p;
  hatchPet(p, 4);
  p.setPace(PACE_RELAXED);
  makeSick(p);
  CHECK_MSG(p.isSick(), "relaxed makes the bar go red later, not the 6 h longer (owner, 2026-11-17)");
}

TEST(sick, self_care_on_the_charger_keeps_it_well) {
  Pet p;
  hatchPet(p, 4);
  p.setSickMode(SICK_ON);
  p.setCharger(true, true, SELFCARE_IDLE_MS);
  for (int i = 0; i < 2 * 24 * 60; i++) {  // two days, the needs left to the drain and self-care
    mockAdvanceMillis(PET_TICK_MS);
    p.update(millis());
  }
  CHECK(!p.isSick());
}

TEST(sick, the_powered_off_catch_up_never_counts) {
  Pet p;
  hatchPet(p, 4);
  p.setSickMode(SICK_ON);
  p.setClock(1000u * 86400);
  minutesAt(p, 100, RED);
  p.syncClock(1000u * 86400 + 600 * 60);  // 10 h off: hygiene down to its floor of 15
  CHECK(p.hygiene < SICK_DIRTY);
  CHECK(!p.isSick());
  minutesAt(p, SICK_AFTER_MIN - 100 - 1, RED);
  CHECK_MSG(!p.isSick(), "the 10 h off added nothing to the count");
  minutesAt(p, 1, RED);
  CHECK(p.isSick());
}

// ---------------------------------------------------------------- sick
TEST(sick, each_hour_sick_is_another_slip_up_and_the_last_hour_has_none) {
  Pet p;
  hatchPet(p, 4);
  makeSick(p);
  CHECK_EQ(p.careMistakes, (uint8_t)1);
  minutesAt(p, SICK_SLIP_MIN - 1, RED);
  CHECK_EQ(p.careMistakes, (uint8_t)1);
  minutesAt(p, 1, RED);
  CHECK_EQ(p.careMistakes, (uint8_t)2);
  minutesAt(p, SICK_RUNAWAY_MIN - SICK_SLIP_MIN, RED);
  CHECK_EQ(p.sickMinutes(), (uint16_t)SICK_RUNAWAY_MIN);
  CHECK_EQ(p.careMistakes, (uint8_t)(SICK_RUNAWAY_MIN / SICK_SLIP_MIN));  // 24: one per hour sick
  minutesAt(p, 600, RED);
  CHECK_MSG(p.sickMinutes() == SICK_RUNAWAY_MIN && p.careMistakes == SICK_RUNAWAY_MIN / SICK_SLIP_MIN,
            "past the run-away nothing more counts");
}

TEST(sick, its_slip_ups_come_on_top_of_the_needs) {
  Pet p;
  hatchPet(p, 4);
  makeSick(p);
  uint8_t before = p.careMistakes;
  for (int i = 0; i < SICK_SLIP_MIN; i++) {  // an hour with every need at 5
    p.fullness = p.joy = p.energy = 5;
    p.hygiene = 0;
    mockAdvanceMillis(PET_TICK_MS);
    p.update(millis());
  }
  CHECK_EQ(p.careMistakes, (uint8_t)(before + 2));  // the needs' one and sickness's one
}

TEST(sick, quiet_hours_and_sleep_hold_its_slip_ups_and_the_run_away) {
  Pet p;
  hatchPet(p, 4);
  makeSick(p);
  minutesAt(p, 30, RED);
  p.quietHours = true;
  minutesAt(p, 600, RED);
  p.quietHours = false;
  p.toggleLight();
  minutesAt(p, 600, RED);
  p.toggleLight();
  CHECK_EQ(p.sickMinutes(), (uint16_t)30);
  CHECK_EQ(p.careMistakes, (uint8_t)1);
  CHECK(p.isSick());
}

TEST(sick, joy_falls_one_more_but_never_under_30_from_sickness) {
  Pet p;
  hatchPet(p, 4);
  makeSick(p);
  auto oneMinute = [&](uint8_t joy) {
    p.fullness = p.energy = 80;
    p.hygiene = 0;  // in the red; drainJoy's own -2 for hygiene under 30 is in both cases
    p.joy = joy;
    mockAdvanceMillis(PET_TICK_MS);
    p.update(millis());
  };
  oneMinute(60);
  CHECK_EQ(p.joy, (uint8_t)56);  // -1, -2 for the hygiene, and sickness's -1
  oneMinute(33);
  CHECK_EQ(p.joy, (uint8_t)30);  // 33 - 3 = 30: sickness takes nothing under 30
  oneMinute(31);
  CHECK_EQ(p.joy, (uint8_t)28);  // the drain's own still goes under 30
}

TEST(sick, it_does_not_evolve_until_it_is_better) {
  Pet p;
  hatchPet(p, 4);  // Charmander: evolves at 16 plus its slip-ups
  makeSick(p);
  p.ageMinutes = 40u * MINUTES_PER_LEVEL;
  p.fullness = p.joy = p.energy = p.hygiene = 80;
  CHECK(!p.canEvolveNow());
  CHECK(p.giveMedicine());
  CHECK(p.canEvolveNow());
}

TEST(sick, the_farewell_button_waits_until_it_is_better) {
  Pet p;
  hatchPet(p, 6);  // Charizard, a final form
  makeSick(p);
  p.ageMinutes = FAREWELL_AGE_MIN;
  CHECK(p.canFarewellNow());
  CHECK(!p.wantFarewellButton());
  p.giveMedicine();
  CHECK(p.wantFarewellButton());
}

TEST(sick, left_sick_a_day_it_runs_away_and_the_medicine_saves_it_up_to_then) {
  Pet p;
  hatchPet(p, 4);
  makeSick(p);
  minutesAt(p, SICK_RUNAWAY_MIN - 1, RED);
  CHECK(!p.sickRunawayDue());
  minutesAt(p, 1, RED);
  CHECK(p.sickRunawayDue());
  CHECK(p.giveMedicine());
  CHECK(!p.sickRunawayDue());
  CHECK_EQ(p.sickMinutes(), (uint16_t)0);
}

TEST(sick, a_run_away_is_not_due_in_a_ceremony) {
  Pet p;
  hatchPet(p, 4);
  makeSick(p);
  minutesAt(p, SICK_RUNAWAY_MIN, RED);
  CHECK(p.sickRunawayDue());
  p.startRunaway();
  CHECK(!p.sickRunawayDue());
}

// ---------------------------------------------------------------- better
TEST(sick, the_medicine_cures_at_once_as_the_days_care) {
  Pet p;
  hatchPet(p, 4);
  makeSick(p);
  minutesAt(p, 90, RED);
  p.lastSeenEpoch = 100u * 86400 + 10 * 3600;  // a known day, so the care can be seen
  uint16_t streak = p.streak;
  p.joy = 50;
  p.bond = 20;
  CHECK(p.giveMedicine());
  CHECK(!p.isSick());
  CHECK_EQ(p.sickMinutes(), (uint16_t)0);
  CHECK_EQ(p.joy, (uint8_t)(50 + SICK_MEDICINE_JOY));
  CHECK(p.showHeart());
  CHECK(p.eating() && p.takingMedicine());
  CHECK_EQ(p.streak, (uint16_t)(streak + 1));
  CHECK_EQ(p.bond, (uint8_t)(20 + 4 + 1));  // the day's first care +4, the medicine +1
  CHECK(!p.savePending());  // saved at once
  mockAdvanceMillis(EAT_ANIM_MS);
  p.update(millis());
  CHECK(!p.takingMedicine());
  // cured, the count to the next sickness starts from 0
  minutesAt(p, SICK_AFTER_MIN - 1, RED);
  CHECK(!p.isSick());
}

TEST(sick, no_medicine_when_well_asleep_or_as_an_egg) {
  Pet p;
  hatchPet(p, 4);
  p.setSickMode(SICK_ON);
  uint8_t joy = p.joy;
  CHECK(!p.giveMedicine());
  CHECK_EQ(p.joy, joy);
  CHECK(!p.takingMedicine());
  makeSick(p);
  p.toggleLight();
  CHECK(!p.giveMedicine());
  CHECK(p.isSick());
  p.toggleLight();
  CHECK(p.giveMedicine());
}

TEST(sick, twelve_hours_out_of_the_red_cure_it_asleep_or_awake_a_red_minute_only_pauses) {
  Pet p;
  hatchPet(p, 4);
  makeSick(p);
  minutesAt(p, 300, CLEAN);
  minutesAt(p, 60, RED);  // back in the red an hour: the cure's count waits, it isn't lost
  p.toggleLight();
  minutesAt(p, SICK_HEAL_MIN - 300 - 1, CLEAN);  // asleep and clean
  CHECK(p.isSick());
  p.quietHours = true;  // any hour
  minutesAt(p, 1, CLEAN);
  CHECK_MSG(!p.isSick(), "12 h out of the red in all: better by itself");
  CHECK_EQ(p.sickMinutes(), (uint16_t)0);
}

TEST(sick, switching_it_off_cures_it_and_clears_the_counts) {
  Pet p;
  hatchPet(p, 4);
  makeSick(p);
  minutesAt(p, 100, RED);
  p.setSickMode(SICK_OFF);
  CHECK(!p.isSick());
  CHECK_EQ(p.sickMinutes(), (uint16_t)0);
  Pet q;
  q.begin();
  CHECK_EQ(q.sickMode, (uint8_t)SICK_OFF);
  CHECK(!q.isSick());
  // a count towards sickness is cleared too: on again, it starts from 0
  p.setSickMode(SICK_ON);
  minutesAt(p, 300, RED);
  p.setSickMode(SICK_OFF);
  p.setSickMode(SICK_ON);
  minutesAt(p, SICK_AFTER_MIN - 1, RED);
  CHECK(!p.isSick());
}

TEST(sick, a_new_egg_is_well) {
  Pet p;
  hatchPet(p, 4);
  makeSick(p);
  p.newEgg();
  CHECK(!p.isSick());
  p.eggTap();
  p.eggTap();
  p.eggTap();
  CHECK(!p.isSick());
  CHECK_EQ(p.sickMinutes(), (uint16_t)0);
}

// Also fixed in #15: careMistakes is a byte and wrapped from 255 back to a spotless 0
TEST(sick, slip_ups_stop_at_255) {
  Pet p;
  hatchPet(p, 4);
  p.careMistakes = 255;
  p.fullness = p.joy = p.energy = p.hygiene = 0;
  mockAdvanceMillis(PET_TICK_MS);
  p.update(millis());
  CHECK_EQ(p.careMistakes, (uint8_t)255);
}

// ---------------------------------------------------------------- the save
TEST(sick, an_old_save_without_the_keys_loads_off_and_well) {
  Pet a;
  hatchPet(a, 4);
  Preferences raw;
  raw.begin("tamapoke", false);
  for (const char *k : { "sickopt", "sick", "sickg", "sickh", "sickt" }) raw.remove(k);
  raw.end();
  Pet b;
  b.begin();
  CHECK_EQ(b.sickMode, (uint8_t)SICK_OFF);
  CHECK(!b.isSick());
  CHECK_EQ(b.sickMinutes(), (uint16_t)0);
}

TEST(sick, it_all_survives_a_restart) {
  Pet a;
  hatchPet(a, 4);
  a.setSickMode(SICK_ON);
  minutesAt(a, 200, RED);
  a.saveNow();
  Pet b;
  b.begin();
  CHECK_EQ(b.sickMode, (uint8_t)SICK_ON);
  minutesAt(b, SICK_AFTER_MIN - 200 - 1, RED);
  CHECK_MSG(!b.isSick(), "the count towards sickness came back");
  minutesAt(b, 1, RED);
  CHECK(b.isSick());
  minutesAt(b, 100, RED);
  minutesAt(b, SICK_HEAL_MIN - 20, CLEAN);
  b.saveNow();
  Pet c;
  c.begin();
  CHECK_MSG(c.isSick(), "sick came back");
  CHECK_EQ(c.sickMinutes(), (uint16_t)(100 + SICK_HEAL_MIN - 20));
  minutesAt(c, 19, CLEAN);
  CHECK(c.isSick());
  minutesAt(c, 1, CLEAN);
  CHECK_MSG(!c.isSick(), "the count towards the cure came back");
}

TEST(sick, counts_the_game_cannot_reach_read_as_0) {
  Pet a;
  hatchPet(a, 4);
  a.setSickMode(SICK_ON);
  a.saveNow();
  Preferences raw;
  raw.begin("tamapoke", false);
  raw.putUShort("sickg", SICK_AFTER_MIN);  // well: one past the last count
  raw.end();
  Pet b;
  b.begin();
  minutesAt(b, SICK_AFTER_MIN - 1, RED);
  CHECK_MSG(!b.isSick(), "an impossible count towards sickness reads as 0");

  raw.begin("tamapoke", false);
  raw.putBool("sick", true);
  raw.putUShort("sickt", SICK_RUNAWAY_MIN + 1);
  raw.putUShort("sickh", SICK_HEAL_MIN);
  raw.end();
  Pet c;
  c.begin();
  CHECK(c.isSick());
  CHECK_EQ(c.sickMinutes(), (uint16_t)0);  // not a run-away
  CHECK(!c.sickRunawayDue());
  minutesAt(c, SICK_HEAL_MIN - 1, CLEAN);
  CHECK_MSG(c.isSick(), "an impossible cure count reads as 0, not as a cure");
  minutesAt(c, 1, CLEAN);
  CHECK(!c.isSick());

  raw.begin("tamapoke", false);
  raw.putUShort("sickt", SICK_RUNAWAY_MIN);  // exactly due is a real state: kept
  raw.end();
  Pet d;
  d.begin();
  CHECK(d.sickRunawayDue());
}

TEST(sick, a_setting_out_of_range_reads_as_off_and_well) {
  Pet a;
  hatchPet(a, 4);
  makeSick(a);
  a.saveNow();
  Preferences raw;
  raw.begin("tamapoke", false);
  raw.putUChar("sickopt", 7);
  raw.end();
  Pet b;
  b.begin();
  CHECK_EQ(b.sickMode, (uint8_t)SICK_OFF);
  CHECK(!b.isSick());
}

TEST(sick, an_egg_loads_well_whatever_the_save_says) {
  Pet a;
  hatchPet(a, 4);
  a.setSickMode(SICK_ON);
  a.newEgg();
  Preferences raw;
  raw.begin("tamapoke", false);
  raw.putBool("sick", true);
  raw.putUShort("sickt", 30);
  raw.end();
  Pet b;
  b.begin();
  CHECK(b.isEgg());
  CHECK(!b.isSick());
}

// ---------------------------------------------------------------- the UI's side (part 2)
TEST(sick, a_sick_pet_is_sad_unless_it_eats_or_sleeps) {
  Pet p;
  hatchPet(p, 4);
  makeSick(p);
  p.fullness = p.joy = p.energy = p.hygiene = 80;
  CHECK_EQ((int)p.mood(), (int)MOOD_SAD);  // the HURT pose and the calm repertoire
  p.giveMedicine();
  CHECK_EQ((int)p.mood(), (int)MOOD_EATING);  // takes the pill
  mockAdvanceMillis(EAT_ANIM_MS);
  p.update(millis());
  CHECK_EQ((int)p.mood(), (int)MOOD_HAPPY);
  makeSick(p);
  p.toggleLight();
  CHECK_EQ((int)p.mood(), (int)MOOD_SLEEPING);
}

TEST(sick, the_medicine_button_shows_while_it_is_sick_and_awake_and_the_run_away_button_wins) {
  Pet p;
  hatchPet(p, 4);
  p.setSickMode(SICK_ON);
  CHECK(!p.wantMedicineButton());
  makeSick(p);
  CHECK(p.wantMedicineButton());
  p.toggleLight();
  CHECK(!p.wantMedicineButton());
  p.toggleLight();
  p.dbgRunawayReady();  // every need at 0 for an hour: the run-away button
  CHECK(p.canRunawayNow());
  CHECK(!p.wantMedicineButton());
  p.fullness = 50;  // one care: that button goes, the medicine's shows
  CHECK(p.wantMedicineButton());
  p.giveMedicine();
  CHECK(!p.wantMedicineButton());
  makeSick(p);
  p.startRunaway();
  CHECK(!p.wantMedicineButton());
}

TEST(sick, the_serial_hook_makes_it_sick_with_no_slip_up_or_well_and_saves) {
  Pet p;
  hatchPet(p, 4);
  CHECK(!p.dbgSick(true, 0));  // the setting is off
  CHECK(!p.isSick());
  p.setSickMode(SICK_ON);
  CHECK(p.dbgSick(true, SICK_WARN_MIN));
  CHECK(p.isSick());
  CHECK_EQ(p.sickMinutes(), (uint16_t)SICK_WARN_MIN);
  CHECK_EQ(p.careMistakes, (uint8_t)0);
  CHECK(!p.savePending());
  Pet q;
  q.begin();
  CHECK(q.isSick());
  CHECK_EQ(q.sickMinutes(), (uint16_t)SICK_WARN_MIN);
  CHECK(p.dbgSick(true, 60000));
  CHECK_EQ(p.sickMinutes(), (uint16_t)SICK_RUNAWAY_MIN);  // capped
  CHECK(p.dbgSick(false, 0));
  CHECK(!p.isSick());
  p.newEgg();
  CHECK(!p.dbgSick(true, 0));  // an egg can't be sick
}

TEST(sick, the_serial_command_takes_on_off_and_minutes_up_to_the_run_away) {
  uint16_t m = 99;
  CHECK_EQ((int)sickCmdParse("SICK OFF", m), (int)SICKCMD_OFF);
  CHECK_EQ((int)sickCmdParse("SICK ON", m), (int)SICKCMD_ON);
  CHECK_EQ(m, (uint16_t)0);
  CHECK_EQ((int)sickCmdParse("SICK ON 720", m), (int)SICKCMD_ON);
  CHECK_EQ(m, (uint16_t)720);
  CHECK_EQ((int)sickCmdParse("SICK ON 1440", m), (int)SICKCMD_ON);
  CHECK_EQ(m, (uint16_t)SICK_RUNAWAY_MIN);
  for (const char *bad : { "SICK", "SICK ", "SICK ON ", "SICK ON 1441", "SICK ON 9999", "SICK ON 12345",
                           "SICK ON -1", "SICK ON 7x", "SICK on", "SICK OFF 3", "SICK ON  5",
                           "SICK ON 00001", "SICK ON 4294967297" }) {  // 5 digits; one that would wrap to 1
    CHECK_MSG(sickCmdParse(bad, m) == SICKCMD_BAD, std::string("should be refused: ") + bad);
    CHECK_EQ(m, (uint16_t)0);
  }
  for (const char *other : { "SICKX", "SCENE?", "STATS", "" })
    CHECK_MSG(sickCmdParse(other, m) == SICKCMD_NONE, std::string("not SICK: ") + other);
}

