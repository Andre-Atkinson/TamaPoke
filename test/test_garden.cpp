// The berry garden (#115, garden.h): the owner's choices of 2026-11-17. A plant grows only
// while watered (sprout 4 h, leaves 12 h, flower 24 h, ripe 40 h of water); a tap or the rain
// where the pet is waters it for 12 h; dry a day it warns, dry two it wilts, ripe it never
// does; three berries a plant. Only a trusted clock moves it on, at most 7 days at a time,
// and a clock set or a stale clock never grows, dries or wilts it. The pet's save keeps it,
// range-checked, through an ending.
#include "framework.h"
#include "shim/Arduino.h"
#include "../garden.h"
#include "../bubble.h"
#include "../pet.h"
#include <string>

static const uint32_t H = 3600, DAY = 86400;
// a Saturday in 2026, 00:00: a block boundary and well inside the clock's range
static const uint32_t T0 = 1767225600UL + 200 * DAY;

static bool watersAt(uint8_t home, uint32_t t) {
  return gardenWeatherWaters(wxKind(travelPlace(home, t), t / WX_BLOCK_S));
}
// the first block start from `from` on where `len` seconds go by with no rain, storm or snow
// where a pet from `home` is
static uint32_t findDry(uint8_t home, uint32_t from, uint32_t len) {
  for (uint32_t s = from - from % WX_BLOCK_S;; s += WX_BLOCK_S) {
    bool dry = true;
    for (uint32_t t = s; t < s + len && dry; t += WX_BLOCK_S) dry = !watersAt(home, t);
    if (dry) return s;
  }
}
// the first block start from `from` on where it rains (a storm or snow) where the pet is
static uint32_t findRain(uint8_t home, uint32_t from) {
  for (uint32_t s = from - from % WX_BLOCK_S;; s += WX_BLOCK_S)
    if (watersAt(home, s)) return s;
}

static bool same(const Garden &a, const Garden &b) {
  return a.plant == b.plant && a.berry == b.berry && a.left == b.left && a.grown == b.grown && a.wet == b.wet &&
         a.dry == b.dry;
}

static Garden planted(uint8_t berry = 0) {
  Garden g;
  uint8_t picked = 0xFF;
  CHECK_EQ(g.tap(berry, true, picked), GTAP_PLANTED);
  CHECK_EQ(picked, (uint8_t)0xFF);
  return g;
}

// ---------------------------------------------------------------- growing
TEST(garden, a_tap_on_the_empty_pot_plants_a_watered_seed_of_the_rolled_berry) {
  for (uint32_t roll : { 0u, 1u, 2u, 3u, 7u, 0x7fffffffu }) {
    Garden g = planted((uint8_t)0);
    g.clear();
    uint8_t picked = 0;
    CHECK_EQ(g.tap(roll, false, picked), GTAP_PLANTED);
    CHECK_EQ(g.berry, (uint8_t)(roll % 3));
    CHECK_EQ(g.stage(), (uint8_t)GST_SEED);
    CHECK_EQ(g.wet, GARDEN_WATER_S);
    CHECK_EQ(g.dry, 0u);
    CHECK_EQ(g.grown, 0u);
  }
}

TEST(garden, it_grows_only_while_watered_to_each_stage_at_its_hour) {
  Garden g = planted();
  g.pass(GARDEN_SPROUT_S - 1);
  CHECK_EQ(g.stage(), (uint8_t)GST_SEED);
  g.pass(1);
  CHECK_EQ(g.stage(), (uint8_t)GST_SPROUT);
  g.pass(GARDEN_WATER_S - GARDEN_SPROUT_S);  // the water runs out at 12 h: leaves
  CHECK_EQ(g.stage(), (uint8_t)GST_LEAVES);
  CHECK(!g.watered());
  g.pass(20 * H);  // dry: it waits
  CHECK_EQ(g.grown, GARDEN_LEAVES_S);
  CHECK_EQ(g.dry, 20 * H);
  uint8_t picked = 0;
  CHECK_EQ(g.tap(0, true, picked), GTAP_WATERED);
  CHECK_EQ(g.dry, 0u);
  g.pass(GARDEN_FLOWER_S - GARDEN_LEAVES_S);
  CHECK_EQ(g.stage(), (uint8_t)GST_FLOWER);
  CHECK_EQ(g.left, (uint8_t)0);
  g.water(GARDEN_WATER_S);
  g.water(GARDEN_WATER_S);
  g.pass(GARDEN_RIPE_S - GARDEN_FLOWER_S - 1);
  CHECK_MSG(!g.ripe(), "16 h of water after the flower: not yet (a watering lasts 12 h)");
  CHECK_EQ(g.grown, GARDEN_FLOWER_S + GARDEN_WATER_S);
  g.water(GARDEN_WATER_S);
  g.pass(GARDEN_RIPE_S - g.grown);
  CHECK(g.ripe());
  CHECK_EQ(g.stage(), (uint8_t)GST_RIPE);
  CHECK_EQ(g.left, (uint8_t)GARDEN_BERRIES);
  CHECK_EQ(g.grown, GARDEN_RIPE_S);
}

