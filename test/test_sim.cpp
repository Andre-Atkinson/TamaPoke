// Simulador de escritorio (sim/): lectura de guiones, avance del reloj falso y el
// escritor de PNG. La parte que corre el firmware se prueba con tools/sim.sh.
#include "framework.h"
#include "../sim/interactive.h"
#include "../sim/png.h"
#include "../sim/runner.h"
#include "../sim/script.h"
#include "../sim/simhw.h"
#include "../rtcbat.h"
#include "../i18n.h"
#include <stdio.h>
#include <sys/stat.h>
#include <set>
#include <string>
#include <vector>

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

// La preparacion antes de arrancar: lang, clock, battery, usb y charging. main() aplica
// todo lo que hay antes de simPreBootEnd() y deja el resto al bucle de pasos.
TEST(SimScript, la_preparacion_llega_hasta_el_primer_paso_que_corre_el_firmware) {
  SimScript s = simParseScript("battery 40\nlang ja\nusb on\nclock 2026-11-13 23:30\ncharging on\ntap 1 1\nlang_after\n");
  CHECK(!s.error.empty());  // paso desconocido
  s = simParseScript("battery 40\nlang ja\nusb on\nclock 2026-11-13 23:30\ncharging on\ntap 1 1\nbattery 10\n");
  CHECK_EQ(s.error, std::string(""));
  CHECK_EQ(simPreBootEnd(s), (size_t)5);
  CHECK_EQ((int)s.cmds[0].op, (int)OP_BATTERY);
  CHECK_EQ((int)s.cmds[1].op, (int)OP_LANG);
  CHECK_EQ((int)s.cmds[5].op, (int)OP_TAP);
}

TEST(SimScript, battery_antes_de_lang_no_deja_el_idioma_para_despues_del_arranque) {
  SimScript s = simParseScript("battery 40\nlang ja\ntap 1 1\n");
  CHECK_EQ(s.error, std::string(""));
  CHECK_EQ(simPreBootEnd(s), (size_t)2);  // el lang (indice 1) esta antes del arranque
  s = simParseScript("usb on\nclock 2026-11-13 23:30\nwait 1s\n");
  CHECK_EQ(simPreBootEnd(s), (size_t)2);
}

TEST(SimScript, sin_pasos_de_preparacion_o_sin_pasos_que_corran) {
  CHECK_EQ(simPreBootEnd(simParseScript("wait 1s\n")), (size_t)0);
  CHECK_EQ(simPreBootEnd(simParseScript("")), (size_t)0);
  CHECK_EQ(simPreBootEnd(simParseScript("lang es\nbattery 5\n")), (size_t)2);  // solo preparacion
}

// cada paso que corre el firmware cierra la preparacion: un lang o clock detras se rechaza
// (si no, main() lo ignoraria en silencio), y battery/usb/charging no la cierran
TEST(SimScript, todos_los_pasos_que_corren_cierran_la_preparacion) {
  const char *const RUN[] = { "tap 1 1", "hold 1 1 1s", "swipe left", "swipe 1 1 200 1 200ms",
                              "press pwr", "press boot", "wait 1s", "advance 1s", "serial X", "shot a" };
  for (const char *step : RUN) {
    std::string s = std::string(step) + "\nlang ja\n";
    CHECK_EQ(simParseScript(s, "r.txt").error, std::string("r.txt:2: lang must come before the first step that runs the firmware"));
    CHECK_EQ(simPreBootEnd(simParseScript(std::string(step) + "\n")), (size_t)0);
  }
  CHECK(simParseScript("battery 5\nusb on\ncharging off\nlang ja\nclock none\n").error.empty());
}

TEST(SimScript, lang_y_clock_tras_un_paso_que_corre_se_rechazan_pero_battery_vale) {
  CHECK(!simParseScript("wait 1s\nlang ja\n").error.empty());
  CHECK(!simParseScript("wait 1s\nclock 2026-11-13 10:00\n").error.empty());
  CHECK(simParseScript("wait 1s\nbattery 5\nusb on\ncharging on\n").error.empty());
}

