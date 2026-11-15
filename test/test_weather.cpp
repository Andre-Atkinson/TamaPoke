// Made-up weather and travelling (weather.h, #60): the odds at each place, spells and
// their fade, the place of each day, when the scene may change, what shows in the sky, the
// falling rain and snow, and the SCENE serial command's arguments. Pure logic.
#include "framework.h"
#include "../power.h"
#include "../weather.h"

static const uint32_t DAY = 86400UL;
static const uint32_t SOME_DAY = 1793232000UL;  // 2026-10-29 00:00, local time (any day will do)

// ---------------------------------------------------------------- the odds
TEST(Weather, each_place_has_odds_that_add_up_and_snow_only_where_it_belongs) {
  for (int p = 0; p < PLACES; p++) {
    int sum = 0;
    for (int k = 0; k < WX_KINDS; k++) sum += WX_ODDS[p][k];
    CHECK_EQ(sum, 100);
  }
  // the volcano: never snow or fog; snow only on the mountain and the snowfield
  CHECK_EQ((int)WX_ODDS[PLACE_VOLCANO][WX_SNOW], 0);
  CHECK_EQ((int)WX_ODDS[PLACE_VOLCANO][WX_FOG], 0);
  for (int p : { PLACE_MEADOW, PLACE_BEACH, PLACE_FOREST, PLACE_VOLCANO }) CHECK_EQ((int)WX_ODDS[p][WX_SNOW], 0);
  CHECK(WX_ODDS[PLACE_MOUNTAIN][WX_SNOW] > 0);
  CHECK(WX_ODDS[PLACE_SNOW][WX_SNOW] > WX_ODDS[PLACE_SNOW][WX_CLEAR]);  // the snowfield snows most
  CHECK(WX_ODDS[PLACE_FOREST][WX_RAIN] >= WX_ODDS[PLACE_FOREST][WX_CLEAR]);
}

// each roll of 0-99 gives a kind, as many rolls for each kind as its odds
TEST(Weather, each_roll_maps_to_a_kind_by_the_odds_exactly) {
  for (int p = 0; p < PLACES; p++) {
    int rolls[WX_KINDS] = {};
    for (uint32_t r = 0; r < 100; r++) rolls[wxFromRoll((uint8_t)p, r)]++;
    for (int k = 0; k < WX_KINDS; k++) CHECK_EQ(rolls[k], (int)WX_ODDS[p][k]);
    CHECK_EQ(wxFromRoll((uint8_t)p, 0), (uint8_t)WX_CLEAR);  // the rolls run clear, cloudy... in order
  }
  CHECK_EQ(wxFromRoll(PLACE_MEADOW, 100), (uint8_t)WX_CLEAR);  // out of range: fair
  CHECK_EQ(wxFromRoll(PLACES, 0), wxFromRoll(PLACE_MEADOW, 0));
}

// a new spell follows the place's odds (20,000 blocks: each kind within 2 points)
TEST(Weather, a_new_spell_follows_the_odds_of_its_place) {
  const int N = 20000;
  for (int p = 0; p < PLACES; p++) {
    int seen[WX_KINDS] = {};
    for (int b = 0; b < N; b++) {
      uint8_t k = wxFresh((uint8_t)p, 600000u + (uint32_t)b);
      CHECK(k < WX_KINDS);
      if (k < WX_KINDS) seen[k]++;
    }
    for (int k = 0; k < WX_KINDS; k++) {
      int pct10 = seen[k] * 1000 / N;  // tenths of a percent
      CHECK_MSG(pct10 >= WX_ODDS[p][k] * 10 - 20 && pct10 <= WX_ODDS[p][k] * 10 + 20,
                "place " + std::to_string(p) + " kind " + std::to_string(k) + ": " + std::to_string(pct10) +
                    " per mille, odds " + std::to_string(WX_ODDS[p][k]) + "%");
      if (WX_ODDS[p][k] == 0) CHECK_EQ(seen[k], 0);
    }
  }
}