TEST(garden, a_watering_never_shortens_the_water_it_has) {
  Garden g = planted();
  g.water(GARDEN_WET_MAX_S + 5 * H);  // capped
  CHECK_EQ(g.wet, GARDEN_WET_MAX_S);
  uint8_t picked = 0;
  g.tap(0, true, picked);
  CHECK_EQ(g.wet, GARDEN_WET_MAX_S);
}

TEST(garden, dry_a_day_it_warns_and_dry_two_days_it_wilts) {
  Garden g = planted();
  g.pass(GARDEN_WATER_S);  // the seed's water runs out
  g.pass(GARDEN_WARN_S - 1);
  CHECK(!g.wiltingSoon());
  CHECK_EQ(g.say(), (uint8_t)GSAY_THIRSTY);
  g.pass(1);
  CHECK(g.wiltingSoon());
  CHECK_EQ(g.say(), (uint8_t)GSAY_WILTING);
  g.pass(GARDEN_WILT_S - GARDEN_WARN_S - 1);
  CHECK(g.growing());
  g.pass(1);
  CHECK_EQ(g.stage(), (uint8_t)GST_WITHERED);
  CHECK_EQ(g.say(), (uint8_t)GSAY_NONE);
  CHECK_EQ(g.grown, 0u);
  // one tap clears the stalk, the next plants
  uint8_t picked = 0;
  CHECK_EQ(g.tap(1, true, picked), GTAP_CLEARED);
  CHECK_EQ(g.stage(), (uint8_t)GST_EMPTY);
  CHECK_EQ(g.tap(1, true, picked), GTAP_PLANTED);
  CHECK_EQ(g.berry, (uint8_t)1);
}

TEST(garden, a_watering_puts_off_the_wilt_and_the_warning) {
  Garden g = planted();
  g.pass(GARDEN_WATER_S + 40 * H);
  CHECK(g.wiltingSoon());
  g.water(GARDEN_WATER_S);
  CHECK(!g.wiltingSoon());
  CHECK_EQ(g.say(), (uint8_t)GSAY_NONE);
  g.pass(GARDEN_WATER_S + GARDEN_WILT_S - 1);
  CHECK_MSG(g.growing(), "the dry run starts again from the watering");
}

TEST(garden, a_ripe_plant_never_wilts) {
  Garden g = planted();
  g.force(GST_RIPE, 0, 2);
  g.pass(30 * DAY);
  CHECK(g.ripe());
  CHECK_EQ(g.left, (uint8_t)GARDEN_BERRIES);
  CHECK_EQ(g.dry, 0u);
  CHECK_EQ(g.say(), (uint8_t)GSAY_RIPE);
  uint8_t picked = 0;
  CHECK_EQ(g.tap(0, true, picked), GTAP_PICKED);
  g.water(GARDEN_WATER_S);
  g.pass(3 * DAY);
  CHECK_MSG(g.left == 2, "the berries left stay as picked");
  // ripening part-way through a dry stretch: the dry time after it doesn't count
  Garden h = planted();
  h.grown = GARDEN_RIPE_S - H;
  h.wet = H;
  h.pass(GARDEN_WILT_S + DAY);
  CHECK(h.ripe());
}

// ---------------------------------------------------------------- picking
TEST(garden, three_berries_one_a_tap_and_then_the_pot_is_empty) {
  Garden g;
  g.force(GST_RIPE, 1, 1);
  uint8_t picked = 0xFF;
  CHECK_EQ(g.tap(9, false, picked), GTAP_WAIT);
  CHECK_EQ(picked, (uint8_t)0xFF);
  CHECK_EQ(g.left, (uint8_t)3);
  for (int i = 2; i >= 0; i--) {
    picked = 0xFF;
    CHECK_EQ(g.tap(9, true, picked), GTAP_PICKED);
    CHECK_EQ(picked, (uint8_t)1);
    CHECK_EQ(g.left, (uint8_t)i);
  }
  CHECK_EQ(g.stage(), (uint8_t)GST_EMPTY);
  CHECK_EQ(g.tap(5, true, picked), GTAP_PLANTED);
  CHECK_EQ(g.berry, (uint8_t)2);
}

