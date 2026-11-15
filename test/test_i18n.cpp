// Consistencia de las traducciones (i18n.cpp).
#include "framework.h"
#include "shim/Arduino.h"
#include "shim/Preferences.h"
#include "../i18n.h"
#include "../pet.h"  // MED_COUNT
#include "../ptglyphs.h"
#include <ctype.h>
#include <string.h>
#include <functional>
#include <vector>

static const char *LANG_NAME[LANG_COUNT] = { "EN", "PT" };

// extrae la firma de los especificadores de printf de una cadena: "%lud" -> "lu"
static std::vector<std::string> specs(const char *s) {
  std::vector<std::string> out;
  const char *p = s;
  while (*p) {
    if (*p != '%') { p++; continue; }
    p++;
    if (*p == '%') { p++; continue; }   // %% es un literal
    std::string sig;
    while (*p && strchr("-+ #0", *p)) p++;             // banderas
    while (*p && isdigit((unsigned char)*p)) p++;      // ancho
    if (*p == '.') { p++; while (*p && isdigit((unsigned char)*p)) p++; }
    while (*p && strchr("lhzjt", *p)) { sig += *p; p++; }  // modificador
    if (!*p) { out.push_back(sig + "<incompleto>"); break; }
    sig += *p++;
    out.push_back(sig);
  }
  return out;
}

static std::string joinSpecs(const std::vector<std::string> &v) {
  std::string s;
  for (size_t i = 0; i < v.size(); i++) { if (i) s += ","; s += "%" + v[i]; }
  return s.empty() ? "(sin formato)" : s;
}

// firma esperada de cada cadena con formato, sacada de las llamadas del sketch
struct ExpectedFmt { StrId id; const char *sig; };
static const ExpectedFmt EXPECTED[] = {
  { S_POKEDEX_FMT,     "%u" },
  { S_NAME_FMT,        "%s,%s,%u" },
  { S_RELEASE_FMT,     "%s" },
  { S_HITS_FMT,        "%u" },
  { S_STR_GAIN_FMT,    "%u" },
  { S_RECORD_FMT,      "%u" },
  { S_SCORE_FMT,       "%u" },
  { S_STREAK_DAYS_FMT, "%u" },
  { S_STREAK_FMT,      "%u,%u" },
  { S_INFO_FMT,        "%s,%lu" },
  { S_MEDALS_FMT,      "%d,%d" },
  { S_REC_FMT,         "%u" },
  { S_LVL_FMT,         "%u" },
  { S_NEXT_LVL_FMT,    "%u,%u" },
  { S_EVO_IN_FMT,      "%u" },
  { S_MISTAKES_FMT,    "%u" },
  { S_FAREWELL_BTN,    "%s" },
  { S_RUNAWAY_BTN,     "%s" },
  { S_SAY_LOVE_FMT,    "%s" },  // the player's name (bubble.cpp)
};
static const int NUM_EXPECTED = (int)(sizeof(EXPECTED) / sizeof(EXPECTED[0]));

static const char *expectedSig(int id) {
  for (int i = 0; i < NUM_EXPECTED; i++)
    if ((int)EXPECTED[i].id == id) return EXPECTED[i].sig;
  return "(sin formato)";
}

TEST(i18n, ningun_idioma_tiene_huecos) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (int id = 0; id < STR_COUNT; id++) {
      const char *s = T((StrId)id);
      char where[64];
      snprintf(where, sizeof(where), "[%s][id %d]", LANG_NAME[lang], id);
      CHECK_MSG(s != NULL, where);
      if (s) CHECK_MSG(s[0] != 0, std::string("cadena vacia ") + where);
    }
  }
  gLang = LANG_DEFAULT;
}

// Every text in every table, for the byte checks below: STRINGS, then the medal names
// and descriptions. `where` names it in a failure.
static void eachText(const std::function<void(const char *s, const std::string &where)> &fn) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (int id = 0; id < STR_COUNT; id++)
      fn(T((StrId)id), std::string("[") + LANG_NAME[lang] + "][id " + std::to_string(id) + "]");
    for (int m = 0; m < MED_COUNT; m++) {
      fn(medalName(m), std::string("[") + LANG_NAME[lang] + "][medalName " + std::to_string(m) + "]");
      fn(medalDesc(m), std::string("[") + LANG_NAME[lang] + "][medalDesc " + std::to_string(m) + "]");
    }
  }
  gLang = LANG_DEFAULT;
}

