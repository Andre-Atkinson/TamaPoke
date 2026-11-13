// Tests de la logica de la mascota (pet.cpp) sobre los shims del host.
#include "framework.h"
#include "shim/Arduino.h"
#include "../pet.h"
#include "../dex.h"
#include "../bedtime.h"
#include <string.h>

// ---------------------------------------------------------------- helpers
static void advance(Pet &p, uint32_t minutes) {
  for (uint32_t i = 0; i < minutes; i++) {
    mockAdvanceMillis(PET_TICK_MS);
    p.update(millis());
  }
}

// mascota recien eclosionada de la especie pedida, con la NVS limpia
static void makePet(Pet &p, int16_t dex) {
  mockNvsReset();
  mockSetMillis(0);
  p.begin();
  if (p.awaitingStarter()) p.chooseStarter(dex);
  p.eggTap();
  p.eggTap();
  p.eggTap();  // 3 toques = eclosion inmediata
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

TEST(tick, buen_cuidado_12h_forja_defensa) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.trDef, (uint8_t)0);
  for (int h = 0; h < 720; h++) {  // mantener todo >= 40 durante 12 h
    setStats(p, 100, 100, 100, 100);
    advance(p, 1);
  }
  CHECK_EQ(p.trDef, (uint8_t)1);
}