// ---------------------------------------------------------------- the clock
TEST(garden, rain_storm_and_snow_water_it_and_nothing_else) {
  CHECK(gardenWeatherWaters(WX_RAIN));
  CHECK(gardenWeatherWaters(WX_STORM));
  CHECK(gardenWeatherWaters(WX_SNOW));
  CHECK(!gardenWeatherWaters(WX_CLEAR));
  CHECK(!gardenWeatherWaters(WX_CLOUDY));
  CHECK(!gardenWeatherWaters(WX_FOG));
}

TEST(garden, rain_where_the_pet_is_waters_it_through_the_block_and_12_hours) {
  for (uint8_t home = 0; home < PLACES; home++) {
    uint32_t r = findRain(home, T0 + 10 * H);
    Garden g = planted();
    g.wet = 0;
    g.dry = 5 * H;
    g.at = r + 60;
    g.settle(r + 120, home, true);
    CHECK_MSG(g.wet == WX_BLOCK_S - 60 + GARDEN_WATER_S - 60, "home " + std::to_string(home));
    CHECK_EQ(g.dry, 0u);
    CHECK_EQ(g.grown, 60u);
    CHECK_EQ(g.at, r + 120);
  }
}

TEST(garden, with_no_rain_it_dries) {
  uint32_t s = findDry(PLACE_VOLCANO, T0, 3 * DAY);
  Garden g = planted();
  g.at = s;
  g.settle(s + 2 * H, PLACE_VOLCANO, true);
  CHECK_EQ(g.grown, 2 * H);
  CHECK_EQ(g.wet, GARDEN_WATER_S - 2 * H);
  g.settle(s + GARDEN_WATER_S + GARDEN_WARN_S, PLACE_VOLCANO, true);
  CHECK_EQ(g.grown, GARDEN_WATER_S);
  CHECK_EQ(g.dry, GARDEN_WARN_S);
  CHECK(g.wiltingSoon());
  g.settle(s + GARDEN_WATER_S + GARDEN_WILT_S, PLACE_VOLCANO, true);
  CHECK_EQ(g.stage(), (uint8_t)GST_WITHERED);
}

// The rain is the travelling pet's: where travelPlace() puts it that day, not its home.
TEST(garden, the_rain_is_where_the_pet_travelled_that_day) {
  int found = 0;
  for (uint32_t t = T0; t < T0 + 200 * DAY && found < 3; t += WX_BLOCK_S) {
    uint8_t home = PLACE_VOLCANO, there = travelPlace(home, t);
    uint32_t b = t / WX_BLOCK_S;
    bool away = gardenWeatherWaters(wxKind(there, b)), atHome = gardenWeatherWaters(wxKind(home, b));
    if (there == home || away == atHome) continue;
    found++;
    Garden g = planted();
    g.wet = 0;
    g.dry = H;
    g.at = t;
    g.settle(t + 60, home, true);
    CHECK_MSG(g.watered() == away, "block " + std::to_string(b));
  }
  CHECK_EQ(found, 3);
}

// An egg stays in the meadow on screen (sceneToday() never travels it), so its pot gets the
// meadow's rain: settle(now, home, travels = false) follows `home`, not travelPlace().
TEST(garden, an_egg_that_does_not_travel_gets_the_rain_of_its_home) {
  int found = 0;
  for (uint32_t t = T0; t < T0 + 200 * DAY && found < 3; t += WX_BLOCK_S) {
    uint8_t there = travelPlace(PLACE_MEADOW, t);
    bool meadow = gardenWeatherWaters(wxKind(PLACE_MEADOW, t / WX_BLOCK_S));
    bool away = gardenWeatherWaters(wxKind(there, t / WX_BLOCK_S));
    if (there == PLACE_MEADOW || meadow == away) continue;
    found++;
    Garden stays = planted(), goes = planted();
    stays.wet = goes.wet = 0;
    stays.dry = goes.dry = H;
    stays.at = goes.at = t;
    stays.settle(t + 60, PLACE_MEADOW, false);
    goes.settle(t + 60, PLACE_MEADOW, true);
    CHECK_MSG(stays.watered() == meadow, "stays: block " + std::to_string(t / WX_BLOCK_S));
    CHECK_MSG(goes.watered() == away, "goes: block " + std::to_string(t / WX_BLOCK_S));
  }
  CHECK_EQ(found, 3);
}

