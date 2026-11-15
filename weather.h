// Made-up weather and travelling (#60): the place the pet is at today and the weather
// there, worked out on the board from the clock alone. No network, and nothing is saved:
// the same date and hour give the same place and weather after a reboot, so the pet's
// save is untouched. Pure logic (no hardware), host-tested in test/test_weather.cpp;
// scene.cpp draws it and keeps the one piece of state, the place on screen.
//
// The idea of weather drawn over the scene comes from the Mu-Muu fork of TamaPoke (MIT,
// credited in CREDITS.md), which shows real weather fetched over Wi-Fi. This fork has no
// radio at all, so the weather here is generated; no code or art is taken from Mu-Muu.
#pragma once
#include <stdint.h>

// The places are the six drawn biomes (DexEntry::biome in dex.h): a pet's own is its home.
enum ScenePlace : uint8_t {
  PLACE_MEADOW, PLACE_BEACH, PLACE_FOREST, PLACE_VOLCANO, PLACE_MOUNTAIN, PLACE_SNOW, PLACES
};
enum WxKind : uint8_t { WX_CLEAR, WX_CLOUDY, WX_RAIN, WX_STORM, WX_SNOW, WX_FOG, WX_KINDS };
#define SCENE_AUTO 0xFF  // the SCENE serial command: follow the clock (no override)

// The words the SCENE serial command takes and SCENE? prints (serialcmd.cpp)
inline const char *const PLACE_KEYS[PLACES] = { "meadow", "beach", "forest", "volcano", "mountain", "snow" };
inline const char *const WX_KEYS[WX_KINDS] = { "clear", "cloudy", "rain", "storm", "snow", "fog" };

// The clock the scene follows: the epoch (local time, as the RTC keeps it) when it is
// trusted, 0 when it is not: never set, or reseeded after a power cut and not set since
// (bedEdge.stale(), clockprompt.h). With 0 the pet is at home in fair weather.
inline uint32_t sceneEpoch(uint32_t epoch, bool stale) { return stale ? 0 : epoch; }

// A well-mixed 32-bit hash ("lowbias32", public domain): neighbouring days and blocks get
// unrelated values. Each use XORs in its own salt so the picks don't follow each other.
inline uint32_t sceneHash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}
#define TRAVEL_SALT 0x3c6ef372U
#define WX_PICK_SALT 0xa54ff53aU
#define WX_KEEP_SALT 0x510e527fU

// ---------------------------------------------------------------- travelling
// One place a day, picked for 06:00, so the player finds the pet somewhere new in the
// morning. Home about 4 days in 10; the other five share the rest equally. scene.cpp
// shows a new place only while nobody is looking (sceneMayChange below).
#define TRAVEL_DAY_START_S (6 * 3600UL)
#define TRAVEL_HOME_PCT 40

// the travel day of an epoch: it turns over at 06:00, not at midnight
inline uint32_t travelDay(uint32_t epoch) { return (epoch - TRAVEL_DAY_START_S) / 86400UL; }

// Where the pet is on the day of `epoch`, from its home place. An egg stays at home (the
// caller passes the meadow), and so does every pet while the clock is unknown (epoch 0).
inline uint8_t travelPlace(uint8_t home, uint32_t epoch) {
  if (home >= PLACES) home = PLACE_MEADOW;
  if (epoch < TRAVEL_DAY_START_S) return home;  // 0: no trusted clock
  uint32_t h = sceneHash(travelDay(epoch) ^ TRAVEL_SALT);
  if (h % 100 < TRAVEL_HOME_PCT) return home;
  uint8_t k = (uint8_t)((h / 100) % (PLACES - 1));  // one of the other five
  return k >= home ? (uint8_t)(k + 1) : k;
}

// The place on screen follows travelPlace() only when the change can't be seen happening:
// on the first frame, when the main scene wasn't drawn for a while (the screen was off or
// another screen was open; at its slowest, the nearly-off dim, a frame comes every 1 s,
// 2 s on a low battery), or while the screen is nearly off (DIM_OFF, brightness 8).
#define SCENE_UNSEEN_MS 2500UL
inline bool sceneMayChange(bool firstFrame, uint32_t sinceLastDrawMs, bool nearlyOff) {
  return firstFrame || nearlyOff || sinceLastDrawMs >= SCENE_UNSEEN_MS;
}