// La fuente del firmware (glcdfont.h de Arduino_GFX) es una tabla CP437 de 256
// glifos, asi que SI admite acentos, pero cada uno tiene que ocupar UN byte: la
// UI centra con strlen(t)*6, y un caracter UTF-8 de dos bytes descuadraria la
// etiqueta. Este test no prohibe los bytes altos, prohibe el UTF-8.
TEST(i18n, los_acentos_van_en_un_solo_byte_no_en_utf8) {
  // secuencias de arranque de UTF-8 multibyte: 0xC2-0xDF, 0xE0-0xEF, 0xF0-0xF4
  auto arranqueUtf8 = [](unsigned char c) {
    return (c >= 0xC2 && c <= 0xDF) || (c >= 0xE0 && c <= 0xEF) || (c >= 0xF0 && c <= 0xF4);
  };
  eachText([&](const char *s, const std::string &where) {
      CHECK_MSG(s != NULL, where);
      if (!s) return;
      for (const char *c = s; *c; c++) {
        unsigned char u = (unsigned char)*c;
        if (u < 128) continue;                 // ASCII: siempre vale
        if (!arranqueUtf8(u)) continue;        // byte alto suelto: es CP437, vale
        // un arranque UTF-8 solo es sospechoso si le sigue un byte de
        // continuacion (0x80-0xBF): entonces son dos glifos donde deberia ir uno
        unsigned char sig = (unsigned char)*(c + 1);
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "parece UTF-8 (0x%02X 0x%02X) en %s: los acentos van en octal, "
                 "un byte por caracter, o se descuadra el centrado", u, sig, where.c_str());
        CHECK_MSG(!(sig >= 0x80 && sig <= 0xBF), msg);
      }
  });
}

// #61: a byte >= 0x80 must be a Portuguese letter the UI font draws: one of the CP437
// accented letters Portuguese uses, or one of ptglyphs.h's 12. A mistyped octal draws
// something else without a word (Latin-1 a-acute \341 is a sharp s in CP437, \244 is
// n-tilde), and so would a byte meant for a glyph that is not in ptglyphs.h.
static bool portugueseCp437(unsigned char u) {
  static const unsigned char OK[] = {
    0x80,  // C-cedilla
    0x82, 0x83, 0x85, 0x87, 0x88,  // e' a^ a` c-cedilla e^
    0x90, 0x93,  // E' o^
    0xA0, 0xA1, 0xA2, 0xA3,  // a' i' o' u'
  };
  for (unsigned char o : OK)
    if (o == u) return true;
  return false;
}

TEST(i18n, every_accented_byte_is_a_portuguese_letter_the_font_draws) {
  int seen = 0;
  eachText([&](const char *s, const std::string &where) {
    for (const char *c = s; c && *c; c++) {
      unsigned char u = (unsigned char)*c;
      if (u < 0x80) continue;
      seen++;
      char msg[160];
      snprintf(msg, sizeof msg, "byte 0x%02X in %s (\"%s\") is not a Portuguese letter the font has",
               u, where.c_str(), s);
      CHECK_MSG(portugueseCp437(u) || ptGlyphIndex(u) >= 0, msg);
    }
  });
  CHECK(seen > 20);  // the PT row really has accents to check
}

// un %u de mas o de menos en una traduccion es una lectura de basura de la pila
TEST(i18n, los_formatos_coinciden_en_todos_los_idiomas) {
  for (int id = 0; id < STR_COUNT; id++) {
    gLang = LANG_EN;
    std::vector<std::string> ref = specs(T((StrId)id));
    for (int lang = 0; lang < LANG_COUNT; lang++) {
      gLang = (Lang)lang;
      std::vector<std::string> got = specs(T((StrId)id));
      char msg[256];
      snprintf(msg, sizeof(msg), "[%s][id %d] \"%s\": %s en vez de %s (EN)", LANG_NAME[lang], id,
               T((StrId)id), joinSpecs(got).c_str(), joinSpecs(ref).c_str());
      CHECK_MSG(got == ref, msg);
    }
  }
  gLang = LANG_DEFAULT;
}

// ...y ademas tienen que cuadrar con lo que pasa el sketch en cada llamada
TEST(i18n, los_formatos_cuadran_con_las_llamadas_del_sketch) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (int id = 0; id < STR_COUNT; id++) {
      std::string got = joinSpecs(specs(T((StrId)id)));
      const char *want = expectedSig(id);
      char msg[256];
      snprintf(msg, sizeof(msg), "[%s][id %d] \"%s\": %s, se esperaba %s", LANG_NAME[lang], id,
               T((StrId)id), got.c_str(), want);
      CHECK_MSG(got == want, msg);
    }
  }
  gLang = LANG_DEFAULT;
}

