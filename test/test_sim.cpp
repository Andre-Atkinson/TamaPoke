// Simulador de escritorio (sim/): lectura de guiones, avance del reloj falso y el
// escritor de PNG. La parte que corre el firmware se prueba con tools/sim.sh.
#include "framework.h"
#include "../sim/png.h"
#include "../sim/runner.h"
#include "../sim/script.h"
#include "../i18n.h"
#include <stdio.h>
#include <sys/stat.h>

// ---------------------------------------------------------------- duraciones
TEST(SimScript, duraciones_con_unidades_y_combinadas) {
  uint64_t ms = 0;
  CHECK(simParseDuration("500ms", ms)); CHECK_EQ(ms, (uint64_t)500);
  CHECK(simParseDuration("2s", ms));    CHECK_EQ(ms, (uint64_t)2000);
  CHECK(simParseDuration("5m", ms));    CHECK_EQ(ms, (uint64_t)300000);
  CHECK(simParseDuration("2h", ms));    CHECK_EQ(ms, (uint64_t)7200000);
  CHECK(simParseDuration("1d", ms));    CHECK_EQ(ms, (uint64_t)86400000);
  CHECK(simParseDuration("1h30m", ms)); CHECK_EQ(ms, (uint64_t)5400000);
  CHECK(simParseDuration("1d2h3m4s5ms", ms));
  CHECK_EQ(ms, (uint64_t)(86400000 + 2 * 3600000 + 3 * 60000 + 4000 + 5));
  CHECK(simParseDuration("250", ms));   CHECK_EQ(ms, (uint64_t)250);  // un numero solo = ms
  CHECK(simParseDuration("0", ms));     CHECK_EQ(ms, (uint64_t)0);
}

TEST(SimScript, duraciones_mal_escritas_se_rechazan) {
  uint64_t ms = 77;
  CHECK(!simParseDuration("", ms));
  CHECK(!simParseDuration("h", ms));
  CHECK(!simParseDuration("2x", ms));
  CHECK(!simParseDuration("30m1h", ms));  // de mayor a menor
  CHECK(!simParseDuration("1h1h", ms));
  CHECK(!simParseDuration("-5s", ms));
  CHECK(!simParseDuration("1.5h", ms));
  CHECK_EQ(ms, (uint64_t)77);  // sin tocar la salida si falla
}

// ---------------------------------------------------------------- reloj
TEST(SimScript, la_hora_del_guion_es_un_epoch_local) {
  uint32_t e = 0;
  CHECK(simParseClock("2026-11-13 10:00", e));     CHECK_EQ(e, (uint32_t)1794564000UL);
  CHECK(simParseClock("2026-11-13T10:00", e));     CHECK_EQ(e, (uint32_t)1794564000UL);
  CHECK(simParseClock("2028-02-29 23:59:59", e));  CHECK_EQ(e, (uint32_t)1835481599UL);  // bisiesto
  CHECK(simParseClock("2025-01-01 00:00", e));     CHECK_EQ(e, (uint32_t)1735689600UL);  // minimo de la 1.75C
  CHECK(simParseClock("1794564000", e));           CHECK_EQ(e, (uint32_t)1794564000UL);
}

TEST(SimScript, horas_imposibles_se_rechazan) {
  uint32_t e = 5;
  CHECK(!simParseClock("2026-02-29 10:00", e));  // 2026 no es bisiesto
  CHECK(!simParseClock("2026-13-01 10:00", e));
  CHECK(!simParseClock("2026-04-31 10:00", e));
  CHECK(!simParseClock("2026-11-13 24:00", e));
  CHECK(!simParseClock("2026-11-13 10:60", e));
  CHECK(!simParseClock("2026-11-13", e));
  CHECK(!simParseClock("2026-11-13 10:00x", e));
  CHECK(!simParseClock("2026-11-13_10:00", e));
  CHECK(!simParseClock("99999999999", e));       // no cabe en 32 bits
  CHECK_EQ(e, (uint32_t)5);
}

