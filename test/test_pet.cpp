// Tests de la logica de la mascota (pet.cpp) sobre los shims del host.
#include "framework.h"
#include "shim/Arduino.h"
#include "../pet.h"
#include "../dex.h"
#include "../bedtime.h"
#include "../audio.h"  // SFX_*, audioSetEnabled/audioSetSleeping (the shim's, muted)
#include "../exercise.h"  // exerciseGain, EX_MAX_COUNT (#78, #126)
#include "../i18n.h"  // bannerLines, medalName (#82)
#include <math.h>
#include <string.h>

// ---------------------------------------------------------------- helpers
static void advance(Pet &p, uint32_t minutes) {
  for (uint32_t i = 0; i < minutes; i++) {
    mockAdvanceMillis(PET_TICK_MS);
    p.update(millis());
  }
}

// mascota recien eclosionada de la especie pedida, con la NVS limpia. Easygoing (#25): the
// nature rolled at hatch would change the drains and boosts, so every test that does not
// set one checks the game as it was before natures.
static void makePet(Pet &p, int16_t dex) {
  mockNvsReset();
  mockSetMillis(0);
  p.begin();
  if (p.awaitingStarter()) p.chooseStarter(dex);
  p.eggTap();
  p.eggTap();
  p.eggTap();  // 3 toques = eclosion inmediata
  p.nature = NAT_EASYGOING;
}

static void setStats(Pet &p, uint8_t f, uint8_t j, uint8_t e, uint8_t h) {
  p.fullness = f; p.joy = j; p.energy = e; p.hygiene = h;
}

// ---------------------------------------------------------------- huevo
TEST(egg, primera_partida_pide_inicial) {
  mockNvsReset();
  Pet p;
  p.begin();
  CHECK(p.isEgg());
  CHECK(p.awaitingStarter());
  CHECK_EQ(p.eggCracks(), (uint8_t)0);
  CHECK_EQ(p.registeredCount(), (uint16_t)0);
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
}

// regresion v1.3: el huevo eclosionaba solo a los 3 min mientras el jugador
// aun estaba eligiendo inicial, y se perdia la eleccion
TEST(egg, no_eclosiona_mientras_se_elige_inicial) {
  mockNvsReset();
  Pet p;
  p.begin();
  CHECK(p.awaitingStarter());
  advance(p, 600);  // 10 horas de juego
  CHECK_MSG(p.isEgg(), "el huevo NO debe eclosionar durante la eleccion");
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
  p.chooseStarter(7);
  CHECK(!p.awaitingStarter());
  advance(p, 3);
  CHECK(!p.isEgg());
  CHECK_EQ(p.speciesId, (int16_t)7);
}

TEST(egg, eclosiona_sola_a_los_3_minutos) {
  mockNvsReset();
  Pet p;
  p.begin();
  p.chooseStarter(1);
  advance(p, 2);
  CHECK(p.isEgg());
  advance(p, 1);
  CHECK(!p.isEgg());
  CHECK_EQ(p.speciesId, (int16_t)1);
}

TEST(egg, tres_toques_eclosionan) {
  mockNvsReset();
  Pet p;
  p.begin();
  p.chooseStarter(4);
  p.eggTap();
  CHECK_EQ(p.eggCracks(), (uint8_t)1);
  CHECK(p.isEgg());
  p.eggTap();
  CHECK_EQ(p.eggCracks(), (uint8_t)2);
  CHECK(p.isEgg());
  p.eggTap();
  CHECK(!p.isEgg());
  CHECK_EQ(p.speciesId, (int16_t)4);
}

TEST(egg, eclosionar_sortea_genes_y_resetea_al_individuo) {
  Pet p;
  makePet(p, 4);
  CHECK_RANGE(p.geneAtk, 90, 110);
  CHECK_RANGE(p.geneDef, 90, 110);
  CHECK_RANGE(p.geneSpe, 90, 110);
  CHECK_EQ(p.trAtk, (uint8_t)0);
  CHECK_EQ(p.trDef, (uint8_t)0);
  CHECK_EQ(p.trSpe, (uint8_t)0);
  CHECK_EQ(p.bond, (uint8_t)0);
  CHECK_EQ(p.berryKnown, false);
  CHECK_STREQ(p.nick, "");
  CHECK_MSG(p.isRegistered(4), "criar = registrar en la pokedex");
  CHECK_EQ(p.registeredCount(), (uint16_t)1);
}

TEST(egg, tocar_huevo_no_hace_nada_si_ya_eclosiono) {
  Pet p;
  makePet(p, 4);
  uint8_t cracks = p.eggCracks();
  p.eggTap();
  CHECK_EQ(p.eggCracks(), cracks);
  CHECK_EQ(p.speciesId, (int16_t)4);
}

TEST(egg, rareza_del_huevo_es_valida) {
  mockNvsReset();
  Pet p;
  p.begin();
  CHECK_RANGE(p.eggRarity(), (uint8_t)R_COMUN, (uint8_t)R_LEGENDARIO);
}

// ---------------------------------------------------------------- comida
TEST(feed, baya_normal_sube_25_y_topa_en_100) {
  Pet p;
  makePet(p, 4);
  p.fullness = 50;
  uint8_t notLoved = (uint8_t)((p.speciesId + 1) % 3);
  p.feedBerry(notLoved);
  CHECK_EQ(p.fullness, (uint8_t)75);
  p.feedBerry(notLoved);
  CHECK_EQ(p.fullness, (uint8_t)100);  // 100 en vez de 105
}

TEST(feed, baya_favorita_sube_mas_y_da_corazon) {
  Pet p;
  makePet(p, 4);  // 4 % 3 == 1 -> le gusta la azul
  CHECK(p.lovesBerry(1));
  CHECK(!p.lovesBerry(0));
  CHECK(!p.lovesBerry(2));
  p.fullness = 10;
  p.joy = 10;
  p.feedBerry(1);
  CHECK_EQ(p.fullness, (uint8_t)45);
  CHECK_EQ(p.joy, (uint8_t)20);
  CHECK(p.berryKnown);
  CHECK(p.showHeart());
  CHECK(p.eating());
}

TEST(feed, chuche_engorda) {
  Pet p;
  makePet(p, 4);
  p.fullness = 50;
  p.joy = 50;
  p.feedCandy();
  CHECK_EQ(p.fullness, (uint8_t)60);
  CHECK_EQ(p.joy, (uint8_t)62);
  CHECK_EQ(p.weight, (uint8_t)12);
}

TEST(feed, dormido_o_huevo_no_come) {
  Pet p;
  makePet(p, 4);
  p.fullness = 40;
  p.toggleLight();  // a dormir
  CHECK(p.sleeping);
  p.feedBerry(1);
  p.feedCandy();
  CHECK_EQ(p.fullness, (uint8_t)40);

  Pet e;
  mockNvsReset();
  e.begin();
  e.chooseStarter(4);
  e.fullness = 40;
  e.feedBerry(0);
  CHECK_EQ(e.fullness, (uint8_t)40);
}

// ---------------------------------------------------------------- treats and toys (#13)
// The plush toy and warm milk (TOY_* and MILK_* in pet.h; the numbers are the owner's).

TEST(treats, the_plush_gives_a_little_joy_and_a_heart_and_nothing_else) {
  Pet p;
  makePet(p, 4);
  setStats(p, 60, 50, 40, 70);
  uint8_t spe = p.trSpe, bond = p.bond;
  CHECK_EQ(p.playToy(), (uint8_t)TOY_JOY);
  CHECK_EQ(p.joy, (uint8_t)(50 + TOY_JOY));
  CHECK(p.playingToy());
  CHECK(p.showHeart());
  CHECK_EQ(p.bond, (uint8_t)(bond + 1));
  // no minigame: no speed, and it costs no energy or food and burns no weight
  CHECK_EQ(p.trSpe, spe);
  CHECK_EQ(p.energy, (uint8_t)40);
  CHECK_EQ(p.fullness, (uint8_t)60);
  CHECK_EQ(p.hygiene, (uint8_t)70);
  CHECK(!p.eating());
  mockAdvanceMillis(TOY_ANIM_MS - 1);
  CHECK(p.playingToy());
  CHECK_EQ(p.toyElapsed(), (uint32_t)(TOY_ANIM_MS - 1));
  mockAdvanceMillis(1);
  CHECK(!p.playingToy());
  CHECK_EQ(p.toyElapsed(), (uint32_t)0);
  setStats(p, 60, 95, 40, 70);
  CHECK_EQ(p.playToy(), (uint8_t)5);  // up to 100: it returns what really went in
  CHECK_EQ(p.joy, (uint8_t)100);
  p.toggleLight();  // put to bed mid-play: the play stops (no plush tossed beside a sleeper)
  CHECK(!p.playingToy());
}

TEST(treats, the_plush_gives_joy_twice_an_hour_then_plays_for_none) {
  Pet p;
  makePet(p, 4);
  setStats(p, 60, 20, 40, 70);
  CHECK_EQ(p.playToy(), (uint8_t)TOY_JOY);  // the hour starts here
  mockAdvanceMillis(30UL * 60 * 1000);
  CHECK_EQ(p.playToy(), (uint8_t)TOY_JOY);
  mockAdvanceMillis(29UL * 60 * 1000);      // 59 min after the first
  CHECK_EQ(p.playToy(), (uint8_t)0);
  CHECK_EQ(p.joy, (uint8_t)(20 + 2 * TOY_JOY));
  CHECK_MSG(p.playingToy(), "past the cap it still plays with it");
  CHECK_MSG(!p.showHeart(), "past the cap: no heart");
  mockAdvanceMillis(59UL * 1000);           // 59 min 59 s
  CHECK_EQ(p.playToy(), (uint8_t)0);
  mockAdvanceMillis(1000);                  // the hour since the first is over
  CHECK_EQ(p.playToy(), (uint8_t)TOY_JOY);
  CHECK(p.showHeart());
  mockAdvanceMillis(10UL * 60 * 1000);
  CHECK_EQ(p.playToy(), (uint8_t)TOY_JOY);  // a new hour, from that play
  CHECK_EQ(p.playToy(), (uint8_t)0);
}

TEST(treats, a_play_at_full_joy_gives_nothing_and_spends_none_of_the_hours_plays) {
  Pet p;
  makePet(p, 4);
  setStats(p, 60, 100, 40, 70);
  CHECK_EQ(p.playToy(), (uint8_t)0);
  CHECK_MSG(p.playingToy(), "at full joy it still plays with it");
  CHECK_MSG(!p.showHeart(), "no joy given: no heart");
  CHECK_EQ(p.joy, (uint8_t)100);
  mockAdvanceMillis(TOY_ANIM_MS);
  setStats(p, 60, 20, 40, 70);  // the joy drops: the hour's two plays that give joy are all still there
  CHECK_EQ(p.playToy(), (uint8_t)TOY_JOY);
  CHECK_EQ(p.playToy(), (uint8_t)TOY_JOY);
  CHECK_EQ(p.playToy(), (uint8_t)0);
}

TEST(treats, a_playful_pet_enjoys_the_plush_more) {
  Pet p;
  makePet(p, 4);
  p.nature = NAT_PLAYFUL;
  setStats(p, 60, 20, 40, 70);
  CHECK_EQ(p.playToy(), (uint8_t)(TOY_JOY + NAT_PLAY_JOY_EXTRA));
}

TEST(treats, no_plush_or_milk_asleep_as_an_egg_or_in_a_ceremony) {
  Pet p;
  makePet(p, 4);
  setStats(p, 60, 50, 40, 70);
  p.toggleLight();  // asleep
  CHECK_EQ(p.playToy(), (uint8_t)0);
  CHECK(!p.playingToy());
  CHECK(!p.giveMilk());
  CHECK(!p.eating());
  CHECK(!p.milkNight());
  CHECK_EQ(p.joy, (uint8_t)50);
  CHECK_EQ(p.weight, (uint8_t)0);
  p.toggleLight();
  p.startFarewell();
  uint8_t joy = p.joy;
  CHECK_EQ(p.playToy(), (uint8_t)0);
  CHECK(!p.playingToy());
  CHECK(!p.giveMilk());
  CHECK(!p.milkNight());
  CHECK_EQ(p.joy, joy);

  Pet e;
  mockNvsReset();
  mockSetMillis(0);
  e.begin();
  e.chooseStarter(4);  // an egg
  uint8_t ej = e.joy;
  CHECK_EQ(e.playToy(), (uint8_t)0);
  CHECK(!e.playingToy());
  CHECK(!e.giveMilk());
  CHECK(!e.milkNight());
  CHECK_EQ(e.joy, ej);
}

TEST(treats, warm_milk_settles_it_happily_and_adds_less_weight_than_candy) {
  Pet p;
  makePet(p, 4);
  setStats(p, 60, 50, 40, 70);
  uint8_t bond = p.bond;
  CHECK(p.giveMilk());
  CHECK_EQ(p.joy, (uint8_t)(50 + MILK_JOY));
  CHECK_EQ(p.weight, (uint8_t)MILK_WEIGHT);
  CHECK(MILK_WEIGHT < 12);  // candy's weight (feed.chuche_engorda)
  CHECK(p.showHeart());
  CHECK(p.eating());
  CHECK(p.drinkingMilk());
  CHECK(p.milkNight());
  CHECK_MSG(!p.sleeping, "the sketch puts it to bed once the drink is over");
  CHECK_EQ(p.bond, (uint8_t)(bond + 1));
  CHECK_EQ(p.fullness, (uint8_t)60);  // not a meal
  CHECK_EQ(p.energy, (uint8_t)40);
  mockAdvanceMillis(EAT_ANIM_MS);
  CHECK(!p.eating());
  CHECK(!p.drinkingMilk());
  // put to bed mid-drink (lights out from the moon strip): no mug beside a sleeper
  CHECK(p.giveMilk());
  CHECK(p.drinkingMilk());
  p.toggleLight();
  CHECK(!p.drinkingMilk());
}

TEST(treats, the_plush_and_the_milk_count_as_the_days_care) {
  Pet p;
  makePet(p, 4);
  p.lastSeenEpoch = 100u * 86400 + 10 * 3600;  // a known day, so the care can be seen
  uint16_t streak = p.streak;
  p.playToy();
  CHECK_EQ(p.streak, (uint16_t)(streak + 1));
  CHECK_EQ(p.lastCareDay, (uint32_t)100);
  p.lastSeenEpoch = 101u * 86400 + 20 * 3600;  // the next evening
  p.giveMilk();
  CHECK_EQ(p.streak, (uint16_t)(streak + 2));
  CHECK_EQ(p.lastCareDay, (uint32_t)101);
}

// "a little more energy that night": +MILK_SLEEP_ENERGY a minute asleep after the milk, on
// top of the nature's rate, until the pet wakes (by hand or by bedtime)
TEST(treats, asleep_after_warm_milk_energy_comes_back_faster_until_it_wakes) {
  Pet p;
  makePet(p, 4);
  setStats(p, 90, 90, 20, 90);
  CHECK(p.giveMilk());
  mockAdvanceMillis(EAT_ANIM_MS);
  CHECK(p.autoSleep());  // the sketch's lights out (milkApply)
  advance(p, 1);
  CHECK_EQ(p.energy, (uint8_t)(20 + 6 + MILK_SLEEP_ENERGY));
  advance(p, 1);
  CHECK_EQ(p.energy, (uint8_t)(20 + 2 * (6 + MILK_SLEEP_ENERGY)));
  // woken by hand: that night is over, and going back to bed without milk is a plain sleep
  p.toggleLight();
  CHECK(!p.milkNight());
  p.energy = 20;
  p.toggleLight();
  advance(p, 1);
  CHECK_EQ(p.energy, (uint8_t)26);
  // woken by bedtime ends it too
  p.toggleLight();
  CHECK(p.giveMilk());
  CHECK(p.autoSleep());
  CHECK(p.autoWake());
  CHECK(!p.milkNight());
  // a sleep with no milk at all is the plain one
  Pet q;
  makePet(q, 4);
  setStats(q, 90, 90, 20, 90);
  q.toggleLight();
  advance(q, 1);
  CHECK_EQ(q.energy, (uint8_t)26);
}

TEST(treats, the_milk_bonus_adds_to_a_lazy_pets_sleep) {
  Pet p;
  makePet(p, 4);
  p.nature = NAT_LAZY;
  setStats(p, 90, 90, 20, 90);
  CHECK(p.giveMilk());
  CHECK(p.autoSleep());
  advance(p, 1);
  CHECK_EQ(p.energy, (uint8_t)(20 + NAT_SLEEP_ENERGY + MILK_SLEEP_ENERGY));
}

// `milked` is RAM only, and syncClock() runs only from setup(), after a restart: so the milk
// bonus never reaches the powered-off catch-up. A restart in the night drops it (the pet stays
// asleep and its catch-up restores energy at the plain rate); a new egg drops it too.
TEST(treats, a_restart_drops_the_milk_bonus_and_a_new_egg_does_too) {
  Pet p;
  makePet(p, 4);
  setStats(p, 90, 90, 20, 90);
  CHECK(p.giveMilk());
  CHECK(p.autoSleep());
  p.setClock(1000u * 86400);  // saves, with its time reference
  CHECK(p.milkNight());
  Pet q;  // the restart: same NVS, a new Pet
  q.begin();
  CHECK_MSG(!q.milkNight(), "the milk is not in the save: a restart drops the bonus");
  CHECK_MSG(q.sleeping, "it is still asleep after the restart");
  CHECK_EQ(q.energy, (uint8_t)20);
  q.syncClock(1000u * 86400 + 2 * 60);  // two minutes powered off
  CHECK_EQ(q.energy, (uint8_t)(20 + 2 * 6));  // the plain rate for an easygoing pet
  p.newEgg();
  CHECK(!p.milkNight());
}

// ---------------------------------------------------------------- tick
TEST(tick, despierto_baja_comida_2_y_energia_1_por_minuto) {
  Pet p;
  makePet(p, 4);
  setStats(p, 100, 100, 100, 100);
  p.poops = 0;
  mockForceRandom(99);  // random(100) < 15 falso: sin cacas
  advance(p, 10);
  mockClearForcedRandom();
  CHECK_EQ(p.fullness, (uint8_t)80);
  CHECK_EQ(p.energy, (uint8_t)90);
  CHECK_EQ(p.hygiene, (uint8_t)90);
  CHECK_EQ(p.joy, (uint8_t)90);
  CHECK_EQ(p.ageMinutes, (uint32_t)10);
  CHECK_EQ(p.poops, (uint8_t)0);
}

TEST(tick, las_barras_nunca_bajan_de_cero) {
  Pet p;
  makePet(p, 4);
  setStats(p, 5, 3, 2, 1);
  advance(p, 30);
  CHECK_EQ(p.fullness, (uint8_t)0);
  CHECK_EQ(p.joy, (uint8_t)0);
  CHECK_EQ(p.energy, (uint8_t)0);
  CHECK_EQ(p.hygiene, (uint8_t)0);
}

TEST(tick, dormir_recupera_energia_con_suelos) {
  Pet p;
  makePet(p, 4);
  setStats(p, 100, 100, 10, 100);
  p.toggleLight();
  advance(p, 20);
  CHECK_MSG(p.energy == 100, "durmiendo la energia sube 6/min hasta 100");
  CHECK_MSG(p.fullness > 80, "durmiendo el hambre baja ~4x mas lento");
  advance(p, 2000);  // dormir muchisimo: los suelos protegen
  CHECK_MSG(p.fullness >= 30, "suelo de comida durmiendo = 30");
  CHECK_MSG(p.joy >= 35, "suelo de felicidad durmiendo = 35");
  CHECK_MSG(p.hygiene >= 45, "suelo de higiene durmiendo = 45");
  CHECK_EQ(p.careMistakes, (uint8_t)0);
}

TEST(tick, sobrepeso_se_quema_solo) {
  Pet p;
  makePet(p, 4);
  p.weight = 30;
  advance(p, 30);
  CHECK_EQ(p.weight, (uint8_t)20);  // -1 cada 3 minutos
}

TEST(tick, descuido_cuenta_una_vez_por_hora) {
  Pet p;
  makePet(p, 4);
  setStats(p, 0, 0, 0, 0);
  advance(p, 1);
  CHECK_EQ(p.careMistakes, (uint8_t)1);
  advance(p, 50);
  CHECK_MSG(p.careMistakes == 1, "el enfriamiento evita contar el mismo descuido cada minuto");
  advance(p, 11);
  CHECK_EQ(p.careMistakes, (uint8_t)2);
}

// regresion v1.4: el descuido restaba 3 de vinculo cada 30 min y era imposible
// recuperarlo; ahora resta 1 y nunca lo deja en 0
TEST(tick, descuido_enfria_el_vinculo_sin_arrasarlo) {
  Pet p;
  makePet(p, 4);
  p.bond = 50;
  setStats(p, 0, 0, 0, 0);
  advance(p, 1);
  CHECK_EQ(p.bond, (uint8_t)49);
  p.bond = 1;
  advance(p, 61);
  CHECK_MSG(p.bond == 1, "el vinculo no cae por debajo de 1");
}

// Steady care builds stamina (#76): STAMINA_CARE_MIN awake minutes with every need at 40
// or more = +1 stamina. Until #76 it took 720 in a row.
TEST(tick, steady_care_builds_stamina) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.trDef, (uint8_t)0);
  for (int m = 0; m < STAMINA_CARE_MIN - 1; m++) {  // every need >= 40
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  CHECK_EQ(p.trDef, (uint8_t)0);
  setStats(p, 100, 100, 100, 100);
  advance(p, 1);
  CHECK_EQ(p.trDef, (uint8_t)1);
  for (int m = 0; m < STAMINA_CARE_MIN; m++) {
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  CHECK_EQ(p.trDef, (uint8_t)2);
}

// La racha de buen cuidado mira la felicidad ANTES de que baje ese minuto (el
// orden del tick: goodTicks, luego drainJoy). Con joy justo en 40 antes del tick,
// el minuto cuenta; si drainJoy corriera antes, joy valdria 39 y el minuto no contaria.
TEST(tick, la_racha_ve_la_felicidad_antes_de_bajar) {
  Pet p;
  makePet(p, 4);
  for (int m = 0; m < STAMINA_CARE_MIN; m++) {
    setStats(p, 100, 40, 100, 100);
    advance(p, 1);
  }
  CHECK_EQ(p.trDef, (uint8_t)1);
}

// A minute with a need under 40 only doesn't count (#76): the count is kept, not reset, so a
// player who checks in now and then still builds stamina.
TEST(tick, a_minute_under_40_pauses_the_stamina_count) {
  Pet p;
  makePet(p, 4);
  for (int m = 0; m < STAMINA_CARE_MIN - 5; m++) {
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  for (int m = 0; m < 10; m++) {  // hungry for ten minutes
    setStats(p, 20, 100, 100, 100);
    advance(p, 1);
  }
  CHECK_EQ(p.trDef, (uint8_t)0);
  for (int m = 0; m < 5; m++) {
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  CHECK_MSG(p.trDef == 1, "the good minutes before the dip still count");
}

// A minute self-care holds up on the charger (#24) is not the player's care (#76).
TEST(tick, self_care_minutes_do_not_build_stamina) {
  Pet p;
  makePet(p, 4);
  for (int m = 0; m < 3 * STAMINA_CARE_MIN; m++) {
    p.setCharger(true, true, SELFCARE_IDLE_MS);
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  CHECK_EQ(p.trDef, (uint8_t)0);
  for (int m = 0; m < STAMINA_CARE_MIN; m++) {  // the player back: a touch ends self-care
    p.setCharger(true, true, 0);
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  CHECK_EQ(p.trDef, (uint8_t)1);
}

// Asleep the count waits (#76, as before): the night neither adds to it nor clears it.
TEST(tick, sleep_neither_builds_stamina_nor_clears_the_count) {
  Pet p;
  makePet(p, 4);
  for (int m = 0; m < STAMINA_CARE_MIN - 1; m++) {
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  p.toggleLight();  // asleep
  for (int m = 0; m < 3 * STAMINA_CARE_MIN; m++) {
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  CHECK_EQ(p.trDef, (uint8_t)0);
  p.toggleLight();  // awake: the next good minute completes the point
  setStats(p, 100, 100, 100, 100);
  advance(p, 1);
  CHECK_EQ(p.trDef, (uint8_t)1);
}

// ---------------------------------------------------------------- niveles
TEST(level, un_nivel_por_hora_de_juego) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.level(), (uint8_t)1);
  p.ageMinutes = 59;
  CHECK_EQ(p.level(), (uint8_t)1);
  p.ageMinutes = 60;
  CHECK_EQ(p.level(), (uint8_t)2);
  p.ageMinutes = 24 * 60;
  CHECK_EQ(p.level(), (uint8_t)25);
}

// Arreglado en v1.8: level() era uint8_t y daba la vuelta a 0 a los ~10,6 dias
TEST(level, no_desborda_con_mascotas_longevas) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 254 * 60;
  uint8_t before = p.level();
  CHECK_EQ(before, (uint8_t)255);
  p.ageMinutes = 255 * 60;  // nivel 256 -> deberia seguir subiendo (o saturar)
  CHECK_MSG(p.level() >= before, "el nivel nunca deberia bajar al envejecer");
}

// ---------------------------------------------------------------- evolucion
TEST(evolve, requiere_nivel_y_buen_cuidado) {
  Pet p;
  makePet(p, 4);  // CHARMANDER evoluciona a nivel 16
  p.ageMinutes = 14 * 60;  // nivel 15
  CHECK(!p.canEvolveNow());
  p.ageMinutes = 15 * 60;  // nivel 16
  CHECK(p.canEvolveNow());
  p.fullness = 39;  // una barra por debajo de 40 lo bloquea
  CHECK(!p.canEvolveNow());
  p.fullness = 40;
  CHECK(p.canEvolveNow());
}

TEST(evolve, cada_descuido_retrasa_un_nivel) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 15 * 60;  // nivel 16
  p.careMistakes = 1;
  CHECK(!p.canEvolveNow());
  p.ageMinutes = 16 * 60;  // nivel 17
  CHECK(p.canEvolveNow());
}

// Arreglado en v1.8: el cast a uint8_t de (evolveLevel + careMistakes) desbordaba
TEST(evolve, muchos_descuidos_no_adelantan_la_evolucion) {
  Pet p;
  makePet(p, 4);       // CHARMANDER: evoluciona a nivel 16
  p.careMistakes = 250;  // 250 descuidos: deberia estar lejisimos de evolucionar
  p.ageMinutes = 9 * 60;  // nivel 10
  CHECK_MSG(!p.canEvolveNow(), "con 250 descuidos no puede evolucionar a nivel 10");
}

TEST(evolve, no_evoluciona_dormido_ni_de_huevo_ni_en_ceremonia) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 20 * 60;
  CHECK(p.canEvolveNow());
  p.toggleLight();
  CHECK(!p.canEvolveNow());
  p.toggleLight();
  p.startFarewell();
  CHECK(!p.canEvolveNow());

  Pet e;
  mockNvsReset();
  e.begin();
  CHECK(!e.canEvolveNow());
}

TEST(evolve, transforma_registra_y_anima) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 20 * 60;
  p.evolve();
  CHECK_EQ(p.speciesId, (int16_t)5);
  CHECK_EQ(p.prevSpeciesId, (int16_t)4);
  CHECK(p.isRegistered(5));
  CHECK(p.evolving());
  CHECK_RANGE(p.evolveT(), 0.0f, 1.0f);
  mockAdvanceMillis(EVOLVE_ANIM_MS + 1);
  CHECK(!p.evolving());
}

TEST(evolve, forma_final_no_evoluciona) {
  Pet p;
  makePet(p, 6);  // CHARIZARD
  p.ageMinutes = 200 * 60;
  CHECK(!p.canEvolveNow());
  p.evolve();
  CHECK_EQ(p.speciesId, (int16_t)6);
}

TEST(evolve, eevee_se_ramifica_en_134_136) {
  for (int seed = 1; seed <= 12; seed++) {
    randomSeed(seed);
    Pet p;
    makePet(p, DEX_EEVEE);
    p.ageMinutes = 40 * 60;
    CHECK(p.canEvolveNow());
    p.evolve();
    CHECK_RANGE(p.speciesId, (int16_t)134, (int16_t)136);
    CHECK(p.isRegistered(p.speciesId));
  }
}

TEST(evolve, boton_de_evolucion_se_pospone_al_declinar) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 15 * 60;  // nivel 16
  CHECK(p.wantEvolveButton());
  p.declineEvolve();
  CHECK_MSG(!p.wantEvolveButton(), "'mantener forma' quita el boton");
  CHECK_MSG(p.canEvolveNow(), "pero la evolucion sigue disponible");
  p.ageMinutes = 16 * 60;  // sube de nivel: se re-ofrece
  CHECK(p.wantEvolveButton());
}