// swipe X0 Y0 X1 Y1 DUR corre 8 tramos de DUR/8: por debajo de 8 sondeos del firmware
// (20 ms) el punto de salida no se lee y el gesto es un toque
TEST(SimScript, un_deslizamiento_de_menos_de_160ms_se_rechaza) {
  CHECK_EQ(simParseScript("swipe 233 313 233 153 0\n", "s.txt").error,
           std::string("s.txt:1: swipe duration must be at least 160ms"));
  CHECK(!simParseScript("swipe 233 313 233 153 159ms\n").error.empty());
  CHECK(simParseScript("swipe 233 313 233 153 160ms\n").error.empty());
  CHECK(simParseScript("swipe 233 313 233 153\n").error.empty());  // por defecto 240 ms
  CHECK(simParseScript("swipe left\n").error.empty());
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

// pmu ok|none (#24): the firmware asks the AXP2101 once, in setup(), so it is boot-only
TEST(SimScript, pmu_is_a_boot_step) {
  SimScript s = simParseScript("pmu none\nusb on\nwait 1s\n");
  CHECK_EQ(s.error, std::string(""));
  CHECK_EQ(simPreBootEnd(s), (size_t)2);
  CHECK_EQ((int)s.cmds[0].op, (int)OP_PMU);
  CHECK_EQ(s.cmds[0].a, 0);
  CHECK_EQ(simParseScript("pmu ok\n").cmds[0].a, 1);
  CHECK_EQ(simParseScript("wait 1s\npmu none\n", "r.txt").error,
           std::string("r.txt:2: pmu must come before the first step that runs the firmware"));
  CHECK(!simParseScript("pmu off\n").error.empty());
  CHECK(!simParseScript("pmu\n").error.empty());
}

// without the AXP2101 the fake gives what rtcbat.cpp gives: USB reads as present
TEST(SimPower, without_the_pmu_usb_reads_present_like_the_firmware) {
  SimPower saved = gSimPower;
  pwrSetup();  // no press latched from another test
  gSimPower.usb = false;
  gSimPower.batPct = 0;
  gSimPower.pmuOk = false;
  CHECK(!batBegin());
  CHECK(usbPresent());
  CHECK(usbPresentNow());
  CHECK_EQ(batPercent(), -1);
  CHECK(!batCharging());
  CHECK(!batShouldShutdown());
  gSimPower.batPct = 60;
  CHECK_EQ(batMillivolts(), 0);                  // no reading without the PMU
  simPwrPress();
  CHECK(!pwrShortPressed());                     // the PKEY is read from the AXP2101
  batPowerOff();
  CHECK(!gSimPower.poweredOff);                  // and so is the power-off
  gSimPower.usb = true;
  gSimPower.charging = true;
  CHECK(!batCharging());                         // and the charger status
  gSimPower.usb = false;
  gSimPower.charging = false;
  gSimPower.batPct = 0;
  gSimPower.pmuOk = true;  // the same board with the PMU answering
  CHECK(batBegin());
  CHECK(!usbPresent());
  CHECK(!usbPresentNow());
  CHECK(batShouldShutdown());
  gSimPower.batPct = 60;
  CHECK(batMillivolts() > 0);                    // controls: the same calls with the PMU
  pwrSetup();                                    // clear the press latched above
  simPwrPress();
  CHECK(pwrShortPressed());
  batPowerOff();
  CHECK(gSimPower.poweredOff);
  gSimPower.usb = true;
  gSimPower.charging = true;
  CHECK(batCharging());
  gSimPower = saved;
  pwrSetup();
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

// El reloj de software de la placa (time()) no da la vuelta; el millis() de 32 bits si,
// a los 49,7 dias. rtcbat_sim.cpp tiene que contar con mockMillis64().
TEST(SimRunner, el_reloj_de_software_no_da_la_vuelta_a_los_49_dias) {
  mockSetMillis(0);
  simClockSet(1794564000UL);  // 2026-11-13 10:00
  simRunFor(52ULL * 86400000ULL, [] { delay(3600000); });  // 52 dias > 2^32 ms
  CHECK_EQ(millis(), (uint32_t)((52ULL * 86400000ULL) & 0xFFFFFFFFULL));  // millis() si dio la vuelta
  CHECK_EQ(rtcEpoch(), (uint32_t)(1794564000UL + 52UL * 86400UL));
}

TEST(SimRunner, el_reloj_de_software_sigue_cuando_millis_da_la_vuelta_con_el_reloj_puesto) {
  mockSetMillis(0xFFFFFF00u);
  simClockSet(1794564000UL);
  simRunFor(3600000, [] { delay(1000); });
  CHECK_EQ(rtcEpoch(), (uint32_t)(1794564000UL + 3600UL));
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

// ======================================================================
// Ventana interactiva (sim/interactive.*): lo que se prueba sin SDL
// ======================================================================

// ---------------------------------------------------------------- velocidades y bateria
TEST(SimWindow, la_velocidad_sube_y_baja_por_la_tabla_y_se_para_en_los_extremos) {
  CHECK_EQ(SIM_SPEED_COUNT, 5);
  CHECK_EQ(kSimSpeeds[0], (uint32_t)1);
  CHECK_EQ(kSimSpeeds[4], (uint32_t)3600);
  CHECK_EQ(simSpeedStep(0, 1), 1);
  CHECK_EQ(simSpeedStep(1, 1), 2);
  CHECK_EQ(simSpeedStep(3, 1), 4);
  CHECK_EQ(simSpeedStep(4, 1), 4);   // tope alto
  CHECK_EQ(simSpeedStep(4, -1), 3);
  CHECK_EQ(simSpeedStep(1, -1), 0);
  CHECK_EQ(simSpeedStep(0, -1), 0);  // tope bajo
}

TEST(SimWindow, la_bateria_se_mueve_de_10_en_10_entre_0_y_100) {
  CHECK_EQ(simBatteryStep(80, 1), 90);
  CHECK_EQ(simBatteryStep(80, -1), 70);
  CHECK_EQ(simBatteryStep(95, 1), 100);   // tope alto: no pasa de 100
  CHECK_EQ(simBatteryStep(100, 1), 100);
  CHECK_EQ(simBatteryStep(5, -1), 0);     // tope bajo
  CHECK_EQ(simBatteryStep(0, -1), 0);
  CHECK_EQ(simBatteryStep(-1, 1), 10);    // sin bateria cuenta como 0
  CHECK_EQ(simBatteryStep(-1, -1), 0);
}

// ---------------------------------------------------------------- cristal redondo
TEST(SimWindow, el_cristal_es_el_circulo_inscrito_y_el_resto_no_es_tactil) {
  CHECK(simInGlass(232, 232));
  CHECK(simInGlass(233, 233));
  CHECK(simInGlass(0, 232));     // los cuatro puntos cardinales del circulo
  CHECK(simInGlass(465, 233));
  CHECK(simInGlass(232, 0));
  CHECK(simInGlass(233, 465));
  CHECK(!simInGlass(0, 0));      // esquinas: fuera del cristal
  CHECK(!simInGlass(465, 465));
  CHECK(!simInGlass(5, 5));
  CHECK(!simInGlass(100, 30));
  // el borde en la diagonal: (68,68) dentro y (67,67) fuera, como la mascara del PNG
  CHECK(simInGlass(68, 68));
  CHECK(!simInGlass(67, 67));
  CHECK(simInGlass(397, 397));
  CHECK(!simInGlass(398, 398));
  CHECK(!simInGlass(-1, 232));   // fuera del panel
  CHECK(!simInGlass(466, 232));
  CHECK(!simInGlass(232, -1));
  CHECK(!simInGlass(232, 466));
}

TEST(SimWindow, un_arrastre_que_sale_del_panel_se_queda_en_el_borde) {
  CHECK_EQ(simClampCoord(-50), 0);
  CHECK_EQ(simClampCoord(0), 0);
  CHECK_EQ(simClampCoord(233), 233);
  CHECK_EQ(simClampCoord(465), 465);
  CHECK_EQ(simClampCoord(466), 465);
  CHECK_EQ(simClampCoord(5000), 465);
}

// ---------------------------------------------------------------- brillo
TEST(SimWindow, el_brillo_normal_se_ve_completo_y_el_atenuado_mas_oscuro_pero_legible) {
  CHECK_EQ(simBrightnessMod(145), (uint8_t)255);  // despierta con bateria
  CHECK_EQ(simBrightnessMod(180), (uint8_t)255);  // despierta con USB
  CHECK_EQ(simBrightnessMod(255), (uint8_t)255);
  uint8_t low = simBrightnessMod(60);             // atenuada (DIM_LOW)
  CHECK(low < 200);                               // claramente mas oscura
  CHECK(low >= 100);                              // y aun legible
  CHECK(simBrightnessMod(8) >= 64);               // nunca por debajo del suelo
  CHECK(simBrightnessMod(1) >= 64);
  for (int b = 1; b < 255; b++) CHECK(simBrightnessMod((uint8_t)b) <= simBrightnessMod((uint8_t)(b + 1)));
}

// ---------------------------------------------------------------- ritmo
namespace {
struct PaceRun {
  SimPace p;
  uint64_t passes = 0, simMs = 0;
  // un fotograma: cada pasada del firmware falso dura passMs; usPerPass es la pared que
  // gasta cada pasada (para el presupuesto)
  SimPaceResult frame(uint32_t wallUs, bool input, uint32_t passMs, uint32_t usPerPass = 0) {
    uint64_t n = 0;
    SimPaceResult r = simPaceFrame(p, wallUs, input, [&] { delay(passMs); n++; },
                                   [] { return false; }, [&]() -> uint32_t { return (uint32_t)(n * usPerPass); });
    passes += r.passes;
    simMs += r.simMs;
    return r;
  }
};
}  // namespace

TEST(SimWindow, a_1x_el_reloj_falso_sigue_al_de_pared_sin_perder_el_resto_de_cada_fotograma) {
  mockSetMillis(0);
  PaceRun run;
  for (int i = 0; i < 120; i++) run.frame(16667, false, 2);  // 120 fotogramas de 16,667 ms = 2000 ms
  CHECK_RANGE((long)millis(), 1995, 2000);
  CHECK_EQ(run.simMs, (uint64_t)millis());
  CHECK(run.passes > 900);
}

TEST(SimWindow, la_velocidad_multiplica_el_tiempo_simulado) {
  mockSetMillis(0);
  PaceRun run;
  run.p.speedIndex = 2;  // 60x
  SimPaceResult r = run.frame(16000, false, 10);
  CHECK_RANGE((long)r.simMs, 960, 970);  // 16 ms * 60
  mockSetMillis(0);
  run.p.speedIndex = 4;  // 3600x
  r = run.frame(16000, false, 10);
  CHECK_RANGE((long)r.simMs, 57600, 57610);
}

TEST(SimWindow, en_pausa_no_corre_nada_y_al_seguir_no_hay_rafaga) {
  mockSetMillis(0);
  PaceRun run;
  run.frame(16000, false, 2);
  simPaceSetPaused(run.p, true);
  uint32_t t = millis();
  for (int i = 0; i < 50; i++) {
    SimPaceResult r = run.frame(200000, true, 2);
    CHECK_EQ(r.passes, (uint64_t)0);
  }
  CHECK_EQ(millis(), t);
  simPaceSetPaused(run.p, false);
  SimPaceResult r = run.frame(16000, false, 2);
  CHECK_RANGE((long)r.simMs, 14, 18);  // un fotograma, no los 10 s de pausa
}

TEST(SimWindow, un_fotograma_parado_no_salta_mas_de_250_ms_de_pared) {
  mockSetMillis(0);
  PaceRun run;
  SimPaceResult r = run.frame(10 * 1000 * 1000, false, 5);  // la ventana estuvo 10 s oculta
  CHECK_RANGE((long)r.simMs, 250, 254);
  mockSetMillis(0);
  run.p.speedIndex = 1;  // 10x
  r = run.frame(10 * 1000 * 1000, false, 5);
  CHECK_RANGE((long)r.simMs, 2500, 2504);  // el tope es de pared, antes de multiplicar
}

TEST(SimWindow, si_el_anfitrion_no_llega_se_acaba_el_presupuesto_y_la_deuda_se_descarta) {
  mockSetMillis(0);
  PaceRun run;
  run.p.speedIndex = 3;  // 600x: 16 ms de pared deben 9600 ms
  // cada pasada gasta 1 ms de pared y simula 10 ms: el presupuesto (12 ms) corta a las 12 pasadas
  SimPaceResult r = run.frame(16000, false, 10, 1000);
  CHECK_EQ(r.passes, (uint64_t)12);
  CHECK_EQ(r.simMs, (uint64_t)120);
  CHECK(r.behind);
  CHECK(r.droppedMs > 9000);
  CHECK_EQ(run.p.debtUs, (int64_t)0);  // se tira, no se acumula
  CHECK(run.p.statBehind);
  // el siguiente fotograma empieza de cero: no arrastra lo no pagado
  run.p.speedIndex = 0;
  r = run.frame(16000, false, 2);
  CHECK(!r.behind);
  CHECK_RANGE((long)r.simMs, 15, 18);
}

TEST(SimWindow, una_pasada_que_se_pasa_deja_deuda_negativa_y_no_corre_nada_hasta_pagarla) {
  mockSetMillis(0);
  PaceRun run;
  SimPaceResult r = run.frame(16000, false, 200);  // un light sleep de 200 ms
  CHECK_EQ(r.passes, (uint64_t)1);
  CHECK_EQ(run.p.debtUs, (int64_t)(16000 - 200000));
  int idle = 0;
  while (idle < 40) {
    r = run.frame(16000, false, 200);
    if (r.passes) break;
    idle++;
    CHECK_EQ(millis(), (uint32_t)200);  // mientras debe, no corre nada
  }
  CHECK_EQ(idle, 11);  // 184 ms de pared hasta alcanzar al reloj, a 16 ms por fotograma
  CHECK_EQ(millis(), (uint32_t)400);  // y entonces corre otra pasada
}

TEST(SimWindow, un_toque_o_boton_perdona_la_deuda_negativa) {
  mockSetMillis(0);
  PaceRun run;
  run.frame(16000, false, 200);  // se pasa de largo: deuda -184 ms
  CHECK(run.p.debtUs < 0);
  SimPaceResult r = run.frame(16000, false, 200);
  CHECK_EQ(r.passes, (uint64_t)0);  // sin entrada, espera
  r = run.frame(16000, true, 2);    // con entrada, corre ya (el GPIO despierta del sueno)
  CHECK(r.passes > 0);
  CHECK_RANGE((long)r.simMs, 14, 18);
}

TEST(SimWindow, el_apagado_del_pmu_corta_el_fotograma_y_no_cuenta_como_ir_atrasado) {
  mockSetMillis(0);
  PaceRun run;
  run.p.speedIndex = 2;
  bool off = false;
  uint64_t n = 0;
  SimPaceResult r = simPaceFrame(run.p, 16000, false, [&] { delay(10); if (++n == 3) off = true; },
                                 [&] { return off; }, [] { return 0u; });
  CHECK_EQ(r.passes, (uint64_t)3);
  CHECK(!r.behind);
  CHECK_EQ(run.p.debtUs, (int64_t)0);
}

TEST(SimWindow, la_velocidad_efectiva_se_mide_con_lo_corrido_y_la_pared_gastada) {
  mockSetMillis(0);
  PaceRun run;
  for (int i = 0; i < 10; i++) run.frame(20000, false, 5);
  CHECK_EQ(run.p.statWallUs, (uint64_t)200000);
  CHECK_RANGE((long)run.p.statSimMs, 195, 200);
}

// ---------------------------------------------------------------- titulo
TEST(SimWindow, el_titulo_dice_placa_hora_velocidad_bateria_usb_y_pausa) {
  std::string t = simTitleText("1.75C", 1794564000UL, 10, false, 0, 80, true, true, false);
  CHECK_STREQ(t.c_str(), "TamaPoke 1.75C | 2026-11-13 10:00:00 | 10x | battery 80%, USB, charging");
  t = simTitleText("1.75", 1794564000UL + 3661, 1, false, 0, 15, false, true, true);
  CHECK_STREQ(t.c_str(), "TamaPoke 1.75 | 2026-11-13 11:01:01 | 1x | battery 15% | paused");  // sin USB no hay "charging"
  t = simTitleText("1.75C", 0, 600, true, 42.5, -1, false, false, false);
  CHECK_STREQ(t.c_str(), "TamaPoke 1.75C | no clock | 600x (running at 42.5x) | no battery");
}

// ---------------------------------------------------------------- consola serie
namespace {
std::vector<std::string> feedAll(SimLineSplitter &s, const std::vector<std::string> &chunks) {
  std::vector<std::string> out;
  for (const std::string &c : chunks) s.feed(c.data(), c.size(), out);
  return out;
}
}  // namespace

TEST(SimWindow, las_lineas_de_la_consola_salen_enteras_aunque_lleguen_a_trozos) {
  SimLineSplitter s;
  std::vector<std::string> out = feedAll(s, { "HAT", "CH\nSPEC ", "25", "\nLV", "L 5" });
  CHECK_EQ((int)out.size(), 2);
  CHECK_STREQ(out[0].c_str(), "HATCH");
  CHECK_STREQ(out[1].c_str(), "SPEC 25");
  out = feedAll(s, { "\n" });  // la tercera termina ahora
  CHECK_EQ((int)out.size(), 1);
  CHECK_STREQ(out[0].c_str(), "LVL 5");
}

TEST(SimWindow, lf_cr_y_crlf_terminan_la_linea_y_las_vacias_se_tiran) {
  SimLineSplitter s;
  std::vector<std::string> out = feedAll(s, { "A\nB\rC\r\nD\n\n\r\n\rE\n" });
  CHECK_EQ((int)out.size(), 5);
  CHECK_STREQ(out[0].c_str(), "A");
  CHECK_STREQ(out[1].c_str(), "B");
  CHECK_STREQ(out[2].c_str(), "C");
  CHECK_STREQ(out[3].c_str(), "D");
  CHECK_STREQ(out[4].c_str(), "E");
  // CRLF partido entre dos lecturas: sigue siendo una sola linea
  SimLineSplitter s2;
  out = feedAll(s2, { "STATS\r", "\nHEALTH\r\n" });
  CHECK_EQ((int)out.size(), 2);
  CHECK_STREQ(out[0].c_str(), "STATS");
  CHECK_STREQ(out[1].c_str(), "HEALTH");
}

TEST(SimWindow, una_linea_demasiado_larga_se_descarta_entera_y_la_siguiente_pasa) {
  SimLineSplitter s;
  std::string ok(SIM_LINE_MAX, 'x');       // justo en el limite: pasa
  std::string tooLong(SIM_LINE_MAX + 1, 'y');
  std::string huge(100000, 'z');           // sin terminador durante mucho rato: no crece sin limite
  std::vector<std::string> out = feedAll(s, { ok + "\n", tooLong + "\n", huge, huge, "\nHATCH\n" });
  CHECK_EQ((int)out.size(), 2);
  CHECK_EQ(out[0].size(), SIM_LINE_MAX);
  CHECK_STREQ(out[1].c_str(), "HATCH");
}

TEST(SimWindow, los_bytes_nulos_de_la_entrada_se_saltan) {
  SimLineSplitter s;
  const char data[] = { 'H', '\0', 'I', '\n' };
  std::vector<std::string> out;
  s.feed(data, sizeof data, out);
  CHECK_EQ((int)out.size(), 1);
  CHECK_STREQ(out[0].c_str(), "HI");
}

// ---------------------------------------------------------------- capturas
static std::set<std::string> gFakeFiles;
static bool fakeExists(const std::string &p) { return gFakeFiles.count(p) != 0; }

TEST(SimWindow, las_capturas_se_numeran_y_no_pisan_las_de_una_sesion_anterior) {
  gFakeFiles.clear();
  int n = 1;
  std::string p = simShotPath("shots", n, fakeExists);
  CHECK_STREQ(p.c_str(), "shots/window-001.png");
  CHECK_EQ(n, 1);
  gFakeFiles.insert("shots/window-001.png");
  gFakeFiles.insert("shots/window-002.png");
  n = 1;
  p = simShotPath("shots", n, fakeExists);
  CHECK_STREQ(p.c_str(), "shots/window-003.png");
  CHECK_EQ(n, 3);
  n = 0;  // un contador sin inicializar empieza en 1
  p = simShotPath("out/dir", n, fakeExists);
  CHECK_STREQ(p.c_str(), "out/dir/window-001.png");
  gFakeFiles.clear();
}
