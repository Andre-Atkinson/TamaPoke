// Minijuegos de Play (gamecore.h, game_*.h, minigames.h). Logica pura.
#include "framework.h"
#include "../minigames.h"
#include <math.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------- recompensa
TEST(minigames, la_puntuacion_normalizada_usa_la_par_de_cada_juego) {
  for (uint16_t s = 0; s <= 30; s++) CHECK_EQ(gameNormScore(GAME_KEEPY, s), (uint8_t)s);  // keepy: tal cual
  CHECK_EQ(gameNormScore(GAME_KEEPY, 31), (uint8_t)30);
  CHECK_EQ(gameNormScore(GAME_KEEPY, 255), (uint8_t)30);
  CHECK_EQ(gameNormScore(GAME_CATCH, 0), (uint8_t)0);
  CHECK_EQ(gameNormScore(GAME_CATCH, 20), (uint8_t)15);
  CHECK_EQ(gameNormScore(GAME_CATCH, 40), (uint8_t)30);
  CHECK_EQ(gameNormScore(GAME_CATCH, 200), (uint8_t)30);
  CHECK_EQ(gameNormScore(GAME_PEEK, 14), (uint8_t)15);
  CHECK_EQ(gameNormScore(GAME_PEEK, 20), (uint8_t)21);
  CHECK_EQ(gameNormScore(GAME_SONG, 6), (uint8_t)15);
  CHECK_EQ(gameNormScore(GAME_SONG, 12), (uint8_t)30);
  for (uint8_t g = 0; g < GAME_COUNT; g++)
    CHECK_MSG(gameNormScore(g, GAME_PAR[g]) == 15, "a la par, 15 en el juego " + std::to_string(g));
  CHECK_EQ(gameNormScore(GAME_COUNT, 10), (uint8_t)0);
}

TEST(minigames, un_record_que_el_juego_no_puede_dar_se_lee_como_0) {
  for (uint8_t g = 0; g < GAME_COUNT; g++) {
    CHECK_EQ(gameRecordOk(g, 0), (uint16_t)0);
    CHECK_EQ(gameRecordOk(g, GAME_REC_MAX[g]), GAME_REC_MAX[g]);
    CHECK_EQ(gameRecordOk(g, GAME_REC_MAX[g] + 1u), (uint16_t)0);
    CHECK_EQ(gameRecordOk(g, 65535), (uint16_t)0);
  }
  CHECK_EQ(gameRecordOk(GAME_COUNT, 1), (uint16_t)0);
}

// ---------------------------------------------------------------- bolsa
template <uint8_t N>
static void checkBag(uint32_t seed) {
  GameRng rng(seed);
  ShuffleBag<N> bag;
  uint8_t prev = N;
  for (int round = 0; round < 2500; round++) {
    bool seen[N] = {};
    for (uint8_t i = 0; i < N; i++) {
      uint8_t g = bag.next(rng);
      CHECK_MSG(g < N, "juego fuera de rango");
      if (g >= N) return;
      CHECK_MSG(!seen[g], "ronda " + std::to_string(round) + ": juego repetido dentro de la ronda");
      seen[g] = true;
      if (N > 1) CHECK_MSG(g != prev, "ronda " + std::to_string(round) + ": el mismo juego dos veces seguidas");
      prev = g;
    }
  }
}

TEST(minigames, la_bolsa_da_cada_juego_una_vez_por_ronda_y_nunca_dos_seguidos) {
  checkBag<GAME_COUNT>(1);
  checkBag<GAME_COUNT>(0xC0FFEE);
  checkBag<5>(7);  // con Bubble Pop tambien valdria
  checkBag<2>(3);
  GameRng rng(9);
  ShuffleBag<1> one;
  for (int i = 0; i < 10; i++) CHECK_EQ(one.next(rng), (uint8_t)0);
}

TEST(minigames, la_bolsa_cambia_de_orden_entre_rondas) {
  GameRng rng(42);
  GameBag bag;
  bool differs = false;
  uint8_t first[GAME_COUNT];
  for (uint8_t i = 0; i < GAME_COUNT; i++) first[i] = bag.next(rng);
  for (int r = 0; r < 20 && !differs; r++)
    for (uint8_t i = 0; i < GAME_COUNT; i++)
      if (bag.next(rng) != first[i]) differs = true;
  CHECK_MSG(differs, "siempre el mismo orden: no baraja");
}

// ---------------------------------------------------------------- salir
static const int EX = GameTouchRouter::EXIT_X + 20, EY = GameTouchRouter::EXIT_Y + 20;  // dentro del boton

TEST(minigames, mantener_el_boton_1_s_sale_y_se_traga_el_resto_del_toque) {
  GameTouchRouter r;
  CHECK_EQ((int)r.update(true, EX, EY, 1000), (int)GT_NONE);  // empieza en el boton: no es del juego
  GameTouch last = GT_NONE;
  uint32_t t = 1000;
  for (; t <= 1980; t += 20) {
    last = r.update(true, EX, EY, t);
    CHECK_MSG(last == GT_NONE, "sale antes de 1 s: t=" + std::to_string(t - 1000));
  }
  CHECK_EQ((int)r.update(true, EX, EY, 2000), (int)GT_EXIT);
  CHECK_EQ((int)r.update(true, EX, EY, 2020), (int)GT_NONE);  // el mismo dedo: ni sale otra vez
  CHECK_EQ((int)r.update(true, 233, 300, 2040), (int)GT_NONE);  // ni juega
  CHECK_EQ((int)r.update(false, 233, 300, 2060), (int)GT_NONE);
  CHECK_EQ((int)r.update(true, 233, 300, 2100), (int)GT_DOWN);  // un toque nuevo, normal
}

TEST(minigames, soltar_antes_de_1_s_no_sale) {
  GameTouchRouter r;
  for (uint32_t t = 0; t <= 900; t += 20) CHECK(r.update(true, EX, EY, t) == GT_NONE);
  CHECK_EQ((int)r.update(false, EX, EY, 920), (int)GT_NONE);  // tampoco es un UP del juego
  CHECK_EQ(r.heldMs(920), (uint32_t)0);
}