// ---------------------------------------------------------------- stats
TEST(stats, formula_base_por_genes_mas_nivel_y_entreno) {
  Pet p;
  makePet(p, 4);  // CHARMANDER: atk 52, def 43, spe 65
  p.geneAtk = p.geneDef = p.geneSpe = 100;
  p.trAtk = p.trDef = p.trSpe = 0;
  p.ageMinutes = 0;  // nivel 1
  CHECK_EQ(p.atkStat(), (uint16_t)(52 + 1));
  CHECK_EQ(p.defStat(), (uint16_t)(43 + 1));
  CHECK_EQ(p.speStat(), (uint16_t)(65 + 1));
  p.ageMinutes = 9 * 60;  // nivel 10
  p.trAtk = 20;
  CHECK_EQ(p.atkStat(), (uint16_t)(52 + 10 + 20));
  p.geneAtk = 110;
  CHECK_EQ(p.atkStat(), (uint16_t)(52 * 110 / 100 + 10 + 20));
}

TEST(stats, el_huevo_no_tiene_stats) {
  mockNvsReset();
  Pet p;
  p.begin();
  CHECK_EQ(p.atkStat(), (uint16_t)0);
  CHECK_EQ(p.defStat(), (uint16_t)0);
  CHECK_EQ(p.speStat(), (uint16_t)0);
}

// The exercises (#78, exercise.h, endless since #126): each hit pays half its step, rising
// every EX_TIER_HITS hits (exerciseGain): 4 hits pay 2, 12 pay 11, 22 pay 30.
TEST(train, a_punch_pays_more_the_longer_the_run) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.trainStrength(4), (uint8_t)2);  // half a point each
  CHECK_EQ(p.trAtk, (uint8_t)2);
  CHECK_EQ(p.trainStrength(12), (uint8_t)11);
  CHECK_EQ(p.trainStrength(22), (uint8_t)30);  // the ribbon's worth in one run
  CHECK_EQ(p.trAtk, (uint8_t)43);
  CHECK_MSG(p.trainStrength(255) == 57, "more than a session can land pays EX_MAX_COUNT's: what fits");
  CHECK_EQ(p.trAtk, (uint8_t)100);
  CHECK_EQ(p.trDef, (uint8_t)0);
}

TEST(train, each_exercise_keeps_its_record_of_the_most_hits_in_a_session) {
  Pet p;
  makePet(p, 4);
  p.trainStrength(7);
  CHECK_EQ(p.punchHi, (uint8_t)7);
  p.trainStrength(5);
  CHECK_EQ(p.punchHi, (uint8_t)7);
  p.trainStrength(255);  // no session lands more than EX_MAX_COUNT: the record can't either
  CHECK_EQ(p.punchHi, (uint8_t)EX_MAX_COUNT);
  CHECK_EQ(p.pushHi, (uint8_t)0);
  p.trainStamina(9);
  CHECK_EQ(p.pushHi, (uint8_t)9);
  p.trainStamina(3);
  CHECK_EQ(p.pushHi, (uint8_t)9);
  CHECK_EQ(p.punchHi, (uint8_t)EX_MAX_COUNT);
}

TEST(train, a_push_up_pays_like_a_punch_and_trains_stamina_only) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.trainStamina(4), (uint8_t)2);
  CHECK_EQ(p.trDef, (uint8_t)2);
  CHECK_EQ(p.trainStamina(20), (uint8_t)25);
  CHECK_EQ(p.trDef, (uint8_t)27);
  CHECK_MSG(p.trAtk == 0 && p.punchHi == 0, "the push-ups train stamina only, and keep their own record");
}

// Arreglado al portar la bateria: trainStrength() devolvia la subida teorica
TEST(train, each_exercise_announces_what_really_went_in) {
  Pet p;
  makePet(p, 4);
  p.trAtk = 95;
  p.trDef = 97;
  CHECK_EQ(p.trainStrength(12), (uint8_t)5);  // 11, but only 5 fit
  CHECK_EQ(p.trAtk, (uint8_t)100);
  CHECK_EQ(p.trainStamina(12), (uint8_t)3);
  CHECK_EQ(p.trDef, (uint8_t)100);
  for (int i = 0; i < 3; i++) p.trainStrength(12);
  CHECK_EQ(p.trAtk, (uint8_t)100);
}

// Both exercises cost and reward like the old bag: tired (-12, not under 5), a little
// hungry (-5), weight burnt (2 a hit: 12 hits burn 24), joy +6, the heart from 4 hits, bond
// +2, the day's care and "exercised" (#44).
static void readyToTrain(Pet &p) {
  makePet(p, 4);
  setStats(p, 80, 50, 80, 80);
  p.weight = 40;
  p.bond = 10;
  mockAdvanceMillis(HEART_MS + 1);  // no heart left from hatching
}

TEST(train, both_exercises_tire_feed_burn_and_cheer_like_the_bag) {
  for (int kind = 0; kind < 2; kind++) {
    Pet p;
    readyToTrain(p);
    p.lastSeenEpoch = 100u * 86400 + 10 * 3600;  // a known day, so the care can be seen
    p.lastCareDay = 0;
    if (kind == 0) p.trainStrength(12);
    else p.trainStamina(12);
    const char *k = kind ? "push-ups" : "punch";
    CHECK_MSG(p.lastCareDay == 100, k);  // it is the day's care (registerCare)
    CHECK_MSG(p.energy == 68, k);
    CHECK_MSG(p.fullness == 75, k);
    CHECK_MSG(p.weight == 16, k);  // 40 - 24
    CHECK_MSG(p.joy == 56, k);
    CHECK_MSG(p.bond == 16, k);  // +2 for the exercise, +4 for the day's first care
    CHECK_MSG(p.showHeart(), k);
    p.energy = 3;  // el suelo protege de dejarlo a cero
    if (kind == 0) p.trainStrength(12);
    else p.trainStamina(12);
    CHECK_MSG(p.energy == 3, k);
  }
}

// A long run costs no more energy or food than a short one: only its weight burnt grows.
TEST(train, a_long_run_burns_more_weight_and_costs_the_same) {
  Pet p;
  readyToTrain(p);
  p.weight = 100;
  p.trainStrength(40);
  CHECK_EQ(p.weight, (uint8_t)20);  // 2 a hit
  CHECK_EQ(p.energy, (uint8_t)68);
  CHECK_EQ(p.fullness, (uint8_t)75);
  p.trainStamina(EX_MAX_COUNT);
  CHECK_EQ(p.weight, (uint8_t)0);  // never under 0
}

// An egg doesn't exercise: nothing paid, nothing spent.
TEST(train, an_egg_does_not_exercise) {
  for (int kind = 0; kind < 2; kind++) {
    Pet p;
    readyToTrain(p);
    p.speciesId = -1;
    uint8_t atk = p.trAtk, def = p.trDef;
    const char *k = kind ? "push-ups" : "punch";
    uint8_t got = kind ? p.trainStamina(12) : p.trainStrength(12);
    CHECK_MSG(got == 0, k);
    CHECK_MSG(p.trAtk == atk && p.trDef == def, k);
    CHECK_MSG(p.energy == 80 && p.fullness == 80 && p.weight == 40 && p.joy == 50, k);
    CHECK_MSG(p.bond == 10 && !p.showHeart(), k);
    CHECK_MSG(p.punchHi == 0 && p.pushHi == 0, k);
  }
}

TEST(train, a_short_session_burns_less_and_shows_the_heart_from_four_hits) {
  Pet p;
  readyToTrain(p);
  CHECK(!p.showHeart());
  p.trainStrength(3);  // burns 6, no heart
  CHECK_EQ(p.weight, (uint8_t)34);
  CHECK(!p.showHeart());
  p.trainStamina(4);  // burns 8, heart
  CHECK_EQ(p.weight, (uint8_t)26);
  CHECK(p.showHeart());
}

// Nothing done (no push-up done, no punch landed, the exit straight away): nothing paid and
// nothing spent, and it isn't the day's care.
TEST(train, a_session_with_nothing_done_neither_pays_nor_costs) {
  for (int kind = 0; kind < 2; kind++) {
    Pet p;
    readyToTrain(p);
    p.lastSeenEpoch = 100u * 86400 + 10 * 3600;
    p.lastCareDay = 0;
    uint8_t got = kind ? p.trainStamina(0) : p.trainStrength(0);
    const char *k = kind ? "push-ups" : "punch";
    CHECK_MSG(got == 0, k);
    CHECK_MSG(p.energy == 80 && p.fullness == 80 && p.weight == 40 && p.joy == 50, k);
    CHECK_MSG(p.bond == 10 && p.lastCareDay == 0 && !p.showHeart(), k);
  }
}

// The records (#126: NVS `punhi` and `pushi`) survive a restart, and one no session can land
// (past EX_MAX_COUNT: a damaged save) reads as none, each on its own.
TEST(train, the_records_survive_a_restart_and_an_impossible_one_reads_as_none) {
  Pet a;
  makePet(a, 4);
  a.trainStrength(9);
  a.trainStamina(14);
  Pet b;
  b.begin();
  CHECK_EQ(b.punchHi, (uint8_t)9);
  CHECK_EQ(b.pushHi, (uint8_t)14);
  Preferences raw;
  raw.begin("tamapoke", false);
  raw.putUChar("punhi", EX_MAX_COUNT + 1);
  raw.end();
  Pet c;
  c.begin();
  CHECK_EQ(c.punchHi, (uint8_t)0);
  CHECK_EQ(c.pushHi, (uint8_t)14);
  raw.begin("tamapoke", false);
  raw.putUChar("punhi", EX_MAX_COUNT);  // justo en el maximo: se conserva
  raw.putUChar("pushi", EX_MAX_COUNT + 1);
  raw.end();
  Pet d;
  d.begin();
  CHECK_EQ(d.punchHi, (uint8_t)EX_MAX_COUNT);
  CHECK_EQ(d.pushHi, (uint8_t)0);
  raw.begin("tamapoke", false);
  raw.putUChar("pushi", EX_MAX_COUNT);
  raw.end();
  Pet e;
  e.begin();
  CHECK_EQ(e.pushHi, (uint8_t)EX_MAX_COUNT);
}

// The records from before the exercises were endless are retired, not migrated (#126, owner's
// decision 2026-11-28: fresh records): the 12-swing punch's `phi` and the old bag's tap record
// `shi` are neither read, written nor cleared, so an older firmware flashed back still finds
// the player's records, and the new ones start at none.
TEST(train, the_old_records_are_left_in_nvs_as_they_were) {
  Pet a;
  makePet(a, 4);
  Preferences raw;
  raw.begin("tamapoke", false);
  raw.putUShort("shi", 57);
  raw.putUChar("phi", 12);
  raw.end();
  Pet b;
  b.begin();
  CHECK_MSG(b.punchHi == 0, "the 12-swing record is not the endless punch's");
  b.trainStrength(5);  // saves
  raw.begin("tamapoke", true);
  CHECK_EQ(raw.getUShort("shi", 0), (uint16_t)57);
  CHECK_EQ(raw.getUChar("phi", 0), (uint8_t)12);
  CHECK_EQ(raw.getUChar("punhi", 0), (uint8_t)5);
  raw.end();
  CHECK_EQ(b.punchHi, (uint8_t)5);
}

TEST(play, minijuego_entrena_velocidad_y_guarda_record) {
  Pet p;
  makePet(p, 4);
  p.joy = 10;
  p.weight = 50;
  p.playGame(GAME_KEEPY, 20);
  CHECK_EQ(p.trSpe, (uint8_t)5);  // gameSpeedPay(keepy, 20): 4 hits at step 1, 8 at step 2 and 8 at step 3: 4.5, half up
  CHECK_EQ(p.joy, (uint8_t)45);   // +35: a game the player touched (#127)
  CHECK_EQ(p.weight, (uint8_t)10);  // 50 - 20*2
  CHECK_EQ(p.gameHi, (uint16_t)20);
  p.playGame(GAME_KEEPY, 5);
  CHECK_MSG(p.gameHi == 20, "un resultado peor no baja el record");
}

// ---------------------------------------------------------------- minijuegos de Play
// mismo bicho y mismas stats para comparar recompensas entre juegos
static void freshPlayer(Pet &p) {
  makePet(p, 4);
  setStats(p, 60, 10, 80, 50);
  p.weight = 80;
  p.trSpe = 10;
}

TEST(play, cada_minijuego_guarda_su_propio_record) {
  Pet p;
  freshPlayer(p);
  p.playGame(GAME_CATCH, 30);
  CHECK_EQ(p.catchHi, (uint16_t)30);
  CHECK_MSG(p.gameHi == 0, "keephi sigue siendo solo el record de keepy-uppy");
  p.playGame(GAME_PEEK, 9);
  p.playGame(GAME_SONG, 7);
  p.playGame(GAME_KEEPY, 12);
  CHECK_EQ(p.peekHi, (uint16_t)9);
  CHECK_EQ(p.songHi, (uint16_t)7);
  CHECK_EQ(p.gameHi, (uint16_t)12);
  CHECK_EQ(p.catchHi, (uint16_t)30);
  CHECK_EQ(p.gameBest(GAME_KEEPY), (uint16_t)12);
  CHECK_EQ(p.gameBest(GAME_CATCH), (uint16_t)30);
  CHECK_EQ(p.gameBest(GAME_PEEK), (uint16_t)9);
  CHECK_EQ(p.gameBest(GAME_SONG), (uint16_t)7);
  CHECK_EQ(p.gameBest(GAME_COUNT), (uint16_t)0);
  p.playGame(GAME_CATCH, 3);
  CHECK_MSG(p.catchHi == 30, "una partida peor no baja el record");
}

TEST(play, a_la_par_todos_los_minijuegos_pagan_lo_mismo) {
  for (uint8_t g = 0; g < GAME_COUNT; g++) {
    Pet p;
    freshPlayer(p);
    p.playGame(g, GAME_PAR[g]);
    std::string where = "juego " + std::to_string(g);
    CHECK_MSG(p.joy == 45, where + ": JOY +5 +30");
    CHECK_MSG(p.trSpe == 13, where + ": VEL +3");
    CHECK_MSG(p.weight == 50, where + ": peso -30");
    CHECK_MSG(p.energy == 63, where + ": ENE -(10 + 15/2)");
    CHECK_MSG(p.showHeart(), where + ": corazon");
  }
}

// playGame() returns the speed it really trained (#76), for the result screen's "SPEED +3"
TEST(play, play_game_returns_the_speed_it_trained) {
  Pet p;
  freshPlayer(p);  // speed 10
  CHECK_EQ(p.playGame(GAME_KEEPY, GAME_PAR[GAME_KEEPY]), (uint8_t)3);
  CHECK_EQ(p.trSpe, (uint8_t)13);
  CHECK_EQ(p.playGame(GAME_KEEPY, 3), (uint8_t)0);  // 3 hits pay no speed
  p.trSpe = 99;
  CHECK_MSG(p.playGame(GAME_CATCH, 200) == 1, "at 99 only 1 fits, not the 100 the run earned");
  CHECK_EQ(p.trSpe, (uint8_t)100);
  CHECK_EQ(p.playGame(GAME_CATCH, 200), (uint8_t)0);
  CHECK_EQ(p.playGame(GAME_COUNT, 10), (uint8_t)0);  // not a game
  mockNvsReset();
  Pet egg;
  egg.begin();
  CHECK_EQ(egg.playGame(GAME_KEEPY, 15), (uint8_t)0);
}

// #127: every game pays by its own ladder (gameSpeedPay), through the real Play path
TEST(play, each_game_pays_its_speed_by_its_ladder) {
  struct Row { uint8_t game; uint16_t score; uint8_t speed; };
  const Row rows[] = { { GAME_KEEPY, 30, 9 }, { GAME_CATCH, 20, 3 }, { GAME_CATCH, 40, 10 }, { GAME_PEEK, 14, 3 },
                       { GAME_SONG, 2, 1 }, { GAME_SONG, 9, 6 }, { GAME_SONG, 12, 10 }, { GAME_KEEPY, 255, 100 } };
  for (const Row &r : rows) {
    Pet p;
    freshPlayer(p);
    p.trSpe = 0;
    CHECK_MSG(p.playGame(r.game, r.score) == r.speed,
              "game " + std::to_string(r.game) + " score " + std::to_string(r.score) + " paid " +
                  std::to_string(p.trSpe));
    CHECK_EQ(p.trSpe, r.speed);
  }
}

TEST(play, what_a_run_costs_is_capped_at_30_but_its_pay_and_its_record_are_not) {
  Pet p;
  freshPlayer(p);
  p.playGame(GAME_KEEPY, 40);
  CHECK_EQ(p.trSpe, (uint8_t)25);   // 10 + 15: the pay rises with the run (gameSpeedPay), past the cost's cap
  CHECK_EQ(p.weight, (uint8_t)20);  // 80 - 30*2: the cost stops at a normalised 30
  CHECK_EQ(p.energy, (uint8_t)55);  // 80 - (10 + 30/2)
  CHECK_EQ(p.gameHi, (uint16_t)40);
}

TEST(play, un_record_imposible_se_recorta_al_maximo_del_juego) {
  Pet p;
  freshPlayer(p);
  p.playGame(GAME_PEEK, 500);
  CHECK_EQ(p.peekHi, (uint16_t)255);
  p.playGame(GAME_SONG, 101);
  CHECK_EQ(p.songHi, (uint16_t)100);
  p.playGame(GAME_CATCH, 1000);
  CHECK_EQ(p.catchHi, (uint16_t)400);
  p.playGame(GAME_KEEPY, 65535);
  CHECK_EQ(p.gameHi, (uint16_t)255);
  p.playGame(GAME_COUNT, 5);  // juego que no existe: nada
  Pet q;
  freshPlayer(q);
  q.playGame(GAME_COUNT, 5);
  CHECK_EQ(q.gameHi, (uint16_t)0);
}

TEST(play, sin_bicho_no_hay_premio_ni_record) {
  mockNvsReset();
  mockSetMillis(0);
  Pet p;
  p.begin();
  if (p.awaitingStarter()) p.chooseStarter(4);
  CHECK(p.isEgg());
  uint8_t joy = p.joy;
  p.playGame(GAME_CATCH, 20);
  CHECK_EQ(p.catchHi, (uint16_t)0);
  CHECK_EQ(p.joy, joy);
}

TEST(nvs, records_de_los_minijuegos_sobreviven_al_reinicio) {
  Pet a;
  freshPlayer(a);
  a.playGame(GAME_KEEPY, 21);
  a.playGame(GAME_CATCH, 33);
  a.playGame(GAME_PEEK, 17);
  a.playGame(GAME_SONG, 8);
  Pet b;
  b.begin();
  CHECK_EQ(b.gameHi, (uint16_t)21);
  CHECK_EQ(b.catchHi, (uint16_t)33);
  CHECK_EQ(b.peekHi, (uint16_t)17);
  CHECK_EQ(b.songHi, (uint16_t)8);
  // in their own keys (#127), and the old ones are neither written nor read
  Preferences raw;
  raw.begin("tamapoke", true);
  CHECK_EQ(raw.getUShort("keephi", 0), (uint16_t)21);
  CHECK_EQ(raw.getUShort("catchhi", 0), (uint16_t)33);
  CHECK_EQ(raw.getUShort("peekhi", 0), (uint16_t)17);
  CHECK_EQ(raw.getUShort("songhi", 0), (uint16_t)8);
  CHECK_MSG(!raw.isKey("ghi") && !raw.isKey("mgc") && !raw.isKey("mgp") && !raw.isKey("mgs"),
            "the old record keys are written again");
  raw.end();
}

// A save from before the games were endless has records in the old keys, which mean something
// else now: they are not read (and not touched, so an older firmware flashed back finds them).
TEST(nvs, the_old_record_keys_are_not_read_and_not_touched) {
  Pet a;
  freshPlayer(a);
  Preferences raw;
  raw.begin("tamapoke", false);
  raw.putUShort("ghi", 12);
  raw.putUShort("mgc", 30);
  raw.putUShort("mgp", 9);
  raw.putUShort("mgs", 7);
  raw.end();
  Pet b;
  b.begin();
  CHECK_EQ(b.gameHi, (uint16_t)0);
  CHECK_EQ(b.catchHi, (uint16_t)0);
  CHECK_EQ(b.peekHi, (uint16_t)0);
  CHECK_EQ(b.songHi, (uint16_t)0);
  b.playGame(GAME_KEEPY, 5);  // saves
  raw.begin("tamapoke", true);
  CHECK_EQ(raw.getUShort("ghi", 0), (uint16_t)12);
  CHECK_EQ(raw.getUShort("mgc", 0), (uint16_t)30);
  CHECK_EQ(raw.getUShort("mgp", 0), (uint16_t)9);
  CHECK_EQ(raw.getUShort("mgs", 0), (uint16_t)7);
  CHECK_EQ(raw.getUShort("keephi", 0), (uint16_t)5);
  raw.end();
}

TEST(nvs, records_imposibles_en_la_nvs_se_leen_como_0) {
  Pet a;
  freshPlayer(a);
  Preferences raw;
  raw.begin("tamapoke", false);
  raw.putUShort("keephi", 256);
  raw.putUShort("catchhi", 401);
  raw.putUShort("peekhi", 256);
  raw.putUShort("songhi", 101);
  raw.end();
  Pet b;
  b.begin();
  CHECK_EQ(b.gameHi, (uint16_t)0);
  CHECK_EQ(b.catchHi, (uint16_t)0);
  CHECK_EQ(b.peekHi, (uint16_t)0);
  CHECK_EQ(b.songHi, (uint16_t)0);
  raw.begin("tamapoke", false);  // justo en el maximo: se conservan
  raw.putUShort("keephi", 255);
  raw.putUShort("catchhi", 400);
  raw.putUShort("peekhi", 255);
  raw.putUShort("songhi", 100);
  raw.end();
  Pet c;
  c.begin();
  CHECK_EQ(c.gameHi, (uint16_t)255);
  CHECK_EQ(c.catchHi, (uint16_t)400);
  CHECK_EQ(c.peekHi, (uint16_t)255);
  CHECK_EQ(c.songHi, (uint16_t)100);
}