// ---------------------------------------------------------------- idiomas
TEST(SimScript, los_codigos_de_idioma_siguen_el_orden_de_i18n) {
  CHECK_EQ(simLangIndex("es"), (int)LANG_ES);
  CHECK_EQ(simLangIndex("en"), (int)LANG_EN);
  CHECK_EQ(simLangIndex("fr"), (int)LANG_FR);
  CHECK_EQ(simLangIndex("de"), (int)LANG_DE);
  CHECK_EQ(simLangIndex("it"), (int)LANG_IT);
  CHECK_EQ(simLangIndex("pt"), (int)LANG_PT);
  CHECK_EQ(simLangIndex("ja"), (int)LANG_JA);
  CHECK_EQ(simLangIndex("ko"), (int)LANG_KO);
  CHECK_EQ(simLangIndex("xx"), -1);
  CHECK_EQ((int)LANG_COUNT, 8);  // un idioma nuevo tambien necesita su codigo en sim/script.cpp
  for (int i = 0; i < LANG_COUNT; i++) CHECK_EQ(simLangIndex(simLangCode(i)), i);
}

// ---------------------------------------------------------------- guiones
TEST(SimScript, un_guion_completo_se_lee_en_orden) {
  SimScript s = simParseScript(
      "# preparacion\n"
      "lang ja\n"
      "clock 2026-11-13 22:30\n"
      "battery 40\n"
      "usb on   # enchufado\n"
      "\n"
      "tap 233 220\n"
      "hold 200 210 3s\n"
      "swipe left\n"
      "swipe 10 20 300 40 500ms\n"
      "press pwr 3\n"
      "press boot\n"
      "advance 2h\n"
      "serial NICK A#1\n"
      "shot care-page_ja.1\n");
  CHECK_EQ(s.error, std::string(""));
  CHECK_EQ(s.cmds.size(), (size_t)13);
  if (s.cmds.size() != 13) return;
  CHECK_EQ((int)s.cmds[0].op, (int)OP_LANG);    CHECK_EQ(s.cmds[0].a, (int32_t)LANG_JA);
  CHECK_EQ((int)s.cmds[1].op, (int)OP_CLOCK);   CHECK_EQ(s.cmds[1].ms, (uint64_t)(1794564000UL + 12 * 3600 + 1800));
  CHECK_EQ((int)s.cmds[2].op, (int)OP_BATTERY); CHECK_EQ(s.cmds[2].a, (int32_t)40);
  CHECK_EQ((int)s.cmds[3].op, (int)OP_USB);     CHECK_EQ(s.cmds[3].a, (int32_t)1);
  CHECK_EQ((int)s.cmds[4].op, (int)OP_TAP);     CHECK_EQ(s.cmds[4].a, (int32_t)233); CHECK_EQ(s.cmds[4].b, (int32_t)220);
  CHECK_EQ(s.cmds[4].line, 7);
  CHECK_EQ((int)s.cmds[5].op, (int)OP_HOLD);    CHECK_EQ(s.cmds[5].ms, (uint64_t)3000);
  CHECK_EQ((int)s.cmds[6].op, (int)OP_SWIPE);
  CHECK_EQ((int)s.cmds[7].op, (int)OP_SWIPE);   CHECK_EQ(s.cmds[7].c, (int32_t)300); CHECK_EQ(s.cmds[7].ms, (uint64_t)500);
  CHECK_EQ((int)s.cmds[8].op, (int)OP_PRESS_PWR);  CHECK_EQ(s.cmds[8].a, (int32_t)3);
  CHECK_EQ((int)s.cmds[9].op, (int)OP_PRESS_BOOT); CHECK_EQ(s.cmds[9].a, (int32_t)1);
  CHECK_EQ((int)s.cmds[10].op, (int)OP_WAIT);   CHECK_EQ(s.cmds[10].ms, (uint64_t)7200000);
  CHECK_EQ((int)s.cmds[11].op, (int)OP_SERIAL); CHECK_EQ(s.cmds[11].text, std::string("NICK A#1"));  // '#' no corta
  CHECK_EQ((int)s.cmds[12].op, (int)OP_SHOT);   CHECK_EQ(s.cmds[12].text, std::string("care-page_ja.1"));
}