TEST(minigames, deslizar_hasta_el_boton_no_sale) {
  GameTouchRouter r;
  CHECK_EQ((int)r.update(true, 233, 200, 0), (int)GT_DOWN);
  for (uint32_t t = 20; t <= 3000; t += 20) CHECK(r.update(true, EX, EY, t) == GT_MOVE);
  CHECK_EQ((int)r.update(false, EX, EY, 3020), (int)GT_UP);
}

TEST(minigames, salir_del_boton_y_volver_no_sale) {
  GameTouchRouter r;
  r.update(true, EX, EY, 0);
  r.update(true, EX, 200, 100);  // fuera
  for (uint32_t t = 120; t <= 3000; t += 20) CHECK(r.update(true, EX, EY, t) == GT_NONE);
}

TEST(minigames, si_el_tactil_deja_de_informar_el_boton_no_completa) {
  GameTouchRouter r;
  r.update(true, EX, EY, 0);
  r.update(true, EX, EY, 50);
  CHECK(r.heldMs(60) > 0);
  CHECK(r.heldMs(50 + GameClock::MAX_STEP_MS) > 0);  // hasta 250 ms entre informes aun cuenta
  CHECK_EQ(r.heldMs(50 + GameClock::MAX_STEP_MS + 1), (uint32_t)0);  // mas: el progreso no cuenta
  CHECK_EQ((int)r.update(true, EX, EY, 1200), (int)GT_NONE);  // hueco de 1150 ms: desarmado
  CHECK_EQ((int)r.update(true, EX, EY, 2500), (int)GT_NONE);
  // con informes cada 260 ms (por encima del limite) nunca completa
  GameTouchRouter slow;
  slow.update(true, EX, EY, 0);
  for (uint32_t t = 260; t <= 6000; t += 260) CHECK_MSG(slow.update(true, EX, EY, t) == GT_NONE, "sale con informes a 260 ms");
}

TEST(minigames, con_informes_lentos_pero_dentro_del_limite_el_boton_si_completa) {
  // un fotograma de juego puede tardar 70-100+ ms y el tactil se lee entre fotogramas
  GameTouchRouter r;
  CHECK_EQ((int)r.update(true, EX, EY, 0), (int)GT_NONE);
  GameTouch last = GT_NONE;
  uint32_t t = 0;
  while (t < 1100 && last != GT_EXIT) {
    t += GameClock::MAX_STEP_MS - 10;  // 240 ms entre informes
    last = r.update(true, EX, EY, t);
  }
  CHECK_EQ((int)last, (int)GT_EXIT);
  CHECK(t >= GameTouchRouter::EXIT_HOLD_MS);
}

TEST(minigames, los_toques_fuera_del_boton_son_del_juego) {
  GameTouchRouter r;
  CHECK_EQ((int)r.update(true, 100, 300, 0), (int)GT_DOWN);
  CHECK_EQ((int)r.update(true, 120, 300, 20), (int)GT_MOVE);
  CHECK_EQ((int)r.update(false, 120, 300, 40), (int)GT_UP);
  CHECK_EQ((int)r.update(false, 120, 300, 60), (int)GT_NONE);
  // bordes del boton
  CHECK(GameTouchRouter::inExit(GameTouchRouter::EXIT_X, GameTouchRouter::EXIT_Y));
  CHECK(!GameTouchRouter::inExit(GameTouchRouter::EXIT_X - 1, GameTouchRouter::EXIT_Y));
  CHECK(!GameTouchRouter::inExit(GameTouchRouter::EXIT_X + GameTouchRouter::EXIT_W, GameTouchRouter::EXIT_Y));
  CHECK(!GameTouchRouter::inExit(GameTouchRouter::EXIT_X, GameTouchRouter::EXIT_Y + GameTouchRouter::EXIT_H));
}

TEST(minigames, el_boton_de_salir_cabe_en_la_pantalla_redonda) {
  const int x0 = GameTouchRouter::EXIT_X, y0 = GameTouchRouter::EXIT_Y;
  const int x1 = x0 + GameTouchRouter::EXIT_W, y1 = y0 + GameTouchRouter::EXIT_H;
  for (int x : { x0, x1 })
    for (int y : { y0, y1 }) CHECK_MSG((x - 233) * (x - 233) + (y - 233) * (y - 233) < 233 * 233, "esquina fuera");
}

TEST(minigames, un_toque_que_empieza_con_la_pantalla_apagada_solo_despierta) {
  GameTouchRouter r;
  CHECK_EQ((int)r.update(true, 233, 300, 0, true), (int)GT_NONE);   // empieza a oscuras: tragado
  CHECK_EQ((int)r.update(true, 240, 300, 20, false), (int)GT_NONE);  // ya despierta, pero es el mismo dedo
  CHECK_EQ((int)r.update(true, 250, 300, 40, false), (int)GT_NONE);
  CHECK_EQ((int)r.update(false, 250, 300, 60, false), (int)GT_NONE);  // ni un UP
  CHECK_EQ((int)r.update(true, 233, 300, 80, false), (int)GT_DOWN);   // el siguiente toque ya es del juego
  // un dedo ya apoyado no se traga porque la pantalla se apague a mitad
  GameTouchRouter d;
  CHECK_EQ((int)d.update(true, 233, 300, 0, false), (int)GT_DOWN);
  CHECK_EQ((int)d.update(true, 235, 300, 20, true), (int)GT_MOVE);
}

// ---------------------------------------------------------------- reloj y flujo
TEST(minigames, el_reloj_del_juego_topa_cada_paso_y_aguanta_la_vuelta_de_millis) {
  GameClock c;
  c.start(1000);
  CHECK_EQ(c.tick(1085), (uint32_t)85);
  CHECK_EQ(c.tick(11085), (uint32_t)250);  // pantalla apagada 10 s: el juego avanza 250 ms
  CHECK_EQ(c.t, (uint32_t)335);
  c.start(0xFFFFFFC0u);
  CHECK_EQ(c.tick(0x24u), (uint32_t)100);
}

TEST(minigames, portada_1200_ms_y_luego_se_juega) {
  GameFlow f;
  f.begin(GAME_PEEK, 5000);
  uint32_t now = 5000;
  for (int i = 0; i < 14; i++) {  // 14 x 85 = 1190 ms
    now += 85;
    CHECK_EQ(f.frame(now, false), (uint32_t)0);
    CHECK_EQ((int)f.phase, (int)GP_TITLE);
  }
  now += 85;
  CHECK_EQ(f.frame(now, false), (uint32_t)0);  // aqui empieza
  CHECK_EQ((int)f.phase, (int)GP_PLAY);
  now += 85;
  CHECK_EQ(f.frame(now, false), (uint32_t)85);
}