TEST(nvs, sin_claves_de_minijuegos_los_records_empiezan_en_0) {
  Pet a;
  freshPlayer(a);  // guardado sin las claves de los records (como uno de antes)
  Preferences raw;
  raw.begin("tamapoke", false);
  raw.remove("keephi");
  raw.remove("catchhi");
  raw.remove("peekhi");
  raw.remove("songhi");
  raw.end();
  Pet b;
  b.begin();
  CHECK_EQ(b.gameHi, (uint16_t)0);
  CHECK_EQ(b.catchHi, (uint16_t)0);
  CHECK_EQ(b.peekHi, (uint16_t)0);
  CHECK_EQ(b.songHi, (uint16_t)0);
}

// A game, the real Play path (#87): scored 10, it cheers (+35 since #127: a game the player
// touched gives full joy whatever it scored), tires (-10, -half the score), makes a little
// hungry (-5) and shows the heart.
TEST(play, a_game_cheers_tires_and_makes_hungry) {
  Pet p;
  makePet(p, 4);
  setStats(p, 50, 50, 50, 50);
  p.playGame(GAME_KEEPY, 10);  // normalised 10
  CHECK_EQ(p.joy, (uint8_t)85);
  CHECK_EQ(p.energy, (uint8_t)35);
  CHECK_EQ(p.fullness, (uint8_t)45);
  CHECK(p.showHeart());
}

// #127: a game ends at its first miss, so a short run is still play: any game the player
// touched gives full joy, +2 bond and the heart, even one that scored 0 (and no speed). One
// nobody touched (the 15 s idle end) keeps the little it always gave: +5 joy, no heart.
TEST(play, a_touched_game_that_scored_0_still_gives_full_joy_the_heart_and_bond) {
  for (uint8_t g = 0; g < GAME_COUNT; g++) {
    Pet p;
    makePet(p, 4);
    setStats(p, 50, 50, 50, 50);
    p.bond = 10;
    uint8_t spe = p.trSpe;
    CHECK_EQ(p.playGame(g, 0), (uint8_t)0);
    CHECK_MSG(p.joy == 85, "joy +35 in game " + std::to_string(g));
    CHECK_MSG(p.showHeart(), "heart in game " + std::to_string(g));
    CHECK_MSG(p.bond == 12, "bond +2 in game " + std::to_string(g));
    CHECK_EQ(p.trSpe, spe);
    CHECK_EQ(p.energy, (uint8_t)40);  // the run's cost: 10 + 0
    CHECK_EQ(p.fullness, (uint8_t)45);
    CHECK_EQ(p.gameBest(g), (uint16_t)0);
  }
  Pet idle;
  makePet(idle, 4);
  setStats(idle, 50, 50, 50, 50);
  idle.bond = 10;
  CHECK_EQ(idle.playGame(GAME_CATCH, 0, false), (uint8_t)0);
  CHECK_EQ(idle.joy, (uint8_t)55);  // +5
  CHECK(!idle.showHeart());
  CHECK_EQ(idle.bond, (uint8_t)12);
  Pet playful;  // a playful pet gets its extra on top of the full joy
  makePet(playful, 4);
  playful.nature = NAT_PLAYFUL;
  setStats(playful, 50, 50, 50, 50);
  playful.playGame(GAME_PEEK, 0);
  CHECK_EQ(playful.joy, (uint8_t)(85 + NAT_PLAY_JOY_EXTRA));
}

TEST(care, limpiar_quita_cacas_y_deja_higiene_a_tope) {
  Pet p;
  makePet(p, 4);
  p.poops = 3;
  p.hygiene = 10;
  p.clean();
  CHECK_EQ(p.poops, (uint8_t)0);
  CHECK_EQ(p.hygiene, (uint8_t)100);
}

TEST(care, mimar_sube_felicidad_y_vinculo) {
  Pet p;
  makePet(p, 4);
  p.joy = 50;
  p.caress();
  CHECK_EQ(p.joy, (uint8_t)55);
  CHECK_EQ(p.bond, (uint8_t)1);
  CHECK(p.showHeart());
}

TEST(care, el_vinculo_tiene_tope_diario) {
  Pet p;
  makePet(p, 4);
  for (int i = 0; i < 50; i++) p.playGame(GAME_KEEPY, 10);  // +2 de vinculo cada vez
  CHECK_MSG(p.bond <= 20, "tope de 20 puntos de vinculo al dia");
  CHECK_EQ(p.bond, (uint8_t)20);
}

TEST(mood, refleja_el_estado) {
  Pet p;
  makePet(p, 4);
  setStats(p, 80, 80, 80, 80);
  CHECK_EQ((int)p.mood(), (int)MOOD_HAPPY);
  p.joy = 20;
  CHECK_EQ((int)p.mood(), (int)MOOD_SAD);
  p.joy = 80;
  p.feedBerry(0);
  CHECK_EQ((int)p.mood(), (int)MOOD_EATING);
  mockAdvanceMillis(EAT_ANIM_MS + 1);
  p.toggleLight();
  CHECK_EQ((int)p.mood(), (int)MOOD_SLEEPING);
}

// ---------------------------------------------------------------- pokedex
TEST(dexreg, bitmap_cubre_1_a_151) {
  Pet p;
  makePet(p, 4);
  memset(p.dexReg, 0, sizeof(p.dexReg));
  CHECK_EQ(p.registeredCount(), (uint16_t)0);
  for (int16_t d = 1; d <= 151; d++) {
    p.dexReg[(d - 1) >> 3] |= (uint8_t)(1 << ((d - 1) & 7));
    CHECK(p.isRegistered(d));
  }
  CHECK_EQ(p.registeredCount(), (uint16_t)151);
}

TEST(dexreg, fuera_de_rango_no_esta_registrado) {
  Pet p;
  makePet(p, 4);
  memset(p.dexReg, 0xFF, sizeof(p.dexReg));
  CHECK(!p.isRegistered(0));
  CHECK(!p.isRegistered(-1));
  CHECK(!p.isRegistered(152));
  CHECK(!p.isRegistered(32767));
  CHECK_EQ(p.registeredCount(), (uint16_t)151);
}

TEST(dexreg, linea_incompleta_se_detecta) {
  Pet p;
  makePet(p, 1);  // BULBASAUR -> IVYSAUR -> VENUSAUR
  CHECK(p.lineHasUnregistered(1));
  p.dexReg[(2 - 1) >> 3] |= (uint8_t)(1 << ((2 - 1) & 7));
  p.dexReg[(3 - 1) >> 3] |= (uint8_t)(1 << ((3 - 1) & 7));
  CHECK_MSG(!p.lineHasUnregistered(1), "linea 1-2-3 completa");
}

TEST(dexreg, la_rama_de_eevee_cuenta_las_tres) {
  Pet p;
  makePet(p, DEX_EEVEE);
  CHECK(p.lineHasUnregistered(DEX_EEVEE));
  for (int16_t d = 134; d <= 136; d++)
    p.dexReg[(d - 1) >> 3] |= (uint8_t)(1 << ((d - 1) & 7));
  CHECK(!p.lineHasUnregistered(DEX_EEVEE));
}

TEST(egg, el_sorteo_siempre_da_una_especie_valida) {
  Pet p;
  makePet(p, 4);
  for (int i = 0; i < 400; i++) {
    randomSeed(i * 7919 + 1);
    int16_t d = p.pickEggSpecies();
    CHECK_RANGE(d, (int16_t)1, (int16_t)151);
    CHECK_MSG(DEX_TBL[d].rarity != R_EVO, "de un huevo nunca sale una forma evolucionada");
  }
}

TEST(egg, el_primer_huevo_es_un_inicial_clasico) {
  for (int seed = 1; seed <= 20; seed++) {
    randomSeed(seed);
    mockNvsReset();
    Pet p;
    p.begin();
    int16_t d = p.pickEggSpecies();
    bool classic = false;
    for (int i = 0; i < NUM_CLASSIC_DEX; i++)
      if (CLASSIC_DEX[i] == d) classic = true;
    CHECK_MSG(classic, "sin pokedex, solo salen iniciales clasicos");
  }
}

TEST(egg, una_escapada_castiga_la_rareza) {
  Pet p;
  makePet(p, 4);
  p.lastEnd = CER_RUNAWAY;
  for (int seed = 1; seed <= 60; seed++) {
    randomSeed(seed);
    int16_t d = p.pickEggSpecies();
    CHECK_MSG(DEX_TBL[d].rarity == R_COMUN, "tras una escapada solo salen comunes");
  }
}

// ---------------------------------------------------------------- racha
TEST(streak, el_primer_cuidado_del_dia_suma) {
  Pet p;
  makePet(p, 4);
  p.setClock(100u * 86400);
  p.caress();
  CHECK_EQ(p.streak, (uint16_t)1);
  CHECK_EQ(p.bestStreak, (uint16_t)1);
  p.caress();
  CHECK_MSG(p.streak == 1, "el segundo cuidado del mismo dia no suma");
  p.setClock(101u * 86400);
  p.caress();
  CHECK_EQ(p.streak, (uint16_t)2);
}

TEST(streak, un_dia_perdido_reinicia) {
  Pet p;
  makePet(p, 4);
  p.setClock(100u * 86400);
  p.caress();
  p.setClock(101u * 86400);
  p.caress();
  p.setClock(105u * 86400);  // hueco de dias
  p.caress();
  CHECK_EQ(p.streak, (uint16_t)1);
  CHECK_MSG(p.bestStreak == 2, "el record de racha se conserva");
}

TEST(streak, sin_reloj_no_hay_racha) {
  Pet p;
  makePet(p, 4);
  p.caress();
  CHECK_EQ(p.streak, (uint16_t)0);
}

TEST(streak, celebra_los_hitos) {
  Pet p;
  makePet(p, 4);
  for (uint32_t d = 100; d < 103; d++) {
    p.setClock(d * 86400);
    p.caress();
  }
  CHECK_EQ(p.streak, (uint16_t)3);
  CHECK_MSG(p.showMilestone(), "hito de 3 dias celebrado");
  CHECK_EQ(p.lastMilestone, (uint16_t)3);
}

TEST(bonus, racha_y_vinculo_mejoran_el_huevo) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.careBonus(), 0);
  p.streak = 30;
  p.bond = 100;
  CHECK_EQ(p.careBonus(), 14);
  p.streak = 300;  // la racha se capa a 30
  CHECK_EQ(p.careBonus(), 14);
}

// ---------------------------------------------------------------- medallas
TEST(medals, nivel_10_da_medalla) {
  Pet p;
  makePet(p, 4);
  CHECK(!p.hasMedal(MED_LV10));
  p.ageMinutes = 9 * 60 - 1;
  advance(p, 1);
  CHECK(p.hasMedal(MED_LV10));
  CHECK(p.showMedal());
}

// sin sobrepeso, nivel 5 y cero descuidos
TEST(medals, en_forma_llega_al_nivel_5) {
  Pet p;
  makePet(p, 4);
  p.weight = 0;
  p.ageMinutes = 4 * 60 - 1;
  advance(p, 1);
  CHECK(p.hasMedal(MED_FIT));
  CHECK_EQ(p.totalMedals, (uint16_t)1);

  Pet q;
  makePet(q, 4);
  q.careMistakes = 1;   // un descuido la deja fuera
  q.ageMinutes = 4 * 60 - 1;
  advance(q, 1);
  CHECK(!q.hasMedal(MED_FIT));
}

TEST(medals, cada_medalla_nueva_suma_al_total) {
  Pet p;
  makePet(p, 4);
  p.weight = 0;
  p.ageMinutes = 9 * 60 - 1;
  advance(p, 1);  // cruza nivel 10: MED_LV10 + MED_FIT de golpe
  CHECK(p.hasMedal(MED_LV10));
  CHECK(p.hasMedal(MED_FIT));
  CHECK_MSG(p.totalMedals == 2, "el total cuenta cada bit ganado, no cada evento");
}

TEST(medals, forma_final_al_nacer) {
  Pet p;
  makePet(p, 143);  // SNORLAX ya es forma final
  CHECK(p.hasMedal(MED_FINAL));
  CHECK_EQ(p.totalMedals, (uint16_t)1);
}

TEST(medals, la_baya_favorita_da_medalla) {
  Pet p;
  makePet(p, 4);
  p.feedBerry(1);  // su favorita
  CHECK(p.berryKnown);
  advance(p, 1);
  CHECK(p.hasMedal(MED_BERRY));
}

TEST(medals, el_total_acumula_entre_crianzas) {
  Pet p;
  makePet(p, 143);
  CHECK_EQ(p.totalMedals, (uint16_t)1);
  p.newEgg();
  p.chooseStarter(144);  // ARTICUNO, otra forma final
  p.eggTap(); p.eggTap(); p.eggTap();
  CHECK_MSG(p.medals == MED_FINAL, "las medallas del individuo se reinician");
  CHECK_MSG(p.totalMedals == 2, "el total del jugador se conserva");
}

// ---------------------------------------------------------------- training ribbons (#76)
// One ribbon per trained stat, at RIBBON_STR / RIBBON_STA / RIBBON_SPD training points.
TEST(ribbons, each_ribbon_comes_at_its_threshold) {
  const struct { uint16_t medal; uint8_t goal; } R[] = {
    { MED_STR, RIBBON_STR }, { MED_STA, RIBBON_STA }, { MED_SPD, RIBBON_SPD } };
  for (uint8_t k = 0; k < 3; k++) {
    Pet p;
    makePet(p, 4);
    uint8_t *tr = k == 0 ? &p.trAtk : k == 1 ? &p.trDef : &p.trSpe;
    std::string where = "ribbon " + std::to_string(k);
    *tr = R[k].goal - 1;
    advance(p, 1);
    CHECK_MSG(!p.hasMedal(R[k].medal), where + ": not one point short");
    CHECK_MSG(p.medals == 0, where + ": no other medal either");
    *tr = R[k].goal;
    advance(p, 1);
    CHECK_MSG(p.hasMedal(R[k].medal), where + ": at the goal");
    CHECK_MSG(p.medals == R[k].medal, where + ": that ribbon only");
    CHECK_MSG(p.showMedal() && (1u << p.bannerShown()) == R[k].medal, where + ": its banner");
    CHECK_MSG(p.totalMedals == 1, where + ": counted once");
  }
}

// ---------------------------------------------------------------- celebration banners (#82, #83)
static int8_t bitOf(uint16_t medal) {
  int8_t i = 0;
  while (!(medal & 1)) {
    medal >>= 1;
    i++;
  }
  return i;
}

// The banner on screen is `b` from now for its whole `ms`, not a moment less: checks its first
// and its last millisecond, and leaves the clock at its end.
static void expectBanner(Pet &p, int8_t b, uint32_t ms, const std::string &what) {
  CHECK_MSG(p.bannerShown() == b, what + ": on screen (got " + std::to_string(p.bannerShown()) + ")");
  mockAdvanceMillis(ms - 1);
  CHECK_MSG(p.bannerShown() == b, what + ": its whole time (got " + std::to_string(p.bannerShown()) + ")");
  mockAdvanceMillis(1);
}

// four medals on one tick: level 10, IN SHAPE (no weight, level 5 or more, no mistakes) and the
// stamina and speed ribbons
static void fourMedalsOnOneTick(Pet &p) {
  makePet(p, 4);
  p.ageMinutes = 9 * MINUTES_PER_LEVEL - 1;
  p.trDef = RIBBON_STA;
  p.trSpe = RIBBON_SPD;
  advance(p, 1);
}

// #82: an older pet can gain several medals on one minute tick. Each gets a banner of its own,
// for its whole time, lowest bit first, and the last one ends like a single banner did. #116:
// an accessory a medal earned comes straight after that medal's (the Cap after level 10, the
// Laurel after IN SHAPE, the first of the three that earn it).
TEST(banners, several_medals_on_one_tick_each_get_their_own_banner) {
  Pet p;
  fourMedalsOnOneTick(p);
  CHECK_EQ(p.medals, (uint16_t)(MED_LV10 | MED_FIT | MED_STA | MED_SPD));
  const int8_t want[] = { bitOf(MED_LV10), (int8_t)(BANNER_ACC + ACC_CAP), bitOf(MED_FIT),
                          (int8_t)(BANNER_ACC + ACC_LAUREL), bitOf(MED_STA), bitOf(MED_SPD) };
  for (int8_t b : want) expectBanner(p, b, MEDAL_BANNER_MS, "banner " + std::to_string(b));
  CHECK_MSG(p.bannerShown() == BANNER_NONE, "nothing after the last");
}

// #83: the day's first care that makes a 7-day streak brings the milestone and the 7 STREAK
// medal at once. The milestone shows first, for its whole time (it was half a second), then
// the medal for its own.
TEST(banners, a_streak_milestone_shows_first_then_the_medal_it_came_with) {
  Pet p;
  makePet(p, 4);
  p.lastSeenEpoch = 100u * 86400 + 12 * 3600;
  p.lastCareDay = 99;  // yesterday: today's first care continues the streak
  p.streak = 6;
  p.caress();
  CHECK_EQ(p.streak, (uint16_t)7);
  CHECK(p.hasMedal(MED_STREAK7));
  expectBanner(p, BANNER_STREAK, MILESTONE_BANNER_MS, "the milestone");
  expectBanner(p, bitOf(MED_STREAK7), MEDAL_BANNER_MS, "then its medal");
  expectBanner(p, BANNER_ACC + ACC_FLOWERS, MEDAL_BANNER_MS, "then the Flowers it earned (#116)");
  CHECK_MSG(p.bannerShown() == BANNER_NONE, "nothing after it");
}

// The banner's words (drawCelebration): a medal's "MEDAL!" and that medal's own name, a
// milestone's "AWESOME!" and the streak's days, in every language.
TEST(banners, each_banner_names_its_own_medal_or_the_streak) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    std::string where = "[lang " + std::to_string(lang) + "]";
    const char *l1 = nullptr, *l2 = nullptr;
    for (int8_t b = 0; b < MED_COUNT; b++) {
      bannerLines(b, 7, &l1, &l2);
      CHECK_MSG(!strcmp(l1, T(S_MEDAL_BANNER)), where + " medal " + std::to_string(b) + ": its title");
      CHECK_MSG(!strcmp(l2, medalName(b)), where + " medal " + std::to_string(b) + ": its own name");
    }
    bannerLines(BANNER_STREAK, 30, &l1, &l2);
    char want[32];
    snprintf(want, sizeof(want), T(S_STREAK_DAYS_FMT), 30u);
    CHECK_MSG(!strcmp(l1, T(S_GREAT)), where + " streak: its title");
    CHECK_MSG(!strcmp(l2, want), where + " streak: its days");
  }
  gLang = LANG_DEFAULT;
}

// One that comes while another shows waits its turn: the one on screen keeps its whole time,
// even for a lower medal. A milestone goes before the medals still waiting.
TEST(banners, one_that_comes_while_another_shows_waits_its_turn) {
  BannerQueue q;
  q.add(MED_STR | MED_SPD, false, 1000);  // strength on screen, speed waiting
  CHECK_EQ((int)q.shown(1000), (int)bitOf(MED_STR));
  q.add(MED_LV10, false, 2000);  // a lower medal, later
  CHECK_EQ((int)q.shown(2000), (int)bitOf(MED_STR));
  q.add(0, true, 3000);  // the day's first care
  uint32_t t = 1000 + MEDAL_BANNER_MS;
  CHECK_EQ((int)q.shown(t - 1), (int)bitOf(MED_STR));
  CHECK_EQ((int)q.shown(t), (int)BANNER_STREAK);
  t += MILESTONE_BANNER_MS;
  CHECK_EQ((int)q.shown(t - 1), (int)BANNER_STREAK);
  CHECK_EQ((int)q.shown(t), (int)bitOf(MED_LV10));
  CHECK_EQ((int)q.shown(t + MEDAL_BANNER_MS), (int)bitOf(MED_SPD));
  CHECK_EQ((int)q.shown(t + 2 * MEDAL_BANNER_MS), (int)BANNER_NONE);
}

// add() takes the banner that ended off the screen first: a medal that comes after it ended, with
// no pass in between, shows at once for its whole time, not behind the stale one.
TEST(banners, a_medal_added_after_the_one_on_screen_ended_shows_at_once) {
  BannerQueue q;
  q.add(MED_STR, false, 1000);
  uint32_t t = 1000 + MEDAL_BANNER_MS + 5000;  // strength ended, and no update came
  q.add(MED_SPD, false, t);
  CHECK_EQ((int)q.shown(t), (int)bitOf(MED_SPD));
  CHECK_EQ((int)q.shown(t + MEDAL_BANNER_MS - 1), (int)bitOf(MED_SPD));
  CHECK_EQ((int)q.shown(t + MEDAL_BANNER_MS), (int)BANNER_NONE);
}

// hold() takes the banner that ended off first too: a minigame that opens late does not bring an
// ended banner back for another whole time.
TEST(banners, hold_does_not_bring_back_a_banner_that_ended) {
  BannerQueue q;
  q.add(MED_STR, false, 0);
  uint32_t t = MEDAL_BANNER_MS + 1000;
  q.hold(t);
  CHECK_EQ((int)q.shown(t), (int)BANNER_NONE);
}

// A milestone waiting behind the banner on screen keeps its place when a medal comes after it:
// the day's first care reaches the milestone while a medal shows, and its checkMedals() adds the
// 7 STREAK medal right after (Pet::registerCare)
TEST(banners, a_waiting_milestone_stays_when_a_medal_comes_after_it) {
  BannerQueue q;
  q.add(MED_STR, false, 1000);  // on screen
  q.add(0, true, 2000);         // the milestone waits
  q.add(MED_STREAK7, false, 2000);
  uint32_t t = 1000 + MEDAL_BANNER_MS;
  CHECK_EQ((int)q.shown(t), (int)BANNER_STREAK);
  t += MILESTONE_BANNER_MS;
  CHECK_EQ((int)q.shown(t), (int)bitOf(MED_STREAK7));
  CHECK_EQ((int)q.shown(t + MEDAL_BANNER_MS), (int)BANNER_NONE);
}

// Bounded by its shape: every medal at once (an old save's first tick after an update could)
// and a milestone make MED_COUNT + 1 banners, each once, then none and an empty queue.
TEST(banners, the_queue_is_bounded_each_once_then_none) {
  BannerQueue q;
  q.add(MED_ALL, true, 0);
  uint32_t t = 0;
  CHECK_EQ((int)q.shown(t), (int)BANNER_STREAK);
  t += MILESTONE_BANNER_MS;
  for (int8_t b = 0; b < MED_COUNT; b++) {
    CHECK_EQ((int)q.shown(t), (int)b);
    CHECK_EQ((int)q.shown(t + MEDAL_BANNER_MS - 1), (int)b);
    t += MEDAL_BANNER_MS;
  }
  CHECK_EQ((int)q.shown(t), (int)BANNER_NONE);
  CHECK_EQ(t, (uint32_t)(MED_COUNT * MEDAL_BANNER_MS + MILESTONE_BANNER_MS));
  q.update(t);
  CHECK(q.on == BANNER_NONE && q.medals == 0 && !q.streak);
}

// The queue keeps time with no pass in between (the screen off, a slow frame): each banner
// starts as the one before it ends, not when a pass next looks.
TEST(banners, each_starts_as_the_one_before_it_ends) {
  BannerQueue q;
  q.add(MED_STR | MED_SPD, false, 1000);
  q.update(1000 + MEDAL_BANNER_MS + 500);  // the first pass after strength ended came late
  CHECK_EQ((int)q.shown(1000 + 2 * MEDAL_BANNER_MS - 1), (int)bitOf(MED_SPD));
  CHECK_EQ((int)q.shown(1000 + 2 * MEDAL_BANNER_MS), (int)BANNER_NONE);
}

// An ending (a farewell, a run-away, a release) clears them: none shows over it, and none that
// was waiting turns up over the pet that hatches after it.
TEST(banners, an_ending_clears_them) {
  for (int k = 0; k < 3; k++) {
    std::string ending = k == 0 ? "farewell" : k == 1 ? "run-away" : "release";
    Pet p;
    fourMedalsOnOneTick(p);  // 16 s of banners
    mockAdvanceMillis(1000);
    if (k == 0) p.startFarewell();
    else if (k == 1) p.startRunaway();
    else p.release();
    CHECK_MSG(p.ceremony != CER_NONE, ending + ": started");
    CHECK_MSG(p.bannerShown() == BANNER_NONE, ending + ": no banner over it");
    mockAdvanceMillis(CEREMONY_MS);
    p.update(millis());  // the ceremony ends: an egg
    if (p.awaitingEgg()) p.chooseEgg(0);
    p.eggTap();
    p.eggTap();
    p.eggTap();
    CHECK_MSG(!p.isEgg(), ending + ": the next pet hatched");
    // (a pet that hatches in its final form gains TOP FORM at once: its own banner may show)
    int8_t b = p.bannerShown();
    CHECK_MSG(b == BANNER_NONE || (b >= 0 && b < MED_COUNT && p.hasMedal((uint16_t)(1u << b))),
              ending + ": a banner of the old pet's on the new one (" + std::to_string(b) + ")");
  }
}