// Spells carry on: about 4 blocks in 10 keep the weather before them, never more than
// WX_KEEP_MAX in a row through the keep rule, and over time the odds still hold.
TEST(Weather, a_spell_carries_on_through_the_blocks_that_keep_it) {
  const uint32_t first = SOME_DAY / WX_BLOCK_S;
  int kept = 0, same = 0, n = 0;
  int seen[WX_KINDS] = {};
  for (uint32_t b = first; b < first + 20000; b++, n++) {
    uint8_t k = wxKind(PLACE_MEADOW, b);
    seen[k]++;
    if (wxKind(PLACE_MEADOW, b - 1) == k) same++;
    if (!wxKeeps(b)) continue;
    // a block that keeps, at the head of a run shorter than the cap, has the same weather
    // as the block before it
    int run = 0;
    for (uint32_t c = b; run < WX_KEEP_MAX + 1 && wxKeeps(c); c--) run++;
    if (run <= WX_KEEP_MAX - 1) {
      kept++;
      CHECK_EQ(k, wxKind(PLACE_MEADOW, b - 1));
    }
  }
  CHECK(kept > 4000);
  CHECK_RANGE(same * 100 / n, 50, 75);  // 40% kept, plus fresh picks that match by chance
  for (int k = 0; k < WX_KINDS; k++)
    CHECK_RANGE(seen[k] * 100 / n, WX_ODDS[PLACE_MEADOW][k] - 3, WX_ODDS[PLACE_MEADOW][k] + 3);
}

// ---------------------------------------------------------------- the clock
TEST(Weather, without_a_trusted_clock_it_is_fair_weather_at_home) {
  CHECK_EQ(sceneEpoch(SOME_DAY, true), 0u);  // reseeded and not set since
  CHECK_EQ(sceneEpoch(SOME_DAY, false), SOME_DAY);
  CHECK_EQ(sceneEpoch(0, false), 0u);  // never set
  for (int p = 0; p < PLACES; p++) {
    WxNow w = wxAt((uint8_t)p, 0);
    CHECK_EQ(w.kind, (uint8_t)WX_CLEAR);
    CHECK_EQ(w.prev, (uint8_t)WX_CLEAR);
    CHECK_EQ(w.mix, (uint8_t)WX_MIX_FULL);
    CHECK_EQ(travelPlace((uint8_t)p, 0), (uint8_t)p);
    CHECK_EQ(travelPlace((uint8_t)p, TRAVEL_DAY_START_S - 1), (uint8_t)p);
  }
  // The guards do this, not the hash: what the clock's own picks would give for epoch 0
  // is neither home nor fair (the salts are chosen so; if they change, keep it this way).
  CHECK(sceneHash(travelDay(0) ^ TRAVEL_SALT) % 100 >= TRAVEL_HOME_PCT);
  for (int p = 0; p < PLACES; p++) CHECK(wxKind((uint8_t)p, 0) != WX_CLEAR);
}

TEST(Weather, the_same_hour_gives_the_same_weather_after_a_reboot) {
  // nothing is stored: the weather is the clock's, so two readings of the same time agree
  for (uint32_t e = SOME_DAY; e < SOME_DAY + 3 * DAY; e += 1234) {
    WxNow a = wxAt(PLACE_FOREST, e), b = wxAt(PLACE_FOREST, e);
    CHECK_EQ(a.kind, b.kind);
    CHECK_EQ(a.prev, b.prev);
    CHECK_EQ(a.mix, b.mix);
    CHECK_EQ(travelPlace(PLACE_FOREST, e), travelPlace(PLACE_FOREST, e));
  }
}