TEST(minigames, quince_segundos_sin_tocar_terminan_la_partida) {
  GameFlow f;
  f.begin(GAME_CATCH, 0);
  uint32_t now = 0;
  while (f.phase == GP_TITLE) f.frame(now += 100, false);
  for (int i = 0; i < 149; i++) f.frame(now += 100, false);
  CHECK(!f.idleOut());
  f.touched();
  for (int i = 0; i < 149; i++) f.frame(now += 100, false);
  CHECK_MSG(!f.idleOut(), "un toque reinicia la cuenta");
  f.frame(now += 100, false);
  CHECK(f.idleOut());
}

TEST(minigames, mientras_el_juego_ensena_algo_no_cuenta_como_inactivo) {
  GameFlow f;
  f.begin(GAME_SONG, 0);
  uint32_t now = 0;
  while (f.phase == GP_TITLE) f.frame(now += 100, false);
  for (int i = 0; i < 300; i++) f.frame(now += 100, true);  // 30 s tocando la melodia
  CHECK(!f.idleOut());
  for (int i = 0; i < 150; i++) f.frame(now += 100, false);
  CHECK(f.idleOut());
}

// ---------------------------------------------------------------- sesion
static void playUntil(MiniGames &m, uint32_t &now) {
  while (m.flow.phase == GP_TITLE) m.frame(now += 85);
}

TEST(minigames, play_reparte_los_cuatro_juegos_antes_de_repetir) {
  MiniGames m;
  bool seen[GAME_COUNT] = {};
  for (int i = 0; i < GAME_COUNT; i++) {
    m.start(1000u * i, 0x1234u + i, 0, false);
    CHECK_MSG(!seen[m.game()], "juego repetido en la primera ronda");
    seen[m.game()] = true;
    CHECK_EQ((int)m.flow.phase, (int)GP_TITLE);
  }
}

TEST(minigames, en_la_portada_los_toques_no_llegan_al_juego) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_KEEPY, now, 0, false);
  for (int i = 0; i < 7; i++) m.frame(now += 85);  // 595 ms: aun en la portada
  CHECK_EQ((int)m.flow.phase, (int)GP_TITLE);
  int bx = (int)m.keepy.ballX, by = (int)m.keepy.ballY;
  CHECK(!m.touch(true, bx, by, now));  // justo encima de la bola
  CHECK(!m.touch(false, bx, by, now + 20));
  CHECK_EQ(m.keepy.score, (uint8_t)0);
  CHECK_EQ(m.flow.lastTouch, (uint32_t)0);
}

TEST(minigames, el_boton_de_salir_funciona_tambien_en_la_portada) {
  MiniGames m;
  m.begin(GAME_SONG, 0, 0, false);
  bool out = false;
  for (uint32_t t = 0; t <= 1000 && !out; t += 20) out = m.touch(true, EX, EY, t);
  CHECK(out);
}

TEST(minigames, la_partida_acaba_cuando_acaba_el_juego) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_CATCH, now, 0, false);
  playUntil(m, now);
  for (int i = 0; i < 400 && !m.over(); i++) {
    m.frame(now += 85);
    if (i % 50 == 0) {  // tocar de vez en cuando (soltando y apoyando): no es inactividad
      m.touch(false, 233, 350, now);
      m.touch(true, 233, 350, now);
    }
  }
  CHECK(m.over());
  CHECK(m.catcher.done());
}

TEST(minigames, sin_tocar_la_partida_acaba_a_los_15_s_con_lo_conseguido) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_SONG, now, 0, false);
  playUntil(m, now);
  for (int i = 0; i < 2000 && !m.over(); i++) m.frame(now += 85);
  CHECK(m.over());
  CHECK(!m.song.done());  // termino por inactividad, no por el juego
  CHECK_EQ(m.score(), (uint16_t)0);
}

TEST(minigames, despues_de_acabar_los_toques_no_cambian_nada) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_KEEPY, now, 0, false);
  playUntil(m, now);
  m.flow.phase = GP_OVER;
  uint16_t s = m.score();
  m.touch(true, (int)m.keepy.ballX, (int)m.keepy.ballY, now);
  CHECK_EQ(m.score(), s);
}

// avanza fotogramas de 85 ms hasta el turno del jugador en Berry Song
static void songToTurn(MiniGames &m, uint32_t &now) {
  playUntil(m, now);
  for (int i = 0; i < 400 && !m.song.playersTurn(); i++) m.frame(now += 85);
}

TEST(minigames, con_el_dedo_quieto_Berry_Song_acaba_a_los_15_s_de_empezar_su_turno) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_SONG, now, 0, false);
  songToTurn(m, now);
  CHECK(m.song.playersTurn());
  uint32_t turn0 = now;
  m.touch(true, 233, 233, now);  // un dedo fuera de los botones, quieto: informa cada 85 ms
  CHECK(m.counted);              // apoyarlo si cuenta
  while (!m.over() && now - turn0 < 40000) {
    m.frame(now += 85);
    m.touch(true, 233, 233, now);
    CHECK_MSG(!m.counted, "un dedo quieto cuenta como actividad");
    if (m.counted) break;
  }
  CHECK(m.over());
  CHECK(!m.song.done());
  CHECK_RANGE(now - turn0, (uint32_t)14000, (uint32_t)16000);
}

TEST(minigames, un_dedo_que_se_mueve_mas_de_8_px_cuenta_y_uno_que_tiembla_no) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_CATCH, now, 0, false);
  playUntil(m, now);
  m.touch(true, 200, 300, now);
  CHECK(m.counted);
  m.touch(true, 208, 300, now += 20);  // 8 px: aun es temblor
  CHECK(!m.counted);
  m.touch(true, 209, 300, now += 20);  // 9 px
  CHECK(m.counted);
  m.touch(true, 213, 300, now += 20);  // 4 px desde el ultimo punto contado
  CHECK(!m.counted);
  m.touch(true, 217, 303, now += 20);  // 8*8 + 3*3 > 64
  CHECK(m.counted);
  m.touch(false, 217, 303, now += 20);
  CHECK(!m.counted);
}