// An ended banner leaves the queue on the loop's next pass (Pet::update), so it cannot come back
// once millis() is 2^31 ms (~24.8 days of uptime) past its end, as the old deadline did: a
// medal's banner, then no new medal for 25 days on the charger.
TEST(banners, an_ended_banner_stays_gone_after_25_days_of_uptime) {
  Pet p;
  makePet(p, 4);
  p.medals |= MED_STA;  // the Laurel already earned (#116): the speed ribbon is the only banner
  p.trDef = RIBBON_STA;
  p.trSpe = RIBBON_SPD;
  advance(p, 1);
  CHECK(p.showMedal());
  mockAdvanceMillis(MEDAL_BANNER_MS);
  p.update(millis());  // the loop's next pass
  CHECK(!p.showMedal());
  mockAdvanceMillis(25u * 86400 * 1000);  // more than 2^31 ms
  CHECK_MSG(p.bannerShown() == BANNER_NONE, "the old banner came back");
}

// The thresholds are within a normal pet's life (pet.h): two punch sessions, about two good
// hours a day of steady care for two or three days, about ten Play games at par.
// A player who looks in once an hour while the pet is awake: feeds it to 80, cleans it,
// pets it to 80 joy and, when it's tired (ENE under 45), puts it to bed for 10 minutes. Energy
// only comes back asleep, so those naps are what keep every need over 40 (pet.h, RIBBON_STA).
static void hourlyCare(Pet &p) {
  uint8_t plain = 0;
  while (p.lovesBerry(plain)) plain++;  // an ordinary berry: no favourite's hearts or medal
  while (p.fullness < 80) p.feedBerry(plain);
  p.clean();
  while (p.joy < 80) p.caress();
  if (p.energy < 45) {
    p.toggleLight();  // a nap
    advance(p, 10);
    p.toggleLight();
  }
}

TEST(ribbons, the_thresholds_are_reachable) {
  CHECK(RIBBON_STR <= exerciseGain(22));  // one 22-punch run (Pet::trainStrength, #78, #126)
  CHECK(RIBBON_SPD <= 10 * 3);  // a game at par
  CHECK(RIBBON_SPD <= 4 * gameSpeedPay(GAME_KEEPY, 30));  // or four runs of 30 keepy-uppy hits (#127)
  Pet p;
  makePet(p, 4);
  for (int i = 0; i < 10; i++) p.playGame(GAME_KEEPY, GAME_PAR[GAME_KEEPY]);
  p.trainStrength(22);
  p.exerciseOver();  // as the last game's screen closes
  CHECK(p.hasMedal(MED_SPD));
  CHECK(p.hasMedal(MED_STR));
  // stamina: three days of hourly care on the normal pace, 16 h awake and 8 h asleep
  for (int day = 0; day < 3 && !p.hasMedal(MED_STA); day++) {
    for (int h = 0; h < 16; h++) {
      hourlyCare(p);
      advance(p, 60);
    }
    p.toggleLight();  // the night
    advance(p, 8 * 60);
    p.toggleLight();
  }
  CHECK_MSG(p.hasMedal(MED_STA), "stamina " + std::to_string(p.trDef) + " after three days of hourly care with naps");
}

// A save from before #76 has the ribbons' bits clear: a pet already past a goal earns its
// ribbon at the next medal check (the minute tick), once.
TEST(ribbons, an_older_pet_past_a_goal_earns_its_ribbon_at_the_next_tick) {
  Pet a;
  makePet(a, 4);
  a.trAtk = 60;
  a.trSpe = 45;
  a.saveNow();
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putUShort("medal", MED_LV10);  // what an older firmware saved: no ribbon bits
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_EQ(b.medals, (uint16_t)MED_LV10);
  uint16_t total = b.totalMedals;
  mockAdvanceMillis(PET_TICK_MS);
  b.update(millis());
  CHECK(b.hasMedal(MED_STR));
  CHECK(b.hasMedal(MED_SPD));
  CHECK(!b.hasMedal(MED_STA));
  CHECK_EQ(b.totalMedals, (uint16_t)(total + 2));
  mockAdvanceMillis(PET_TICK_MS);
  b.update(millis());
  CHECK_EQ(b.totalMedals, (uint16_t)(total + 2));
}

TEST(ribbons, a_new_pet_starts_without_ribbons) {
  Pet p;
  makePet(p, 4);
  p.trAtk = p.trDef = p.trSpe = 100;
  advance(p, 1);
  CHECK(p.hasMedal(MED_STR) && p.hasMedal(MED_STA) && p.hasMedal(MED_SPD));
  p.newEgg();
  p.eggTap(); p.eggTap(); p.eggTap();
  CHECK_EQ(p.trAtk, (uint8_t)0);
  CHECK(!p.hasMedal(MED_STR) && !p.hasMedal(MED_STA) && !p.hasMedal(MED_SPD));
}

// ---------------------------------------------------------------- ceremonias
TEST(ceremony, despedida_solo_en_forma_final_y_a_los_3_dias) {
  Pet p;
  makePet(p, 6);  // CHARIZARD, forma final
  p.ageMinutes = FAREWELL_AGE_MIN - 1;
  CHECK(!p.canFarewellNow());
  p.ageMinutes = FAREWELL_AGE_MIN;
  CHECK(p.canFarewellNow());

  Pet q;
  makePet(q, 4);  // CHARMANDER no es forma final
  q.ageMinutes = FAREWELL_AGE_MIN * 3;
  CHECK(!q.canFarewellNow());
}

TEST(ceremony, la_despedida_deja_un_huevo_nuevo) {
  Pet p;
  makePet(p, 6);
  p.ageMinutes = FAREWELL_AGE_MIN;
  p.startFarewell();
  CHECK_EQ(p.ceremony, (uint8_t)CER_FAREWELL);
  CHECK_RANGE(p.ceremonyT(), 0.0f, 1.0f);
  mockAdvanceMillis(CEREMONY_MS + 1);
  p.update(millis());
  CHECK(p.isEgg());
  CHECK_EQ(p.ceremony, (uint8_t)CER_NONE);
  CHECK_EQ(p.lastEnd, (uint8_t)CER_FAREWELL);
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
}

TEST(ceremony, escaparse_pide_una_hora_de_abandono_total) {
  Pet p;
  makePet(p, 4);
  setStats(p, 0, 0, 0, 0);
  CHECK(!p.canRunawayNow());
  advance(p, RUNAWAY_TICKS - 1);
  CHECK(!p.canRunawayNow());
  advance(p, 1);
  CHECK(p.canRunawayNow());
}

TEST(ceremony, un_solo_cuidado_salva_del_abandono) {
  Pet p;
  makePet(p, 4);
  p.dbgRunawayReady();
  CHECK(p.canRunawayNow());
  p.feedBerry(0);
  advance(p, 1);
  CHECK_MSG(!p.canRunawayNow(), "comer resetea el contador de abandono");
}

// The run-away button needs the four needs still at 0, not only the hour's count: tick()
// resets the count on its next minute, so the button and its warning used to stay for up
// to a minute after a care, or after a night's sleep had brought the energy back
TEST(ceremony, a_care_takes_the_run_away_button_away_at_once) {
  Pet p;
  makePet(p, 4);
  p.dbgRunawayReady();
  CHECK(p.canRunawayNow());
  p.feedBerry(0);
  CHECK_MSG(!p.canRunawayNow(), "fed: the button goes at once, before the next minute");
}

TEST(ceremony, a_pet_woken_rested_shows_no_run_away_button) {
  Pet q;
  makePet(q, 4);
  q.dbgRunawayReady();
  CHECK(q.autoSleep());
  advance(q, 9 * 60);  // the night: the energy comes back, the count stays
  CHECK(q.energy > 0);
  CHECK(q.autoWake());
  CHECK_MSG(!q.canRunawayNow(), "woken rested: no run-away button and no warning at the morning wake");
}

TEST(ceremony, durante_la_ceremonia_no_se_puede_interactuar) {
  Pet p;
  makePet(p, 4);
  setStats(p, 50, 50, 50, 50);
  p.poops = 2;
  p.startRunaway();
  p.feedBerry(0);
  p.feedCandy();
  p.playGame(GAME_KEEPY, 10);
  p.clean();
  p.caress();
  p.toggleLight();
  CHECK_EQ(p.fullness, (uint8_t)50);
  CHECK_EQ(p.joy, (uint8_t)50);
  CHECK_EQ(p.gameHi, (uint16_t)0);  // no game, so no record either
  CHECK_EQ(p.poops, (uint8_t)2);
  CHECK(!p.sleeping);
}

TEST(ceremony, soltar_marca_el_final_como_liberacion) {
  Pet p;
  makePet(p, 4);
  p.release();
  CHECK_EQ(p.ceremony, (uint8_t)CER_RELEASE);
  CHECK_EQ(p.lastEnd, (uint8_t)CER_RELEASE);
  mockAdvanceMillis(CEREMONY_MS + 1);
  p.update(millis());
  CHECK(p.isEgg());
}

TEST(ceremony, un_huevo_no_se_despide_ni_se_suelta) {
  mockNvsReset();
  Pet p;
  p.begin();
  p.startFarewell();
  p.release();
  p.startRunaway();
  CHECK_EQ(p.ceremony, (uint8_t)CER_NONE);
}

// ---------------------------------------------------------------- offline
TEST(offline, aplica_el_tiempo_apagado_con_suelo_de_15) {
  Pet p;
  makePet(p, 4);
  setStats(p, 80, 80, 80, 100);
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 600 * 60);  // 600 minutos apagado
  CHECK_EQ(p.ageMinutes, (uint32_t)600);
  CHECK_MSG(p.fullness == 15, "suelo de comida offline = 15");
  CHECK_MSG(p.energy == 15, "suelo de energia offline = 15");
  CHECK_MSG(p.joy == 15, "suelo de felicidad offline = 15");
  CHECK_MSG(p.hygiene == 15, "suelo de higiene offline = 15");
  CHECK_MSG(p.careMistakes == 0, "en ausencia no se acumulan descuidos");
  CHECK_EQ(p.poops, (uint8_t)2);  // una caca cada 4 h
}

TEST(offline, tope_de_dos_semanas) {
  Pet p;
  makePet(p, 4);
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 90u * 86400);  // 90 dias
  CHECK_EQ(p.ageMinutes, (uint32_t)(14u * 24 * 60));
}

TEST(offline, menos_de_dos_minutos_no_hace_nada) {
  Pet p;
  makePet(p, 4);
  setStats(p, 80, 80, 80, 100);
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 90);
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
  CHECK_EQ(p.fullness, (uint8_t)80);
}

TEST(offline, el_huevo_eclosiona_en_tu_ausencia) {
  Pet p;
  makePet(p, 4);
  p.newEgg();  // ya hay pokedex: no pide inicial
  CHECK(!p.awaitingStarter());
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 600);  // 10 minutos
  CHECK(!p.isEgg());
  CHECK_RANGE(p.speciesId, (int16_t)1, (int16_t)151);
}

TEST(offline, durmiendo_se_descansa_tambien_apagado) {
  Pet p;
  makePet(p, 4);
  setStats(p, 90, 90, 20, 90);
  p.toggleLight();
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 120 * 60);
  CHECK_EQ(p.energy, (uint8_t)100);
  CHECK_MSG(p.fullness >= 30, "suelos de sueno tambien offline");
  CHECK_MSG(p.poops == 0, "durmiendo no ensucia");
}

TEST(offline, sin_reloj_no_se_aplica_nada) {
  Pet p;
  makePet(p, 4);
  setStats(p, 80, 80, 80, 100);
  p.syncClock(0);
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
  CHECK_EQ(p.fullness, (uint8_t)80);
}

// ---------------------------------------------------------------- naturalezas (#25)
// A fresh pet of that nature, with the needs where a few minutes' drain is easy to count:
// food 40 (the poop roll needs more than 40, so no random number is drawn, and food stays at
// 30 or more for 4 minutes, so joy gets no penalty), the rest at 90, no poops, no weight.
static void naturePet(Pet &p, uint8_t nat, uint8_t pace = PACE_NORMAL) {
  makePet(p, 4);
  p.nature = nat;
  p.setPace(pace);
  setStats(p, 40, 90, 90, 90);
  p.poops = 0;
  p.weight = 0;
}

static void checkNeeds(const Pet &p, uint8_t f, uint8_t j, uint8_t e, uint8_t h, const char *what) {
  char msg[160];
  snprintf(msg, sizeof msg, "%s: food %u joy %u energy %u hygiene %u, expected %u %u %u %u", what,
           p.fullness, p.joy, p.energy, p.hygiene, f, j, e, h);
  CHECK_MSG(p.fullness == f && p.joy == j && p.energy == e && p.hygiene == h, msg);
}

TEST(nature, hatching_rolls_each_of_the_five) {
  Pet p;
  makePet(p, 4);
  int seen[NAT_COUNT] = { 0 };
  for (int i = 0; i < 250; i++) {
    p.newEgg();
    p.eggTap();
    p.eggTap();
    p.eggTap();
    CHECK(!p.isEgg());
    CHECK(p.nature < NAT_COUNT);
    if (p.nature < NAT_COUNT) seen[p.nature]++;
  }
  for (int n = 0; n < NAT_COUNT; n++) CHECK_RANGE(seen[n], 25, 75);  // 50 each on average
}

TEST(nature, easygoing_drains_as_before) {
  Pet p;
  naturePet(p, NAT_EASYGOING);
  advance(p, 4);
  checkNeeds(p, 32, 86, 86, 86, "easygoing, 4 min");
  naturePet(p, NAT_EASYGOING, PACE_RELAXED);
  advance(p, 8);
  checkNeeds(p, 32, 86, 86, 86, "easygoing relaxed, 8 min");
}

// one extra point off its own need on every fourth minute, the other needs as easygoing
TEST(nature, greedy_food_drains_a_point_more_every_fourth_minute) {
  Pet p;
  naturePet(p, NAT_GREEDY);
  advance(p, 3);
  checkNeeds(p, 34, 87, 87, 87, "greedy, 3 min");
  advance(p, 1);
  checkNeeds(p, 31, 86, 86, 86, "greedy, 4 min");
}

TEST(nature, playful_joy_drains_a_point_more_every_fourth_minute) {
  Pet p;
  naturePet(p, NAT_PLAYFUL);
  advance(p, 3);
  checkNeeds(p, 34, 87, 87, 87, "playful, 3 min");
  advance(p, 1);
  checkNeeds(p, 32, 85, 86, 86, "playful, 4 min");
}

TEST(nature, tidy_hygiene_drains_a_point_more_every_fourth_minute) {
  Pet p;
  naturePet(p, NAT_TIDY);
  advance(p, 3);
  checkNeeds(p, 34, 87, 87, 87, "tidy, 3 min");
  advance(p, 1);
  checkNeeds(p, 32, 86, 86, 85, "tidy, 4 min");
}

TEST(nature, lazy_energy_drains_a_point_more_every_fourth_minute) {
  Pet p;
  naturePet(p, NAT_LAZY);
  advance(p, 3);
  checkNeeds(p, 34, 87, 87, 87, "lazy, 3 min");
  advance(p, 1);
  checkNeeds(p, 32, 86, 85, 86, "lazy, 4 min");
}

// The greedy pet's extra food point comes after the poop roll, so the roll sees the food it
// always saw (more than 40). From 49: 47, 45, 43, then minute 4 rolls at 41 and only then
// takes the extra point (41 - 1 = 40).
TEST(nature, greedy_extra_point_comes_after_the_poop_roll) {
  Pet p;
  naturePet(p, NAT_GREEDY);
  setStats(p, 49, 90, 90, 90);
  mockForceRandom(99);  // random(100) < 15 fails: no poop
  advance(p, 3);
  CHECK_EQ(p.fullness, (uint8_t)43);
  CHECK_EQ(p.poops, (uint8_t)0);
  mockForceRandom(0);  // the roll passes whenever it runs
  advance(p, 1);
  mockClearForcedRandom();
  CHECK_EQ(p.poops, (uint8_t)1);
  CHECK_EQ(p.fullness, (uint8_t)40);
}

// The relaxed pace drains on even minutes only: the extra point comes on every fourth of
// those (minute 8), the same share of the drain as at the normal pace.
TEST(nature, relaxed_pace_the_extra_point_comes_every_eighth_minute) {
  Pet p;
  naturePet(p, NAT_GREEDY, PACE_RELAXED);
  advance(p, 7);
  checkNeeds(p, 34, 87, 87, 87, "greedy relaxed, 7 min");
  advance(p, 1);
  checkNeeds(p, 31, 86, 86, 86, "greedy relaxed, 8 min");
  naturePet(p, NAT_PLAYFUL, PACE_RELAXED);
  advance(p, 8);
  checkNeeds(p, 32, 85, 86, 86, "playful relaxed, 8 min");
  naturePet(p, NAT_TIDY, PACE_RELAXED);
  advance(p, 8);
  checkNeeds(p, 32, 86, 86, 85, "tidy relaxed, 8 min");
  naturePet(p, NAT_LAZY, PACE_RELAXED);
  advance(p, 8);
  checkNeeds(p, 32, 86, 85, 86, "lazy relaxed, 8 min");
}

// Powered off (syncClock), the same extra point every fourth minute, down to the same floor
// of 15: a long power-off ends exactly where it does for an easygoing pet.
TEST(nature, powered_off_the_extra_point_keeps_the_floor_of_15) {
  static const uint8_t WANT[NAT_COUNT][4] = {
    { 40, 60, 60, 80 },  // easygoing: 20 min at food -2, the rest -1
    { 35, 60, 60, 80 },  // greedy: 5 points more food
    { 40, 55, 60, 80 },  // playful: joy
    { 40, 60, 60, 75 },  // tidy: hygiene
    { 40, 60, 55, 80 },  // lazy: energy
  };
  for (uint8_t n = 0; n < NAT_COUNT; n++) {
    Pet p;
    makePet(p, 4);
    p.nature = n;
    setStats(p, 80, 80, 80, 100);
    p.setClock(1000u * 86400);
    p.syncClock(1000u * 86400 + 20 * 60);
    checkNeeds(p, WANT[n][0], WANT[n][1], WANT[n][2], WANT[n][3], NATURE_KEYS[n]);
    p.syncClock(1000u * 86400 + 20 * 60 + 600 * 60);  // 10 h more
    checkNeeds(p, 15, 15, 15, 15, NATURE_KEYS[n]);
    CHECK_EQ(p.careMistakes, (uint8_t)0);
  }
}

// asleep: no extra drain for any nature, and a lazy pet's energy comes back 8 a minute
TEST(nature, asleep_lazy_restores_8_energy_a_minute) {
  for (uint8_t n = 0; n < NAT_COUNT; n++) {
    Pet p;
    makePet(p, 4);
    p.nature = n;
    setStats(p, 90, 90, 20, 90);
    p.toggleLight();
    advance(p, 5);
    CHECK_EQ(p.energy, (uint8_t)(n == NAT_LAZY ? 60 : 50));
    checkNeeds(p, 88, 88, p.energy, 89, NATURE_KEYS[n]);
  }
}

TEST(nature, asleep_and_powered_off_lazy_restores_8_energy_a_minute) {
  for (uint8_t n = 0; n < NAT_COUNT; n++) {
    Pet p;
    makePet(p, 4);
    p.nature = n;
    setStats(p, 90, 90, 20, 90);
    p.toggleLight();
    p.setClock(1000u * 86400);
    p.syncClock(1000u * 86400 + 5 * 60);
    CHECK_EQ(p.energy, (uint8_t)(n == NAT_LAZY ? 60 : 50));
    checkNeeds(p, 88, 88, p.energy, 89, NATURE_KEYS[n]);
  }
}

// species 4 (Charmander) loves the blue berry (speciesId % 3 == 1)
TEST(nature, greedy_berries_fill_5_more) {
  for (uint8_t n = 0; n < NAT_COUNT; n++) {
    uint8_t extra = n == NAT_GREEDY ? 5 : 0;
    Pet p;
    makePet(p, 4);
    p.nature = n;
    setStats(p, 40, 50, 50, 50);
    p.feedBerry(0);  // red: not its favourite
    CHECK_EQ(p.fullness, (uint8_t)(40 + 25 + extra));
    CHECK_EQ(p.joy, (uint8_t)50);
    setStats(p, 40, 50, 50, 50);
    p.feedBerry(1);  // blue: its favourite
    CHECK_EQ(p.fullness, (uint8_t)(40 + 35 + extra));
    CHECK_EQ(p.joy, (uint8_t)60);
  }
}

TEST(nature, playful_minigames_cheer_5_more) {
  for (uint8_t n = 0; n < NAT_COUNT; n++) {
    uint8_t extra = n == NAT_PLAYFUL ? 5 : 0;
    Pet p;
    makePet(p, 4);
    p.nature = n;
    setStats(p, 60, 40, 80, 60);
    p.playGame(GAME_KEEPY, 10);  // a touched game: joy + 35 (#127)
    CHECK_EQ(p.joy, (uint8_t)(40 + 35 + extra));
  }
}

TEST(nature, tidy_bath_lifts_joy_by_10) {
  for (uint8_t n = 0; n < NAT_COUNT; n++) {
    Pet p;
    makePet(p, 4);
    p.nature = n;
    setStats(p, 60, 40, 80, 20);
    p.poops = 2;
    p.clean();
    CHECK_EQ(p.hygiene, (uint8_t)100);
    CHECK_EQ(p.poops, (uint8_t)0);
    CHECK_EQ(p.joy, (uint8_t)(n == NAT_TIDY ? 50 : 40));
  }
  Pet p;  // an egg has no nature yet: the last pet's does not lift its joy
  makePet(p, 4);
  p.nature = NAT_TIDY;
  p.newEgg();
  p.joy = 40;
  p.clean();
  CHECK_EQ(p.joy, (uint8_t)40);
}

// ---- the training bonus (#25 "Stat growth")
TEST(nature, the_training_bonus_is_25_percent_rounded_up) {
  static const uint8_t BASE[] = { 0, 1, 2, 3, 4, 5, 6, 8, 18, 100, 204, 205, 255 };
  static const uint8_t WITH[] = { 0, 2, 3, 4, 5, 7, 8, 10, 23, 125, 255, 255, 255 };
  for (size_t i = 0; i < sizeof BASE; i++) {
    CHECK_EQ(natureTrainGain(NAT_GREEDY, TRAIN_STRENGTH, BASE[i]), WITH[i]);
    CHECK_EQ(natureTrainGain(NAT_TIDY, TRAIN_STAMINA, BASE[i]), WITH[i]);
    CHECK_EQ(natureTrainGain(NAT_PLAYFUL, TRAIN_SPEED, BASE[i]), WITH[i]);
    // a bonus only: the other stats, the other natures and garbage get the gain as it is
    for (uint8_t stat = 0; stat <= TRAIN_NONE; stat++) {
      CHECK_EQ(natureTrainGain(NAT_EASYGOING, stat, BASE[i]), BASE[i]);
      CHECK_EQ(natureTrainGain(NAT_LAZY, stat, BASE[i]), BASE[i]);
      CHECK_EQ(natureTrainGain(NAT_COUNT, stat, BASE[i]), BASE[i]);
      CHECK_EQ(natureTrainGain(200, stat, BASE[i]), BASE[i]);
    }
    CHECK_EQ(natureTrainGain(NAT_GREEDY, TRAIN_SPEED, BASE[i]), BASE[i]);
    CHECK_EQ(natureTrainGain(NAT_GREEDY, TRAIN_STAMINA, BASE[i]), BASE[i]);
    CHECK_EQ(natureTrainGain(NAT_TIDY, TRAIN_STRENGTH, BASE[i]), BASE[i]);
    CHECK_EQ(natureTrainGain(NAT_PLAYFUL, TRAIN_NONE, BASE[i]), BASE[i]);
  }
}

// strength: the rhythm punch (Pet::trainStrength, #78, #126): 1 punch pays 1, 22 pay 30
TEST(nature, greedy_trains_strength_25_percent_more) {
  for (uint8_t n = 0; n < NAT_COUNT; n++) {
    bool bonus = n == NAT_GREEDY;
    Pet p;
    makePet(p, 4);
    p.nature = n;
    CHECK_EQ(p.trainStrength(1), (uint8_t)(bonus ? 2 : 1));  // a small gain still counts
    CHECK_EQ(p.trainStrength(22), (uint8_t)(bonus ? 38 : 30));
    CHECK_EQ(p.trAtk, (uint8_t)(bonus ? 40 : 31));
    p.trAtk = 90;  // the cap still holds, and the punch says what really went in
    CHECK_EQ(p.trainStrength(22), (uint8_t)10);
    CHECK_EQ(p.trAtk, (uint8_t)100);
    CHECK_EQ(p.trSpe, (uint8_t)0);
    CHECK_EQ(p.trDef, (uint8_t)0);
  }
}