// Medal names go in the celebration banner (size 2, a 320 px box) and the descriptions
// in the medal grid: size 2 from 44 px into a 196 px box, so 12 letters (144 px) before
// the box's edge.
TEST(i18n, las_medallas_estan_en_todos_los_idiomas) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (int m = 0; m < MED_COUNT; m++) {
      const char *n = medalName(m), *d = medalDesc(m);
      char where[64];
      snprintf(where, sizeof(where), "[%s][medalla %d]", LANG_NAME[lang], m);
      CHECK_MSG(n && n[0], std::string("medalName vacio ") + where);
      CHECK_MSG(d && d[0], std::string("medalDesc vacio ") + where);
      if (n) CHECK_MSG(strlen(n) <= 12, std::string("nombre demasiado largo ") + where + ": " + n);
      if (d) CHECK_MSG(strlen(d) <= 12, std::string("descripcion demasiado larga ") + where + ": " + d);
    }
  }
  gLang = LANG_DEFAULT;
}

// ---------- the `lang` key (#61) ----------
// The stored byte keeps the old eight-language enum's codes: 1 is English, 5 is
// Portuguese, anything else loads as English (i18n.h). Each case below is one the
// issue lists.
static void storeLang(uint8_t v) {
  mockNvsReset();
  Preferences p;
  p.begin("tamapoke", false);
  p.putUChar("lang", v);
  p.end();
}

static uint8_t storedLang() {
  Preferences p;
  p.begin("tamapoke", true);
  uint8_t v = p.getUChar("lang", 0xEE);  // 0xEE: no key
  p.end();
  return v;
}

TEST(i18n, a_stored_1_loads_english) {
  storeLang(1);
  gLang = LANG_PT;
  loadLang();
  CHECK_EQ((int)gLang, (int)LANG_EN);
  gLang = LANG_DEFAULT;
}

TEST(i18n, a_stored_5_loads_portuguese) {
  storeLang(5);
  gLang = LANG_EN;
  loadLang();
  CHECK_EQ((int)gLang, (int)LANG_PT);
  gLang = LANG_DEFAULT;
}

// Spanish, French, German, Italian, Japanese and Korean were removed: a board set to
// one of them shows English after the update, and the stored byte is left alone.
TEST(i18n, a_removed_language_loads_english_and_keeps_its_byte) {
  const uint8_t removed[] = {0, 2, 3, 4, 6, 7};
  for (uint8_t v : removed) {
    storeLang(v);
    gLang = LANG_PT;
    loadLang();
    CHECK_MSG(gLang == LANG_EN, std::string("stored ") + std::to_string(v) + " did not load English");
    CHECK_EQ((int)storedLang(), (int)v);  // loadLang() never writes
  }
  gLang = LANG_DEFAULT;
}

TEST(i18n, an_out_of_range_byte_loads_english) {
  const uint8_t bad[] = {8, 9, 200, 255};
  for (uint8_t v : bad) {
    storeLang(v);
    gLang = LANG_PT;
    loadLang();
    CHECK_MSG(gLang == LANG_EN, std::string("stored ") + std::to_string(v) + " did not load English");
  }
  gLang = LANG_DEFAULT;
}

TEST(i18n, no_stored_language_loads_english) {
  mockNvsReset();
  gLang = LANG_PT;
  loadLang();
  CHECK_EQ((int)gLang, (int)LANG_EN);
  CHECK_EQ((int)storedLang(), 0xEE);  // and writes nothing
  gLang = LANG_DEFAULT;
}

// setLang() writes the old codes, so the new loadLang() and an older image both read
// the same language back.
TEST(i18n, setLang_writes_a_code_loadLang_reads_back) {
  mockNvsReset();
  setLang(LANG_PT);
  CHECK_EQ((int)gLang, (int)LANG_PT);
  CHECK_EQ((int)storedLang(), 5);
  gLang = LANG_EN;
  loadLang();
  CHECK_EQ((int)gLang, (int)LANG_PT);

  setLang(LANG_EN);
  CHECK_EQ((int)gLang, (int)LANG_EN);
  CHECK_EQ((int)storedLang(), 1);
  gLang = LANG_PT;
  loadLang();
  CHECK_EQ((int)gLang, (int)LANG_EN);
  gLang = LANG_DEFAULT;
}