// los deslizamientos cortos tienen que pasar los umbrales de handleTouch(): > 80 px en
// su eje, < 70 px en el otro
TEST(SimScript, los_deslizamientos_cortos_superan_el_umbral_del_firmware) {
  SimScript s = simParseScript("swipe left\nswipe right\nswipe up\nswipe down\n");
  CHECK_EQ(s.cmds.size(), (size_t)4);
  if (s.cmds.size() != 4) return;
  int dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
  for (int i = 0; i < 4; i++) {
    const SimCmd &c = s.cmds[i];
    int dx = c.c - c.a, dy = c.d - c.b;
    CHECK(dx * dirs[i][0] > 80 || dirs[i][0] == 0);
    CHECK(dy * dirs[i][1] > 80 || dirs[i][1] == 0);
    CHECK(abs(dirs[i][0] ? dy : dx) < 70);
    CHECK(c.ms < 800);
  }
}

TEST(SimScript, los_errores_dicen_fichero_y_linea) {
  CHECK_EQ(simParseScript("wait 1s\ntapp 1 2\n", "x.txt").error, std::string("x.txt:2: unknown step 'tapp'"));
  SimScript late = simParseScript("tap 1 2\nlang ja\n", "y.txt");
  CHECK_EQ(late.error, std::string("y.txt:2: lang must come before the first step that runs the firmware"));
  CHECK(late.cmds.empty());
  CHECK(!simParseScript("tap 466 10\n").error.empty());     // fuera del panel
  CHECK(!simParseScript("tap 10\n").error.empty());
  CHECK(!simParseScript("lang xx\n").error.empty());
  CHECK(!simParseScript("battery 101\n").error.empty());
  CHECK(!simParseScript("usb maybe\n").error.empty());
  CHECK(!simParseScript("press pwr 0\n").error.empty());
  CHECK(!simParseScript("press power\n").error.empty());
  CHECK(!simParseScript("wait soon\n").error.empty());
  CHECK(!simParseScript("shot ../escape\n").error.empty());  // nada de rutas
  CHECK(!simParseScript("serial\n").error.empty());
  CHECK(!simParseScript("clock 2026-02-30 10:00\n").error.empty());
  CHECK(simParseScript("clock none\nbattery none\n").error.empty());
}

// ---------------------------------------------------------------- reloj falso
TEST(SimRunner, avanzar_dos_horas_con_pasadas_de_200_ms) {
  mockSetMillis(1000);
  uint64_t passes = simRunFor(7200000, [] { delay(200); });
  CHECK_EQ(millis(), (uint32_t)(1000 + 7200000));
  CHECK_EQ(passes, (uint64_t)36000);
}

TEST(SimRunner, el_ultimo_paso_puede_pasarse_pero_nunca_quedarse_corto) {
  mockSetMillis(0);
  simRunFor(1000, [] { delay(300); });  // 300, 600, 900, 1200
  CHECK_EQ(millis(), (uint32_t)1200);
}

TEST(SimRunner, una_pasada_sin_espera_cuesta_un_milisegundo) {
  mockSetMillis(0);
  uint64_t passes = simRunFor(50, [] {});
  CHECK_EQ(millis(), (uint32_t)50);
  CHECK_EQ(passes, (uint64_t)50);
}

TEST(SimRunner, cero_no_corre_ninguna_pasada) {
  mockSetMillis(10);
  CHECK_EQ(simRunFor(0, [] { delay(5); }), (uint64_t)0);
  CHECK_EQ(millis(), (uint32_t)10);
}