// stamina's active route: the push-ups (Pet::trainStamina, #78, #126), paid like the punch;
// tidy trains it 25% more, as it does the passive one (Pet::tick)
TEST(nature, tidy_trains_stamina_in_the_push_ups_25_percent_more) {
  for (uint8_t n = 0; n < NAT_COUNT; n++) {
    bool bonus = n == NAT_TIDY;
    Pet p;
    makePet(p, 4);
    p.nature = n;
    CHECK_EQ(p.trainStamina(1), (uint8_t)(bonus ? 2 : 1));
    CHECK_EQ(p.trainStamina(22), (uint8_t)(bonus ? 38 : 30));
    CHECK_EQ(p.trDef, (uint8_t)(bonus ? 40 : 31));
    p.trDef = 90;
    CHECK_EQ(p.trainStamina(22), (uint8_t)10);
    CHECK_EQ(p.trDef, (uint8_t)100);
    CHECK_EQ(p.trAtk, (uint8_t)0);
    CHECK_EQ(p.trSpe, (uint8_t)0);
  }
}

// speed: the Play minigames (Pet::playReward), by gameSpeedPay (#127)
TEST(nature, playful_trains_speed_25_percent_more) {
  for (uint8_t n = 0; n < NAT_COUNT; n++) {
    bool bonus = n == NAT_PLAYFUL;
    Pet p;
    makePet(p, 4);
    p.nature = n;
    // playGame() returns the speed gained (#76): the boosted gain, what really went in
    CHECK_EQ(p.playGame(GAME_KEEPY, 5), (uint8_t)(bonus ? 2 : 1));  // 5 hits: 1 point
    CHECK_EQ(p.trSpe, (uint8_t)(bonus ? 2 : 1));
    CHECK_EQ(p.playGame(GAME_KEEPY, 30), (uint8_t)(bonus ? 12 : 9));  // 30 hits: 9 points, 25% more rounded up
    CHECK_EQ(p.trSpe, (uint8_t)(bonus ? 14 : 10));
    p.trSpe = 97;
    CHECK_EQ(p.playGame(GAME_KEEPY, 30), (uint8_t)3);
    CHECK_EQ(p.trSpe, (uint8_t)100);
    CHECK_EQ(p.trAtk, (uint8_t)0);
    CHECK_EQ(p.trDef, (uint8_t)0);
  }
}

// A damaged save can hold trained stats past 100 (the other 0-100 values are clamped too);
// load() caps each, and a playful pet at the cap stays there after a top-score game.
TEST(nature, trained_stats_out_of_range_load_capped_at_100) {
  Pet a;
  makePet(a, 4);
  a.saveNow();
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putUChar("tatk", 250);
    raw.putUChar("tdef", 101);
    raw.putUChar("tspe", 250);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_EQ(b.trAtk, (uint8_t)100);
  CHECK_EQ(b.trDef, (uint8_t)100);
  CHECK_EQ(b.trSpe, (uint8_t)100);
  b.nature = NAT_PLAYFUL;
  b.playGame(GAME_KEEPY, 30);  // 9 points, 12 for a playful pet: 112 must not wrap
  CHECK_EQ(b.trSpe, (uint8_t)100);
  // defence in depth: playReward does not rely on load() for the 8-bit sum (a value that
  // load() caps can't reach this; it is set directly)
  b.trSpe = 250;
  b.playGame(GAME_KEEPY, 30);
  CHECK_EQ(b.trSpe, (uint8_t)100);
}

// stamina by itself (Pet::tick, #76): a point per STAMINA_CARE_MIN (30) awake minutes with
// every need at 40 or more, 24 if tidy (GOOD_STEP 4 a minute, 5 for a tidy pet)
TEST(nature, tidy_trains_stamina_by_itself_25_percent_faster) {
  CHECK_EQ(STAMINA_CARE_MIN, 30);  // the minutes below assume it
  for (uint8_t n = 0; n < NAT_COUNT; n++) {
    uint32_t mins = n == NAT_TIDY ? 24 : 30;
    Pet p;
    makePet(p, 4);
    p.nature = n;
    for (uint32_t m = 0; m < mins - 1; m++) {
      setStats(p, 100, 100, 100, 100);
      advance(p, 1);
    }
    CHECK_EQ(p.trDef, (uint8_t)0);
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
    CHECK_EQ(p.trDef, (uint8_t)1);
    CHECK_EQ(p.trAtk, (uint8_t)0);
    CHECK_EQ(p.trSpe, (uint8_t)0);
  }
}

// ---- the save
TEST(nature, survives_a_restart) {
  for (uint8_t n = 0; n < NAT_COUNT; n++) {
    Pet a;
    makePet(a, 4);
    a.nature = n;
    a.saveNow();
    Pet b;
    b.begin();
    CHECK_EQ(b.nature, n);
  }
}

TEST(nature, a_pet_saved_before_natures_loads_as_easygoing) {
  Pet a;
  makePet(a, 7);
  a.nature = NAT_LAZY;
  a.trAtk = 12;
  a.saveNow();
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.remove("nature");  // as a save from before #25
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_EQ(b.nature, (uint8_t)NAT_EASYGOING);
  CHECK_EQ(b.speciesId, (int16_t)7);  // the rest of the pet as it was
  CHECK_EQ(b.trAtk, (uint8_t)12);
}

TEST(nature, a_nature_out_of_range_loads_as_easygoing) {
  static const uint8_t BAD[] = { NAT_COUNT, 200, 255 };
  for (uint8_t v : BAD) {
    Pet a;
    makePet(a, 4);
    a.saveNow();
    {
      Preferences raw;
      raw.begin("tamapoke", false);
      raw.putUChar("nature", v);
      raw.end();
    }
    Pet b;
    b.begin();
    CHECK_EQ(b.nature, (uint8_t)NAT_EASYGOING);
    CHECK_EQ(b.speciesId, (int16_t)4);
  }
  Pet a;  // the last valid one is kept
  makePet(a, 4);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putUChar("nature", NAT_LAZY);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_EQ(b.nature, (uint8_t)NAT_LAZY);
}

// ---------------------------------------------------------------- guardado
TEST(save, el_estado_sobrevive_a_un_reinicio) {
  Pet a;
  makePet(a, 25);  // PIKACHU
  a.ageMinutes = 1234;
  a.streak = 9;
  a.bestStreak = 11;
  a.bond = 44;
  a.trAtk = 17;
  a.weight = 33;
  a.gameHi = 21;
  a.rename("SPARKY");  // rename persiste todo el estado

  Pet b;
  b.begin();  // mismo "NVS": simula un reinicio
  CHECK_EQ(b.speciesId, (int16_t)25);
  CHECK_EQ(b.ageMinutes, (uint32_t)1234);
  CHECK_EQ(b.streak, (uint16_t)9);
  CHECK_EQ(b.bestStreak, (uint16_t)11);
  CHECK_EQ(b.bond, (uint8_t)44);
  CHECK_EQ(b.trAtk, (uint8_t)17);
  CHECK_EQ(b.weight, (uint8_t)33);
  CHECK_EQ(b.gameHi, (uint16_t)21);
  CHECK_STREQ(b.nick, "SPARKY");
  CHECK(b.isRegistered(25));
  CHECK(!b.awaitingStarter());
}

TEST(save, el_apodo_se_recorta_a_11_caracteres) {
  Pet p;
  makePet(p, 4);
  p.rename("NOMBRE-LARGUISIMO-QUE-NO-CABE");
  CHECK_EQ((int)strlen(p.nick), 11);
  CHECK_STREQ(p.nick, "NOMBRE-LARG");
}

TEST(save, borrado_de_fabrica_deja_todo_como_nuevo) {
  Pet a;
  makePet(a, 4);
  a.factoryReset();
  Pet b;
  b.begin();
  CHECK(b.isEgg());
  CHECK(b.awaitingStarter());
  CHECK_EQ(b.registeredCount(), (uint16_t)0);
}

TEST(save, el_autoguardado_se_marca_y_se_vuelca) {
  Pet p;
  makePet(p, 4);
  CHECK(!p.savePending());
  advance(p, 5);
  CHECK_MSG(p.savePending(), "cada 5 ticks queda un guardado pendiente");
  p.flushSave();
  CHECK(!p.savePending());
}

// Save data from a different build (e.g. a fork with more than 151 species)
// or a damaged NVS page must not index past DEX_TBL on the next boot.
TEST(save, out_of_range_save_data_is_sanitised_on_load) {
  Pet a;
  makePet(a, 4);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putShort("dexn", 300);   // not a species in this firmware
    raw.putShort("eggT2", 400);
    raw.putUChar("poop", 200);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_MSG(b.isEgg(), "an unknown species falls back to a fresh egg");
  CHECK(b.poops <= 3);
  CHECK(b.fullness <= 100 && b.joy <= 100 && b.energy <= 100 && b.hygiene <= 100);
  CHECK_EQ(b.registeredCount(), (uint16_t)1);  // the earlier Charmander is kept
  b.eggTap();
  b.eggTap();
  b.eggTap();
  CHECK(b.speciesId >= 1 && b.speciesId <= DEX_COUNT);
}

// The stamina count is saved (#76, NVS "tdefm"): a restart keeps the good minutes so far.
TEST(save, the_stamina_count_survives_a_restart) {
  Pet a;
  makePet(a, 4);
  for (int m = 0; m < STAMINA_CARE_MIN - 1; m++) {
    setStats(a, 100, 100, 100, 100);
    advance(a, 1);
  }
  a.saveNow();
  Pet b;
  b.begin();
  CHECK_EQ(b.trDef, (uint8_t)0);
  setStats(b, 100, 100, 100, 100);
  mockAdvanceMillis(PET_TICK_MS);
  b.update(millis());
  CHECK_MSG(b.trDef == 1, "one more good minute after the restart completes the point");
}

// A new pet starts its own count (#76): the last pet's good minutes don't carry over.
TEST(save, a_new_pet_starts_its_own_stamina_count) {
  Pet p;
  makePet(p, 4);
  for (int m = 0; m < STAMINA_CARE_MIN - 1; m++) {
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  p.newEgg();
  p.eggTap(); p.eggTap(); p.eggTap();
  CHECK(!p.isEgg());
  setStats(p, 100, 100, 100, 100);
  advance(p, 1);
  CHECK_EQ(p.trDef, (uint8_t)0);
}

// #76: the training points and the weight are 0-100, the stamina count under
// STAMINA_CARE_MIN, and the medal word holds only the medals this firmware defines.
TEST(save, out_of_range_training_weight_and_medals_are_clamped) {
  Pet a;
  makePet(a, 4);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putUChar("tatk", 200);
    raw.putUChar("tdef", 101);
    raw.putUChar("tspe", 255);
    raw.putUChar("wgt", 180);
    raw.putUShort("tdefm", STAMINA_CARE_MIN * GOOD_STEP);  // one past the last valid count
    raw.putUShort("medal", 0xFFFF);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_EQ(b.speciesId, (int16_t)4);
  CHECK_EQ(b.trAtk, (uint8_t)100);
  CHECK_EQ(b.trDef, (uint8_t)100);
  CHECK_EQ(b.trSpe, (uint8_t)100);
  CHECK_EQ(b.weight, (uint8_t)100);
  CHECK_EQ(b.medals, (uint16_t)MED_ALL);
  // the count loaded as 0: a point needs a whole STAMINA_CARE_MIN again
  b.trDef = 50;
  for (int m = 0; m < STAMINA_CARE_MIN - 1; m++) {
    setStats(b, 100, 100, 100, 100);
    mockAdvanceMillis(PET_TICK_MS);
    b.update(millis());
  }
  CHECK_EQ(b.trDef, (uint8_t)50);
}

TEST(save, out_of_range_stats_and_egg_target_are_clamped) {
  Pet a;
  makePet(a, 4);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putUChar("full", 250);
    raw.putUChar("hyg", 180);
    raw.putUChar("lend", 99);
    raw.putShort("eggT2", -5);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_EQ(b.speciesId, (int16_t)4);  // a valid pet is left alone
  CHECK_EQ(b.fullness, (uint8_t)100);
  CHECK_EQ(b.hygiene, (uint8_t)100);
  CHECK_EQ(b.lastEnd, (uint8_t)CER_NONE);
}

// ---------------------------------------------------------------- ritmo relajado
// Despierto, las necesidades bajan solo en los minutos pares (la mitad de rapido).
// Todo lo demas del tick sigue siendo por minuto.
TEST(pace, relajado_baja_la_mitad_que_el_normal) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  setStats(p, 100, 100, 100, 100);
  mockForceRandom(99);  // sin cacas
  advance(p, 10);
  mockClearForcedRandom();
  CHECK_EQ(p.fullness, (uint8_t)90);  // normal: 80
  CHECK_EQ(p.energy, (uint8_t)95);    // normal: 90
  CHECK_EQ(p.hygiene, (uint8_t)95);   // normal: 90
  CHECK_EQ(p.joy, (uint8_t)95);       // normal: 90
  CHECK_EQ(p.ageMinutes, (uint32_t)10);
}

// cacas (tirada y castigo de higiene), sobrepeso y extras de felicidad, a media velocidad
TEST(pace, relajado_reduce_cacas_sobrepeso_y_extras_de_felicidad) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  setStats(p, 100, 100, 100, 100);
  p.weight = 60;
  mockForceRandom(0);  // siempre caca si puede
  advance(p, 10);      // 5 caidas: cacas 1,2,3,3,3
  mockClearForcedRandom();
  CHECK_EQ(p.poops, (uint8_t)3);
  CHECK_EQ(p.fullness, (uint8_t)90);
  CHECK_EQ(p.energy, (uint8_t)90);   // -2 por caida (sobrepeso)
  CHECK_EQ(p.hygiene, (uint8_t)47);  // 100 -5 -(4+8+12+12+12)
  CHECK_EQ(p.joy, (uint8_t)95);
}

TEST(pace, relajado_baja_la_felicidad_extra_con_poca_comida_o_higiene) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  setStats(p, 20, 100, 100, 20);
  mockForceRandom(99);
  advance(p, 4);  // 2 caidas, cada una -1 -2 (comida<30) -2 (higiene<30)
  mockClearForcedRandom();
  CHECK_EQ(p.joy, (uint8_t)90);
  CHECK_EQ(p.fullness, (uint8_t)16);
  CHECK_EQ(p.hygiene, (uint8_t)18);
}

// edad, nivel, peso y racha de buen cuidado siguen por minuto en el ritmo relajado
TEST(pace, relajado_sigue_subiendo_un_nivel_por_hora) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  CHECK_EQ(p.level(), (uint16_t)1);
  for (int m = 0; m < 120; m++) { setStats(p, 100, 100, 100, 100); advance(p, 1); }
  CHECK_EQ(p.ageMinutes, (uint32_t)120);
  CHECK_EQ(p.level(), (uint16_t)3);
}

TEST(pace, relajado_sigue_quemando_peso_cada_3_minutos) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  p.weight = 30;
  advance(p, 30);
  CHECK_EQ(p.weight, (uint8_t)20);  // igual que en normal
}

TEST(pace, relajado_sigue_forjando_el_folego_al_mismo_ritmo) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  for (int m = 0; m < STAMINA_CARE_MIN; m++) { setStats(p, 100, 100, 100, 100); advance(p, 1); }
  CHECK_EQ(p.trDef, (uint8_t)1);
}

// Igual que el de ritmo normal: en relajado la felicidad solo baja en los minutos
// pares, pero la racha la sigue viendo antes de bajar.
TEST(pace, relajado_la_racha_ve_la_felicidad_antes_de_bajar) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  for (int m = 0; m < STAMINA_CARE_MIN; m++) { setStats(p, 100, 40, 100, 100); advance(p, 1); }
  CHECK_EQ(p.trDef, (uint8_t)1);
}

TEST(pace, relajado_sigue_contando_el_descuido_en_el_primer_minuto) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  setStats(p, 0, 0, 0, 0);
  advance(p, 1);  // minuto impar: no hay caida, pero el descuido se detecta igual
  CHECK_EQ(p.careMistakes, (uint8_t)1);
  advance(p, 50);
  CHECK_EQ(p.careMistakes, (uint8_t)1);
  advance(p, 11);
  CHECK_EQ(p.careMistakes, (uint8_t)2);
}

TEST(pace, relajado_sigue_exigiendo_una_hora_de_abandono_total) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  setStats(p, 0, 0, 0, 0);
  advance(p, RUNAWAY_TICKS - 1);
  CHECK(!p.canRunawayNow());
  advance(p, 1);
  CHECK(p.canRunawayNow());
}

// descuidado de verdad llega a cometer descuidos, solo que tarda el doble
TEST(pace, relajado_tarda_el_doble_en_llegar_al_descuido) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  setStats(p, 100, 100, 100, 100);
  mockForceRandom(99);
  advance(p, 89);
  CHECK_EQ(p.careMistakes, (uint8_t)0);  // comida 12; en normal ya iria por el minuto 45
  advance(p, 1);
  mockClearForcedRandom();
  CHECK_EQ(p.fullness, (uint8_t)10);
  CHECK_EQ(p.careMistakes, (uint8_t)1);
}

TEST(pace, dormido_baja_igual_en_los_dos_ritmos) {
  uint8_t res[2][4];
  for (int pace = 0; pace < 2; pace++) {
    Pet p;
    makePet(p, 4);
    p.setPace(pace);
    setStats(p, 100, 100, 50, 100);
    p.toggleLight();
    advance(p, 60);
    res[pace][0] = p.fullness; res[pace][1] = p.joy; res[pace][2] = p.energy; res[pace][3] = p.hygiene;
  }
  for (int i = 0; i < 4; i++) CHECK_EQ(res[1][i], res[0][i]);
  CHECK_EQ(res[0][0], (uint8_t)70);  // 30 caidas de -1 cada 2 min
  CHECK_EQ(res[0][3], (uint8_t)80);  // 20 caidas de -1 cada 3 min
}

// ---------------------------------------------------------------- ajustes Care (NVS)
TEST(care, por_defecto_normal_y_sin_horario) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.pace, (uint8_t)PACE_NORMAL);
  CHECK_EQ(p.bedPreset, (uint8_t)0);
}

TEST(care, ritmo_y_horario_sobreviven_a_un_reinicio) {
  Pet a;
  makePet(a, 4);
  a.setPace(PACE_RELAXED);
  a.setBedPreset(2);
  Pet b;
  b.begin();
  CHECK_EQ(b.pace, (uint8_t)PACE_RELAXED);
  CHECK_EQ(b.bedPreset, (uint8_t)2);
}

TEST(care, los_setters_rechazan_valores_fuera_de_rango) {
  Pet p;
  makePet(p, 4);
  p.setPace(7);
  p.setBedPreset(9);
  CHECK_EQ(p.pace, (uint8_t)PACE_NORMAL);
  CHECK_EQ(p.bedPreset, (uint8_t)0);
  // el primer valor invalido de cada ajuste (PACE_COUNT / BED_PRESETS), no solo valores lejanos
  p.setPace(PACE_RELAXED);
  p.setBedPreset(3);
  p.setPace(PACE_COUNT);
  p.setBedPreset(BED_PRESETS);
  CHECK_EQ(p.pace, (uint8_t)PACE_NORMAL);
  CHECK_EQ(p.bedPreset, (uint8_t)0);
  // y el ultimo valido se acepta
  p.setPace(PACE_COUNT - 1);
  p.setBedPreset(BED_PRESETS - 1);
  CHECK_EQ(p.pace, (uint8_t)(PACE_COUNT - 1));
  CHECK_EQ(p.bedPreset, (uint8_t)(BED_PRESETS - 1));
}

TEST(care, basura_en_la_nvs_vuelve_a_los_valores_por_defecto) {
  Pet a;
  makePet(a, 4);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putUChar("pace", 200);
    raw.putUChar("bed", 77);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_EQ(b.speciesId, (int16_t)4);  // la mascota no se toca
  CHECK_EQ(b.pace, (uint8_t)PACE_NORMAL);
  CHECK_EQ(b.bedPreset, (uint8_t)0);
  // el primer valor invalido (PACE_COUNT / BED_PRESETS), no solo basura lejana
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putUChar("pace", PACE_COUNT);
    raw.putUChar("bed", BED_PRESETS);
    raw.end();
  }
  Pet c;
  c.begin();
  CHECK_EQ(c.pace, (uint8_t)PACE_NORMAL);
  CHECK_EQ(c.bedPreset, (uint8_t)0);
}

TEST(care, un_huevo_nuevo_conserva_los_ajustes) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  p.setBedPreset(3);
  p.newEgg();
  CHECK_EQ(p.pace, (uint8_t)PACE_RELAXED);
  CHECK_EQ(p.bedPreset, (uint8_t)3);
  Pet q;
  q.begin();
  CHECK_EQ(q.pace, (uint8_t)PACE_RELAXED);
  CHECK_EQ(q.bedPreset, (uint8_t)3);
}

// ---------------------------------------------------------------- dormir/despertar por horario
TEST(auto, dormir_y_despertar_cambian_el_estado_y_lo_guardan) {
  Pet p;
  makePet(p, 4);
  CHECK(!p.sleeping);
  CHECK(p.autoSleep());
  CHECK(p.sleeping);
  Pet b;
  b.begin();
  CHECK_MSG(b.sleeping, "dormir por horario se guarda como el boton de luz");
  CHECK(p.autoWake());
  CHECK(!p.sleeping);
  Pet c;
  c.begin();
  CHECK(!c.sleeping);
}

TEST(auto, no_hace_nada_si_ya_esta_en_ese_estado) {
  Pet p;
  makePet(p, 4);
  CHECK(!p.autoWake());  // despierto: nada que despertar
  CHECK(!p.sleeping);
  CHECK(p.autoSleep());
  CHECK(!p.autoSleep());  // ya dormido
  CHECK(p.sleeping);
}

TEST(auto, dormir_no_actua_en_un_huevo) {
  mockNvsReset();
  Pet p;
  p.begin();
  p.chooseStarter(4);  // huevo, ya eligio inicial
  CHECK(p.isEgg());
  CHECK(!p.autoSleep());
  CHECK(!p.sleeping);
}

TEST(auto, dormir_no_actua_durante_una_ceremonia) {
  Pet p;
  makePet(p, 4);
  p.startRunaway();
  CHECK(p.ceremony != CER_NONE);
  CHECK(!p.autoSleep());
  CHECK(!p.sleeping);
}

// la eleccion de inicial se carga de la NVS aunque no sea un huevo (datos raros)
TEST(auto, dormir_no_actua_mientras_se_elige_inicial) {
  Pet a;
  makePet(a, 4);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putBool("stpk", true);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK(!b.isEgg());
  CHECK(b.awaitingStarter());
  CHECK(!b.autoSleep());
  CHECK(!b.sleeping);
}

TEST(auto, despertar_no_actua_durante_una_ceremonia) {
  Pet p;
  makePet(p, 4);
  p.sleeping = true;
  p.startRunaway();
  CHECK(!p.autoWake());
  CHECK(p.sleeping);
}

TEST(auto, despertar_no_actua_en_un_huevo) {
  mockNvsReset();
  Pet e;
  e.begin();
  e.chooseStarter(4);
  e.sleeping = true;  // estado imposible en juego: la guarda tiene que aguantar igual
  CHECK(e.isEgg());
  CHECK(!e.autoWake());
  CHECK(e.sleeping);
}

TEST(auto, despertar_no_actua_mientras_se_elige_inicial) {
  Pet a;
  makePet(a, 4);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putBool("stpk", true);
    raw.putBool("sleep", true);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK(!b.isEgg() && b.sleeping && b.awaitingStarter());
  CHECK(!b.autoWake());
  CHECK(b.sleeping);
}

// a clock set back across midnight (board.py clock after drift) must not look
// like a gap in care and reset the streak
TEST(streak, un_reloj_atrasado_no_reinicia_la_racha) {
  Pet p;
  makePet(p, 4);
  p.setClock(100u * 86400);
  p.caress();
  p.setClock(101u * 86400);
  p.caress();
  CHECK_EQ(p.streak, (uint16_t)2);
  p.setClock(100u * 86400 + 3600);  // the clock goes back a day
  p.caress();
  CHECK_EQ(p.streak, (uint16_t)2);
}

// a care registered with a far-future clock (a mistyped TIME, CAREDAY tests)
// must not freeze the streak until that date: real days take over again
TEST(streak, una_fecha_de_cuidado_futura_no_congela_la_racha) {
  Pet p;
  makePet(p, 4);
  p.setClock(20400u * 86400);
  p.caress();
  p.setClock(30000u * 86400);  // clock set years ahead by mistake
  p.caress();
  for (uint32_t d = 20401; d <= 20403; d++) {
    p.setClock(d * 86400 + 3600);
    p.caress();
  }
  CHECK_EQ(p.streak, (uint16_t)3);
}

// SAVE persists even when nothing was marked pending
TEST(save, saveNow_escribe_aunque_no_haya_pendiente) {
  Pet p;
  makePet(p, 4);
  p.flushSave();
  CHECK(!p.savePending());
  p.lastSeenEpoch = 100u * 86400 + 600;  // RAM only, nothing pending
  p.saveNow();
  CHECK_EQ(p.savedEpoch(), (uint32_t)(100u * 86400 + 600));
}

// A saved egg (dexn = -1) goes through the clamps without newEgg() resetting anything.
TEST(save, an_egg_save_with_wild_values_is_clamped_field_by_field) {
  Pet a;
  makePet(a, 4);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putShort("dexn", -1);
    raw.putShort("eggT2", 400);
    raw.putUChar("poop", 200);
    raw.putUChar("joy", 255);
    raw.putUChar("ene", 255);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK(b.isEgg());
  CHECK_EQ(b.poops, (uint8_t)3);
  CHECK_EQ(b.joy, (uint8_t)100);
  CHECK_EQ(b.energy, (uint8_t)100);
  // eggTarget is private: the hatched species shows whether 400 was replaced
  b.eggTap();
  b.eggTap();
  b.eggTap();
  CHECK(b.speciesId >= 1 && b.speciesId <= DEX_COUNT);
}