// ---------------------------------------------------------------- weather
// The weather is decided per 3-hour block of the clock (00:00, 03:00, 06:00...). A block
// carries on the one before 4 times in 10, up to 4 times in a row, so a spell lasts from
// 3 to 15 hours. A change fades in over the first 20 minutes of its block.
#define WX_BLOCK_S (3 * 3600UL)
#define WX_KEEP_PCT 40
#define WX_KEEP_MAX 4
#define WX_FADE_S (20 * 60UL)
#define WX_MIX_FULL 16

// The odds of each kind of weather at each place, in percent (each row adds up to 100).
// No snow outside the mountain and the snowfield, and none at all at the volcano.
inline const uint8_t WX_ODDS[PLACES][WX_KINDS] = {
  // clear cloudy rain storm snow fog
  { 45, 30, 20, 5, 0, 0 },    // meadow: sun, clouds and rain
  { 55, 20, 20, 5, 0, 0 },    // beach: sun and showers
  { 30, 20, 30, 5, 0, 15 },   // forest: rain and mist
  { 60, 25, 10, 5, 0, 0 },    // volcano: mostly clear
  { 35, 20, 5, 5, 20, 15 },   // mountain: fog and snow
  { 30, 15, 0, 0, 40, 15 },   // snowfield: snow and fog
};

// a new spell for `block` at `place`, from the odds
inline uint8_t wxFresh(uint8_t place, uint32_t block) {
  const uint8_t *odds = WX_ODDS[place < PLACES ? place : PLACE_MEADOW];
  uint32_t r = sceneHash(block ^ WX_PICK_SALT) % 100;
  for (uint8_t k = 0; k < WX_KINDS; k++) {
    if (r < odds[k]) return k;
    r -= odds[k];
  }
  return WX_CLEAR;  // not reached: each row adds up to 100
}

// does `block` carry on the weather of the block before it?
inline bool wxKeeps(uint32_t block) { return sceneHash(block ^ WX_KEEP_SALT) % 100 < WX_KEEP_PCT; }

// the weather of `block` at `place`: back through the blocks that carry on (at most
// WX_KEEP_MAX), then a new spell where that run starts
inline uint8_t wxKind(uint8_t place, uint32_t block) {
  for (uint8_t i = 0; i < WX_KEEP_MAX && wxKeeps(block); i++) block--;
  return wxFresh(place, block);
}

// The weather on screen: `kind`, fading in over `prev` (mix 0-16; at 16 the change is
// over and prev == kind).
struct WxNow {
  uint8_t kind = WX_CLEAR;
  uint8_t prev = WX_CLEAR;
  uint8_t mix = WX_MIX_FULL;
};

inline WxNow wxFixed(uint8_t kind) {
  WxNow w;
  w.kind = w.prev = kind < WX_KINDS ? kind : WX_CLEAR;
  return w;
}

// The weather at `place` at `epoch` (0: no trusted clock, fair weather).
inline WxNow wxAt(uint8_t place, uint32_t epoch) {
  if (!epoch) return wxFixed(WX_CLEAR);
  uint32_t block = epoch / WX_BLOCK_S;
  uint32_t in = epoch % WX_BLOCK_S;
  WxNow w = wxFixed(wxKind(place, block));
  uint8_t before = wxKind(place, block - 1);
  if (before != w.kind && in < WX_FADE_S) {
    w.prev = before;
    w.mix = (uint8_t)(in * WX_MIX_FULL / WX_FADE_S);
  }
  return w;
}

// ---------------------------------------------------------------- what it looks like
// How strongly `w` shows something that only some kinds have, 0-16: the kinds that have it
// (a bitmask of 1 << WxKind), weighted by how far each has faded in.
inline uint8_t wxAmount(const WxNow &w, uint8_t kinds) {
  int a = 0;
  if (w.kind < WX_KINDS && (kinds & (1u << w.kind))) a += w.mix;
  if (w.prev < WX_KINDS && (kinds & (1u << w.prev))) a += WX_MIX_FULL - w.mix;
  return (uint8_t)a;
}
#define WX_BIT(k) (uint8_t)(1u << (k))

// how overcast the sky is, 0 (clear) to 16, blended across a change
inline uint8_t wxGrey(const WxNow &w) {
  static const uint8_t GREY[WX_KINDS] = { 0, 6, 9, 12, 9, 5 };
  uint8_t k = w.kind < WX_KINDS ? w.kind : WX_CLEAR, p = w.prev < WX_KINDS ? w.prev : WX_CLEAR;
  return (uint8_t)((GREY[k] * w.mix + GREY[p] * (WX_MIX_FULL - w.mix)) / WX_MIX_FULL);
}
inline uint8_t wxFog(const WxNow &w) { return wxAmount(w, WX_BIT(WX_FOG)); }
inline uint8_t wxWet(const WxNow &w) { return wxAmount(w, WX_BIT(WX_RAIN) | WX_BIT(WX_STORM)); }