TEST(garden, many_small_settles_add_up_to_one_big_one) {
  for (uint8_t home : { (uint8_t)PLACE_MEADOW, (uint8_t)PLACE_FOREST, (uint8_t)PLACE_SNOW }) {
    Garden a = planted(), b = planted();
    a.at = b.at = T0 + 1234;
    for (uint32_t t = T0 + 1234 + 30; t <= T0 + 1234 + 4 * DAY; t += 30) a.settle(t, home, true);
    b.settle(T0 + 1234 + 4 * DAY, home, true);
    CHECK_MSG(same(a, b), "home " + std::to_string(home));
    CHECK_EQ(a.at, b.at);
  }
}

TEST(garden, a_stale_clock_stands_it_still_and_the_first_trusted_time_only_starts_it) {
  Garden g = planted();
  g.at = T0;
  Garden before = g;
  g.settle(0, PLACE_MEADOW, true);  // stale
  CHECK(same(g, before));
  CHECK_EQ(g.at, 0u);
  g.settle(T0 + 5 * DAY, PLACE_MEADOW, true);  // set: it starts from here
  CHECK(same(g, before));
  CHECK_EQ(g.at, T0 + 5 * DAY);
  g.settle(T0 + 5 * DAY + H, PLACE_MEADOW, true);
  CHECK_MSG(g.grown >= H, "an hour of the seed's water counts");
}

TEST(garden, a_clock_that_goes_back_starts_it_afresh) {
  Garden g = planted();
  g.at = T0 + DAY;
  Garden before = g;
  g.settle(T0, PLACE_MEADOW, true);
  CHECK(same(g, before));
  CHECK_EQ(g.at, T0);
}

TEST(garden, the_rain_on_screen_waters_it_with_a_trusted_clock_only) {
  Garden g = planted();
  g.wet = 0;
  g.dry = 2 * H;
  g.rainSeen(0);
  CHECK(!g.watered());
  g.rainSeen(T0 + 3 * WX_BLOCK_S + 600);
  CHECK_EQ(g.wet, WX_BLOCK_S - 600 + GARDEN_WATER_S);
  CHECK_EQ(g.dry, 0u);
  Garden e;  // an empty pot: nothing to water
  e.rainSeen(T0);
  CHECK(!e.growing() && e.wet == 0);
}

// After a long time away only the last GARDEN_CATCHUP_S count. A plant nearly ripe and dry:
// rain in its first two days would ripen it, and the last seven days start with two dry ones
// that wilt it, so only the cap gives a wilted plant.
TEST(garden, it_catches_up_at_most_7_days) {
  const uint8_t home = PLACE_MEADOW;
  const uint32_t gap = 30 * DAY;
  uint32_t now = 0;
  for (uint32_t s = T0; s < T0 + 400 * DAY; s += WX_BLOCK_S) {
    uint32_t n = s + gap;
    bool early = false;
    for (uint32_t t = s; t < s + 2 * DAY && !early; t += WX_BLOCK_S) early = watersAt(home, t);
    if (!early || findDry(home, n - GARDEN_CATCHUP_S, 2 * DAY + WX_BLOCK_S) != n - GARDEN_CATCHUP_S) continue;
    now = n;
    break;
  }
  CHECK(now != 0);
  Garden g = planted();
  g.grown = GARDEN_RIPE_S - H;
  g.wet = 0;
  g.dry = 0;
  g.at = now - gap;
  g.settle(now, home, true);
  CHECK_EQ(g.stage(), (uint8_t)GST_WITHERED);
  CHECK_EQ(g.at, now);
  // ...and from 7 days back, the same
  Garden h = planted();
  h.grown = GARDEN_RIPE_S - H;
  h.wet = h.dry = 0;
  h.at = now - GARDEN_CATCHUP_S;
  h.settle(now, home, true);
  CHECK_EQ(h.stage(), (uint8_t)GST_WITHERED);
}

TEST(garden, the_home_given_is_the_one_counted_so_an_ending_counts_from_its_settle) {
  // the same stretch, two homes: each settle uses the home it is given
  for (uint32_t t = T0; t < T0 + 100 * DAY; t += WX_BLOCK_S) {
    bool m = watersAt(PLACE_MEADOW, t), v = watersAt(PLACE_VOLCANO, t);
    if (m == v) continue;
    Garden a = planted(), b = planted();
    a.wet = b.wet = 0;
    a.at = b.at = t;
    a.settle(t + 60, PLACE_MEADOW, true);
    b.settle(t + 60, PLACE_VOLCANO, true);
    CHECK_EQ(a.watered(), m);
    CHECK_EQ(b.watered(), v);
    return;
  }
  CHECK_MSG(false, "no block where the meadow and the volcano differ");
}