TEST(save, species_zero_is_not_a_pet) {
  Pet a;
  makePet(a, 4);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putShort("dexn", 0);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK(b.isEgg());
  CHECK(b.speciesId != 0);
}

// ---------------------------------------------------------------- eleccion de huevo
// Ajuste "Care" (NVS "eggch"): tras un final salen tres huevos a elegir. Uno es la
// tirada de siempre y los otros dos son comunes de otro color de tipo.

// acaba la vida actual con una ceremonia y deja el huevo (o la eleccion) siguiente
static void endLife(Pet &p, uint8_t how) {
  if (how == CER_FAREWELL) p.startFarewell();
  else if (how == CER_RUNAWAY) p.startRunaway();
  else p.release();
  mockAdvanceMillis(CEREMONY_MS + 1);
  p.update(millis());
}

static void registerRange(Pet &p, int16_t from, int16_t to) {
  for (int16_t d = from; d <= to; d++) p.dexReg[(d - 1) >> 3] |= (uint8_t)(1 << ((d - 1) & 7));
}

// comprobacion propia (no Pet::validEggChoices, que es lo que se prueba): tres
// especies de huevo (1..151, no formas evolucionadas) y distintas
static bool threeEggSpecies(const int16_t c[EGG_CHOICES]) {
  for (int i = 0; i < EGG_CHOICES; i++) {
    if (c[i] < 1 || c[i] > DEX_COUNT) return false;
    if (DEX_TBL[c[i]].rarity == R_EVO) return false;
    for (int j = 0; j < i; j++)
      if (c[j] == c[i]) return false;
  }
  return true;
}

static bool choicesValid(const Pet &p) {
  int16_t c[EGG_CHOICES];
  for (uint8_t i = 0; i < EGG_CHOICES; i++) c[i] = p.eggChoice(i);
  return threeEggSpecies(c);
}

TEST(eggpick, apagado_por_defecto_un_final_da_un_solo_huevo) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.eggPickMode, (uint8_t)EGGPICK_OFF);
  endLife(p, CER_RELEASE);
  CHECK(p.isEgg());
  CHECK_MSG(!p.awaitingEgg(), "con el ajuste apagado no hay eleccion");
  p.eggTap();
  p.eggTap();
  p.eggTap();
  CHECK(!p.isEgg());
}

TEST(eggpick, encendido_un_final_ofrece_tres_huevos_validos) {
  for (uint8_t how : { (uint8_t)CER_FAREWELL, (uint8_t)CER_RELEASE, (uint8_t)CER_RUNAWAY }) {
    Pet p;
    makePet(p, 6);
    p.setEggPickMode(EGGPICK_ON);
    endLife(p, how);
    CHECK(p.isEgg());
    CHECK_MSG(p.awaitingEgg(), "tras cualquier final, con el ajuste encendido, toca elegir");
    CHECK_MSG(choicesValid(p), "tres especies de huevo validas y distintas");
  }
}

TEST(eggpick, la_primera_partida_sigue_con_el_inicial) {
  mockNvsReset();
  Pet p;
  p.begin();
  p.setEggPickMode(EGGPICK_ON);
  p.newEgg();  // sin pokedex, como tras WIPE
  CHECK(p.awaitingStarter());
  CHECK_MSG(!p.awaitingEgg(), "la primera partida no ofrece huevos: elige inicial");
}

TEST(eggpick, el_tiempo_no_corre_mientras_elige) {
  Pet p;
  makePet(p, 4);
  p.setEggPickMode(EGGPICK_ON);
  p.setClock(1000u * 86400);
  endLife(p, CER_RELEASE);
  CHECK(p.awaitingEgg());
  advance(p, 600);  // 10 horas encendido
  CHECK_MSG(p.isEgg(), "el huevo no eclosiona solo mientras elige");
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
  CHECK(p.awaitingEgg());
}

TEST(eggpick, el_tiempo_apagado_no_se_aplica_mientras_elige) {
  Pet p;
  makePet(p, 4);
  p.setEggPickMode(EGGPICK_ON);
  p.setClock(1000u * 86400);
  endLife(p, CER_RELEASE);
  CHECK(p.awaitingEgg());
  p.syncClock(1000u * 86400 + 3 * 86400);  // tres dias apagado
  CHECK_MSG(p.isEgg(), "el huevo no eclosiona en ausencia mientras elige");
  CHECK_EQ(p.ageMinutes, (uint32_t)0);
}

TEST(eggpick, tocar_no_eclosiona_mientras_elige) {
  Pet p;
  makePet(p, 4);
  p.setEggPickMode(EGGPICK_ON);
  endLife(p, CER_RELEASE);
  p.eggTap();
  p.eggTap();
  p.eggTap();
  CHECK_MSG(p.isEgg(), "aun no hay huevo que tocar");
  CHECK_EQ(p.eggCracks(), (uint8_t)0);
  CHECK(p.awaitingEgg());
}

TEST(eggpick, elegir_fija_el_huevo_y_reanuda_el_tiempo) {
  for (uint8_t pick = 0; pick < EGG_CHOICES; pick++) {
    Pet p;
    makePet(p, 4);
    p.setEggPickMode(EGGPICK_ON);
    endLife(p, CER_RELEASE);
    int16_t want = p.eggChoice(pick);
    p.chooseEgg(EGG_CHOICES);  // fuera de rango: nada
    CHECK(p.awaitingEgg());
    p.chooseEgg(pick);
    CHECK(!p.awaitingEgg());
    CHECK_EQ(p.eggRarity(), DEX_TBL[want].rarity);  // la pantalla del huevo ve el elegido
    CHECK_EQ(p.eggAccent(), DEX_TBL[want].accent);
    advance(p, 3);  // ya corre el tiempo: eclosiona solo a los 3 min
    CHECK(!p.isEgg());
    CHECK_EQ(p.speciesId, want);
  }
}

TEST(eggpick, uno_de_los_tres_es_la_tirada_de_siempre) {
  Pet p;
  makePet(p, 4);
  p.setEggPickMode(EGGPICK_ON);
  registerRange(p, 1, 40);  // >= 25: tambien pueden salir legendarios
  p.lastEnd = CER_FAREWELL;
  p.streak = 30;
  p.bond = 100;
  int notCommon = 0;
  for (int seed = 1; seed <= 300; seed++) {
    randomSeed(seed);
    int16_t expected = p.pickEggSpecies();
    randomSeed(seed);
    p.newEgg();
    CHECK(p.awaitingEgg());
    int found = 0;
    for (uint8_t i = 0; i < EGG_CHOICES; i++)
      if (p.eggChoice(i) == expected) found++;
    CHECK_MSG(found == 1, "la tirada normal (mismas probabilidades) es uno de los tres");
    if (DEX_TBL[expected].rarity != R_COMUN) notCommon++;
  }
  CHECK_MSG(notCommon > 0, "el test tambien cubre huevos raros");
}

TEST(eggpick, los_otros_dos_son_comunes) {
  Pet p;
  makePet(p, 4);
  registerRange(p, 1, 40);
  p.lastEnd = CER_FAREWELL;
  p.streak = 30;
  p.bond = 100;
  int sawRare = 0;
  for (int seed = 1; seed <= 300; seed++) {
    randomSeed(seed);
    int16_t special = p.pickEggSpecies();
    int16_t out[EGG_CHOICES];
    p.rollEggChoices(special, out);
    int notCommon = 0;
    for (int16_t d : out)
      if (DEX_TBL[d].rarity != R_COMUN) notCommon++;
    CHECK_MSG(notCommon == (DEX_TBL[special].rarity != R_COMUN ? 1 : 0),
              "solo el huevo de la tirada normal puede ser raro o legendario");
    if (notCommon) sawRare++;
  }
  CHECK(sawRare > 0);
}

TEST(eggpick, el_huevo_de_la_tirada_normal_cambia_de_sitio) {
  Pet p;
  makePet(p, 4);
  int where[EGG_CHOICES] = { 0, 0, 0 };
  for (int seed = 1; seed <= 90; seed++) {
    randomSeed(seed);
    int16_t out[EGG_CHOICES];
    p.rollEggChoices(4, out);
    for (int i = 0; i < EGG_CHOICES; i++)
      if (out[i] == 4) where[i]++;
  }
  for (int i = 0; i < EGG_CHOICES; i++)
    CHECK_MSG(where[i] > 0, "el huevo de la tirada normal sale en cualquier posicion");
}

TEST(eggpick, una_escapada_da_tres_comunes) {
  Pet p;
  makePet(p, 4);
  p.setEggPickMode(EGGPICK_ON);
  registerRange(p, 1, 40);
  p.streak = 30;
  p.bond = 100;
  p.lastEnd = CER_RUNAWAY;
  for (int seed = 1; seed <= 100; seed++) {
    randomSeed(seed);
    p.newEgg();
    for (uint8_t i = 0; i < EGG_CHOICES; i++)
      CHECK_MSG(DEX_TBL[p.eggChoice(i)].rarity == R_COMUN, "tras una escapada los tres son comunes");
  }
}

// CIE76 colour difference of two RGB565 accents (#34), worked out here apart from
// tools/gen_dex.py, which writes eggSpotsLookAlike()'s table into dex.h: each
// channel widened to 8 bits by repeating its top bits, sRGB, D65 white.
static void labOf565(uint16_t c, double lab[3]) {
  int r5 = c >> 11, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
  int rgb[3] = { (r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2) };
  double lin[3];
  for (int i = 0; i < 3; i++) {
    double v = rgb[i] / 255.0;
    lin[i] = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
  }
  double xyz[3] = {
    (0.4124 * lin[0] + 0.3576 * lin[1] + 0.1805 * lin[2]) / 0.95047,
    0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2],
    (0.0193 * lin[0] + 0.1192 * lin[1] + 0.9505 * lin[2]) / 1.08883,
  };
  const double e = 6.0 / 29.0;
  double f[3];
  for (int i = 0; i < 3; i++)
    f[i] = xyz[i] > e * e * e ? cbrt(xyz[i]) : xyz[i] / (3 * e * e) + 4.0 / 29.0;
  lab[0] = 116 * f[1] - 16;
  lab[1] = 500 * (f[0] - f[1]);
  lab[2] = 200 * (f[1] - f[2]);
}

static double deltaE76(uint16_t a, uint16_t b) {
  double la[3], lb[3];
  labOf565(a, la);
  labOf565(b, lb);
  return sqrt((la[0] - lb[0]) * (la[0] - lb[0]) + (la[1] - lb[1]) * (la[1] - lb[1]) +
              (la[2] - lb[2]) * (la[2] - lb[2]));
}

// the test's own reading of "these two eggs' spots look alike" (not eggSpotsLookAlike)
static bool spotsTooClose(int16_t a, int16_t b) {
  uint16_t ca = DEX_TBL[a].accent, cb = DEX_TBL[b].accent;
  return ca == cb || deltaE76(ca, cb) < EGG_SPOT_MIN_DE;
}

TEST(eggpick, the_look_alike_table_matches_the_accents_in_dex) {
  // every pair of accents in DEX_TBL: the generated table says "alike" exactly when
  // the colour difference computed here is under the threshold, so an accent
  // changed by hand in dex.h, or a generator that measures differently, fails here
  uint16_t acc[DEX_COUNT];
  int n = 0;
  for (int16_t d = 1; d <= DEX_COUNT; d++) {
    bool seen = false;
    for (int i = 0; i < n; i++)
      if (acc[i] == DEX_TBL[d].accent) seen = true;
    if (!seen) acc[n++] = DEX_TBL[d].accent;
  }
  CHECK_EQ(n, 14);  // one accent per type
  int alikePairs = 0, wrong = 0;
  char first[96] = "";
  for (int i = 0; i < n; i++) {
    CHECK_MSG(eggSpotsLookAlike(acc[i], acc[i]), "an accent looks like itself");
    for (int j = i + 1; j < n; j++) {
      double de = deltaE76(acc[i], acc[j]);
      bool want = de < EGG_SPOT_MIN_DE;
      if (want) alikePairs++;
      if (eggSpotsLookAlike(acc[i], acc[j]) == want && eggSpotsLookAlike(acc[j], acc[i]) == want) continue;
      if (!wrong++)
        snprintf(first, sizeof first, "first: 0x%04X / 0x%04X, dE %.1f, table says %d", acc[i], acc[j], de,
                 (int)eggSpotsLookAlike(acc[i], acc[j]));
    }
  }
  CHECK_MSG(wrong == 0, std::string("the table agrees with the colour difference, both ways round; ") + first);
  // the four pairs #34 set out to keep apart, and the closest pair it leaves alone
  CHECK_MSG(spotsTooClose(27, 74), "ground (Sandshrew) / rock (Geodude)");
  CHECK_MSG(spotsTooClose(23, 92), "poison (Ekans) / ghost (Gastly)");
  CHECK_MSG(spotsTooClose(25, 27), "electric (Pikachu) / ground (Sandshrew)");
  CHECK_MSG(spotsTooClose(19, 74), "normal (Rattata) / rock (Geodude)");
  CHECK_MSG(!spotsTooClose(7, 144), "water (Squirtle) / ice (Articuno), dE 27.4, may share a roll");
  CHECK_EQ(alikePairs, 4);
}

TEST(eggpick, no_two_eggs_in_a_roll_look_alike) {
  // every egg species as the usual roll, with the Pokedex nearly empty (passes 0
  // and 1), half full and full (no new line anywhere: pass 1)
  int rolls = 0, closeToSpecial = 0, bad = 0;
  char first[96] = "";
  for (int16_t upTo : { (int16_t)4, (int16_t)80, (int16_t)151 }) {
    Pet p;
    makePet(p, 4);
    registerRange(p, 1, upTo);
    for (int16_t special = 1; special <= DEX_COUNT; special++) {
      if (DEX_TBL[special].rarity == R_EVO) continue;
      bool hasCloseCommon = false;
      for (int16_t d = 1; d <= DEX_COUNT; d++)
        if (d != special && DEX_TBL[d].rarity == R_COMUN && spotsTooClose(d, special)) hasCloseCommon = true;
      for (int seed = 1; seed <= 10; seed++) {
        randomSeed(seed);
        int16_t out[EGG_CHOICES];
        p.rollEggChoices(special, out);
        bool ok = threeEggSpecies(out) && !spotsTooClose(out[0], out[1]) &&
                  !spotsTooClose(out[0], out[2]) && !spotsTooClose(out[1], out[2]);
        if (!ok && !bad++)
          snprintf(first, sizeof first, "first: Pokedex to %d, special %d, seed %d: eggs %d %d %d", upTo,
                   special, seed, out[0], out[1], out[2]);
        rolls++;
        if (hasCloseCommon) closeToSpecial++;
      }
    }
  }
  CHECK_MSG(bad == 0, std::string("three distinct eggs, no two with spots that look alike; ") + first);
  CHECK_EQ(rolls, 3 * 79 * 10);
  CHECK_MSG(closeToSpecial > 0, "the special egg sometimes has look-alike Commons to avoid");
}

TEST(eggpick, the_colour_rule_never_runs_out_of_eggs) {
  // With today's accents and table, whatever the usual roll and the first extra
  // egg, some Common is left whose spots look like neither: passes 0 and 1 always
  // have a candidate when no line is new, so passes 2 and 3 are never needed.
  int fewest = DEX_COUNT;
  for (int16_t s = 1; s <= DEX_COUNT; s++) {
    if (DEX_TBL[s].rarity == R_EVO) continue;
    int seconds = 0;
    for (int16_t c = 1; c <= DEX_COUNT; c++) {
      if (c == s || DEX_TBL[c].rarity != R_COMUN) continue;
      if (eggSpotsLookAlike(DEX_TBL[s].accent, DEX_TBL[c].accent)) continue;
      seconds++;
      int thirds = 0;
      for (int16_t d = 1; d <= DEX_COUNT; d++) {
        if (d == s || d == c || DEX_TBL[d].rarity != R_COMUN) continue;
        if (eggSpotsLookAlike(DEX_TBL[s].accent, DEX_TBL[d].accent)) continue;
        if (eggSpotsLookAlike(DEX_TBL[c].accent, DEX_TBL[d].accent)) continue;
        thirds++;
      }
      if (thirds < fewest) fewest = thirds;
    }
    CHECK_MSG(seconds > 0, "a second egg whose spots don't look like the usual roll's");
  }
  CHECK_MSG(fewest > 0, "a third egg whose spots look like neither, whatever the first two");
}

static bool everyColourAlike(uint16_t, uint16_t) { return true; }

TEST(eggpick, with_no_colour_left_the_fallback_still_fills_three_slots) {
  // a look-alike rule that no pair passes: passes 0 and 1 find nothing, so the
  // extras come from passes 2 (a new line) and 3 (any Common)
  int notThree = 0, notCommon = 0, oldLine = 0, noSpecial = 0;
  for (int16_t upTo : { (int16_t)4, (int16_t)60, (int16_t)151 }) {
    Pet p;
    makePet(p, 4);
    registerRange(p, 1, upTo);
    p.lastEnd = CER_FAREWELL;
    p.streak = 30;
    p.bond = 100;
    for (int seed = 1; seed <= 200; seed++) {
      randomSeed(seed);
      int16_t special = p.pickEggSpecies();
      int16_t out[EGG_CHOICES];
      p.rollEggChoices(special, out, everyColourAlike);
      if (!threeEggSpecies(out)) {
        notThree++;
        continue;
      }
      int found = 0;
      for (int16_t d : out) {
        if (d == special) {
          found++;
          continue;
        }
        if (DEX_TBL[d].rarity != R_COMUN) notCommon++;
        if (upTo < 151 && !p.lineHasUnregistered(d)) oldLine++;
      }
      if (found != 1) noSpecial++;
    }
  }
  CHECK_EQ(notThree, 0);   // three distinct egg species, even with no colour left
  CHECK_EQ(notCommon, 0);  // the extras are Common
  CHECK_EQ(oldLine, 0);    // pass 2 still prefers a new line while there is one
  CHECK_EQ(noSpecial, 0);  // the usual roll is one of the three
}

TEST(eggpick, los_comunes_prefieren_lineas_incompletas) {
  Pet p;
  makePet(p, 4);
  registerRange(p, 1, 60);  // muchas lineas comunes ya completas
  for (int seed = 1; seed <= 200; seed++) {
    randomSeed(seed);
    int16_t out[EGG_CHOICES];
    p.rollEggChoices(1, out);  // el especial da igual: se miran los otros dos
    for (int16_t d : out)
      if (d != 1)
        CHECK_MSG(p.lineHasUnregistered(d), "los comunes extra completan la pokedex");
  }
}

TEST(eggpick, sin_linea_nueva_cede_la_linea_antes_que_el_color) {
  // Everything registered but the Commons whose spots look like the special egg's
  // (ground: ground, rock and electric, #34): the only Commons with an incomplete
  // line look alike, so pass 0 (far colour and new line) finds nothing and pass 1
  // (far colour only) does. If the colour gave way first, the extras would be
  // look-alikes.
  Pet p;
  makePet(p, 4);
  const int16_t special = 27;  // Sandshrew, ground
  registerRange(p, 1, 151);
  int alikeOthers = 0;
  for (int16_t d = 1; d <= DEX_COUNT; d++)
    if (DEX_TBL[d].rarity == R_COMUN && spotsTooClose(d, special)) {
      p.dexReg[(d - 1) >> 3] &= (uint8_t)~(1 << ((d - 1) & 7));
      if (DEX_TBL[d].accent != DEX_TBL[special].accent) alikeOthers++;
    }
  CHECK_MSG(alikeOthers >= 2, "Commons of other, look-alike colours are left unregistered");
  int bad = 0;
  for (int seed = 1; seed <= 100; seed++) {
    randomSeed(seed);
    int16_t out[EGG_CHOICES];
    p.rollEggChoices(special, out);
    if (!threeEggSpecies(out) || spotsTooClose(out[0], out[1]) || spotsTooClose(out[0], out[2]) ||
        spotsTooClose(out[1], out[2]))
      bad++;
  }
  CHECK_MSG(bad == 0, "the line gives way before the colour: three eggs, no two spots alike");
}

TEST(eggpick, a_saved_choice_keeps_its_eggs_even_if_they_look_alike) {
  // A choice saved before #34 (or by any roll) is not rolled again for its colours:
  // the player sees the same three eggs after a restart or an update.
  Pet a;
  makePet(a, 4);
  a.setEggPickMode(EGGPICK_ON);
  endLife(a, CER_RELEASE);
  CHECK(a.awaitingEgg());
  const int16_t saved[EGG_CHOICES] = { 27, 74, 25 };  // ground, rock, electric: all Common
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putShort("ec0", saved[0]);
    raw.putShort("ec1", saved[1]);
    raw.putShort("ec2", saved[2]);
    raw.end();
  }
  CHECK(spotsTooClose(saved[0], saved[1]));
  Pet b;
  b.begin();
  CHECK(b.awaitingEgg());
  for (uint8_t i = 0; i < EGG_CHOICES; i++) CHECK_EQ(b.eggChoice(i), saved[i]);
}

TEST(eggpick, el_shiny_no_cambia_y_va_con_el_elegido) {
  // misma semilla, ajuste apagado y encendido: el shiny del bicho que nace es el
  // mismo (se sortea antes que los huevos extra) elija el huevo que elija
  int shinies = 0;
  for (int seed = 1; seed <= 400; seed++) {
    Pet off;
    makePet(off, 4);
    off.streak = 30;
    off.bond = 100;  // shiny 1/8 tras despedida: salen bastantes
    off.lastEnd = CER_FAREWELL;
    randomSeed(seed);
    off.newEgg();
    off.eggTap();
    off.eggTap();
    off.eggTap();
    Pet on;
    makePet(on, 4);
    on.setEggPickMode(EGGPICK_ON);
    on.streak = 30;
    on.bond = 100;
    on.lastEnd = CER_FAREWELL;
    randomSeed(seed);
    on.newEgg();
    on.chooseEgg((uint8_t)(seed % EGG_CHOICES));
    on.eggTap();
    on.eggTap();
    on.eggTap();
    CHECK_MSG(on.shiny == off.shiny, "el sorteo shiny es el de siempre");
    if (on.shiny) shinies++;
  }
  CHECK(shinies > 0);
}

TEST(eggpick, la_eleccion_sobrevive_a_un_reinicio) {
  Pet a;
  makePet(a, 4);
  a.setEggPickMode(EGGPICK_ON);
  endLife(a, CER_FAREWELL);
  CHECK(a.awaitingEgg());
  Pet b;
  b.begin();
  CHECK_EQ(b.eggPickMode, (uint8_t)EGGPICK_ON);
  CHECK_MSG(b.awaitingEgg(), "apagar a mitad de la eleccion la conserva");
  for (uint8_t i = 0; i < EGG_CHOICES; i++) CHECK_EQ(b.eggChoice(i), a.eggChoice(i));
  b.chooseEgg(2);
  Pet c;
  c.begin();
  CHECK_MSG(!c.awaitingEgg(), "elegir se guarda");
  c.eggTap();
  c.eggTap();
  c.eggTap();
  CHECK_EQ(c.speciesId, a.eggChoice(2));
}

TEST(eggpick, un_guardado_antiguo_sin_las_claves_sigue_como_siempre) {
  Pet a;
  makePet(a, 4);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.remove("eggch");
    raw.remove("ecpk");
    raw.remove("ec0");
    raw.remove("ec1");
    raw.remove("ec2");
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_EQ(b.speciesId, (int16_t)4);
  CHECK_EQ(b.eggPickMode, (uint8_t)EGGPICK_OFF);
  CHECK(!b.awaitingEgg());
  endLife(b, CER_RELEASE);
  CHECK_MSG(!b.awaitingEgg(), "sin la clave el ajuste esta apagado: un huevo como siempre");
}

TEST(eggpick, un_guardado_antiguo_con_un_huevo_pendiente_sigue_como_siempre) {
  // el guardado antiguo es un huevo por eclosionar con el ajuste apagado: sin la
  // clave "ecpk" no puede salir una eleccion (un bicho ya nacido la limpiaria
  // aunque el valor por defecto fuese true)
  Pet a;
  makePet(a, 4);
  endLife(a, CER_RELEASE);
  CHECK(a.isEgg());
  CHECK(!a.awaitingEgg());
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.remove("eggch");
    raw.remove("ecpk");
    raw.remove("ec0");
    raw.remove("ec1");
    raw.remove("ec2");
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK(b.isEgg());
  CHECK_EQ(b.eggPickMode, (uint8_t)EGGPICK_OFF);
  CHECK_MSG(!b.awaitingEgg(), "un huevo guardado sin las claves sigue siendo un huevo, sin eleccion");
  for (int i = 0; i < 3; i++) {
    a.eggTap();
    b.eggTap();
  }
  CHECK(!a.isEgg());
  CHECK(!b.isEgg());
  CHECK_EQ(b.speciesId, a.speciesId);  // nace lo mismo que habria nacido
}