TEST(minigames, deslizar_en_Berry_Catch_sigue_contando_como_actividad) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_CATCH, now, 0, false);
  playUntil(m, now);
  m.touch(true, 150, 350, now);
  uint32_t t0 = now;
  int x = 150, dir = 20;
  while (now - t0 < 22000) {  // 22 s deslizando sin parar: pasa de los 15 s de inactividad
    m.frame(now += 85);
    x += dir;
    if (x > 300 || x < 150) dir = -dir;
    m.touch(true, x, 350, now);
    CHECK_MSG(!m.over(), "acaba por inactividad deslizando: t=" + std::to_string(now - t0));
    if (m.over()) break;
  }
  CHECK(!m.over());
}

TEST(minigames, en_Berry_Catch_el_deslizamiento_mueve_el_objetivo) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_CATCH, now, 0, false);
  playUntil(m, now);
  m.touch(true, 150, 350, now);
  CHECK_EQ(m.catcher.targetX, (int16_t)150);
  m.touch(true, 320, 350, now += 20);  // MOVE
  CHECK_EQ(m.catcher.targetX, (int16_t)320);
}

TEST(minigames, a_los_14_s_de_turno_sin_tocar_Berry_Song_no_ha_acabado_aun_por_inactividad) {
  // el tiempo que suena la melodia no cuenta: la cuenta empieza al acabar de sonar
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_SONG, now, 0, false);
  songToTurn(m, now);
  CHECK(m.song.playersTurn());
  uint32_t turn0 = now;
  CHECK(turn0 > GameFlow::TITLE_MS + 2000);  // la melodia de 2 notas dura mas de 2 s
  while (now - turn0 < 14000) m.frame(now += 85);
  CHECK_MSG(!m.over(), "la melodia se contaba como inactividad");
  while (!m.over() && now - turn0 < 20000) m.frame(now += 85);
  CHECK(m.over());
}

TEST(minigames, solo_Berry_Song_se_queda_ocupada_mientras_suena) {
  MiniGames m;
  m.begin(GAME_SONG, 0, 0, false);
  CHECK(m.busy());  // LEAD
  m.begin(GAME_KEEPY, 0, 0, false);
  CHECK(!m.busy());
}

TEST(minigames, puntuar_keepy_solo_cuenta_al_apoyar_no_al_mantener) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_KEEPY, now, 0, false);
  playUntil(m, now);
  int bx = (int)m.keepy.ballX, by = (int)m.keepy.ballY;
  m.touch(true, bx, by, now);
  for (int i = 0; i < 10; i++) m.touch(true, bx, by, now += 20);  // 10 MOVE sobre la bola
  CHECK_EQ(m.keepy.score, (uint8_t)1);
  CHECK_EQ(m.score(), (uint16_t)1);
}

TEST(minigames, empezar_un_toque_a_oscuras_no_llega_al_juego_ni_siquiera_con_la_bola_congelada) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_KEEPY, now, 0, false);
  playUntil(m, now);
  int bx = (int)m.keepy.ballX, by = (int)m.keepy.ballY;
  uint32_t touched0 = m.flow.lastTouch;
  for (int i = 0; i < 300; i++) {  // 300 toques a ciegas, sin fotogramas (pantalla apagada)
    CHECK(!m.touch(true, bx, by, now += 20, true));
    CHECK(!m.counted);
    CHECK(!m.touch(false, bx, by, now += 20, false));
  }
  CHECK_EQ(m.keepy.score, (uint8_t)0);
  CHECK_EQ(m.flow.lastTouch, touched0);
  // el mismo dedo, ya despierta, sigue tragado hasta soltar
  m.touch(true, bx, by, now += 20, true);
  m.touch(true, bx, by, now += 20, false);
  m.touch(true, bx, by, now += 20, false);
  CHECK_EQ(m.keepy.score, (uint8_t)0);
  m.touch(false, bx, by, now += 20, false);
  m.touch(true, bx, by, now += 20, false);
  CHECK_EQ(m.keepy.score, (uint8_t)1);
}

TEST(minigames, la_bola_de_keepy_bajo_el_boton_de_salir_se_puede_golpear_y_el_boton_sigue_saliendo) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_KEEPY, now, 0, false);
  playUntil(m, now);
  m.keepy.ballX = 233;
  m.keepy.ballY = 40;  // bajo el boton (y 20..60)
  CHECK(GameTouchRouter::inExit(233, 40));
  CHECK(!m.touch(true, 233, 40, now += 20));
  CHECK_EQ(m.keepy.score, (uint8_t)1);  // el toque que empieza en el boton golpea
  CHECK(m.counted);
  CHECK(m.keepy.takeEvents() & GEV_GOOD);
  for (int i = 0; i < 10; i++) m.touch(true, 233, 40, now += 20);  // mantener no vuelve a golpear
  CHECK_EQ(m.keepy.score, (uint8_t)1);
  // y mantenerlo 1 s sigue saliendo
  bool out = false;
  for (int i = 0; i < 60 && !out; i++) out = m.touch(true, 233, 40, now += 20);
  CHECK(out);
  m.touch(false, 233, 40, now += 20);
  // lejos de la bola: es solo el boton, no puntua ni cuenta como actividad
  m.keepy.ballX = 233;
  m.keepy.ballY = 200;
  uint8_t before = m.keepy.score;
  uint32_t lt = m.flow.lastTouch;
  CHECK(!m.touch(true, 233, 40, now += 20));
  CHECK_EQ(m.keepy.score, before);
  CHECK(!m.counted);
  CHECK_EQ(m.flow.lastTouch, lt);
  // en los otros juegos el boton sigue siendo solo el boton
  MiniGames p;
  now = 0;
  p.begin(GAME_PEEK, now, 0, false);
  playUntil(p, now);
  p.touch(true, 233, 40, now += 20);
  CHECK(!p.counted);
}