// ---------------------------------------------------------------- the save
TEST(garden, every_state_saves_and_loads_as_it_was) {
  for (uint8_t st = 0; st < GST_COUNT; st++)
    for (uint8_t moist = 0; moist < 3; moist++)
      for (uint8_t c = 0; c < 3; c++) {
        if (st == GST_RIPE && moist == 2) continue;
        Garden g;
        g.force(st, moist, c);
        g.at = T0 + st;
        Garden h;
        CHECK(h.load(g.packed(), g.grown, g.wet, g.dry, g.at, CLOCK_EPOCH_MIN, CLOCK_EPOCH_END));
        CHECK_MSG(same(g, h) && h.at == g.at, std::string(GARDEN_STAGE_KEYS[st]) + " " + std::to_string(moist));
      }
  Garden r;
  r.force(GST_RIPE, 1, 2);
  uint8_t picked = 0;
  r.tap(0, true, picked);  // two left
  Garden h;
  CHECK(h.load(r.packed(), r.grown, r.wet, r.dry, 0, CLOCK_EPOCH_MIN, CLOCK_EPOCH_END));
  CHECK_EQ(h.left, (uint8_t)2);
}

TEST(garden, a_damaged_save_loads_as_an_empty_pot) {
  Garden ok;
  ok.force(GST_LEAVES, 0, 1);
  ok.dry = 3 * H;
  struct Bad { uint8_t b; uint32_t g, w, d; const char *why; };
  const uint8_t grow = GARDEN_GROWING | 1 << 2;
  const Bad bad[] = {
    { 3, 0, 0, 0, "no such plant" },
    { GARDEN_GROWING | 3 << 2, GARDEN_LEAVES_S, 0, 0, "no such berry" },
    { (uint8_t)(grow | 0x40), GARDEN_LEAVES_S, 0, 0, "a stray high bit" },
    { grow, GARDEN_RIPE_S + 1, 0, 0, "grown past ripe" },
    { grow, GARDEN_LEAVES_S, GARDEN_WET_MAX_S + 1, 0, "more water than it can hold" },
    { grow, GARDEN_LEAVES_S, 0, GARDEN_WILT_S, "dry long enough to have wilted" },
    { grow, GARDEN_LEAVES_S, H, H, "wet and dry at once" },
    { (uint8_t)(grow | 1 << 4), GARDEN_LEAVES_S, 0, 0, "berries left before it is ripe" },
    { grow, GARDEN_RIPE_S, 0, 0, "ripe with no berries left" },
    { (uint8_t)(GARDEN_EMPTY | 1 << 4), 0, 0, 0, "an empty pot with berries" },
    { GARDEN_WITHERED, H, 0, 0, "a wilted stalk with growth" },
    { GARDEN_EMPTY, 0, H, 0, "an empty pot with water" },
  };
  for (const Bad &x : bad) {
    Garden g = ok;
    CHECK_MSG(!g.load(x.b, x.g, x.w, x.d, T0, CLOCK_EPOCH_MIN, CLOCK_EPOCH_END), x.why);
    CHECK_MSG(g.stage() == GST_EMPTY && g.wet == 0 && g.dry == 0 && g.grown == 0 && g.left == 0, x.why);
    CHECK_MSG(g.at == T0, x.why);
  }
  Garden g;
  CHECK(g.load(ok.packed(), ok.grown, ok.wet, ok.dry, CLOCK_EPOCH_END, CLOCK_EPOCH_MIN, CLOCK_EPOCH_END));
  CHECK_MSG(g.at == 0, "a saved time past the clock's range reads as none");
  CHECK(same(g, ok));
  // a saved time before the clock's range reads as none too, the rest of the save intact
  for (uint32_t low : { 1u, CLOCK_EPOCH_MIN - 1 }) {
    Garden l;
    CHECK_MSG(l.load(ok.packed(), ok.grown, ok.wet, ok.dry, low, CLOCK_EPOCH_MIN, CLOCK_EPOCH_END),
              "a low saved time still loads the plant");
    CHECK_MSG(l.at == 0, "a saved time before the clock's range reads as none: " + std::to_string(low));
    CHECK(same(l, ok));
  }
  Garden m;
  CHECK(m.load(ok.packed(), ok.grown, ok.wet, ok.dry, CLOCK_EPOCH_MIN, CLOCK_EPOCH_MIN, CLOCK_EPOCH_END));
  CHECK_MSG(m.at == CLOCK_EPOCH_MIN, "the clock's first second is a valid reference");
}