// A change of weather fades in over the first 20 minutes of its block; a block that keeps
// the weather before it shows it at once.
TEST(Weather, a_change_fades_in_over_twenty_minutes) {
  int changes = 0, steady = 0;
  for (uint32_t b = SOME_DAY / WX_BLOCK_S; b < SOME_DAY / WX_BLOCK_S + 400; b++) {
    uint8_t now = wxKind(PLACE_FOREST, b), before = wxKind(PLACE_FOREST, b - 1);
    uint32_t start = b * WX_BLOCK_S;
    WxNow w0 = wxAt(PLACE_FOREST, start), w10 = wxAt(PLACE_FOREST, start + 600),
          w20 = wxAt(PLACE_FOREST, start + WX_FADE_S), wEnd = wxAt(PLACE_FOREST, start + WX_BLOCK_S - 1);
    CHECK_EQ(w0.kind, now);
    CHECK_EQ(wEnd.kind, now);
    CHECK_EQ(wEnd.mix, (uint8_t)WX_MIX_FULL);
    CHECK_EQ(wEnd.prev, now);
    if (now == before) {
      steady++;
      CHECK_EQ(w0.mix, (uint8_t)WX_MIX_FULL);
      CHECK_EQ(w0.prev, now);
      continue;
    }
    changes++;
    CHECK_EQ(w0.prev, before);
    CHECK_EQ(w0.mix, (uint8_t)0);
    CHECK_EQ(w10.prev, before);
    CHECK_EQ(w10.mix, (uint8_t)(WX_MIX_FULL / 2));
    CHECK_EQ(w20.mix, (uint8_t)WX_MIX_FULL);
    CHECK_EQ(w20.prev, now);
  }
  CHECK(changes > 50);
  CHECK(steady > 50);
}

// ---------------------------------------------------------------- travelling
TEST(Travel, home_about_four_days_in_ten_and_the_other_places_share_the_rest) {
  for (int home = 0; home < PLACES; home++) {
    int seen[PLACES] = {};
    const int N = 10000;
    for (int d = 0; d < N; d++) {
      uint8_t p = travelPlace((uint8_t)home, SOME_DAY + 12 * 3600 + (uint32_t)d * DAY);
      CHECK(p < PLACES);
      if (p < PLACES) seen[p]++;
    }
    CHECK_RANGE(seen[home] * 100 / N, TRAVEL_HOME_PCT - 2, TRAVEL_HOME_PCT + 2);
    for (int p = 0; p < PLACES; p++)
      if (p != home) CHECK_RANGE(seen[p] * 100 / N, 10, 14);  // (100 - 40) / 5 = 12 each
  }
}

TEST(Travel, a_place_lasts_from_six_in_the_morning_to_six_the_next) {
  int moved = 0;
  for (uint32_t d = 0; d < 200; d++) {
    uint32_t six = SOME_DAY + d * DAY + 6 * 3600;
    uint8_t p = travelPlace(PLACE_BEACH, six);
    for (uint32_t t = six; t < six + DAY; t += 3600) CHECK_EQ(travelPlace(PLACE_BEACH, t), p);
    CHECK_EQ(travelPlace(PLACE_BEACH, six + DAY - 1), p);
    if (travelPlace(PLACE_BEACH, six - 1) != p) moved++;  // the day before, until 05:59:59
  }
  CHECK(moved > 80);  // it does travel: most mornings are somewhere else
}

TEST(Travel, a_home_out_of_range_is_the_meadow) {
  CHECK_EQ(travelPlace(PLACES, 0), (uint8_t)PLACE_MEADOW);
  CHECK_EQ(travelPlace(0xFF, 0), (uint8_t)PLACE_MEADOW);
  for (uint32_t d = 0; d < 50; d++) CHECK(travelPlace(0xFF, SOME_DAY + d * DAY) < PLACES);
}