TEST(minigames, play_con_otra_entropia_reparte_otra_primera_ronda) {
  bool game[GAME_COUNT] = {};
  uint32_t states[8];
  for (uint32_t e = 0; e < 8; e++) {
    MiniGames m;
    m.start(0, 0x1000u + e, 0, false);
    game[m.game()] = true;
    states[e] = m.rng.s;
  }
  int kinds = 0;
  for (bool g : game) kinds += g;
  CHECK_MSG(kinds >= 2, "la entropia no cambia el primer juego");
  for (int a = 0; a < 8; a++)
    for (int b = a + 1; b < 8; b++) CHECK_MSG(states[a] != states[b], "dos entropias dan el mismo dado");
  // misma entropia = misma partida
  MiniGames x, y;
  x.start(0, 77, 0, false);
  y.start(0, 77, 0, false);
  CHECK_EQ(x.game(), y.game());
  CHECK_EQ(x.rng.s, y.rng.s);
}

TEST(minigames, ganar_Berry_Song_se_avisa_para_la_fanfarria) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_SONG, now, 0, false);
  CHECK(!m.won());
  m.song.won = true;
  CHECK(m.won());
  m.begin(GAME_KEEPY, now, 0, false);
  m.song.won = true;
  CHECK(!m.won());  // solo cuenta en Berry Song
}

// ---------------------------------------------------------------- keepy-uppy
TEST(keepy, la_bola_sale_arriba_a_1_6_px_por_paso) {
  for (uint32_t seed = 1; seed < 200; seed++) {
    KeepyGame k;
    k.reset(seed);
    CHECK_RANGE(k.ballX, 150.0f, 315.0f);
    CHECK_EQ(k.ballY, 96.0f);
    CHECK_EQ(fabsf(k.ballVX), 1.6f);
    CHECK_EQ(k.ballVY, 0.0f);
  }
  KeepyGame k;
  k.reset(1);
  k.score = 50;
  k.respawn();
  CHECK_EQ(fabsf(k.ballVX), 4.0f);  // tope
}

TEST(keepy, la_fisica_es_la_de_antes) {
  KeepyGame k;
  k.reset(1);
  k.ballX = 233; k.ballY = 96; k.ballVX = 0; k.ballVY = 0; k.petX = 233;
  k.step(85);  // k = 1
  CHECK(fabsf(k.ballVY - 0.40f) < 1e-5f);
  CHECK(fabsf(k.ballY - 96.40f) < 1e-4f);
  k.step(170);  // k = 2
  CHECK(fabsf(k.ballVY - 1.20f) < 1e-5f);
  CHECK(fabsf(k.ballY - 98.80f) < 1e-4f);
  k.score = 40;  // la gravedad se topa en 0.80
  k.ballVY = 0;
  k.step(85);
  CHECK(fabsf(k.ballVY - 0.80f) < 1e-5f);
  // la persecucion del bicho: 0.12 de la distancia, como mucho 7 px por paso
  k.reset(1);
  k.ballX = 300; k.ballY = 200; k.ballVX = 0; k.ballVY = 0; k.petX = 100;
  k.step(85);
  CHECK(fabsf(k.petX - 107.0f) < 1e-4f);
  k.petX = 290;
  k.step(85);
  CHECK(fabsf(k.petX - (290.0f + 10.0f * 0.12f)) < 1e-3f);
}

TEST(keepy, un_toque_dentro_de_74_px_golpea) {
  KeepyGame k;
  k.reset(3);
  k.ballX = 200; k.ballY = 200; k.ballVX = 1; k.ballVY = 3;
  k.touchDown(200 + 74, 200);  // justo en el borde: no
  CHECK_EQ(k.score, (uint8_t)0);
  k.touchDown(200 + 50, 200);
  CHECK_EQ(k.score, (uint8_t)1);
  CHECK(fabsf(k.ballVY - (-(6.6f + 0.22f))) < 1e-5f);
  CHECK(fabsf(k.ballVX - (1.0f + (-50) * 0.12f)) < 1e-5f);
  CHECK(k.takeEvents() & GEV_GOOD);
  CHECK(k.hasHit);
  k.score = 20;
  k.touchDown(200, 200);
  CHECK(fabsf(k.ballVY - (-(6.6f + 3.5f))) < 1e-5f);  // impulso topado por encima de 16
  k.ballVX = 6;
  k.touchDown(150, 200);  // empuja a la derecha: se topa en 6.5
  CHECK_EQ(k.ballVX, 6.5f);
}

TEST(keepy, el_marcador_no_da_la_vuelta) {
  KeepyGame k;
  k.reset(1);
  k.score = 255;
  k.touchDown((int)k.ballX, (int)k.ballY);
  CHECK_EQ(k.score, (uint8_t)255);
}

TEST(keepy, la_pared_redonda_devuelve_la_bola) {
  KeepyGame k;
  k.reset(1);
  k.ballX = 233 + 204; k.ballY = 233; k.ballVX = 4; k.ballVY = 0; k.score = 0;
  k.step(85);
  float dx = k.ballX - 233, dy = k.ballY - 233;
  CHECK(fabsf(sqrtf(dx * dx + dy * dy) - 205.0f) < 0.01f);
  CHECK(k.ballVX < 0);  // rebota hacia dentro
  CHECK(fabsf(k.ballVX + 4.0f * 0.85f) < 0.1f);
}

TEST(keepy, tres_caidas_acaban_y_la_tercera_no_saca_otra_bola) {
  KeepyGame k;
  k.reset(5);
  for (int miss = 1; miss <= 3; miss++) {
    k.ballY = 383; k.ballVY = 5; k.ballX = 233; k.ballVX = 0;
    k.step(85);
    CHECK_EQ(k.misses, (uint8_t)miss);
    if (miss < 3) CHECK_EQ(k.ballY, 96.0f);  // sale otra
  }
  CHECK(k.done());
  CHECK(k.ballY > 384.0f);
  float y = k.ballY;
  k.step(85);
  CHECK_EQ(k.ballY, y);  // acabado: ya no se mueve
}

