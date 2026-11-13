// Horario de dormir (bedtime.h): ventanas y detector de bordes. Logica pura.
#include "framework.h"
#include "../bedtime.h"

static uint16_t hm(int h, int m) { return (uint16_t)(h * 60 + m); }

// ---------------------------------------------------------------- minuto del dia
TEST(Bedtime, minuto_del_dia_sale_de_la_hora_local_de_la_rtc) {
  CHECK_EQ(bedMinuteOfDay(0), (uint16_t)0);
  CHECK_EQ(bedMinuteOfDay(86399), (uint16_t)1439);
  CHECK_EQ(bedMinuteOfDay(86400), (uint16_t)0);
  CHECK_EQ(bedMinuteOfDay(1767225600UL + 22 * 3600 + 5 * 60 + 59), hm(22, 5));  // 2026-01-01 22:05:59
}

// ---------------------------------------------------------------- ventanas
TEST(Bedtime, preset_1_es_21_a_07_con_los_bordes_exactos) {
  CHECK(!isBedtime(1, hm(20, 59)));
  CHECK(isBedtime(1, hm(21, 0)));    // el inicio entra
  CHECK(isBedtime(1, hm(23, 59)));
  CHECK(isBedtime(1, hm(0, 0)));     // cruza la medianoche
  CHECK(isBedtime(1, hm(6, 59)));
  CHECK(!isBedtime(1, hm(7, 0)));    // el final ya no
  CHECK(!isBedtime(1, hm(12, 0)));
}

TEST(Bedtime, preset_2_es_22_a_07) {
  CHECK(!isBedtime(2, hm(21, 59)));
  CHECK(isBedtime(2, hm(22, 0)));
  CHECK(isBedtime(2, hm(3, 30)));
  CHECK(isBedtime(2, hm(6, 59)));
  CHECK(!isBedtime(2, hm(7, 0)));
}

TEST(Bedtime, preset_3_es_23_a_08) {
  CHECK(!isBedtime(3, hm(22, 59)));
  CHECK(isBedtime(3, hm(23, 0)));
  CHECK(isBedtime(3, hm(0, 0)));
  CHECK(isBedtime(3, hm(7, 59)));
  CHECK(!isBedtime(3, hm(8, 0)));
  CHECK(!isBedtime(3, hm(15, 0)));
}

// ninguno de los presets actuales es de mismo dia, pero la funcion general debe valer
TEST(Bedtime, una_ventana_que_no_cruza_la_medianoche_tambien_funciona) {
  BedWindow w = { hm(9, 0), hm(17, 0) };
  CHECK(!bedInWindow(w, hm(8, 59)));
  CHECK(bedInWindow(w, hm(9, 0)));
  CHECK(bedInWindow(w, hm(16, 59)));
  CHECK(!bedInWindow(w, hm(17, 0)));
  CHECK(!bedInWindow(w, hm(23, 0)));
  CHECK(!bedInWindow(BedWindow{ hm(5, 0), hm(5, 0) }, hm(5, 0)));  // vacia
}

TEST(Bedtime, apagado_y_valores_desconocidos_nunca_es_hora_de_dormir) {
  for (int m = 0; m < 1440; m++) {
    CHECK(!isBedtime(0, (uint16_t)m));
    CHECK(!isBedtime(4, (uint16_t)m));
    CHECK(!isBedtime(255, (uint16_t)m));
  }
}

// ---------------------------------------------------------------- detector de bordes
// recorre minutos consecutivos y recoge los eventos como texto ("S@21:0 W@7:0 ")
static std::string eventsOver(BedEdge &e, uint8_t preset, int fromMin, int minutes) {
  std::string out;
  for (int i = 0; i < minutes; i++) {
    int m = (fromMin + i) % 1440;
    BedEvent ev = e.update(preset, (uint16_t)m);
    if (ev == BED_SLEEP) out += "S@" + std::to_string(m / 60) + ":" + std::to_string(m % 60) + " ";
    if (ev == BED_WAKE) out += "W@" + std::to_string(m / 60) + ":" + std::to_string(m % 60) + " ";
  }
  return out;
}