// The place on screen changes only while nobody is looking: the first frame, after the
// main scene was not drawn for SCENE_UNSEEN_MS, or with the screen nearly off.
TEST(Travel, the_scene_changes_only_while_nobody_is_looking) {
  CHECK(sceneMayChange(true, 0, false));                   // the first frame
  CHECK(!sceneMayChange(false, 100, false));               // 10 fps, watched
  CHECK(!sceneMayChange(false, 1000, false));              // the nearly-off dim's 1 s frame...
  CHECK(sceneMayChange(false, 1000, true));                // ...which may change it anyway
  CHECK(!sceneMayChange(false, SCENE_UNSEEN_MS - 1, false));
  CHECK(sceneMayChange(false, SCENE_UNSEEN_MS, false));    // the screen was off, or another screen
  // every frame interval of a lit, watched main screen is shorter than the unseen gap
  PowerCtx c = {};
  c.batPct = 5;  // a low battery doubles each interval
  for (uint32_t idle : { 0UL, 70000UL, 100000UL }) {
    c.idleMs = idle;
    for (bool sleeping : { false, true }) {
      c.sleeping = sleeping;
      if (dimStageNow(c) != DIM_OFF) CHECK(frameIntervalMs(c) < SCENE_UNSEEN_MS);
    }
  }
}

// ---------------------------------------------------------------- the sky
TEST(Weather, the_stars_and_the_sun_show_only_through_a_clear_enough_sky) {
  CHECK(wxStarsOut(wxFixed(WX_CLEAR)));
  for (uint8_t k : { WX_CLOUDY, WX_RAIN, WX_STORM, WX_SNOW, WX_FOG }) CHECK(!wxStarsOut(wxFixed(k)));
  CHECK(wxSunOut(wxFixed(WX_CLEAR)));
  CHECK(wxSunOut(wxFixed(WX_CLOUDY)));  // the sun peeks through light cloud
  for (uint8_t k : { WX_RAIN, WX_STORM, WX_SNOW, WX_FOG }) CHECK(!wxSunOut(wxFixed(k)));
  // clear to rain: the stars are out at the start of the fade and gone by its end
  WxNow w;
  w.kind = WX_RAIN;
  w.prev = WX_CLEAR;
  w.mix = 0;
  CHECK(wxStarsOut(w));
  w.mix = WX_MIX_FULL;
  CHECK(!wxStarsOut(w));
  bool was = true;
  for (uint8_t m = 0; m <= WX_MIX_FULL; m++) {  // and once gone they stay gone
    w.mix = m;
    bool out = wxStarsOut(w);
    CHECK(was || !out);
    was = out;
  }
}

TEST(Weather, the_falling_rain_and_snow_fade_with_the_weather) {
  CHECK_EQ(wxDrops(wxFixed(WX_RAIN)), (uint8_t)WX_RAIN_DROPS);
  CHECK_EQ(wxDrops(wxFixed(WX_STORM)), (uint8_t)WX_STORM_DROPS);
  CHECK(WX_STORM_DROPS > WX_RAIN_DROPS);
  CHECK_EQ(wxFlakes(wxFixed(WX_SNOW)), (uint8_t)WX_SNOW_FLAKES);
  for (uint8_t k : { WX_CLEAR, WX_CLOUDY, WX_FOG }) {
    CHECK_EQ(wxDrops(wxFixed(k)), (uint8_t)0);
    CHECK_EQ(wxFlakes(wxFixed(k)), (uint8_t)0);
  }
  WxNow w;
  w.kind = WX_CLEAR;
  w.prev = WX_RAIN;
  w.mix = WX_MIX_FULL / 2;  // the rain half gone
  CHECK_EQ(wxDrops(w), (uint8_t)(WX_RAIN_DROPS / 2));
  CHECK_EQ(wxWet(w), (uint8_t)(WX_MIX_FULL / 2));
  // clouds: clear weather keeps the scene's own two by day and none at night
  CHECK_EQ(wxClouds(wxFixed(WX_CLEAR), 2), (uint8_t)2);
  CHECK_EQ(wxClouds(wxFixed(WX_CLEAR), 0), (uint8_t)0);
  CHECK(wxClouds(wxFixed(WX_CLOUDY), 0) > 2);
  CHECK_EQ(wxClouds(wxFixed(WX_FOG), 2), (uint8_t)0);
  // lightning only once the storm has fully come in
  CHECK(wxLightning(wxFixed(WX_STORM)));
  w.kind = WX_STORM;
  w.prev = WX_RAIN;
  w.mix = WX_MIX_FULL - 1;
  CHECK(!wxLightning(w));
  CHECK(!wxLightning(wxFixed(WX_RAIN)));
}