// ---------------------------------------------------------------- Berry Catch
TEST(catch, el_ritmo_sube_a_lo_largo_de_los_30_s) {
  CHECK_EQ(CatchGame::spawnGapAt(0), (uint32_t)1100);
  CHECK_EQ(CatchGame::spawnGapAt(15000), (uint32_t)875);
  CHECK_EQ(CatchGame::spawnGapAt(30000), (uint32_t)650);
  CHECK_EQ(CatchGame::spawnGapAt(90000), (uint32_t)650);
  CHECK_EQ(CatchGame::fallSpeedAt(0), 150.0f);
  CHECK_EQ(CatchGame::fallSpeedAt(15000), 205.0f);
  CHECK_EQ(CatchGame::fallSpeedAt(30000), 260.0f);
  CHECK_EQ(CatchGame::rottenPctAt(4999), (uint8_t)0);
  CHECK_EQ(CatchGame::rottenPctAt(5000), (uint8_t)13);
  CHECK_EQ(CatchGame::rottenPctAt(30000), (uint8_t)30);
}

TEST(catch, las_bayas_caen_dentro_de_la_franja_y_sin_podridas_al_principio) {
  for (uint32_t seed = 1; seed <= 60; seed++) {
    CatchGame c;
    c.reset(seed, 0, false);
    int spawned = 0, rotten = 0;
    for (int i = 0; i < 400 && !c.done(); i++) {
      bool live[CatchGame::MAX_BERRIES];
      for (int b = 0; b < CatchGame::MAX_BERRIES; b++) live[b] = c.berries[b].live;
      c.step(85);
      for (int b = 0; b < CatchGame::MAX_BERRIES; b++) {
        const CatchGame::Berry &br = c.berries[b];
        if (br.live && !live[b]) {
          spawned++;
          CHECK_RANGE(br.x, (int16_t)CatchGame::X_MIN, (int16_t)CatchGame::X_MAX);
          if (br.kind == CatchGame::ROTTEN) {
            rotten++;
            CHECK_MSG(c.t >= CatchGame::CLEAN_START_MS, "baya podrida antes de 5 s");
          }
        }
      }
    }
    CHECK_RANGE(spawned, 30, 40);  // ~35 en 30 s
    if (seed == 1) CHECK_MSG(rotten > 0, "nunca salen podridas");
  }
}

static CatchGame catchWithBerry(int x, uint8_t kind, uint8_t fav = 0, bool known = false) {
  CatchGame c;
  c.reset(1, fav, known);
  c.nextSpawn = 1000000;  // sin bayas nuevas
  c.petX = 233;
  c.targetX = 233;
  c.berries[0].live = true;
  c.berries[0].x = (int16_t)x;
  c.berries[0].y = CatchGame::CATCH_Y - 1;
  c.berries[0].vy = 200;
  c.berries[0].kind = kind;
  return c;
}

TEST(catch, se_atrapa_a_55_px_del_bicho_y_no_a_56) {
  CatchGame c = catchWithBerry(233 + 55, CatchGame::RED);
  c.step(85);
  CHECK_EQ(c.score, (int16_t)1);
  CHECK(!c.berries[0].live);
  CHECK(c.eating());
  c = catchWithBerry(233 - 55, CatchGame::BLUE);
  c.step(85);
  CHECK_EQ(c.score, (int16_t)1);
  c = catchWithBerry(233 + 56, CatchGame::RED);
  c.step(85);
  CHECK_EQ(c.score, (int16_t)0);
  CHECK(c.berries[0].live);  // sigue cayendo
  for (int i = 0; i < 10; i++) c.step(85);
  CHECK(!c.berries[0].live);  // y se pierde abajo
  CHECK_EQ(c.score, (int16_t)0);
}

TEST(catch, un_paso_largo_no_atraviesa_la_linea_sin_atrapar) {
  CatchGame c = catchWithBerry(233, CatchGame::GREEN);
  c.berries[0].y = CatchGame::CATCH_Y - 10;
  c.berries[0].vy = 260;
  c.step(250);  // 65 px de golpe: pasa la linea y la de perdida
  CHECK_EQ(c.score, (int16_t)1);
}

TEST(catch, las_podridas_quitan_2_sin_bajar_de_0) {
  CatchGame c = catchWithBerry(233, CatchGame::ROTTEN);
  c.score = 5;
  c.step(85);
  CHECK_EQ(c.score, (int16_t)3);
  CHECK(c.hurting());
  CHECK(c.takeEvents() & GEV_BAD);
  c = catchWithBerry(233, CatchGame::ROTTEN);
  c.score = 1;
  c.step(85);
  CHECK_EQ(c.score, (int16_t)0);
  CHECK_EQ(c.result(), (uint16_t)0);
}

TEST(catch, la_baya_favorita_vale_doble_solo_si_ya_se_conoce) {
  CatchGame c = catchWithBerry(233, CatchGame::BLUE, CatchGame::BLUE, true);
  c.step(85);
  CHECK_EQ(c.score, (int16_t)2);
  uint8_t e = c.takeEvents();
  CHECK(e & GEV_HEART);
  CHECK(e & GEV_GOOD);
  CHECK(c.loved);
  c = catchWithBerry(233, CatchGame::BLUE, CatchGame::BLUE, false);  // aun no descubierta
  c.step(85);
  CHECK_EQ(c.score, (int16_t)1);
  CHECK(!(c.takeEvents() & GEV_HEART));
  CHECK(!c.loved);
  c = catchWithBerry(233, CatchGame::RED, CatchGame::BLUE, true);  // otra baya
  c.step(85);
  CHECK_EQ(c.score, (int16_t)1);
}

TEST(catch, el_bicho_va_hacia_el_dedo_a_700_px_por_segundo) {
  CatchGame c;
  c.reset(1, 0, false);
  c.touch(400);
  CHECK_EQ(c.targetX, (int16_t)CatchGame::X_MAX);  // dentro de la franja
  c.touch(10);
  CHECK_EQ(c.targetX, (int16_t)CatchGame::X_MIN);
  c.touch(333);
  c.step(100);
  CHECK(fabsf(c.petX - 303.0f) < 0.01f);  // 233 + 70
  c.step(100);
  CHECK(fabsf(c.petX - 333.0f) < 0.01f);  // llega y no se pasa
}

TEST(catch, termina_a_los_30_s_y_no_salen_mas_bayas) {
  CatchGame c;
  c.reset(2, 0, false);
  while (c.t < CatchGame::DURATION_MS - 100) c.step(100);
  CHECK(!c.done());
  for (auto &b : c.berries) b.live = false;
  c.nextSpawn = CatchGame::DURATION_MS - 50;  // tocaria una baya justo al final
  c.step(250);  // el ultimo paso se pasa de los 30 s
  CHECK(c.done());
  CHECK_EQ(c.t, CatchGame::DURATION_MS);  // el reloj no pasa de 30 s
  CHECK_EQ(c.timeLeft(), (uint32_t)0);
  int live = 0;
  for (auto &b : c.berries) live += b.live;
  CHECK_EQ(live, 0);  // la partida acabo: ya no sale ninguna
}

