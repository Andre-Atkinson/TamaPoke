// Consistencia de las traducciones (i18n.cpp).
#include "framework.h"
#include "shim/Arduino.h"
#include "shim/Preferences.h"
#include "../i18n.h"
#include "../pet.h"  // MED_COUNT
#include <ctype.h>
#include <string.h>
#include <vector>

static const char *LANG_NAME[LANG_COUNT] = { "ES", "EN", "FR", "DE", "IT", "PT", "JA", "KO" };

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

// La fuente del firmware (glcdfont.h de Arduino_GFX) es una tabla CP437 de 256
// glifos, asi que SI admite acentos, pero cada uno tiene que ocupar UN byte: la
// UI centra con strlen(t)*6, y un caracter UTF-8 de dos bytes descuadraria la
// etiqueta. Este test no prohibe los bytes altos, prohibe el UTF-8.
TEST(i18n, los_acentos_van_en_un_solo_byte_no_en_utf8) {
  // secuencias de arranque de UTF-8 multibyte: 0xC2-0xDF, 0xE0-0xEF, 0xF0-0xF4
  auto arranqueUtf8 = [](unsigned char c) {
    return (c >= 0xC2 && c <= 0xDF) || (c >= 0xE0 && c <= 0xEF) || (c >= 0xF0 && c <= 0xF4);
  };
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    if (LANG_IS_CJK(lang)) continue;  // los CJK van en UTF-8 a proposito, con fuente U8g2
    gLang = (Lang)lang;
    for (int id = 0; id < STR_COUNT; id++) {
      const char *s = T((StrId)id);
      if (!s) continue;
      for (const char *c = s; *c; c++) {
        unsigned char u = (unsigned char)*c;
        if (u < 128) continue;                 // ASCII: siempre vale
        if (!arranqueUtf8(u)) continue;        // byte alto suelto: es CP437, vale
        // un arranque UTF-8 solo es sospechoso si le sigue un byte de
        // continuacion (0x80-0xBF): entonces son dos glifos donde deberia ir uno
        unsigned char sig = (unsigned char)*(c + 1);
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "parece UTF-8 (0x%02X 0x%02X) en [%s][id %d]: los acentos van en octal, "
                 "un byte por caracter, o se descuadra el centrado", u, sig, LANG_NAME[lang], id);
        CHECK_MSG(!(sig >= 0x80 && sig <= 0xBF), msg);
      }
    }
  }
  gLang = LANG_DEFAULT;
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

TEST(i18n, las_medallas_estan_en_todos_los_idiomas) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    // los topes de longitud van en BYTES, que solo equivalen a caracteres con la
    // fuente CP437. En CJK cada kana ocupa 3 bytes y el limite real es de anchura
    // en pantalla, que aqui no se puede medir: eso se comprueba en la placa.
    const bool cjk = LANG_IS_CJK(lang);
    for (int m = 0; m < MED_COUNT; m++) {
      const char *n = medalName(m), *l = medalLabel(m), *d = medalDesc(m);
      char where[64];
      snprintf(where, sizeof(where), "[%s][medalla %d]", LANG_NAME[lang], m);
      CHECK_MSG(n && n[0], std::string("medalName vacio ") + where);
      CHECK_MSG(l && l[0], std::string("medalLabel vacio ") + where);
      CHECK_MSG(d && d[0], std::string("medalDesc vacio ") + where);
      // la etiqueta corta va en una casilla de la cuadricula de medallas
      if (!cjk && l) CHECK_MSG(strlen(l) <= 6, std::string("etiqueta demasiado larga ") + where + ": " + l);
      if (!cjk && n) CHECK_MSG(strlen(n) <= 12, std::string("nombre demasiado largo ") + where + ": " + n);
      if (!cjk && d) CHECK_MSG(strlen(d) <= 16, std::string("descripcion demasiado larga ") + where + ": " + d);
    }
  }
  gLang = LANG_DEFAULT;
}

TEST(i18n, las_medallas_no_llevan_acentos) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    if (LANG_IS_CJK(lang)) continue;  // los CJK van en UTF-8 con fuente U8g2
    gLang = (Lang)lang;
    for (int m = 0; m < MED_COUNT; m++) {
      const char *tab[3] = { medalName(m), medalLabel(m), medalDesc(m) };
      for (int t = 0; t < 3; t++)
        for (const char *c = tab[t]; c && *c; c++)
          CHECK_MSG((unsigned char)*c < 128, std::string("no ASCII: ") + tab[t]);
    }
  }
  gLang = LANG_DEFAULT;
}

TEST(i18n, el_idioma_se_guarda_y_se_recupera) {
  mockNvsReset();
  setLang(LANG_DE);
  CHECK_EQ((int)gLang, (int)LANG_DE);
  gLang = LANG_ES;
  loadLang();
  CHECK_EQ((int)gLang, (int)LANG_DE);
  gLang = LANG_DEFAULT;
}

TEST(i18n, sin_ajuste_guardado_arranca_en_el_idioma_por_defecto) {
  mockNvsReset();
  gLang = LANG_IT;
  loadLang();
  CHECK_EQ((int)gLang, (int)LANG_DEFAULT);
  gLang = LANG_DEFAULT;
}

TEST(i18n, un_idioma_invalido_en_la_nvs_no_rompe_nada) {
  mockNvsReset();
  Preferences p;
  p.begin("tamapoke", false);
  p.putUChar("lang", 200);  // valor corrupto
  p.end();
  gLang = LANG_ES;
  loadLang();
  CHECK_EQ((int)gLang, (int)LANG_DEFAULT);
  gLang = LANG_DEFAULT;
}