// The stars only show under a sky this clear, and so does the night line "look at the
// stars" of the speech bubbles (#23, bubble.h), so the two never disagree.
#define WX_STARS_GREY 3
inline bool wxStarsOut(const WxNow &w) { return wxGrey(w) < WX_STARS_GREY && wxFog(w) < 8; }
// the sun and the moon: still out through light cloud, hidden by rain, storm, snow and fog
#define WX_SUN_GREY 7
inline bool wxSunOut(const WxNow &w) { return wxGrey(w) < WX_SUN_GREY && wxFog(w) < 8; }

// Clouds in the sky: clear weather keeps the scene's own (`clearClouds`: two by day, none
// at dusk or at night, as before #60), the others bring their own.
inline uint8_t wxClouds(const WxNow &w, uint8_t clearClouds) {
  static const uint8_t CLOUDS[WX_KINDS] = { 0, 6, 6, 6, 5, 0 };
  uint8_t k = w.kind < WX_KINDS ? w.kind : WX_CLEAR, p = w.prev < WX_KINDS ? w.prev : WX_CLEAR;
  int a = k == WX_CLEAR ? clearClouds : CLOUDS[k];
  int b = p == WX_CLEAR ? clearClouds : CLOUDS[p];
  return (uint8_t)((a * w.mix + b * (WX_MIX_FULL - w.mix) + WX_MIX_FULL / 2) / WX_MIX_FULL);
}

// Falling rain and snow at full strength, faded in and out with the weather.
#define WX_RAIN_DROPS 32
#define WX_STORM_DROPS 52
#define WX_SNOW_FLAKES 34
inline uint8_t wxDrops(const WxNow &w) {
  return (uint8_t)((wxAmount(w, WX_BIT(WX_RAIN)) * WX_RAIN_DROPS + wxAmount(w, WX_BIT(WX_STORM)) * WX_STORM_DROPS) /
                   WX_MIX_FULL);
}
inline uint8_t wxFlakes(const WxNow &w) {
  return (uint8_t)(wxAmount(w, WX_BIT(WX_SNOW)) * WX_SNOW_FLAKES / WX_MIX_FULL);
}
// lightning only once a storm has fully come in
inline bool wxLightning(const WxNow &w) { return w.kind == WX_STORM && w.mix == WX_MIX_FULL; }

// Falling rain, snow and lightning are drawn only at the full frame rate (100 ms, or the
// calm 150 ms after a minute untouched). Slower, while dimmed, while the pet sleeps untouched
// or on a low battery, they would jump in steps; the sky, the clouds and the fog stay.
#define WX_PARTICLE_MAX_MS 150
inline bool wxParticlesLive(uint32_t frameMs) { return frameMs && frameMs <= WX_PARTICLE_MAX_MS; }

// ---------------------------------------------------------------- SCENE (serial)
// The arguments of the SCENE serial command: "<place|auto> [<weather>|auto]", lower or
// upper case, single spaces or more. Sets `place` and `wx` (a key's index, or SCENE_AUTO)
// and returns true; returns false, changing neither, on anything else.
inline bool sceneWordIs(const char *s, int n, const char *key) {
  int i = 0;
  for (; i < n && key[i]; i++) {
    char c = s[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != key[i]) return false;
  }
  return i == n && !key[i];
}
inline int sceneWordFind(const char *s, int n, const char *const *keys, int count) {
  if (sceneWordIs(s, n, "auto")) return SCENE_AUTO;
  for (int k = 0; k < count; k++)
    if (sceneWordIs(s, n, keys[k])) return k;
  return -1;
}
inline bool sceneCmdParse(const char *args, uint8_t &place, uint8_t &wx) {
  const char *w[3];
  int len[3], n = 0;
  for (const char *p = args; *p;) {
    while (*p == ' ') p++;
    if (!*p) break;
    if (n == 3) return false;
    w[n] = p;
    while (*p && *p != ' ') p++;
    len[n] = (int)(p - w[n]);
    n++;
  }
  if (n < 1 || n > 2) return false;
  int pl = sceneWordFind(w[0], len[0], PLACE_KEYS, PLACES);
  int k = n == 2 ? sceneWordFind(w[1], len[1], WX_KEYS, WX_KINDS) : SCENE_AUTO;
  if (pl < 0 || k < 0) return false;
  place = (uint8_t)pl;
  wx = (uint8_t)k;
  return true;
}