// ---------------------------------------------------------------- Peekaboo
TEST(peek, veinte_apariciones_y_nunca_dos_seguidas_en_el_mismo_arbusto) {
  for (uint32_t seed = 1; seed <= 200; seed++) {
    PeekGame p;
    p.reset(seed);
    int ups = 0;
    uint8_t prev = PeekGame::BUSHES;
    bool used[PeekGame::BUSHES] = {};
    bool wasUp = false;
    for (int i = 0; i < 2000 && !p.done(); i++) {
      p.step(50);
      if (p.up() && !wasUp) {
        ups++;
        CHECK_MSG(p.bush != prev, "mismo arbusto dos veces seguidas");
        prev = p.bush;
        used[p.bush] = true;
      }
      wasUp = p.up();
    }
    CHECK(p.done());
    CHECK_EQ(ups, 20);
    if (seed == 1)
      for (bool u : used) CHECK_MSG(u, "algun arbusto nunca se usa");
  }
}

TEST(peek, cada_vez_asoma_menos_tiempo) {
  CHECK_EQ(PeekGame::upMsFor(0), (uint32_t)1500);
  CHECK_EQ(PeekGame::upMsFor(19), (uint32_t)750);
  CHECK_EQ(PeekGame::gapMsFor(0), (uint32_t)500);
  CHECK_EQ(PeekGame::gapMsFor(19), (uint32_t)300);
  for (uint8_t i = 1; i < 20; i++) CHECK(PeekGame::upMsFor(i) <= PeekGame::upMsFor(i - 1));
  // una partida sin tocar dura unos 30 s
  PeekGame p;
  p.reset(4);
  while (!p.done()) p.step(10);
  CHECK_RANGE(p.t, (uint32_t)25000, (uint32_t)35000);
}

static PeekGame peekUp(uint32_t seed = 1) {
  PeekGame p;
  p.reset(seed);
  while (!p.up()) p.step(10);
  return p;
}

TEST(peek, tocarlo_mientras_asoma_suma_una_vez) {
  PeekGame p = peekUp();
  int bx = PeekGame::BX[p.bush], by = PeekGame::BY[p.bush];
  p.touchDown(bx, by - 60);
  CHECK_EQ(p.score, (uint8_t)1);
  CHECK(p.takeEvents() & GEV_GOOD);
  p.touchDown(bx, by - 60);
  CHECK_EQ(p.score, (uint8_t)1);  // el mismo asomo no cuenta dos veces
  CHECK(p.phaseEnd <= p.t + PeekGame::DUCK_AFTER_HIT_MS);  // se esconde enseguida
}

TEST(peek, tocar_cuando_ya_se_escondio_no_suma) {
  PeekGame p = peekUp();
  uint8_t b = p.bush;
  while (p.up()) p.step(10);
  p.touchDown(PeekGame::BX[b], PeekGame::BY[b] - 60);
  CHECK_EQ(p.score, (uint8_t)0);
  CHECK(p.rustling(b));  // solo se mueve el arbusto
}

TEST(peek, tocar_otro_arbusto_no_cuesta_nada) {
  PeekGame p = peekUp();
  uint8_t other = PeekGame::BUSHES;  // un arbusto cuyo centro queda fuera de la zona del que asoma
  for (uint8_t b = 0; b < PeekGame::BUSHES && other == PeekGame::BUSHES; b++)
    if (b != p.bush && !PeekGame::inBush(p.bush, PeekGame::BX[b], PeekGame::BY[b])) other = b;
  CHECK(other < PeekGame::BUSHES);
  p.touchDown(PeekGame::BX[other], PeekGame::BY[other]);
  CHECK_EQ(p.score, (uint8_t)0);
  CHECK(p.rustling(other));
  CHECK(p.up());  // el bicho sigue asomado
  p.touchDown(PeekGame::BX[p.bush], PeekGame::BY[p.bush]);
  CHECK_EQ(p.score, (uint8_t)1);
}

TEST(peek, el_segundo_asomo_tambien_se_puede_golpear) {
  PeekGame p = peekUp(2);
  uint8_t first = p.bush;
  p.touchDown(PeekGame::BX[first], PeekGame::BY[first] - 60);
  CHECK_EQ(p.score, (uint8_t)1);
  CHECK(p.hit);
  while (p.up()) p.step(10);
  while (!p.up()) p.step(10);  // otro asomo
  CHECK_EQ(p.pops, (uint8_t)1);
  p.touchDown(PeekGame::BX[p.bush], PeekGame::BY[p.bush] - 60);
  CHECK_EQ(p.score, (uint8_t)2);
}

TEST(peek, la_zona_de_toque_es_el_arbusto_y_lo_que_asoma) {
  for (uint8_t b = 0; b < PeekGame::BUSHES; b++) {
    int x = PeekGame::BX[b], y = PeekGame::BY[b];
    CHECK(PeekGame::inBush(b, x - 60, y));
    CHECK(PeekGame::inBush(b, x + 60, y));
    CHECK(!PeekGame::inBush(b, x + 61, y));
    CHECK(PeekGame::inBush(b, x, y - 140));
    CHECK(!PeekGame::inBush(b, x, y - 141));
    CHECK(PeekGame::inBush(b, x, y + 40));
    CHECK(!PeekGame::inBush(b, x, y + 41));
    // dentro de la pantalla redonda, con margen para el arbusto
    CHECK((x - 233) * (x - 233) + (y - 233) * (y - 233) < 170 * 170);
  }
  CHECK(!PeekGame::inBush(PeekGame::BUSHES, 233, 310));
}

// ---------------------------------------------------------------- Berry Song
// avanza hasta el turno del jugador (o el final) y devuelve las notas que sonaron
static std::vector<uint8_t> listen(SongGame &s) {
  std::vector<uint8_t> notes;
  s.takeNote();  // la ultima nota que toco el jugador no es de la melodia
  for (int i = 0; i < 2000 && !s.playersTurn() && !s.done(); i++) {
    s.step(50);
    uint8_t n = s.takeNote();
    if (n != SongGame::NO_PAD) notes.push_back(n);
  }
  return notes;
}
static void press(SongGame &s, uint8_t pad) { s.touchDown(SongGame::PX[pad], SongGame::PY[pad]); }