static void hatchPet(Pet &p) {
  mockNvsReset();
  mockSetMillis(0);
  p.begin();
  if (p.awaitingStarter()) p.chooseStarter(4);
  p.eggTap();
  p.eggTap();
  p.eggTap();
  mockAdvanceMillis(EAT_ANIM_MS + HEART_MS + 1);
}

TEST(garden, the_pet_save_keeps_it_and_an_old_save_has_an_empty_pot) {
  Pet p;
  hatchPet(p);
  CHECK_EQ(p.garden.stage(), (uint8_t)GST_EMPTY);  // a save from before the garden
  p.garden.force(GST_FLOWER, 2, 2);
  p.garden.at = T0;
  p.saveNow();
  Pet q;
  q.begin();
  CHECK(same(q.garden, p.garden));
  CHECK_EQ(q.garden.at, T0);
  CHECK(q.garden.wiltingSoon());
}

TEST(garden, it_stays_through_an_ending) {
  Pet p;
  hatchPet(p);
  p.garden.force(GST_LEAVES, 1, 1);
  Garden before = p.garden;
  p.startFarewell();
  mockAdvanceMillis(CEREMONY_MS);
  p.update(millis());
  CHECK(p.isEgg());
  CHECK(same(p.garden, before));
  Pet q;
  q.begin();
  CHECK(same(q.garden, before));
}

TEST(garden, a_clock_set_starts_it_afresh) {
  Pet p;
  hatchPet(p);
  p.garden.at = T0;
  p.setClock(T0 + 3 * DAY);
  CHECK_EQ(p.garden.at, 0u);
}

// ---------------------------------------------------------------- the pet eats a picked berry
TEST(garden, a_picked_berry_is_a_food_strip_berry_and_a_bigger_treat) {
  for (uint8_t color = 0; color < 3; color++) {
    Pet a, b;
    hatchPet(a);
    a.fullness = a.joy = 20;
    a.bond = 10;
    a.lastSeenEpoch = 100u * 86400 + 10 * 3600;
    a.lastCareDay = 99;
    a.saveNow();
    b.begin();  // the same pet
    b.lastSeenEpoch = a.lastSeenEpoch;
    a.feedBerry(color);
    CHECK(b.pickBerry(color));
    std::string c = GARDEN_BERRY_KEYS[color];
    CHECK_MSG(b.fullness == a.fullness + GARDEN_PICK_FULL, c);
    CHECK_MSG(b.joy == a.joy + GARDEN_PICK_JOY, c);
    CHECK_MSG(b.bond == a.bond + GARDEN_PICK_BOND, c);
    CHECK_MSG(b.showHeart(), c);
    CHECK_MSG(b.eating(), c);
    CHECK_MSG(b.berryKnown == a.berryKnown, c);
    CHECK_MSG(b.lastCareDay == 100, c + ": the day's care");
  }
}

TEST(garden, the_pet_eats_only_hatched_awake_and_not_already_eating) {
  Pet p;
  hatchPet(p);
  p.garden.force(GST_RIPE, 1, 0);
  uint8_t picked = 0;
  CHECK_EQ(p.tapGarden(1, picked), GTAP_PICKED);
  CHECK(p.eating());
  uint8_t full = p.fullness;
  CHECK_EQ(p.tapGarden(1, picked), GTAP_WAIT);  // still eating
  CHECK_EQ(p.garden.left, (uint8_t)2);
  CHECK(!p.pickBerry(0));
  CHECK_EQ(p.fullness, full);
  mockAdvanceMillis(EAT_ANIM_MS);
  p.toggleLight();  // asleep
  CHECK(p.sleeping);
  CHECK_EQ(p.tapGarden(1, picked), GTAP_WAIT);
  CHECK_EQ(p.garden.left, (uint8_t)2);
  p.toggleLight();
  p.startFarewell();
  CHECK_EQ(p.tapGarden(1, picked), GTAP_WAIT);
  mockAdvanceMillis(CEREMONY_MS);
  p.update(millis());
  CHECK(p.isEgg());
  CHECK_EQ(p.tapGarden(1, picked), GTAP_WAIT);
  CHECK_EQ(p.garden.left, (uint8_t)2);
}