TEST(eggpick, candidatos_invalidos_en_la_nvs_se_vuelven_a_sortear) {
  // fuera de rango, forma evolucionada (2 IVYSAUR), repetidos, negativos
  const int16_t bad[][EGG_CHOICES] = {
    { 300, 4, 7 }, { 1, 2, 7 }, { 4, 4, 7 }, { -1, 4, 7 }, { 1, 4, 0 }, { 1, 4, 152 },
  };
  for (auto &b3 : bad) {
    Pet a;
    makePet(a, 4);
    a.setEggPickMode(EGGPICK_ON);
    endLife(a, CER_RELEASE);
    {
      Preferences raw;
      raw.begin("tamapoke", false);
      raw.putShort("ec0", b3[0]);
      raw.putShort("ec1", b3[1]);
      raw.putShort("ec2", b3[2]);
      raw.end();
    }
    Pet b;
    b.begin();
    CHECK(b.awaitingEgg());
    CHECK_MSG(choicesValid(b), "lo leido de NVS se comprueba antes de usarlo");
  }
}

TEST(eggpick, candidatos_invalidos_conservan_la_tirada_guardada) {
  // un guardado con los candidatos danados sigue ofreciendo el huevo raro o
  // legendario que ya habia tocado, no una tirada nueva
  Pet a;
  makePet(a, 4);
  a.setEggPickMode(EGGPICK_ON);
  registerRange(a, 1, 40);  // >= 25: tambien pueden salir legendarios
  a.lastEnd = CER_FAREWELL;
  a.streak = 30;
  a.bond = 100;
  int notCommon = 0;
  for (int seed = 1; seed <= 60; seed++) {
    randomSeed(seed);
    int16_t expected = a.pickEggSpecies();
    randomSeed(seed);
    a.newEgg();
    CHECK(a.awaitingEgg());
    {
      Preferences raw;
      raw.begin("tamapoke", false);
      raw.putShort("ec0", 300);
      raw.putShort("ec1", 4);
      raw.putShort("ec2", 4);
      raw.end();
    }
    Pet b;
    b.begin();
    CHECK(b.awaitingEgg());
    CHECK(choicesValid(b));
    int found = 0;
    for (uint8_t i = 0; i < EGG_CHOICES; i++)
      if (b.eggChoice(i) == expected) found++;
    CHECK_MSG(found == 1, "la tirada guardada sigue entre los tres tras recomponerlos");
    if (DEX_TBL[expected].rarity != R_COMUN) notCommon++;
  }
  CHECK_MSG(notCommon > 0, "el test cubre huevos raros o legendarios");
}

TEST(eggpick, una_tirada_guardada_que_no_es_de_huevo_se_sustituye) {
  // eggT2 danado (2 IVYSAUR no es una especie de huevo) junto con candidatos
  // danados: los tres siguen siendo huevos validos
  Pet a;
  makePet(a, 4);
  a.setEggPickMode(EGGPICK_ON);
  endLife(a, CER_RELEASE);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putShort("eggT2", 2);
    raw.putShort("ec0", 300);
    raw.putShort("ec1", 4);
    raw.putShort("ec2", 4);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK(b.awaitingEgg());
  CHECK_MSG(choicesValid(b), "una tirada que no es de huevo no entra en los tres");
  // y la tirada guardada pasa a ser uno de los tres (eggTarget es privado: se ve
  // por la rareza y el color que exponen eggRarity()/eggAccent())
  CHECK_MSG(b.eggRarity() != R_EVO, "eggTarget ya no es una forma evolucionada");
  int match = 0;
  for (uint8_t i = 0; i < EGG_CHOICES; i++) {
    const DexEntry &c = DEX_TBL[b.eggChoice(i)];
    if (c.rarity == b.eggRarity() && c.accent == b.eggAccent()) match++;
  }
  CHECK_MSG(match >= 1, "eggTarget es uno de los tres mientras se elige");
}

TEST(eggpick, los_comunes_extra_varian_entre_finales) {
  // si los dos comunes extra no se sortearan, saldrian siempre los mismos
  for (int16_t upTo : { (int16_t)4, (int16_t)60 }) {
    Pet p;
    makePet(p, 4);
    registerRange(p, 1, upTo);
    bool seen[DEX_COUNT + 1] = { false };
    int distinct = 0;
    for (int seed = 1; seed <= 100; seed++) {
      randomSeed(seed);
      int16_t out[EGG_CHOICES];
      p.rollEggChoices(1, out);  // el especial fijo: se miran los otros dos
      for (int16_t d : out)
        if (d != 1 && !seen[d]) {
          seen[d] = true;
          distinct++;
        }
    }
    CHECK_MSG(distinct > 10, "los dos comunes extra se sortean, no son siempre los mismos");
  }
}

TEST(eggpick, el_validador_rechaza_cada_caso_malo) {
  const int16_t bad[][EGG_CHOICES] = {
    { 300, 4, 7 }, { 1, 2, 7 }, { 4, 4, 7 }, { -1, 4, 7 }, { 1, 4, 0 }, { 1, 4, 152 }, { 1, 7, 1 },
  };
  for (auto &b3 : bad) CHECK(!Pet::validEggChoices(b3));
  const int16_t good[EGG_CHOICES] = { 151, 1, 7 };
  CHECK(Pet::validEggChoices(good));
}

TEST(eggpick, candidatos_validos_en_la_nvs_se_respetan) {
  Pet a;
  makePet(a, 4);
  a.setEggPickMode(EGGPICK_ON);
  endLife(a, CER_RELEASE);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putShort("ec0", 151);  // MEW: legendario, valido
    raw.putShort("ec1", 1);
    raw.putShort("ec2", 7);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_EQ(b.eggChoice(0), (int16_t)151);
  CHECK_EQ(b.eggChoice(1), (int16_t)1);
  CHECK_EQ(b.eggChoice(2), (int16_t)7);
}

TEST(eggpick, ajuste_fuera_de_rango_vuelve_a_apagado) {
  Pet p;
  makePet(p, 4);
  p.setEggPickMode(EGGPICK_ON);
  p.setEggPickMode(EGGPICK_COUNT);  // el primer valor invalido
  CHECK_EQ(p.eggPickMode, (uint8_t)EGGPICK_OFF);
  p.setEggPickMode(EGGPICK_ON);
  p.setEggPickMode(200);
  CHECK_EQ(p.eggPickMode, (uint8_t)EGGPICK_OFF);
  for (uint8_t v : { (uint8_t)EGGPICK_COUNT, (uint8_t)99 }) {
    {
      Preferences raw;
      raw.begin("tamapoke", false);
      raw.putUChar("eggch", v);
      raw.end();
    }
    Pet b;
    b.begin();
    CHECK_EQ(b.speciesId, (int16_t)4);  // la mascota no se toca
    CHECK_EQ(b.eggPickMode, (uint8_t)EGGPICK_OFF);
  }
}

TEST(eggpick, el_ajuste_se_guarda_y_sobrevive_a_un_huevo_nuevo) {
  Pet p;
  makePet(p, 4);
  p.setEggPickMode(EGGPICK_ON);
  p.newEgg();
  CHECK_EQ(p.eggPickMode, (uint8_t)EGGPICK_ON);
  Pet q;
  q.begin();
  CHECK_EQ(q.eggPickMode, (uint8_t)EGGPICK_ON);
}

TEST(eggpick, inicial_y_huevo_a_la_vez_gana_el_inicial) {
  Pet a;
  makePet(a, 4);
  a.setEggPickMode(EGGPICK_ON);
  endLife(a, CER_RELEASE);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putBool("stpk", true);
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK(b.awaitingStarter());
  CHECK_MSG(!b.awaitingEgg(), "un guardado con las dos elecciones abre solo la del inicial");
}

TEST(eggpick, una_mascota_ya_nacida_no_queda_eligiendo) {
  Pet a;
  makePet(a, 4);
  {
    Preferences raw;
    raw.begin("tamapoke", false);
    raw.putBool("ecpk", true);  // guardado danado: eleccion pendiente con mascota
    raw.end();
  }
  Pet b;
  b.begin();
  CHECK_EQ(b.speciesId, (int16_t)4);
  CHECK_MSG(!b.awaitingEgg(), "sin huevo no hay nada que elegir");
  uint32_t age = b.ageMinutes;
  advance(b, 2);
  CHECK_MSG(b.ageMinutes == age + 2, "el tiempo sigue corriendo");
}

TEST(eggpick, cancelar_sale_de_la_eleccion) {
  Pet p;
  makePet(p, 4);
  p.setEggPickMode(EGGPICK_ON);
  endLife(p, CER_RELEASE);
  p.cancelEggChoice();
  CHECK(!p.awaitingEgg());
  Pet q;
  q.begin();
  CHECK_MSG(!q.awaitingEgg(), "cancelar se guarda");
}

// ---------------------------------------------------------------- self-care while charging (#24)
// On USB, with the PMU answering, and untouched SELFCARE_IDLE_MS: once a minute the first
// need under 40 (food, hygiene, energy, joy) goes back up to 50.

static const uint32_t LONG_IDLE = 60UL * 60 * 1000;  // an hour without a touch

// Fresh Charmander with no random poops (random(100) < 15 never holds).
static void makeQuietPet(Pet &p) {
  makePet(p, 4);
  mockForceRandom(99);
}

TEST(selfcare, on_usb_after_ten_idle_minutes_food_goes_back_to_half) {
  Pet p;
  makeQuietPet(p);
  setStats(p, 41, 80, 80, 100);  // food 41 -> 39 this minute
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 1);
  CHECK_EQ(p.fullness, (uint8_t)SELFCARE_LEVEL);
  CHECK_EQ(p.fullness, (uint8_t)50);  // half, not full
  CHECK_EQ(p.joy, (uint8_t)79);       // the others only drained
  CHECK_EQ(p.energy, (uint8_t)79);
  CHECK_EQ(p.hygiene, (uint8_t)99);
  mockClearForcedRandom();
}

TEST(selfcare, a_need_at_40_or_more_is_left_alone) {
  Pet p;
  makeQuietPet(p);
  setStats(p, 42, 41, 41, 41);  // after this minute's drain: all exactly 40
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 1);
  CHECK_EQ(p.fullness, (uint8_t)40);
  CHECK_EQ(p.joy, (uint8_t)40);
  CHECK_EQ(p.energy, (uint8_t)40);
  CHECK_EQ(p.hygiene, (uint8_t)40);
  mockClearForcedRandom();
}

TEST(selfcare, food_then_hygiene_then_energy_then_joy) {
  Pet p;
  makeQuietPet(p);
  setStats(p, 20, 20, 20, 20);
  p.poops = 2;
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 1);
  CHECK_EQ(p.fullness, (uint8_t)50);
  CHECK(p.hygiene < 40);
  CHECK(p.energy < 40);
  CHECK(p.joy < 40);
  advance(p, 1);
  CHECK_EQ(p.hygiene, (uint8_t)50);
  CHECK_EQ(p.poops, (uint8_t)0);
  CHECK(p.energy < 40);
  CHECK(p.joy < 40);
  advance(p, 1);
  CHECK_EQ(p.energy, (uint8_t)50);
  CHECK(p.joy < 40);
  advance(p, 1);
  CHECK_EQ(p.joy, (uint8_t)50);
  CHECK(p.fullness >= 40 && p.hygiene >= 40 && p.energy >= 40);
  mockClearForcedRandom();
}

TEST(selfcare, one_need_per_minute_and_only_on_the_minute) {
  Pet p;
  makeQuietPet(p);
  setStats(p, 30, 30, 30, 30);
  p.setCharger(true, true, LONG_IDLE);
  for (int i = 0; i < 59; i++) {  // 59 loop passes within the first minute: no tick yet
    mockAdvanceMillis(1000);
    p.setCharger(true, true, LONG_IDLE);
    p.update(millis());
  }
  CHECK_EQ(p.fullness, (uint8_t)30);
  CHECK_EQ(p.hygiene, (uint8_t)30);
  mockAdvanceMillis(1000);  // the minute ticks
  p.update(millis());
  CHECK_EQ(p.fullness, (uint8_t)50);
  CHECK_MSG(p.hygiene < 40 && p.energy < 40 && p.joy < 40, "only one need per minute");
  mockClearForcedRandom();
}

TEST(selfcare, hygiene_also_clears_the_poops) {
  Pet p;
  makeQuietPet(p);
  setStats(p, 80, 80, 80, 41);
  p.poops = 3;  // hygiene 41 - 1 - 12 = 28 this minute
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 1);
  CHECK_EQ(p.hygiene, (uint8_t)50);
  CHECK_EQ(p.poops, (uint8_t)0);
  advance(p, 1);
  CHECK_MSG(p.hygiene == 49, "without the poops hygiene drains 1 a minute again");
  mockClearForcedRandom();
}

TEST(selfcare, usb_unknown_counts_as_unplugged) {
  Pet p;
  makeQuietPet(p);
  setStats(p, 30, 30, 30, 30);
  // the PMU never answered: the firmware's usbPresent() reports true then
  p.setCharger(false, true, LONG_IDLE);
  advance(p, 3);
  CHECK(p.fullness < 40 && p.hygiene < 40 && p.energy < 40 && p.joy < 40);
  p.setCharger(true, true, LONG_IDLE);  // control: the same pet with a PMU that answers
  advance(p, 1);
  CHECK_EQ(p.fullness, (uint8_t)50);
  mockClearForcedRandom();
}

TEST(selfcare, never_on_battery) {
  Pet p;
  makeQuietPet(p);
  setStats(p, 30, 30, 30, 30);
  p.setCharger(true, false, LONG_IDLE);
  advance(p, 3);
  CHECK(p.fullness < 40 && p.hygiene < 40 && p.energy < 40 && p.joy < 40);
  p.setCharger(true, true, LONG_IDLE);  // control: plugged in
  advance(p, 1);
  CHECK_EQ(p.fullness, (uint8_t)50);
  mockClearForcedRandom();
}

// Like the sketch: on every pass setCharger() gets the time since the last touch, then update().
static void chargeMinutes(Pet &p, uint32_t minutes, uint32_t lastTouch) {
  for (uint32_t i = 0; i < minutes; i++) {
    mockAdvanceMillis(PET_TICK_MS);
    p.setCharger(true, true, millis() - lastTouch);
    p.update(millis());
  }
}

TEST(selfcare, waits_for_ten_untouched_minutes) {
  Pet p;
  makeQuietPet(p);
  setStats(p, 80, 80, 39, 100);  // energy: 1 a minute, no slip-up in 10 minutes
  uint32_t touch = millis();
  chargeMinutes(p, 9, touch);
  CHECK_MSG(p.energy == 30, "9 minutes untouched: not yet");
  chargeMinutes(p, 1, touch);
  CHECK_MSG(p.energy == 50, "10 minutes untouched: self-care");
  // a touch starts the 10 minutes again
  setStats(p, 80, 80, 39, 100);
  touch = millis();
  chargeMinutes(p, 9, touch);
  CHECK_EQ(p.energy, (uint8_t)30);
  chargeMinutes(p, 1, touch);
  CHECK_EQ(p.energy, (uint8_t)50);
  mockClearForcedRandom();
}

TEST(selfcare, is_not_the_players_care) {
  Pet p;
  makeQuietPet(p);
  p.setClock(100u * 86400);  // with a clock, the player's care would start a streak
  uint16_t medals = p.medals, total = p.totalMedals;
  setStats(p, 20, 20, 20, 20);
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 4);
  CHECK(p.fullness >= 40 && p.hygiene >= 40 && p.energy >= 40 && p.joy >= 40);  // it did care
  CHECK_EQ(p.streak, (uint16_t)0);
  CHECK_EQ(p.bestStreak, (uint16_t)0);
  CHECK_EQ(p.lastCareDay, (uint32_t)0);
  CHECK_EQ(p.bond, (uint8_t)0);
  CHECK_EQ(p.medals, medals);
  CHECK_EQ(p.totalMedals, total);
  CHECK(!p.berryKnown);
  mockClearForcedRandom();
}

// flash wear: a top-up rides on tick()'s periodic save, never a write of its own
TEST(selfcare, makes_no_save_of_its_own) {
  Pet p;
  makeQuietPet(p);  // hatching saved food 80
  setStats(p, 20, 80, 80, 100);
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 1);
  CHECK_EQ(p.fullness, (uint8_t)50);
  Preferences raw;
  raw.begin("tamapoke", true);
  CHECK_MSG(raw.getUChar("full", 0) == 80, "the top-up is not written to flash by itself");
  raw.end();
  mockClearForcedRandom();
}

// a heart or the eating animation would wake the screen and restart the idle time
TEST(selfcare, is_silent) {
  Pet p;
  makeQuietPet(p);
  setStats(p, 20, 80, 80, 100);
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 1);
  CHECK_EQ(p.fullness, (uint8_t)50);
  CHECK(!p.eating());
  CHECK(!p.showHeart());
  mockClearForcedRandom();
}

TEST(selfcare, off_while_asleep_and_back_after_waking) {
  Pet p;
  makeQuietPet(p);
  p.toggleLight();  // asleep
  setStats(p, 20, 80, 80, 100);  // under the sleep floor of 30: stays 20
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 5);
  CHECK_EQ(p.fullness, (uint8_t)20);
  CHECK(p.autoWake());  // bedtime ends; still untouched and on USB
  advance(p, 1);
  CHECK_EQ(p.fullness, (uint8_t)50);
  mockClearForcedRandom();
}

TEST(selfcare, off_as_an_egg) {
  mockNvsReset();
  mockSetMillis(0);
  Pet p;
  p.begin();
  p.chooseStarter(4);  // an egg that hatches at 3 minutes
  setStats(p, 20, 20, 20, 20);
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 2);
  CHECK(p.isEgg());
  CHECK_EQ(p.fullness, (uint8_t)20);
}

TEST(selfcare, off_during_a_ceremony) {
  Pet p;
  makeQuietPet(p);
  setStats(p, 20, 20, 20, 20);
  p.setCharger(true, true, LONG_IDLE);
  mockAdvanceMillis(PET_TICK_MS - 5);
  p.update(millis());
  p.release();  // the farewell scene runs for CEREMONY_MS
  mockAdvanceMillis(5);  // the minute ticks during the scene
  p.update(millis());
  CHECK(p.ceremony != CER_NONE);
  CHECK_EQ(p.fullness, (uint8_t)20);
  mockClearForcedRandom();
}

TEST(selfcare, never_in_the_powered_off_catch_up) {
  Pet p;
  makeQuietPet(p);
  setStats(p, 80, 80, 80, 100);
  p.setCharger(true, true, LONG_IDLE);
  p.setClock(1000u * 86400);
  p.syncClock(1000u * 86400 + 600 * 60);  // 600 minutes powered off
  CHECK_EQ(p.fullness, (uint8_t)15);  // the catch-up floor, untouched
  CHECK_EQ(p.hygiene, (uint8_t)15);
  advance(p, 1);  // the first live minute does self-care
  CHECK_EQ(p.fullness, (uint8_t)50);
  mockClearForcedRandom();
}

TEST(selfcare, a_need_it_lifts_this_minute_is_not_a_slip_up) {
  Pet p;
  makeQuietPet(p);
  p.careMistakes = 2;
  setStats(p, 11, 80, 80, 100);  // food 11 -> 9 this minute: a slip-up without self-care
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 1);
  CHECK_EQ(p.fullness, (uint8_t)50);
  CHECK_MSG(p.careMistakes == 2, "no new slip-up, and the old ones stay");
  mockClearForcedRandom();
}

TEST(selfcare, a_run_away_cannot_build_on_the_charger) {
  Pet p;
  makeQuietPet(p);
  p.dbgRunawayReady();  // everything at 0 for an hour
  CHECK(p.canRunawayNow());
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 1);
  CHECK_EQ(p.fullness, (uint8_t)50);
  CHECK(!p.canRunawayNow());
  mockClearForcedRandom();
}

TEST(selfcare, every_minute_in_relaxed_pace_too) {
  Pet p;
  makeQuietPet(p);
  p.setPace(PACE_RELAXED);  // no drain on odd minutes
  setStats(p, 39, 80, 80, 100);
  p.setCharger(true, true, LONG_IDLE);
  advance(p, 1);  // minute 1: odd, nothing drains
  CHECK_EQ(p.joy, (uint8_t)80);
  CHECK_EQ(p.fullness, (uint8_t)50);
  mockClearForcedRandom();
}

// ---------------------------------------------------------------- "ready to evolve" chime
TEST(evochime, sounds_when_the_button_appears) {
  EvoChime c;
  CHECK(!c.update(false, 4, 16, false));
  CHECK(c.update(true, 4, 16, false));
  CHECK_MSG(!c.update(true, 4, 16, false), "staying ready doesn't chime again");
}

TEST(evochime, once_per_level) {
  EvoChime c;
  CHECK(c.update(true, 4, 16, false));
  CHECK(!c.update(false, 4, 16, false));
  CHECK_MSG(!c.update(true, 4, 16, false), "back at the same level: no second chime");
}

TEST(evochime, again_at_a_new_level) {
  EvoChime c;
  CHECK(c.update(true, 4, 16, false));
  CHECK(!c.update(false, 4, 16, false));  // e.g. "keep this form" until the next level
  CHECK(c.update(true, 4, 17, false));
}

// the sketch keeps one EvoChime from boot: the next pet starts again at level 1, and
// every new pet is an egg (species -1) first, which the loop sees on its way
TEST(evochime, a_new_pet_chimes_at_the_same_level_again) {
  EvoChime c;
  CHECK(c.update(true, 4, 16, false));
  CHECK(!c.update(false, 4, 16, false));  // released with the button still showing
  CHECK(!c.update(false, -1, 1, false));  // the egg
  for (uint16_t lv = 1; lv < 16; lv++) CHECK(!c.update(false, 4, lv, false));  // the new pet grows
  CHECK(c.update(true, 4, 16, false));
}

// evolve() while the button was showing and then losing it again: the new form is
// ready at the level the old one chimed at, and that is a new button
TEST(evochime, a_new_form_chimes_at_the_same_level) {
  EvoChime c;
  CHECK(c.update(true, 4, 40, false));
  CHECK(!c.update(false, 4, 40, false));  // a need dips under 40
  CHECK(c.update(true, 5, 40, false));
}

TEST(evochime, needs_the_button_to_appear) {
  EvoChime c;
  CHECK(c.update(true, 4, 16, false));
  CHECK_MSG(!c.update(true, 4, 17, false), "still ready at the next level: no chime, as before");
  CHECK_MSG(!c.update(true, 5, 17, false), "a new form that is ready at once: the button never appeared");
}

// The case that needed it: a pet at its evolution level left on the charger.
TEST(evochime, a_ready_pet_on_the_charger_chimes_once) {
  Pet p;
  makePet(p, 4);  // Charmander evolves at level 16
  p.ageMinutes = 15 * MINUTES_PER_LEVEL;  // level 16 for the next hour
  p.setCharger(true, true, LONG_IDLE);
  EvoChime c;
  bool was = false;
  int chimes = 0, appearances = 0;
  for (int m = 0; m < MINUTES_PER_LEVEL - 1; m++) {
    advance(p, 1);
    bool ready = p.wantEvolveButton();
    if (ready && !was) appearances++;
    was = ready;
    if (c.update(ready, p.speciesId, p.level(), false)) chimes++;
  }
  CHECK_EQ(p.level(), (uint16_t)16);
  CHECK_MSG(appearances >= 3, "self-care brings the button back several times an hour");
  CHECK_EQ(chimes, 1);
}

// ---------------------------------------------------------------- quiet hours (#44)
// The evolve chime, the run-away warning, the hourly level-up and a medal that time earned
// are sounds nobody asked for. In quiet hours (attnQuietAt, test_attention.cpp: the bedtime
// window, or 22:00-07:00 with bedtime off, and a clock that is stale or never set) each one
// that comes due is spent silently and does not sound later. The screen is unchanged.
int mockLastSfx();  // test/shim/shim.cpp: the last sound that would have played
uint32_t mockSfxCount();

// a fresh EvoChime is also the sketch's after a reboot
TEST(quietalerts, evolve_chime_due_in_quiet_hours_is_spent_silently) {
  EvoChime c;
  CHECK_MSG(!c.update(true, 4, 16, true), "the button appears in quiet hours: no chime");
  CHECK(c.quieted);
  CHECK(!c.update(true, 4, 16, true));  // still up, still quiet hours
  CHECK_MSG(!c.quieted, "only the pass it came due on is a silent one (one serial line, not one a pass)");
  CHECK_MSG(!c.update(true, 4, 16, false), "quiet hours end with the button still there: no chime then");
  CHECK(!c.quieted);
  CHECK(!c.update(false, 4, 16, false));  // a need dips under 40
  CHECK_MSG(!c.update(true, 4, 16, false), "back at the same level: the silent one counted");
  CHECK(!c.update(false, 4, 16, false));
  CHECK_MSG(c.update(true, 4, 17, false), "the next level chimes as usual");
  CHECK(!c.quieted);
}

// The pass where quiet hours end is also the one where the age reaches the next level,
// with the button up all along: it did not appear, so no chime (needs_the_button_to_appear)
TEST(quietalerts, evolve_chime_spent_in_quiet_hours_is_not_replayed_at_the_next_level) {
  EvoChime c;
  CHECK(!c.update(true, 4, 16, true));
  CHECK_MSG(!c.update(true, 4, 17, false), "quiet hours end at level 17, the button still up: no chime");
}