// La racha de buen cuidado mira la felicidad ANTES de que baje ese minuto (el
// orden del tick: goodTicks, luego drainJoy). Con joy justo en 40 antes del tick,
// el minuto cuenta; si drainJoy corriera antes, joy valdria 39 y la racha se
// reiniciaria cada minuto.
TEST(tick, la_racha_ve_la_felicidad_antes_de_bajar) {
  Pet p;
  makePet(p, 4);
  for (int h = 0; h < 720; h++) {
    setStats(p, 100, 40, 100, 100);
    advance(p, 1);
  }
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

TEST(train, saco_da_un_punto_cada_4_golpes_con_tope) {
  Pet p;
  makePet(p, 4);
  CHECK_EQ(p.trainStrength(0), (uint8_t)0);
  CHECK_EQ(p.trainStrength(40), (uint8_t)10);
  CHECK_EQ(p.trAtk, (uint8_t)10);
  CHECK_MSG(p.trainStrength(1000) == 18, "tope de 18 por sesion");
  CHECK_EQ(p.strHi, (uint16_t)1000);
}

// Arreglado al portar la bateria: trainStrength() devolvia la subida teorica
TEST(train, el_saco_anuncia_lo_que_de_verdad_sube) {
  Pet p;
  makePet(p, 4);
  p.trAtk = 95;
  uint8_t before = p.trAtk;
  uint8_t gain = p.trainStrength(72);  // 72/4 = 18, pero solo caben 5
  CHECK_EQ(gain, (uint8_t)(p.trAtk - before));
}

TEST(train, entrenamiento_topa_en_100) {
  Pet p;
  makePet(p, 4);
  for (int i = 0; i < 20; i++) p.trainStrength(80);
  CHECK_EQ(p.trAtk, (uint8_t)100);
}

TEST(train, el_saco_cansa_y_quema_peso) {
  Pet p;
  makePet(p, 4);
  p.weight = 40;
  p.energy = 80;
  p.fullness = 80;
  p.trainStrength(30);
  CHECK_EQ(p.energy, (uint8_t)68);
  CHECK_EQ(p.fullness, (uint8_t)75);
  CHECK_EQ(p.weight, (uint8_t)30);
  p.energy = 3;  // el suelo protege de dejarlo a cero
  p.trainStrength(30);
  CHECK_EQ(p.energy, (uint8_t)3);
}

TEST(play, minijuego_entrena_velocidad_y_guarda_record) {
  Pet p;
  makePet(p, 4);
  p.joy = 10;
  p.weight = 50;
  p.playResult(20);
  CHECK_EQ(p.trSpe, (uint8_t)4);
  CHECK_EQ(p.joy, (uint8_t)45);   // +5 +30 por marcar mas de 15
  CHECK_EQ(p.weight, (uint8_t)10);  // 50 - 20*2
  CHECK_EQ(p.gameHi, (uint16_t)20);
  p.playResult(5);
  CHECK_MSG(p.gameHi == 20, "un resultado peor no baja el record");
}

TEST(play, jugar_sube_felicidad_y_cansa) {
  Pet p;
  makePet(p, 4);
  setStats(p, 50, 50, 50, 50);
  p.play();
  CHECK_EQ(p.joy, (uint8_t)75);
  CHECK_EQ(p.energy, (uint8_t)40);
  CHECK_EQ(p.fullness, (uint8_t)45);
  CHECK(p.showHeart());
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
  for (int i = 0; i < 50; i++) p.play();  // +2 de vinculo cada vez
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

TEST(ceremony, durante_la_ceremonia_no_se_puede_interactuar) {
  Pet p;
  makePet(p, 4);
  setStats(p, 50, 50, 50, 50);
  p.poops = 2;
  p.startRunaway();
  p.feedBerry(0);
  p.feedCandy();
  p.play();
  p.clean();
  p.caress();
  p.toggleLight();
  CHECK_EQ(p.fullness, (uint8_t)50);
  CHECK_EQ(p.joy, (uint8_t)50);
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

TEST(pace, relajado_sigue_forjando_defensa_en_12_horas) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  for (int h = 0; h < 720; h++) { setStats(p, 100, 100, 100, 100); advance(p, 1); }
  CHECK_EQ(p.trDef, (uint8_t)1);
}

// Igual que el de ritmo normal: en relajado la felicidad solo baja en los minutos
// pares, pero la racha la sigue viendo antes de bajar.
TEST(pace, relajado_la_racha_ve_la_felicidad_antes_de_bajar) {
  Pet p;
  makePet(p, 4);
  p.setPace(PACE_RELAXED);
  for (int h = 0; h < 720; h++) { setStats(p, 100, 40, 100, 100); advance(p, 1); }
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

TEST(eggpick, tres_colores_de_tipo_distintos) {
  // con pokedex vacia, a medias y con todas las lineas comunes completas
  for (int16_t upTo : { (int16_t)4, (int16_t)80, (int16_t)151 }) {
    Pet p;
    makePet(p, 4);
    registerRange(p, 1, upTo);
    for (int seed = 1; seed <= 200; seed++) {
      randomSeed(seed);
      int16_t out[EGG_CHOICES];
      p.rollEggChoices(p.pickEggSpecies(), out);
      CHECK(threeEggSpecies(out));
      CHECK_MSG(DEX_TBL[out[0]].accent != DEX_TBL[out[1]].accent &&
                DEX_TBL[out[0]].accent != DEX_TBL[out[2]].accent &&
                DEX_TBL[out[1]].accent != DEX_TBL[out[2]].accent,
                "cada huevo da una pista de tipo distinta");
    }
  }
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
  // Todo registrado salvo los comunes del color del huevo especial: los unicos
  // comunes con la linea incompleta comparten su color, asi que la pasada 0
  // (color nuevo y linea nueva) no encuentra nada y la pasada 1 (solo color
  // nuevo) si. Si cediera antes el color, los extras saldrian del color del
  // especial.
  Pet p;
  makePet(p, 4);
  const int16_t special = 4;
  registerRange(p, 1, 151);
  int sameColour = 0;
  for (int16_t d = 1; d <= DEX_COUNT; d++)
    if (DEX_TBL[d].rarity == R_COMUN && DEX_TBL[d].accent == DEX_TBL[special].accent) {
      p.dexReg[(d - 1) >> 3] &= (uint8_t)~(1 << ((d - 1) & 7));
      sameColour++;
    }
  CHECK_MSG(sameColour >= 2, "hay otros comunes del mismo color sin registrar");
  for (int seed = 1; seed <= 100; seed++) {
    randomSeed(seed);
    int16_t out[EGG_CHOICES];
    p.rollEggChoices(special, out);
    CHECK(threeEggSpecies(out));
    CHECK_MSG(DEX_TBL[out[0]].accent != DEX_TBL[out[1]].accent &&
              DEX_TBL[out[0]].accent != DEX_TBL[out[2]].accent &&
              DEX_TBL[out[1]].accent != DEX_TBL[out[2]].accent,
              "cede la linea antes que el color: tres colores distintos");
  }
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