TEST(garden, planting_watering_and_clearing_wait_for_the_periodic_save) {
  Pet p;
  hatchPet(p);
  p.saveNow();
  CHECK(!p.savePending());
  uint8_t picked = 0;
  CHECK_EQ(p.tapGarden(0, picked), GTAP_PLANTED);
  CHECK(p.savePending());
  Pet q;
  q.begin();
  CHECK_MSG(q.garden.stage() == GST_EMPTY, "not written yet");
  p.flushSave();
  q.begin();
  CHECK_EQ(q.garden.stage(), (uint8_t)GST_SEED);
  CHECK_EQ(p.tapGarden(0, picked), GTAP_WATERED);
  CHECK(p.savePending());
  p.garden.force(GST_WITHERED, 0, 0xFF);
  p.saveNow();
  CHECK_EQ(p.tapGarden(0, picked), GTAP_CLEARED);
  CHECK(p.savePending());
}

// ---------------------------------------------------------------- the speech lines
TEST(garden, its_lines_take_part_of_the_friendly_share_and_never_come_before_a_need) {
  BubbleIn in{};
  in.needs = attnLevels(80, 80, 80, 80);
  in.clockKnown = false;
  in.last = BUB_NONE;
  in.garden = GSAY_RIPE;
  CHECK_EQ(bubblePick(in, BUBBLE_GARDEN_FROM), (uint8_t)BUB_RIPE);
  CHECK_EQ(bubblePick(in, BUBBLE_GARDEN_FROM + BUBBLE_GARDEN_PCT - 1), (uint8_t)BUB_RIPE);
  CHECK(bubblePick(in, BUBBLE_GARDEN_FROM + BUBBLE_GARDEN_PCT) != BUB_RIPE);
  CHECK(bubblePick(in, BUBBLE_GARDEN_FROM - 1) != BUB_RIPE);
  in.garden = GSAY_THIRSTY;
  CHECK_EQ(bubblePick(in, BUBBLE_GARDEN_FROM), (uint8_t)BUB_THIRSTY);
  CHECK(bubblePick(in, BUBBLE_GARDEN_FROM + BUBBLE_GARDEN_PCT) != BUB_THIRSTY);
  in.garden = GSAY_WILTING;  // twice as often
  CHECK_EQ(bubblePick(in, BUBBLE_GARDEN_FROM + 2 * BUBBLE_GARDEN_PCT - 1), (uint8_t)BUB_THIRSTY);
  in.last = BUB_THIRSTY;  // not twice in a row
  CHECK(bubblePick(in, BUBBLE_GARDEN_FROM) != BUB_THIRSTY);
  in.last = BUB_NONE;
  in.needs = attnLevels(10, 80, 80, 80);  // hungry first
  CHECK_EQ(bubblePick(in, BUBBLE_GARDEN_FROM), (uint8_t)BUB_HUNGRY);
  in.needs = attnLevels(80, 80, 80, 80);
  in.sick = true;
  CHECK_EQ(bubblePick(in, BUBBLE_GARDEN_FROM), (uint8_t)BUB_SICK);
  in.sick = false;
  in.garden = GSAY_NONE;
  for (uint32_t r = 0; r < 1000; r++) {
    uint8_t l = bubblePick(in, r);
    CHECK(l != BUB_RIPE && l != BUB_THIRSTY);
  }
}