TEST(i18n, setLang_rechaza_valores_fuera_de_rango) {
  mockNvsReset();
  setLang(LANG_PT);
  setLang((Lang)LANG_COUNT);
  CHECK_EQ((int)gLang, (int)LANG_PT);
  setLang((Lang)250);
  CHECK_EQ((int)gLang, (int)LANG_PT);
  CHECK_EQ((int)storedLang(), 5);  // nor written
  gLang = LANG_DEFAULT;
}

// ---------- ptglyphs.h (#61) ----------
// The packed bits are what the GFXfont draws: each must match its art. GFXfont order:
// rows one after another, PTG_COLS bits each, most significant bit first.
TEST(PtGlyphs, the_packed_bits_match_the_art) {
  for (int g = 0; g < PTG_COUNT; g++)
    for (int r = 0; r < PTG_ROWS; r++)
      for (int c = 0; c < PTG_COLS; c++) {
        int i = r * PTG_COLS + c;
        bool bit = PT_GLYPH_BITS.b[g * PTG_BYTES + i / 8] & (0x80 >> (i % 8));
        bool ink = PT_GLYPHS[g].art[r][c] == '#';
        char msg[80];
        snprintf(msg, sizeof msg, "glyph 0x%02X row %d column %d", PT_GLYPHS[g].code, r, c);
        CHECK_MSG(bit == ink, msg);
      }
}

// Like a classic 5x7 glyph: ink in columns 0-4 and rows 0-6 only (the 6th column is the
// gap to the next letter, row 7 the descender row), only '#' and '.', and some ink.
TEST(PtGlyphs, each_glyph_fits_the_classic_cell) {
  for (int g = 0; g < PTG_COUNT; g++) {
    int ink = 0;
    for (int r = 0; r < PTG_ROWS; r++) {
      CHECK_EQ((int)strlen(PT_GLYPHS[g].art[r]), PTG_COLS);
      for (int c = 0; c < PTG_COLS; c++) {
        char px = PT_GLYPHS[g].art[r][c];
        char msg[80];
        snprintf(msg, sizeof msg, "glyph 0x%02X row %d column %d", PT_GLYPHS[g].code, r, c);
        CHECK_MSG(px == '#' || px == '.', msg);
        if (px == '#') {
          ink++;
          CHECK_MSG(c < 5 && r < 7, std::string("ink outside the 5x7 cell: ") + msg);
        }
      }
    }
    CHECK(ink > 0);
  }
}

TEST(PtGlyphs, the_lookup_finds_the_12_codes_and_nothing_else) {
  int found = 0;
  for (int b = 0; b < 256; b++) {
    int i = ptGlyphIndex((uint8_t)b);
    if (i < 0) continue;
    found++;
    CHECK_EQ((int)PT_GLYPHS[i].code, b);
  }
  CHECK_EQ(found, PTG_COUNT);
  CHECK_EQ(PTG_COUNT, 12);
  for (int g = 0; g < PTG_COUNT; g++) CHECK_EQ(ptGlyphIndex(PT_GLYPHS[g].code), g);
  // ASCII, the CP437 letters Portuguese uses and the bytes printT() must hand on to the
  // classic font are not glyphs here
  for (uint8_t b : {(uint8_t)'A', (uint8_t)'a', (uint8_t)'\n', (uint8_t)0x80, (uint8_t)0x87, (uint8_t)0xA0})
    CHECK_EQ(ptGlyphIndex(b), -1);
}

// las etiquetas de la pagina Care van en botones de 320 px a tamano 4 (24 px por letra)
TEST(i18n, los_valores_de_la_pagina_care_caben_en_el_boton) {
  const StrId vals[] = { S_PACE_NORMAL, S_PACE_RELAXED, S_BED_OFF, S_EGG_CHOOSE, S_EGG_SURPRISE };
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (StrId id : vals) CHECK_MSG(strlen(T(id)) * 24 <= 300, std::string("demasiado ancho: ") + T(id));
  }
  gLang = LANG_DEFAULT;
}