// Falling rain and snow only at the full frame rate: hidden while dimmed, while the pet
// sleeps untouched and on a low battery, and never with the screen off.
TEST(Weather, rain_and_snow_fall_only_at_the_full_frame_rate) {
  CHECK(!wxParticlesLive(0));  // screen off: nothing is drawn at all
  CHECK(wxParticlesLive(100));
  CHECK(wxParticlesLive(150));
  CHECK(!wxParticlesLive(200));
  PowerCtx c = {};
  c.batPct = 80;
  c.idleMs = 0;
  CHECK(wxParticlesLive(frameIntervalMs(c)));   // in use
  c.idleMs = 70000;
  CHECK(wxParticlesLive(frameIntervalMs(c)));   // calm, a minute untouched
  c.idleMs = 100000;
  CHECK(!wxParticlesLive(frameIntervalMs(c)));  // dimmed
  c.idleMs = 400000;
  CHECK(!wxParticlesLive(frameIntervalMs(c)));  // nearly off
  c.idleMs = 70000;
  c.sleeping = true;
  CHECK(!wxParticlesLive(frameIntervalMs(c)));  // asleep and untouched
  c.sleeping = false;
  c.idleMs = 0;
  c.batPct = 10;
  CHECK(!wxParticlesLive(frameIntervalMs(c)));  // low battery
  c.batPct = 80;
  c.screenOff = true;
  CHECK(!wxParticlesLive(frameIntervalMs(c)));
}

// ---------------------------------------------------------------- SCENE (serial)
TEST(SceneCmd, takes_a_place_and_a_weather_or_auto) {
  uint8_t p = 0, k = 0;
  CHECK(sceneCmdParse(" beach rain", p, k));
  CHECK_EQ(p, (uint8_t)PLACE_BEACH);
  CHECK_EQ(k, (uint8_t)WX_RAIN);
  CHECK(sceneCmdParse("SNOW  Snow ", p, k));  // the place and the weather can share a word
  CHECK_EQ(p, (uint8_t)PLACE_SNOW);
  CHECK_EQ(k, (uint8_t)WX_SNOW);
  CHECK(sceneCmdParse("volcano", p, k));  // the weather follows the clock there
  CHECK_EQ(p, (uint8_t)PLACE_VOLCANO);
  CHECK_EQ(k, (uint8_t)SCENE_AUTO);
  CHECK(sceneCmdParse("auto fog", p, k));
  CHECK_EQ(p, (uint8_t)SCENE_AUTO);
  CHECK_EQ(k, (uint8_t)WX_FOG);
  CHECK(sceneCmdParse(" AUTO", p, k));
  CHECK_EQ(p, (uint8_t)SCENE_AUTO);
  CHECK_EQ(k, (uint8_t)SCENE_AUTO);
  for (int i = 0; i < PLACES; i++) CHECK(sceneCmdParse(PLACE_KEYS[i], p, k) && p == i);
}

TEST(SceneCmd, refuses_anything_else_and_changes_nothing) {
  for (const char *bad : { "", "   ", "beach rain storm", "lava", "beach hail", "beachrain", "beac", "beaches",
                           "rain", "beach rain auto" }) {
    uint8_t p = 7, k = 9;
    CHECK_MSG(!sceneCmdParse(bad, p, k), std::string("'") + bad + "'");
    CHECK_EQ(p, (uint8_t)7);
    CHECK_EQ(k, (uint8_t)9);
  }
}