TEST(SimRunner, sobrevive_a_la_vuelta_de_millis) {
  mockSetMillis(0xFFFFFF00u);
  simRunFor(1000, [] { delay(100); });
  CHECK_EQ(millis(), (uint32_t)(0xFFFFFF00u + 1000u));  // 0x2E8 tras dar la vuelta
}

TEST(SimRunner, tramos_de_mas_de_25_dias_se_parten) {
  // con una sola comparacion con signo, 30 dias (> 2^31 ms) pararia en el acto
  mockSetMillis(0);
  uint64_t passes = simRunFor(30ULL * 86400000ULL, [] { delay(3600000); });
  CHECK_EQ(passes, (uint64_t)(30 * 24));
  CHECK_EQ(millis(), (uint32_t)(30ULL * 86400000ULL));  // 2,592e9 cabe en 32 bits sin signo
}

TEST(SimRunner, el_apagado_del_pmu_corta_el_avance) {
  mockSetMillis(0);
  int n = 0;
  uint64_t passes = simRunFor(10000, [&] { delay(100); n++; }, [&] { return n >= 3; });
  CHECK_EQ(passes, (uint64_t)3);
  CHECK_EQ(millis(), (uint32_t)300);
}

// ---------------------------------------------------------------- PNG
TEST(SimPng, crc32_y_adler32_de_los_vectores_conocidos) {
  const char *nine = "123456789";
  CHECK_EQ(pngCrc32((const uint8_t *)nine, 9), (uint32_t)0xCBF43926u);
  const char *wiki = "Wikipedia";
  CHECK_EQ(pngAdler32((const uint8_t *)wiki, 9), (uint32_t)0x11E60398u);
}

TEST(SimPng, cabecera_zlib_valida_y_adler_al_final) {
  uint8_t data[1000];
  for (int i = 0; i < 1000; i++) data[i] = (uint8_t)(i < 600 ? 7 : i);
  std::vector<uint8_t> z = pngZlibCompress(data, sizeof data);
  CHECK(z.size() > 6);
  CHECK_EQ((int)z[0], 0x78);
  CHECK_EQ(((z[0] << 8) | z[1]) % 31, 0);  // FCHECK
  uint32_t adler = ((uint32_t)z[z.size() - 4] << 24) | ((uint32_t)z[z.size() - 3] << 16) |
                   ((uint32_t)z[z.size() - 2] << 8) | z[z.size() - 1];
  CHECK_EQ(adler, pngAdler32(data, sizeof data));
  CHECK(z.size() < 600);  // las 600 repeticiones caben en unos pocos codigos
}

// Patron que test_tools.py vuelve a leer con zlib de Python (un decodificador
// independiente): mitad lisa (repeticiones), mitad degradado (literales).
static uint16_t selftestPixel(int x, int y) {
  if (x < 20) return 0x1234;
  return (uint16_t)(x * 2017 + y * 31 + x * y);
}

TEST(SimPng, imagen_de_prueba_para_el_decodificador_de_python) {
  const int W = 37, H = 23;
  std::vector<uint16_t> px(W * H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) px[y * W + x] = selftestPixel(x, y);
  std::vector<uint8_t> png = pngEncodeRgb565(px.data(), W, H, false);
  const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
  CHECK(png.size() > 8 && memcmp(png.data(), sig, 8) == 0);
  CHECK(memcmp(png.data() + 12, "IHDR", 4) == 0);
  CHECK_EQ((int)png[16 + 3], W);
  CHECK_EQ((int)png[20 + 3], H);
  CHECK_EQ((int)png[25], 2);  // RGB
  mkdir("build", 0755);
  CHECK(pngWriteRgb565("build/sim_png_selftest.png", px.data(), W, H, false));
  CHECK(pngWriteRgb565("build/sim_png_selftest_round.png", px.data(), W, H, true));
}