// pantalla de eleccion de huevo, tamano 2 (12 px por letra): el titulo va a y=84,
// donde el circulo mide ~350 px, y la pista bajo los huevos a y=336, donde mide ~410
TEST(i18n, los_textos_de_la_eleccion_de_huevo_caben) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    CHECK_MSG(strlen(T(S_CHOOSE_EGG)) * 12 <= 330, std::string("titulo demasiado ancho: ") + T(S_CHOOSE_EGG));
    for (StrId id : { S_EGG_TAP_AGAIN, S_EGG_COMMON, S_EGG_RARE, S_EGG_LEGEND })
      CHECK_MSG(strlen(T(id)) * 12 <= 390, std::string("demasiado ancho: ") + T(id));
    CHECK_MSG(strlen(T(S_EGGS)) * 12 <= 300, std::string("etiqueta demasiado ancha: ") + T(S_EGGS));
  }
  gLang = LANG_DEFAULT;
}
// Minijuegos de Play: los nombres y pistas de la portada tienen que caber: el nombre va a
// tamano 3 (18 px por letra, 400 px) y las pistas a tamano 2 (12 px por letra, 400 px).
static const StrId GAME_STRS[] = { S_GAME_KEEPY, S_GAME_CATCH, S_GAME_PEEK, S_GAME_SONG,
                                   S_HINT_KEEPY, S_HINT_CATCH, S_HINT_PEEK, S_HINT_SONG,
                                   S_SONG_LISTEN, S_SONG_TURN };

TEST(i18n, los_textos_de_los_minijuegos_caben_en_pantalla) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (StrId id : GAME_STRS) {
      const char *t = T(id);
      CHECK_MSG(t && t[0], std::string("vacio en ") + LANG_NAME[lang]);
      bool title = id <= S_GAME_SONG || id >= S_SONG_LISTEN;  // tamano 3
      size_t max = title ? 22 : 33;
      CHECK_MSG(strlen(t) <= max, std::string("no cabe: ") + t);
    }
  }
  gLang = LANG_DEFAULT;
}

// el orden de los nombres y pistas sigue a GameId: el sketch los indexa con el juego
TEST(i18n, nombres_y_pistas_de_los_minijuegos_siguen_el_orden_de_GameId) {
  CHECK_EQ((int)S_GAME_CATCH - (int)S_GAME_KEEPY, (int)GAME_CATCH);
  CHECK_EQ((int)S_GAME_PEEK - (int)S_GAME_KEEPY, (int)GAME_PEEK);
  CHECK_EQ((int)S_GAME_SONG - (int)S_GAME_KEEPY, (int)GAME_SONG);
  CHECK_EQ((int)S_HINT_SONG - (int)S_HINT_KEEPY, (int)GAME_SONG);
  CHECK_EQ((int)S_HINT_KEEPY - (int)S_GAME_KEEPY, (int)GAME_COUNT);
}

// The hold-to-exit button is a fixed touch target, EXIT_W = 160 px in every minigame
// (gamecore.h) and on the 1.75C's strength bag (trainingtouch.h); its label at size 2
// keeps 8 px inside each end, so 12 letters (#61: "Mantem para sair" spilled out).
TEST(i18n, the_hold_to_exit_label_fits_its_button) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    CHECK_MSG(strlen(T(S_HOLD_EXIT)) * 12 <= 160 - 16, std::string("too wide: ") + T(S_HOLD_EXIT));
  }
  gLang = LANG_DEFAULT;
}

// The clock screen's cancel hint: size 2 at y=410, where the lowest row of ink (y=423)
// has a 269 px chord; with the 12 px edge margin the settings pills keep
// (settingslayout.h) that is 245 px, so 20 letters (#61, as #54 found for Spanish).
TEST(i18n, the_clock_cancel_hint_fits_the_round_edge) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    CHECK_MSG(strlen(T(S_CLOCK_CANCEL)) * 12 <= 245, std::string("too wide: ") + T(S_CLOCK_CANCEL));
  }
  gLang = LANG_DEFAULT;
}

// The 1.75C toasts (#61): size 2 in a 380 px box (flushUI), kept a letter (12 px) clear of
// each end: at most 356 px, 29 letters.
TEST(i18n, the_toasts_fit_their_box) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (StrId id : {S_TOAST_SOUND_ON, S_TOAST_MUTED, S_TOAST_LOCKED, S_TOAST_UNLOCKED})
      CHECK_MSG(strlen(T(id)) * 12 <= 380 - 2 * 12, std::string("too wide: ") + T(id));
  }
  gLang = LANG_DEFAULT;
}

// Clock screen title while the clock is stale (#10): size 3 (18 px per letter) at y=44,
// where the round panel is 272 px wide, so at most 14 letters (252 px).
TEST(i18n, the_clock_prompt_title_fits) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    const char *t = T(S_CLOCK_ASK);
    CHECK_MSG(t && t[0], std::string("empty in ") + LANG_NAME[lang]);
    CHECK_MSG(strlen(t) <= 14, std::string("too wide: ") + t);
  }
  gLang = LANG_DEFAULT;
}