TEST(quietalerts, run_away_warning_sounds_once_each_time_the_button_appears) {
  RunawayWarn w;
  CHECK(!w.update(false, false));
  CHECK(w.update(true, false));
  CHECK(!w.quieted);
  CHECK_MSG(!w.update(true, false), "the button stays: no second warning");
  CHECK(!w.update(false, false));  // a single care saves the pet
  CHECK_MSG(w.update(true, false), "an hour of neglect again: a new warning");
}

TEST(quietalerts, run_away_warning_due_in_quiet_hours_is_spent_silently) {
  RunawayWarn w;
  CHECK_MSG(!w.update(true, true), "the button appears in quiet hours: no warning");
  CHECK(w.quieted);
  CHECK(!w.update(true, true));  // still up, still quiet hours
  CHECK_MSG(!w.quieted, "only the pass it came due on is a silent one (one serial line, not one a pass)");
  CHECK_MSG(!w.update(true, false), "quiet hours end with the button still there: no warning then");
  CHECK(!w.quieted);
  CHECK(!w.update(false, false));
  CHECK_MSG(w.update(true, false), "a care, then an hour of neglect again: it warns as usual");
}

// an awake pet with the sound on; the shim's sfxPlay() counts what would have played
static void awakeWithSound(Pet &p) {
  makePet(p, 4);  // Charmander: not a final form, so no medal of its own
  audioSetEnabled(true);
  audioSetSleeping(false);
  CHECK(!p.sleeping);
}

TEST(quietalerts, the_hourly_level_up_is_silent_in_quiet_hours) {
  Pet p;
  awakeWithSound(p);
  p.ageMinutes = MINUTES_PER_LEVEL - 3;
  p.quietHours = true;
  uint32_t n = mockSfxCount();
  advance(p, 3);
  CHECK_EQ(p.level(), (uint16_t)2);
  CHECK_EQ(mockSfxCount(), n);
  p.quietHours = false;
  advance(p, MINUTES_PER_LEVEL);
  CHECK_EQ(p.level(), (uint16_t)3);
  CHECK_EQ(mockSfxCount(), n + 1);
  CHECK_EQ(mockLastSfx(), (int)SFX_LEVEL);
}

// "fit" (no weight left at level 5 or more, no slip-ups) is earned by the minute tick
// burning the weight off: time earns it, nobody asked for it
static void justBeforeFit(Pet &p) {
  awakeWithSound(p);
  p.ageMinutes = 5 * MINUTES_PER_LEVEL + 1;  // level 6; the weight burns at 303
  p.weight = 1;
  p.careMistakes = 0;
  setStats(p, 90, 90, 90, 90);
}

TEST(quietalerts, a_medal_time_earned_is_silent_in_quiet_hours_and_still_earned) {
  Pet p;
  justBeforeFit(p);
  p.quietHours = true;
  uint32_t n = mockSfxCount();
  advance(p, 2);
  CHECK(p.hasMedal(MED_FIT));
  CHECK_MSG(p.showMedal(), "the medal banner shows as usual");
  CHECK_EQ(mockSfxCount(), n);
  // level 10 by age at night: the level-up and the medal, both silent
  p.ageMinutes = 9 * MINUTES_PER_LEVEL - 1;
  advance(p, 1);
  CHECK(p.hasMedal(MED_LV10));
  CHECK_EQ(mockSfxCount(), n);

  Pet q;
  justBeforeFit(q);  // the same in the daytime: the medal sounds
  n = mockSfxCount();
  advance(q, 2);
  CHECK(q.hasMedal(MED_FIT));
  CHECK_EQ(mockSfxCount(), n + 1);
  CHECK_EQ(mockLastSfx(), (int)SFX_MEDAL);
}

// The player feeds the favourite berry: the medal is theirs. With the care of the day
// already counted (here: no clock), feedBerry() leaves it to the next tick to notice, and
// it still sounds in quiet hours, as it does when a tap earns a medal straight away.
TEST(quietalerts, a_medal_the_players_care_earned_still_sounds_in_quiet_hours) {
  Pet p;
  awakeWithSound(p);
  p.quietHours = true;
  uint8_t fav = 0;
  while (!p.lovesBerry(fav)) fav++;
  uint32_t n = mockSfxCount();
  p.feedBerry(fav);
  CHECK(p.berryKnown);
  advance(p, 1);
  CHECK(p.hasMedal(MED_BERRY));
  CHECK_EQ(mockSfxCount(), n + 1);
  CHECK_EQ(mockLastSfx(), (int)SFX_MEDAL);
}

// Exercise burns weight too, so a "fit" the player earns by playing or training is the
// player's medal: it sounds in quiet hours. Only a tick that notices time's own medal is
// silent. With no clock the day's care is not counted and the tick would notice it a minute
// later; with a clock and the day's first care, registerCare() gains it straight away.
static void exerciseAtNight(Pet &p) {
  awakeWithSound(p);
  p.ageMinutes = 5 * MINUTES_PER_LEVEL + 1;  // level 6
  p.weight = 2;
  p.careMistakes = 0;
  setStats(p, 90, 90, 90, 90);
  p.quietHours = true;
}

TEST(quietalerts, a_fit_earned_by_training_sounds_in_quiet_hours_without_a_clock) {
  Pet p;
  exerciseAtNight(p);
  uint32_t n = mockSfxCount();
  p.trainStrength(12);  // burns 24: no weight left
  CHECK_EQ(p.weight, (uint8_t)0);
  advance(p, 1);
  CHECK(p.hasMedal(MED_FIT));
  CHECK_EQ(mockSfxCount(), n + 1);
  CHECK_EQ(mockLastSfx(), (int)SFX_MEDAL);
}

TEST(quietalerts, a_fit_earned_by_training_sounds_in_quiet_hours_on_the_days_first_care) {
  Pet p;
  exerciseAtNight(p);
  p.lastSeenEpoch = 100u * 86400 + 23 * 3600;  // 23:00 of a known day
  p.lastCareDay = 0;
  uint32_t n = mockSfxCount();
  p.trainStrength(12);
  CHECK_EQ(p.weight, (uint8_t)0);
  CHECK_EQ(p.lastCareDay, (uint32_t)100);  // the care counted
  CHECK(p.hasMedal(MED_FIT));
  advance(p, 1);
  CHECK_EQ(mockSfxCount(), n + 1);
  CHECK_EQ(mockLastSfx(), (int)SFX_MEDAL);
}

TEST(quietalerts, a_fit_earned_by_playing_a_game_sounds_in_quiet_hours) {
  Pet p;
  exerciseAtNight(p);
  uint32_t n = mockSfxCount();
  p.playGame(GAME_KEEPY, 5);  // pays 5 points: burns 10
  CHECK_EQ(p.weight, (uint8_t)0);
  advance(p, 1);
  CHECK(p.hasMedal(MED_FIT));
  CHECK_EQ(mockSfxCount(), n + 1);
  CHECK_EQ(mockLastSfx(), (int)SFX_MEDAL);
}

// An IN SHAPE that exercise completes is gained by the next tick, as before #44, not inside
// playGame() or trainStrength(): the game's result screen would hide its banner (4 s).
TEST(quietalerts, an_exercise_fit_is_gained_by_the_next_tick_not_inside_the_game) {
  Pet p;
  exerciseAtNight(p);
  p.trainStrength(12);
  CHECK_EQ(p.weight, (uint8_t)0);
  CHECK_MSG(!p.hasMedal(MED_FIT), "not inside trainStrength()");
  CHECK(!p.showMedal());
  advance(p, 1);
  CHECK(p.hasMedal(MED_FIT));
  CHECK_MSG(p.showMedal(), "the banner starts with the tick, on the creature's screen");

  Pet q;
  exerciseAtNight(q);
  q.playGame(GAME_KEEPY, 5);
  CHECK_EQ(q.weight, (uint8_t)0);
  CHECK_MSG(!q.hasMedal(MED_FIT), "not inside playGame()");
  advance(q, 1);
  CHECK(q.hasMedal(MED_FIT));
}

// Only the one tick right after the exercise counts as the player's: an hour later, the age's
// level 10 at night is silent again
TEST(quietalerts, only_the_tick_right_after_exercise_counts_as_the_players) {
  Pet p;
  exerciseAtNight(p);
  p.weight = 50;  // the exercise (24) leaves some: no IN SHAPE
  p.trainStrength(12);
  advance(p, 1);
  CHECK(!p.hasMedal(MED_FIT));
  p.ageMinutes = 9 * MINUTES_PER_LEVEL - 1;
  uint32_t n = mockSfxCount();
  advance(p, 1);
  CHECK(p.hasMedal(MED_LV10));
  CHECK_EQ(mockSfxCount(), n);
}

// One tick that gains a medal of time (level 10) and one of the player's care (the favourite
// berry, noticed a minute after the feeding): the player's makes the sound, the level-up
// stays silent.
TEST(quietalerts, a_tick_gaining_a_time_medal_and_a_players_one_still_sounds) {
  Pet p;
  awakeWithSound(p);
  p.quietHours = true;
  uint8_t fav = 0;
  while (!p.lovesBerry(fav)) fav++;
  p.feedBerry(fav);
  CHECK(p.berryKnown);
  CHECK(!p.hasMedal(MED_BERRY));  // no clock: nothing noticed yet
  p.ageMinutes = 9 * MINUTES_PER_LEVEL - 1;
  uint32_t n = mockSfxCount();
  advance(p, 1);
  CHECK(p.hasMedal(MED_LV10));
  CHECK(p.hasMedal(MED_BERRY));
  CHECK_EQ(mockSfxCount(), n + 1);
  CHECK_EQ(mockLastSfx(), (int)SFX_MEDAL);
}

// The stamina ribbon comes from steady care, counted by the minute tick (#76): with nobody
// there it is a medal of time, silent in quiet hours; its banner shows anyway.
static void oneMinuteShortOfStamina(Pet &p) {
  awakeWithSound(p);
  p.trDef = RIBBON_STA - 1;
  for (int m = 0; m < STAMINA_CARE_MIN - 1; m++) {
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  CHECK_EQ(p.trDef, (uint8_t)(RIBBON_STA - 1));
  setStats(p, 100, 100, 100, 100);
}

TEST(quietalerts, the_stamina_ribbon_is_silent_in_quiet_hours_and_still_earned) {
  Pet p;
  oneMinuteShortOfStamina(p);
  p.quietHours = true;
  uint32_t n = mockSfxCount();
  advance(p, 1);
  CHECK_EQ(p.trDef, (uint8_t)RIBBON_STA);
  CHECK(p.hasMedal(MED_STA));
  CHECK_MSG(p.showMedal(), "the banner shows as usual");
  CHECK_EQ(mockSfxCount(), n);

  Pet q;
  oneMinuteShortOfStamina(q);  // the same in the daytime: it sounds
  n = mockSfxCount();
  advance(q, 1);
  CHECK(q.hasMedal(MED_STA));
  CHECK_EQ(mockSfxCount(), n + 1);
  CHECK_EQ(mockLastSfx(), (int)SFX_MEDAL);
}

// A ribbon an older save already qualifies for, noticed by the first tick after the update
// with nobody there: silent in quiet hours too.
TEST(quietalerts, an_older_pets_ribbon_noticed_by_the_tick_is_silent_in_quiet_hours) {
  Pet p;
  awakeWithSound(p);
  p.trAtk = 50;
  p.trSpe = 50;
  p.quietHours = true;
  uint32_t n = mockSfxCount();
  advance(p, 1);
  CHECK(p.hasMedal(MED_STR));
  CHECK(p.hasMedal(MED_SPD));
  CHECK_EQ(mockSfxCount(), n);
}

// A ribbon the player's exercise completes is theirs: it sounds in quiet hours, whether the
// end of the game's screen (exerciseOver) or the next tick gains it.
TEST(quietalerts, a_ribbon_earned_by_exercise_sounds_in_quiet_hours) {
  Pet p;
  awakeWithSound(p);
  p.trSpe = RIBBON_SPD - 3;
  p.quietHours = true;
  uint32_t n = mockSfxCount();
  p.playGame(GAME_KEEPY, GAME_PAR[GAME_KEEPY]);  // +3: at the goal
  CHECK_MSG(!p.hasMedal(MED_SPD), "not inside playGame(): its result screen is up");
  p.exerciseOver();
  CHECK(p.hasMedal(MED_SPD));
  CHECK(p.showMedal());
  CHECK_EQ(mockSfxCount(), n + 1);
  CHECK_EQ(mockLastSfx(), (int)SFX_MEDAL);

  Pet q;  // the punch's, gained by the next tick (the screen's end not seen)
  awakeWithSound(q);
  q.trAtk = RIBBON_STR - 5;
  q.quietHours = true;
  n = mockSfxCount();
  q.trainStrength(7);  // 7 punches: +5
  advance(q, 1);
  CHECK(q.hasMedal(MED_STR));
  CHECK_EQ(mockSfxCount(), n + 1);
}

// Bedtime closes a game and puts the pet to sleep in one pass (bedApply: autoSleep()): the
// ribbon its exercise completed is gained without SFX_MEDAL, as checkMedals() plays nothing
// while the pet sleeps.
TEST(quietalerts, a_ribbon_gained_as_bedtime_closes_the_game_is_silent) {
  Pet p;
  awakeWithSound(p);
  p.trSpe = RIBBON_SPD - 3;
  p.quietHours = true;
  p.playGame(GAME_KEEPY, GAME_PAR[GAME_KEEPY]);  // +3: at the goal
  CHECK(p.autoSleep());
  uint32_t n = mockSfxCount();
  p.exerciseOver();
  CHECK_EQ(mockSfxCount(), n);
  CHECK_MSG(p.hasMedal(MED_SPD), "gained silently");
}

// ---------------------------------------------------------------- the exercise screen (#69, #76)
// exerciseOver(): as a minigame or an exercise closes, the medals its exercise completed.
TEST(exercise, the_end_of_the_screen_gains_what_the_exercise_completed) {
  Pet p;
  makePet(p, 4);
  p.ageMinutes = 5 * MINUTES_PER_LEVEL + 1;  // level 6: IN SHAPE needs only the weight gone
  p.weight = 2;
  p.trSpe = RIBBON_SPD - 3;
  p.playGame(GAME_KEEPY, GAME_PAR[GAME_KEEPY]);
  CHECK(!p.hasMedal(MED_FIT));
  CHECK(!p.hasMedal(MED_SPD));
  p.exerciseOver();
  CHECK(p.hasMedal(MED_FIT));
  CHECK(p.hasMedal(MED_SPD));
  CHECK(p.showMedal());
}

// Without an exercise that paid (left by the hold, or nothing since the last tick), the end
// of the screen gains nothing: a goal reached some other way waits for the tick.
TEST(exercise, a_screen_that_paid_nothing_gains_nothing) {
  Pet p;
  makePet(p, 4);
  p.trAtk = RIBBON_STR;
  p.exerciseOver();
  CHECK(!p.hasMedal(MED_STR));
  CHECK(!p.showMedal());
  advance(p, 1);
  CHECK(p.hasMedal(MED_STR));
}

// #69: a medal or streak banner that came up under a minigame's or an exercise's result waits for
// it to close, then shows its whole time on the creature's screen. #83: the milestone first,
// the medal it came with after it, each for its whole time.
TEST(exercise, a_banner_under_the_screen_waits_for_it_to_close) {
  Pet p;
  makePet(p, 4);
  p.lastSeenEpoch = 100u * 86400 + 12 * 3600;
  p.lastCareDay = 99;  // yesterday: today's first care continues the streak
  p.streak = 6;        // ...to 7: the 7 STREAK medal and the 7-day milestone
  p.playGame(GAME_KEEPY, GAME_PAR[GAME_KEEPY]);
  CHECK(p.hasMedal(MED_STREAK7));
  CHECK_MSG(p.showMilestone(), "the milestone first");
  CHECK_MSG(!p.showMedal(), "the medal waits behind it");
  ExerciseScreen screen;
  for (int i = 0; i < 100; i++) {  // 10 s of result screen, a pass every 100 ms
    mockAdvanceMillis(100);
    screen.update(true, p);
  }
  CHECK_MSG(p.showMilestone(), "the milestone waited");
  CHECK_MSG(!p.showMedal(), "and the medal behind it");
  screen.update(false, p);  // the creature's screen is back
  expectBanner(p, BANNER_STREAK, MILESTONE_BANNER_MS, "all of the milestone's time on the creature's screen");
  expectBanner(p, bitOf(MED_STREAK7), MEDAL_BANNER_MS, "then all of the medal's");
  expectBanner(p, BANNER_ACC + ACC_FLOWERS, MEDAL_BANNER_MS, "then the Flowers it earned (#116)");
  CHECK(p.bannerShown() == BANNER_NONE);
}

// ExerciseScreen calls exerciseOver() once, on the pass the screen has closed.
TEST(exercise, the_screen_closing_gains_the_ribbon_once) {
  Pet p;
  makePet(p, 4);
  p.trSpe = RIBBON_SPD - 3;
  ExerciseScreen screen;
  screen.update(false, p);
  screen.update(true, p);
  p.playGame(GAME_KEEPY, GAME_PAR[GAME_KEEPY]);
  screen.update(true, p);
  CHECK(!p.hasMedal(MED_SPD));
  screen.update(false, p);
  CHECK(p.hasMedal(MED_SPD));
  uint16_t total = p.totalMedals;
  screen.update(false, p);
  CHECK_EQ(p.totalMedals, total);
  // a second pass of the closed screen must not call exerciseOver() again: the exercise is
  // still flagged (no tick ran), so a goal reached meanwhile would be gained early
  p.trAtk = RIBBON_STR;
  screen.update(false, p);
  CHECK_MSG(!p.hasMedal(MED_STR), "exerciseOver() ran on a second closed pass");
}

// Holding never starts a banner that wasn't showing
TEST(exercise, holding_starts_no_banner) {
  Pet p;
  makePet(p, 4);
  ExerciseScreen screen;
  for (int i = 0; i < 10; i++) screen.update(true, p);
  CHECK(!p.showMedal());
  CHECK(!p.showMilestone());
}

// #88: an exercise that the screen going dark ended closes on a dark screen. The ribbon it
// completed is gained at once, and its banner waits until the screen is back on, then shows
// its whole time on the creature's screen.
TEST(exercise, a_screen_closed_dark_keeps_its_banner_until_the_screen_is_back) {
  Pet p;
  makePet(p, 4);
  p.trAtk = RIBBON_STR - 1;
  ExerciseScreen screen;
  screen.update(true, p, false);
  p.trainStrength(2);  // +1: the ribbon's goal; then PWR ends the session
  screen.update(false, p, true);
  CHECK_MSG(p.hasMedal(MED_STR), "gained as the screen closes");
  for (int i = 0; i < 600; i++) {  // a minute in the dark, a pass every 100 ms
    mockAdvanceMillis(100);
    screen.update(false, p, true);
  }
  CHECK_MSG(p.showMedal(), "the banner waited for the screen");
  screen.update(false, p, false);  // PWR: the screen is back on
  mockAdvanceMillis(MEDAL_BANNER_MS - 100);
  screen.update(false, p, false);
  CHECK_MSG(p.showMedal(), "all of its time on the creature's screen");
  mockAdvanceMillis(200);
  screen.update(false, p, false);
  CHECK_MSG(!p.showMedal(), "and no longer");
}

// Only a screen that closed dark holds: one that closed lit lets its banner run out even if
// the screen goes dark later, and once back on, a later dark spell holds nothing again.
TEST(exercise, only_a_screen_that_closed_dark_holds_until_it_is_back) {
  Pet p;
  makePet(p, 4);
  p.trAtk = RIBBON_STR - 1;
  ExerciseScreen lit;
  lit.update(true, p, false);
  p.trainStrength(2);
  lit.update(false, p, false);  // closed lit: the banner starts now
  CHECK(p.showMedal());
  mockAdvanceMillis(100);
  for (int i = 0; i < 50; i++) {  // the screen goes dark afterwards
    mockAdvanceMillis(100);
    lit.update(false, p, true);
  }
  CHECK_MSG(!p.showMedal(), "a screen closed lit holds nothing in the dark");

  Pet q;
  makePet(q, 4);
  q.trAtk = RIBBON_STR - 1;
  ExerciseScreen dark;
  dark.update(true, q, false);
  q.trainStrength(2);
  dark.update(false, q, true);  // closed dark: held
  dark.update(false, q, false);  // back on: released
  mockAdvanceMillis(100);
  for (int i = 0; i < 50; i++) {  // dark again: the hold is over
    mockAdvanceMillis(100);
    dark.update(false, q, true);
  }
  CHECK_MSG(!q.showMedal(), "the screen back on ended the hold");
}

// The pet's own animations end for good (Pet::update): a deadline left behind would read as
// ahead again once millis() is more than 2^31 ms past it (~25 days of uptime with no new
// evolution, plush play, warm milk, meal or heart), and careBusy() would hold back bedtime and
// the attention calls for weeks.
TEST(timers, an_ended_animation_stays_ended_after_25_days_of_uptime) {
  Pet p;
  makePet(p, 4);
  setStats(p, 60, 50, 60, 70);
  p.ageMinutes = 20 * 60;
  p.evolve();         // the evolution's animation
  p.playToy();        // the plush (and its heart)
  p.giveMilk();       // warm milk: drinking, eating
  CHECK(p.evolving() && p.playingToy() && p.drinkingMilk() && p.eating() && p.showHeart());
  mockAdvanceMillis(100);
  p.update(millis());  // a pass while they run leaves them alone
  CHECK_MSG(p.evolving() && p.playingToy() && p.drinkingMilk() && p.eating() && p.showHeart(),
            "a running animation is left alone");
  mockAdvanceMillis(EVOLVE_ANIM_MS + TOY_ANIM_MS + EAT_ANIM_MS + HEART_MS);
  p.update(millis());  // the loop's next pass
  mockAdvanceMillis(25u * 86400 * 1000);  // more than 2^31 ms
  CHECK_MSG(!p.evolving(), "the evolution came back");
  CHECK_MSG(!p.playingToy(), "the plush play came back");
  CHECK_MSG(!p.drinkingMilk(), "the warm milk came back");
  CHECK_MSG(!p.eating(), "the meal came back");
  CHECK_MSG(!p.showHeart(), "the heart came back");
}

// ---------------------------------------------------------------- idle perks (#22)
// A finished doze or ball game: a little energy or joy (capped per hour by idleplan.h's
// IdlePerks). Never lowers a stat, and is not the player's care.

TEST(idleperk, adds_a_little_energy_or_joy_up_to_100) {
  Pet p;
  makePet(p, 4);
  setStats(p, 60, 50, 40, 70);
  p.idlePerk(3, 0);
  CHECK_EQ(p.energy, (uint8_t)43);
  CHECK_EQ(p.joy, (uint8_t)50);
  p.idlePerk(0, 3);
  CHECK_EQ(p.joy, (uint8_t)53);
  CHECK_EQ(p.energy, (uint8_t)43);
  CHECK_EQ(p.fullness, (uint8_t)60);  // nothing else moves
  CHECK_EQ(p.hygiene, (uint8_t)70);
  setStats(p, 60, 99, 98, 70);
  p.idlePerk(3, 3);
  CHECK_EQ(p.energy, (uint8_t)100);  // capped, never wraps
  CHECK_EQ(p.joy, (uint8_t)100);
  p.idlePerk(255, 255);
  CHECK_EQ(p.energy, (uint8_t)100);
  CHECK_EQ(p.joy, (uint8_t)100);
}

TEST(idleperk, nothing_asleep_as_an_egg_or_in_a_ceremony) {
  Pet p;
  makePet(p, 4);
  setStats(p, 60, 50, 40, 70);
  p.sleeping = true;
  p.idlePerk(3, 3);
  CHECK_EQ(p.energy, (uint8_t)40);
  CHECK_EQ(p.joy, (uint8_t)50);
  p.sleeping = false;
  p.startFarewell();
  p.idlePerk(3, 3);
  CHECK_EQ(p.energy, (uint8_t)40);
  CHECK_EQ(p.joy, (uint8_t)50);
  Pet e;
  mockNvsReset();
  mockSetMillis(0);
  e.begin();  // a first game: choosing a starter, then an egg
  uint8_t en = e.energy, jo = e.joy;
  e.idlePerk(3, 3);
  CHECK_EQ(e.energy, en);
  CHECK_EQ(e.joy, jo);
  e.chooseStarter(4);
  e.idlePerk(3, 3);
  CHECK(e.isEgg());
  CHECK_EQ(e.energy, en);
  CHECK_EQ(e.joy, jo);
}

TEST(idleperk, is_not_the_players_care) {
  Pet p;
  makePet(p, 4);
  p.lastSeenEpoch = 1794564000UL;  // a known day, so care would count
  setStats(p, 60, 50, 40, 70);
  uint16_t streak = p.streak;
  uint32_t careDay = p.lastCareDay;
  uint8_t bond = p.bond;
  mockNvsReset();  // an empty store: any write by idlePerk() would show up as keys
  p.idlePerk(3, 3);
  CHECK_EQ(p.streak, streak);
  CHECK_EQ(p.lastCareDay, careDay);
  CHECK_EQ(p.bond, bond);
  CHECK(!p.showHeart());  // a heart would wake the screen and read as a touch
  CHECK(!p.savePending());
  CHECK_EQ(mockNvsKeyCount("tamapoke"), (size_t)0);  // no save of its own: the periodic one keeps it
}