TEST(i18n, setLang_rechaza_valores_fuera_de_rango) {
  mockNvsReset();
  setLang(LANG_FR);
  setLang((Lang)LANG_COUNT);
  CHECK_EQ((int)gLang, (int)LANG_FR);
  setLang((Lang)250);
  CHECK_EQ((int)gLang, (int)LANG_FR);
  gLang = LANG_DEFAULT;
}

// La pagina Care: las fuentes CJK del firmware son subconjuntos (unifont_t_japanese3 tiene
// pocos kanji, unifont_t_korean2 solo hangul comun), asi que el japones va solo en kana y
// el coreano solo en silabas hangul. Los codigos concretos se comprobaron contra los datos
// de U8g2 a mano; este test impide que se cuele un kanji o un simbolo raro.
static std::vector<uint32_t> codepoints(const char *s) {
  std::vector<uint32_t> out;
  for (const unsigned char *p = (const unsigned char *)s; *p;) {
    uint32_t c = *p;
    int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    c = n == 1 ? c : n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 0x07;
    for (int i = 1; i < n; i++) c = (c << 6) | (p[i] & 0x3F);
    out.push_back(c);
    p += n;
  }
  return out;
}

TEST(i18n, la_pagina_care_en_japones_va_solo_en_kana_y_en_coreano_solo_en_hangul) {
  const StrId care[] = { S_CARE, S_PACE, S_PACE_NORMAL, S_PACE_RELAXED, S_BEDTIME, S_BED_OFF,
                         S_CHOOSE_EGG, S_EGG_TAP_AGAIN, S_EGG_COMMON, S_EGGS, S_EGG_CHOOSE,
                         S_EGG_SURPRISE };
  for (StrId id : care) {
    gLang = LANG_JA;
    for (uint32_t c : codepoints(T(id)))
      CHECK_MSG((c >= 0x3041 && c <= 0x30FF) || c == ' ', std::string("no es kana: ") + T(id));
    gLang = LANG_KO;
    for (uint32_t c : codepoints(T(id)))
      CHECK_MSG((c >= 0xAC00 && c <= 0xD7A3) || c == ' ', std::string("no es hangul: ") + T(id));
  }
  gLang = LANG_DEFAULT;
}

// las etiquetas de la pagina Care van en botones de 320 px a tamano 4 (24 px por letra)
TEST(i18n, los_valores_de_la_pagina_care_caben_en_el_boton) {
  const StrId vals[] = { S_PACE_NORMAL, S_PACE_RELAXED, S_BED_OFF, S_EGG_CHOOSE, S_EGG_SURPRISE };
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    if (LANG_IS_CJK(lang)) continue;  // el ancho CJK solo se puede ver en la placa
    gLang = (Lang)lang;
    for (StrId id : vals) CHECK_MSG(strlen(T(id)) * 24 <= 300, std::string("demasiado ancho: ") + T(id));
  }
  gLang = LANG_DEFAULT;
}

// pantalla de eleccion de huevo, tamano 2 (12 px por letra): el titulo va a y=84,
// donde el circulo mide ~350 px, y la pista bajo los huevos a y=336, donde mide ~410
TEST(i18n, los_textos_de_la_eleccion_de_huevo_caben) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    if (LANG_IS_CJK(lang)) continue;  // el ancho CJK solo se puede ver en la placa
    gLang = (Lang)lang;
    CHECK_MSG(strlen(T(S_CHOOSE_EGG)) * 12 <= 330, std::string("titulo demasiado ancho: ") + T(S_CHOOSE_EGG));
    for (StrId id : { S_EGG_TAP_AGAIN, S_EGG_COMMON, S_EGG_RARE, S_EGG_LEGEND })
      CHECK_MSG(strlen(T(id)) * 12 <= 390, std::string("demasiado ancho: ") + T(id));
    CHECK_MSG(strlen(T(S_EGGS)) * 12 <= 300, std::string("etiqueta demasiado ancha: ") + T(S_EGGS));
  }
  gLang = LANG_DEFAULT;
}
// Minijuegos de Play: los nombres y pistas de la portada. Japones solo en kana y coreano
// solo en silabas hangul (mas espacio y la puntuacion ASCII "!.?", que las dos fuentes
// U8g2 traen); en los idiomas latinos, que quepan: el nombre va a tamano 3 (18 px por
// letra, 400 px) y las pistas a tamano 2 (12 px por letra, 400 px).
static const StrId GAME_STRS[] = { S_GAME_KEEPY, S_GAME_CATCH, S_GAME_PEEK, S_GAME_SONG,
                                   S_HINT_KEEPY, S_HINT_CATCH, S_HINT_PEEK, S_HINT_SONG,
                                   S_SONG_LISTEN, S_SONG_TURN };
static bool asciiPunct(uint32_t c) { return c == ' ' || c == '!' || c == '.' || c == '?'; }

TEST(i18n, los_minijuegos_en_japones_van_solo_en_kana_y_en_coreano_solo_en_hangul) {
  for (StrId id : GAME_STRS) {
    gLang = LANG_JA;
    for (uint32_t c : codepoints(T(id)))
      CHECK_MSG((c >= 0x3041 && c <= 0x30FF) || asciiPunct(c), std::string("no es kana: ") + T(id));
    gLang = LANG_KO;
    for (uint32_t c : codepoints(T(id)))
      CHECK_MSG((c >= 0xAC00 && c <= 0xD7A3) || asciiPunct(c), std::string("no es hangul: ") + T(id));
  }
  gLang = LANG_DEFAULT;
}

TEST(i18n, los_textos_de_los_minijuegos_caben_en_pantalla) {
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    if (LANG_IS_CJK(lang)) continue;  // el ancho CJK se mira en el simulador
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