TEST(song, la_melodia_crece_de_una_en_una_y_conserva_el_principio) {
  SongGame s;
  s.reset(11);
  for (uint8_t len = 2; len <= 6; len++) {
    std::vector<uint8_t> heard = listen(s);
    CHECK_EQ(heard.size(), (size_t)len);
    for (uint8_t i = 0; i < len && i < heard.size(); i++) CHECK_EQ(heard[i], s.seq[i]);
    for (uint8_t i = 0; i < len; i++) press(s, s.seq[i]);
    CHECK_EQ(s.best, len);
  }
}

TEST(song, mientras_suena_la_melodia_los_toques_no_cuentan) {
  SongGame s;
  s.reset(5);
  s.step(SongGame::LEAD_MS + 10);  // primera nota
  CHECK(s.busy());
  press(s, s.seq[0]);
  CHECK_EQ(s.k, (uint8_t)0);
  CHECK_EQ(s.tries, SongGame::TRIES);
  listen(s);
  CHECK(!s.busy());
  CHECK(s.playersTurn());
}

TEST(song, un_fallo_cuesta_un_intento_y_repite_la_misma_melodia) {
  SongGame s;
  s.reset(8);
  listen(s);
  press(s, s.seq[0]);
  press(s, (uint8_t)((s.seq[1] + 1) % SongGame::PADS));  // mal
  CHECK_EQ(s.tries, (uint8_t)(SongGame::TRIES - 1));
  CHECK(s.takeEvents() & GEV_BAD);
  std::vector<uint8_t> again = listen(s);
  CHECK_EQ(again.size(), (size_t)2);  // la misma longitud
  CHECK_EQ(s.len, (uint8_t)2);
}

TEST(song, tres_fallos_acaban_con_la_mejor_melodia_como_puntuacion) {
  SongGame s;
  s.reset(9);
  listen(s);
  press(s, s.seq[0]);
  press(s, s.seq[1]);  // 2 notas bien
  for (int miss = 0; miss < 3; miss++) {
    listen(s);
    press(s, (uint8_t)((s.seq[0] + 1) % SongGame::PADS));
  }
  CHECK(s.done());
  CHECK(!s.won);
  CHECK_EQ(s.result(), (uint16_t)2);
}

TEST(song, repetir_las_12_notas_gana) {
  SongGame s;
  s.reset(3);
  for (uint8_t len = 2; len <= SongGame::MAX_LEN; len++) {
    listen(s);
    for (uint8_t i = 0; i < len; i++) press(s, s.seq[i]);
  }
  CHECK(s.done());
  CHECK(s.won);
  CHECK(s.takeEvents() & GEV_WIN);
  CHECK_EQ(s.result(), (uint16_t)12);
}

TEST(song, esta_ocupada_en_todas_las_fases_menos_el_turno_y_el_final) {
  SongGame s;
  s.reset(6);
  CHECK_EQ((int)s.phase, (int)SongGame::LEAD);
  CHECK(s.busy());
  s.step(SongGame::LEAD_MS + 10);
  CHECK_EQ((int)s.phase, (int)SongGame::SHOW);
  CHECK(s.busy());
  listen(s);
  CHECK_EQ((int)s.phase, (int)SongGame::TURN);
  CHECK(!s.busy());
  press(s, s.seq[0]);
  press(s, s.seq[1]);  // acierta la ronda
  CHECK_EQ((int)s.phase, (int)SongGame::PAUSE);
  CHECK(s.busy());
  listen(s);
  press(s, (uint8_t)((s.seq[0] + 1) % SongGame::PADS));  // falla
  CHECK_EQ((int)s.phase, (int)SongGame::WRONG);
  CHECK(s.busy());
  for (int miss = 0; miss < 2; miss++) {
    listen(s);
    press(s, (uint8_t)((s.seq[0] + 1) % SongGame::PADS));
  }
  CHECK(s.done());
  CHECK(!s.busy());
}

TEST(song, las_notas_se_aceleran_con_melodias_largas) {
  CHECK_EQ(SongGame::noteMsFor(2), (uint32_t)500);
  CHECK_EQ(SongGame::noteMsFor(5), (uint32_t)425);
  CHECK_EQ(SongGame::noteMsFor(8), (uint32_t)350);
  CHECK_EQ(SongGame::noteMsFor(12), (uint32_t)350);
}

TEST(song, cada_boton_se_toca_en_su_circulo_y_no_se_solapan) {
  for (uint8_t p = 0; p < SongGame::PADS; p++) {
    CHECK_EQ(SongGame::padAt(SongGame::PX[p], SongGame::PY[p]), p);
    CHECK_EQ(SongGame::padAt(SongGame::PX[p], SongGame::PY[p] - SongGame::HIT_R), p);
    CHECK_EQ(SongGame::padAt(SongGame::PX[p], SongGame::PY[p] - SongGame::HIT_R - 1), SongGame::NO_PAD);
    int x = SongGame::PX[p], y = SongGame::PY[p];
    CHECK((x - 233) * (x - 233) + (y - 233) * (y - 233) < (233 - SongGame::PAD_R) * (233 - SongGame::PAD_R));
    for (uint8_t q = p + 1; q < SongGame::PADS; q++) {
      int dx = SongGame::PX[q] - x, dy = SongGame::PY[q] - y;
      CHECK(dx * dx + dy * dy >= 4 * SongGame::HIT_R * SongGame::HIT_R);
    }
  }
  CHECK_EQ(SongGame::padAt(233, 200), SongGame::NO_PAD);
}

TEST(song, al_tocar_se_ilumina_el_boton_y_suena_su_nota) {
  SongGame s;
  s.reset(2);
  listen(s);
  press(s, s.seq[0]);
  CHECK_EQ(s.litPad(), s.seq[0]);
  CHECK_EQ(s.takeNote(), s.seq[0]);
  s.step(SongGame::PRESS_MS);
  CHECK_EQ(s.litPad(), SongGame::NO_PAD);
}