// ---------------------------------------------------------------- the screen
// mirrors app.h (EVO_BTN_*, FAR_BTN_*, BTN_HIT and the four care buttons) and TamaPoke.ino
// (inPetZone): update these with them
TEST(garden, the_pot_is_clear_of_the_pet_the_buttons_and_the_panel_edge) {
  const int EVO_X0 = 233 - 128, EVO_X1 = 233 + 128, EVO_Y0 = 172, EVO_Y1 = 172 + 64;
  const int FAR_X0 = 233 - 204, FAR_X1 = 233 + 204, FAR_Y0 = 176, FAR_Y1 = 176 + 58;
  const int BTN[4][2] = { { 140, 390 }, { 202, 404 }, { 264, 404 }, { 326, 390 } };
  for (int x = 0; x < 466; x++)
    for (int y = 0; y < 466; y++) {
      if (!gardenHit(x, y)) continue;
      std::string at = " at " + std::to_string(x) + "," + std::to_string(y);
      CHECK_MSG(!(x > 110 && x < 356 && y > 95 && y < 310), "the pet's touch zone" + at);
      CHECK_MSG(!(x >= EVO_X0 && x <= EVO_X1 && y >= EVO_Y0 && y <= EVO_Y1), "the evolve button" + at);
      CHECK_MSG(!(x >= FAR_X0 && x <= FAR_X1 && y >= FAR_Y0 && y <= FAR_Y1), "the farewell button" + at);
      for (auto &b : BTN) {
        int dx = x - b[0], dy = y - b[1];
        CHECK_MSG(dx * dx + dy * dy > 36 * 36, "a care button" + at);
      }
      CHECK_MSG(y < 312, "the bars' panel" + at);
    }
  // the garden box is inside the tap box and on the round panel, below the lowest bubble
  for (int x : { GARDEN_BOX_X0, GARDEN_BOX_X1 })
    for (int y : { GARDEN_BOX_Y0, GARDEN_BOX_Y1 }) {
      CHECK(gardenHit(x, y));
      int dx = x - 233, dy = y - 233;
      CHECK(dx * dx + dy * dy < 233 * 233);
    }
  CHECK(GARDEN_BOX_Y0 >= BUBBLE_TIP_LOW - 8);  // the bubbles' boxes end by y 242
  CHECK(GARDEN_BOX_X0 > PET_WALK_MAX + 66);    // right of where most sprites reach
}

// ---------------------------------------------------------------- the serial command
TEST(garden, the_GARDEN_command_reads_sets_and_refuses) {
  GardenCmdArgs a;
  CHECK_EQ(gardenCmdParse("HEALTH", a), GCMD_NONE);
  CHECK_EQ(gardenCmdParse("GARDENS", a), GCMD_NONE);
  CHECK_EQ(gardenCmdParse("GARDEN?", a), GCMD_READ);
  CHECK_EQ(gardenCmdParse("GARDEN", a), GCMD_BAD);
  CHECK_EQ(gardenCmdParse("GARDEN ", a), GCMD_BAD);
  CHECK_EQ(gardenCmdParse("GARDEN ripe", a), GCMD_SET);
  CHECK_EQ(a.stage, (uint8_t)GST_RIPE);
  CHECK_EQ(a.moist, (uint8_t)1);
  CHECK_EQ(a.color, (uint8_t)0xFF);
  CHECK_EQ(gardenCmdParse("GARDEN leaves wilting green", a), GCMD_SET);
  CHECK_EQ(a.stage, (uint8_t)GST_LEAVES);
  CHECK_EQ(a.moist, (uint8_t)2);
  CHECK_EQ(a.color, (uint8_t)2);
  CHECK_EQ(gardenCmdParse("GARDEN flower blue", a), GCMD_SET);
  CHECK_EQ(a.moist, (uint8_t)1);
  CHECK_EQ(a.color, (uint8_t)1);
  CHECK_EQ(gardenCmdParse("GARDEN seed dry", a), GCMD_SET);
  CHECK_EQ(a.moist, (uint8_t)0);
  CHECK_EQ(gardenCmdParse("GARDEN empty", a), GCMD_SET);
  CHECK_EQ(gardenCmdParse("GARDEN withered", a), GCMD_SET);
  CHECK_EQ(a.stage, (uint8_t)GST_WITHERED);
  for (const char *bad : { "GARDEN tree", "GARDEN seed wet red extra", "GARDEN seed purple", "GARDEN red seed",
                           "GARDEN ripe wilting", "GARDEN empty wet", "GARDEN withered red", "GARDEN seed red wet",
                           "GARDEN seedlingseedling", "GARDEN?x" }) {
    CHECK_MSG(gardenCmdParse(bad, a) == GCMD_BAD || strcmp(bad, "GARDEN?x") == 0, bad);
  }
  CHECK_EQ(gardenCmdParse("GARDEN?x", a), GCMD_NONE);
}

TEST(garden, GARDEN_forces_each_stage_as_it_looks) {
  for (uint8_t st = 0; st < GST_COUNT; st++) {
    Garden g;
    g.force(st, 1, 1);
    CHECK_EQ(g.stage(), st);
    if (st >= GST_SEED && st <= GST_RIPE) CHECK(g.watered());
    Garden d;
    d.force(st, 2, 0);
    if (st >= GST_SEED && st < GST_RIPE) CHECK(d.wiltingSoon());
  }
  Garden g;
  g.force(GST_LEAVES, 1, 2);
  g.force(GST_FLOWER, 0, 0xFF);
  CHECK_EQ(g.berry, (uint8_t)2);  // kept
  CHECK(!g.watered());
}