TEST(BedEdge, una_noche_normal_da_un_dormir_a_las_21_y_un_despertar_a_las_7) {
  BedEdge e;
  e.update(1, hm(12, 0));  // arranque de dia: sin evento
  std::string got = eventsOver(e, 1, hm(12, 1), 24 * 60);
  CHECK_STREQ(got.c_str(), "S@21:0 W@7:0 ");
}

TEST(BedEdge, un_despertar_manual_en_plena_noche_no_vuelve_a_dormirlo) {
  BedEdge e;
  e.update(1, hm(20, 0));
  CHECK_EQ((int)e.update(1, hm(21, 0)), (int)BED_SLEEP);
  // ella lo despierta a las 2: el detector sigue dentro de la ventana, sin eventos
  std::string got = eventsOver(e, 1, hm(21, 1), 9 * 60 + 58);  // hasta las 06:58
  CHECK_STREQ(got.c_str(), "");
  CHECK_EQ((int)e.update(1, hm(6, 59)), (int)BED_NONE);
  CHECK_EQ((int)e.update(1, hm(7, 0)), (int)BED_WAKE);  // el borde de la manana sigue ahi
}

TEST(BedEdge, arrancar_dentro_de_la_ventana_duerme_una_sola_vez) {
  BedEdge e;
  CHECK_EQ((int)e.update(1, hm(2, 0)), (int)BED_SLEEP);
  CHECK_EQ((int)e.update(1, hm(2, 1)), (int)BED_NONE);
  CHECK_EQ((int)e.update(1, hm(2, 2)), (int)BED_NONE);
}

TEST(BedEdge, arrancar_fuera_de_la_ventana_no_hace_nada) {
  BedEdge e;
  CHECK_EQ((int)e.update(1, hm(12, 0)), (int)BED_NONE);
  BedEdge off;
  CHECK_EQ((int)off.update(0, hm(2, 0)), (int)BED_NONE);  // apagado
}

TEST(BedEdge, cambiar_de_horario_a_mitad_no_duerme_ni_despierta_solo) {
  // activarlo ya dentro de la ventana: nada hasta el proximo borde
  BedEdge e;
  e.update(0, hm(22, 30));
  CHECK_EQ((int)e.update(1, hm(22, 31)), (int)BED_NONE);
  CHECK_EQ((int)e.update(1, hm(22, 32)), (int)BED_NONE);
  CHECK_EQ((int)e.update(1, hm(7, 0)), (int)BED_WAKE);  // la manana si cuenta
  // apagarlo dentro de la ventana: tampoco despierta
  BedEdge f;
  f.update(1, hm(23, 0));
  CHECK_EQ((int)f.update(0, hm(23, 1)), (int)BED_NONE);
  CHECK_EQ((int)f.update(0, hm(7, 0)), (int)BED_NONE);
  // otro horario: 21-07 -> 23-08 a las 22:30; el borde de las 23:00 duerme
  BedEdge g;
  g.update(1, hm(22, 0));
  CHECK_EQ((int)g.update(3, hm(22, 30)), (int)BED_NONE);
  CHECK_EQ((int)g.update(3, hm(22, 59)), (int)BED_NONE);
  CHECK_EQ((int)g.update(3, hm(23, 0)), (int)BED_SLEEP);
}

TEST(BedEdge, un_salto_de_reloj_cuenta_como_una_muestra_mas) {
  BedEdge e;
  e.update(1, hm(23, 0));  // arranque dentro (SLEEP, ya consumido)
  // salto atras de 23:00 a 12:00: fuera de la ventana -> despertar
  CHECK_EQ((int)e.update(1, hm(12, 0)), (int)BED_WAKE);
  // salto adelante de 12:00 a 22:00: dentro -> dormir
  CHECK_EQ((int)e.update(1, hm(22, 0)), (int)BED_SLEEP);
  // salto sobre el final: 06:59 -> 07:01 despierta una vez
  e.update(1, hm(6, 59));
  CHECK_EQ((int)e.update(1, hm(7, 1)), (int)BED_WAKE);
  CHECK_EQ((int)e.update(1, hm(7, 2)), (int)BED_NONE);
  // una ventana entera entre dos muestras (20:59 -> 07:30): ambas fuera, sin eventos
  BedEdge g;
  g.update(1, hm(20, 59));
  CHECK_EQ((int)g.update(1, hm(7, 30)), (int)BED_NONE);
}
