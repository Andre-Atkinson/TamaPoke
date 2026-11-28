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

// ---------------------------------------------------------------- the pay (#127)
// The reference: point i (0-based) of a run is paid at step i / GAME_STEP + 1 and pays that
// step / GAME_STEP; the sum, rounded half up (in floating point, with a hair of slack for the
// .5 cases that don't add up exactly).
static uint32_t payRef(uint8_t g, uint32_t score) {
  double sum = 0;
  for (uint32_t i = 0; i < score; i++) sum += (double)(i / GAME_STEP[g] + 1) / GAME_STEP[g];
  uint32_t p = (uint32_t)floor(sum + 0.5 + 1e-9);
  return p > 100 ? 100 : p;
}

TEST(minigames, the_speed_pay_is_the_ladder_a_table_of_values) {
  CHECK_EQ(gameSpeedPay(GAME_KEEPY, 0), (uint8_t)0);
  CHECK_EQ(gameSpeedPay(GAME_KEEPY, 15), (uint8_t)3);
  CHECK_EQ(gameSpeedPay(GAME_KEEPY, 30), (uint8_t)9);
  CHECK_EQ(gameSpeedPay(GAME_CATCH, 20), (uint8_t)3);
  CHECK_EQ(gameSpeedPay(GAME_PEEK, 14), (uint8_t)3);
  CHECK_EQ(gameSpeedPay(GAME_SONG, 2), (uint8_t)1);
  CHECK_EQ(gameSpeedPay(GAME_SONG, 6), (uint8_t)3);
  CHECK_EQ(gameSpeedPay(GAME_SONG, 9), (uint8_t)6);
  CHECK_EQ(gameSpeedPay(GAME_SONG, 12), (uint8_t)10);
  // the first step pays 1/8 a point in keepy-uppy: 8 hits pay 1, 4 round to 1 (half up), 3 to 0
  CHECK_EQ(gameSpeedPay(GAME_KEEPY, 8), (uint8_t)1);
  CHECK_EQ(gameSpeedPay(GAME_KEEPY, 4), (uint8_t)1);
  CHECK_EQ(gameSpeedPay(GAME_KEEPY, 3), (uint8_t)0);
  CHECK_EQ(gameSpeedPay(GAME_COUNT, 30), (uint8_t)0);  // not a game
  for (uint8_t g = 0; g < GAME_COUNT; g++)
    CHECK_MSG(gameSpeedPay(g, GAME_PAR[g]) == 3, "par pays +3 in game " + std::to_string(g));
}

TEST(minigames, the_speed_pay_follows_the_ladder_at_every_score_and_never_passes_100) {
  for (uint8_t g = 0; g < GAME_COUNT; g++) {
    uint8_t prev = 0;
    for (uint32_t s = 0; s <= GAME_REC_MAX[g]; s++) {
      uint8_t pay = gameSpeedPay(g, (uint16_t)s);
      CHECK_MSG(pay == payRef(g, s),
                "game " + std::to_string(g) + " score " + std::to_string(s) + ": " + std::to_string(pay) + " not " +
                    std::to_string(payRef(g, s)));
      CHECK_MSG(pay >= prev && pay <= 100, "not rising, or over 100: game " + std::to_string(g) + " score " + std::to_string(s));
      prev = pay;
    }
    // past the cap (a bad score) pays what the cap does; nothing wraps in 32 bits
    CHECK_EQ(gameSpeedPay(g, 65535), gameSpeedPay(g, GAME_REC_MAX[g]));
    CHECK_EQ(gameSpeedPay(g, GAME_REC_MAX[g]), (uint8_t)100);  // a run to the cap is worth the stat's top
  }
}

TEST(minigames, a_pay_step_takes_about_the_same_effort_in_every_game) {
  for (uint8_t g = 0; g < GAME_COUNT; g++) {
    CHECK_MSG(GAME_STEP[g] * 2 >= GAME_PAR[g] - 1 && GAME_STEP[g] * 2 <= GAME_PAR[g] + 1,
              "a step is about half the par: game " + std::to_string(g));
    CHECK_EQ(gameStep(g, 0), (uint8_t)1);
    CHECK_EQ(gameStep(g, GAME_STEP[g] - 1), (uint8_t)1);
    CHECK_EQ(gameStep(g, GAME_STEP[g]), (uint8_t)2);
    CHECK_EQ(gameStep(g, 2 * GAME_STEP[g] - 1), (uint8_t)2);
    CHECK_EQ(gameStep(g, 2 * GAME_STEP[g]), (uint8_t)3);
    CHECK_EQ(gameStep(g, GAME_PAR[g]), (uint8_t)(GAME_PAR[g] / GAME_STEP[g] + 1));
    CHECK_EQ(gameStep(g, 65535), gameStep(g, GAME_REC_MAX[g]));  // no wrap
  }
  CHECK_EQ(gameStep(GAME_COUNT, 50), (uint8_t)1);
}

TEST(minigames, each_game_ends_by_itself_at_the_cap_its_record_can_hold) {
  CHECK_EQ(GAME_REC_MAX[GAME_KEEPY], (uint16_t)255);
  CHECK_EQ(GAME_REC_MAX[GAME_CATCH], (uint16_t)400);
  CHECK_EQ(GAME_REC_MAX[GAME_PEEK], (uint16_t)255);
  CHECK_EQ(GAME_REC_MAX[GAME_SONG], (uint16_t)100);
  CHECK_EQ(KeepyGame::MAX_HITS, (uint8_t)255);
  CHECK_EQ(CatchGame::MAX_CATCHES * 2, (int)GAME_REC_MAX[GAME_CATCH]);  // a known favourite scores 2
  CHECK_EQ(PeekGame::MAX_HITS, (uint8_t)255);
  CHECK_EQ(SongGame::MAX_LEN, (uint8_t)100);
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

TEST(minigames, la_partida_acaba_cuando_acaba_el_juego_y_conserva_lo_conseguido) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_CATCH, now, 0, false);
  playUntil(m, now);
  m.touch(true, 150, 350, now);  // steers the pet to one side
  m.touch(false, 150, 350, now + 20);
  m.catcher.nextSpawn = 1000000;  // no more berries but the one below
  m.catcher.warm = false;
  m.catcher.score = 3;
  m.catcher.caught = 3;
  m.catcher.berries[0].live = true;
  m.catcher.berries[0].x = 366;  // far from the pet: it lands uncaught
  m.catcher.berries[0].y = CatchGame::CATCH_Y - 1;
  m.catcher.berries[0].vy = 200;
  m.catcher.berries[0].kind = CatchGame::RED;
  for (int i = 0; i < 100 && !m.over(); i++) m.frame(now += 85);
  CHECK(m.over());
  CHECK(m.catcher.done());
  CHECK_EQ(m.score(), (uint16_t)3);
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
  m.catcher.nextSpawn = 1000000;  // no berries: a miss would end the game, and this checks the idle end
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
  k.step(170);  // k = 2: two sub-steps of 85 ms (#127: never more than 85 ms of game time at once)
  CHECK(fabsf(k.ballVY - 1.20f) < 1e-5f);
  CHECK(fabsf(k.ballY - 98.40f) < 1e-4f);
  k.score = 31;  // la gravedad se topa en 0.80 (31 es el ultimo a ritmo 1)
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

TEST(keepy, before_the_first_hit_a_drop_deals_a_new_ball_and_after_it_a_drop_ends_the_game) {
  KeepyGame k;
  k.reset(5);
  for (int drop = 1; drop <= 5; drop++) {  // the warm-up: dropped, dealt again, never over
    k.ballY = 383; k.ballVY = 5; k.ballX = 233; k.ballVX = 0;
    k.step(85);
    CHECK(!k.done());
    CHECK(!k.dropped);
    CHECK_EQ(k.ballY, 96.0f);
    CHECK_EQ(k.score, (uint8_t)0);
  }
  k.ballX = 233; k.ballY = 200;
  k.touchDown(233, 200);  // the first hit: the warm-up is over
  CHECK_EQ(k.score, (uint8_t)1);
  k.ballY = 383; k.ballVY = 5; k.ballX = 233; k.ballVX = 0;
  k.step(85);
  CHECK(k.done());  // one drop ends it, and no new ball comes
  CHECK(k.dropped);
  CHECK(k.ballY > 384.0f);
  CHECK_EQ(k.result(), (uint16_t)1);  // it keeps what was done
  float y = k.ballY;
  k.step(85);
  CHECK_EQ(k.ballY, y);  // over: it no longer moves
  k.touchDown((int)k.ballX, (int)k.ballY);
  CHECK_EQ(k.score, (uint8_t)1);  // ...nor hits
}

TEST(keepy, the_pace_is_1_up_to_31_hits_then_rises_ever_more_slowly_and_never_reaches_2_5) {
  for (uint16_t s = 0; s <= 31; s++) CHECK_EQ(KeepyGame::paceAt(s), 1.0f);
  CHECK(KeepyGame::paceAt(32) > 1.0f);
  CHECK(fabsf(KeepyGame::paceAt(91) - 1.75f) < 1e-5f);
  CHECK(fabsf(KeepyGame::paceAt(151) - 2.0f) < 1e-5f);
  CHECK_RANGE(KeepyGame::paceAt(255), 2.17f, 2.19f);
  float prev = 1.0f, prevStep = 1e9f;
  for (uint16_t s = 32; s <= 255; s++) {
    float p = KeepyGame::paceAt(s);
    CHECK_MSG(p > prev, "the pace does not rise with every hit: " + std::to_string(s));
    CHECK_MSG(p - prev <= prevStep + 1e-6f, "the pace does not slow its rise: " + std::to_string(s));
    CHECK(p < 2.5f);
    prevStep = p - prev;
    prev = p;
  }
}

// A frame's physics runs in as few sub-steps as keep each under STEP_MS of game time: at pace 2.1 and
// 85 ms that is 3 of 0.7 steps. (Falling from rest, y gains g*r*r*(n+1)/(2n) over n sub-steps of r/n.)
TEST(keepy, a_frame_of_a_fast_game_is_split_into_sub_steps_of_at_most_85_ms_of_game_time) {
  KeepyGame k;
  k.reset(1);
  k.score = 200;  // the pace is about 2.1 here, gravity 0.80
  float r = KeepyGame::paceAt(200);
  int n = (int)ceilf(r);
  CHECK_EQ(n, 3);
  k.ballX = 233; k.ballY = 96; k.ballVX = 0; k.ballVY = 0; k.petX = 233;
  k.step(85);
  CHECK(fabsf(k.ballVY - 0.80f * r) < 1e-4f);
  float expect = 96.0f + 0.80f * r * r * (n + 1) / (2.0f * n);
  CHECK_MSG(fabsf(k.ballY - expect) < 1e-3f, "y " + std::to_string(k.ballY) + " not " + std::to_string(expect));
  // up to 31 hits a frame of 85 ms or less is one step, as it always was
  KeepyGame j;
  j.reset(1);
  j.score = 31;
  j.ballX = 233; j.ballY = 96; j.ballVX = 0; j.ballVY = 0;
  j.step(85);
  CHECK(fabsf(j.ballY - (96.0f + 0.80f)) < 1e-4f);
}

// A hit on a ball at the floor line (the latest a hit can come) leaves the ball in the air for
// at least this long before it comes back to the floor, at every score: the pace never makes
// the arc too short to answer. The arc is 25 steps of 85 ms game time at the top lift and
// gravity, 2.18 times faster at the end: 0.98 s at worst (measured at several frame sizes).
TEST(keepy, a_hit_on_a_ball_at_the_floor_line_always_leaves_about_a_second_at_every_score) {
  float worst = 1e9f;
  for (int s = 0; s <= 253; s++) {  // s hits before this one: the 254th is the last that doesn't end the game
    for (uint32_t frame : { 40u, 100u }) {
      KeepyGame k;
      k.reset(1);
      k.score = (uint8_t)s;
      k.ballX = 233; k.ballY = 384; k.ballVX = 0; k.ballVY = 7;
      CHECK(k.touchDown(233, 384));
      uint32_t t0 = k.t;
      for (int i = 0; i < 400 && !k.done(); i++) k.step(frame);
      CHECK_MSG(k.done(), "the ball never came back at score " + std::to_string(s));
      float ms = (float)(k.t - t0);
      if (ms < worst) worst = ms;
      CHECK_MSG(ms >= 900.0f, "score " + std::to_string(s) + ": only " + std::to_string(ms) + " ms in the air");
    }
  }
  CHECK_MSG(worst >= 940.0f, "the worst arc is " + std::to_string(worst) + " ms");
}

// A bot that taps the falling ball once it is low (so late), with a random offset of up to 40 px,
// on frames of 40 to 100 ms, never drops it: it reaches the 255th hit that ends the game.
TEST(keepy, a_late_and_untidy_player_reaches_the_cap_without_a_drop) {
  for (uint32_t seed = 1; seed <= 24; seed++) {
    KeepyGame k;
    k.reset(seed * 977);
    GameRng jitter(seed * 31 + 7);
    uint8_t farthest = 0;
    for (int i = 0; i < 400000 && !k.done(); i++) {
      k.step(40 + jitter.below(61));
      if (k.done()) break;
      if (k.ballVY > 0 && k.ballY >= 250) {
        int dx, dy;
        do {
          dx = (int)jitter.below(81) - 40;
          dy = (int)jitter.below(81) - 40;
        } while (dx * dx + dy * dy > 40 * 40);
        k.touchDown((int)k.ballX + dx, (int)k.ballY + dy);
        farthest = k.score;
      }
    }
    CHECK_MSG(k.done() && !k.dropped && k.score == 255,
              "seed " + std::to_string(seed) + " ended at " + std::to_string(k.score) + (k.dropped ? " (dropped)" : ""));
    (void)farthest;
  }
}


// ---------------------------------------------------------------- Berry Catch
TEST(catch, the_ramps_rise_with_the_berries_caught_towards_a_floor) {
  CHECK_EQ(CatchGame::spawnGapFor(0), (uint32_t)1100);
  CHECK_EQ(CatchGame::spawnGapFor(25), (uint32_t)648);
  CHECK_EQ(CatchGame::spawnGapFor(50), (uint32_t)567);
  CHECK_EQ(CatchGame::spawnGapFor(100), (uint32_t)514);
  CHECK_EQ(CatchGame::fallSpeedFor(0), (uint32_t)150);
  CHECK_EQ(CatchGame::fallSpeedFor(25), (uint32_t)261);
  CHECK_EQ(CatchGame::fallSpeedFor(50), (uint32_t)307);
  CHECK_EQ(CatchGame::fallSpeedFor(100), (uint32_t)349);
  for (uint16_t c = 0; c < CatchGame::CLEAN_CATCHES; c++) CHECK_EQ(CatchGame::rottenPctFor(c), (uint8_t)0);
  CHECK_EQ(CatchGame::rottenPctFor(5), (uint8_t)10);
  CHECK_EQ(CatchGame::rottenPctFor(25), (uint8_t)22);
  for (uint16_t c = 1; c <= CatchGame::MAX_CATCHES; c++) {
    CHECK_MSG(CatchGame::spawnGapFor(c) <= CatchGame::spawnGapFor(c - 1), "berries come no more often at " + std::to_string(c));
    CHECK_MSG(CatchGame::fallSpeedFor(c) >= CatchGame::fallSpeedFor(c - 1), "berries fall no faster at " + std::to_string(c));
    CHECK_MSG(CatchGame::rottenPctFor(c) >= CatchGame::rottenPctFor(c - 1), "no more rotten ones at " + std::to_string(c));
    CHECK(CatchGame::spawnGapFor(c) > 450);          // a floor past what anyone keeps up for long
    CHECK(CatchGame::fallSpeedFor(c) < 420);
    CHECK(CatchGame::rottenPctFor(c) < 35);
  }
  // a count, and not a score: far past the cap the maths still holds (no wrap)
  CHECK(CatchGame::spawnGapFor(65535) >= 450 && CatchGame::fallSpeedFor(65535) <= 420 && CatchGame::rottenPctFor(65535) < 35);
}

// The plan needs a berry to arrive after the one before it. Between two spawns the berries
// caught can go up by a few (the next spawn reads the count at its own time), which makes the
// next berry fall faster; the spawn gap has to cover that, with room for a slow frame.
TEST(catch, berries_arrive_in_the_order_they_spawn_at_every_level) {
  for (uint16_t c = 0; c < CatchGame::MAX_CATCHES; c++) {
    for (uint16_t d = 0; d <= 4 && c + d <= CatchGame::MAX_CATCHES; d++) {
      float travel = CatchGame::CATCH_Y - CatchGame::SPAWN_Y;
      float arriveLater = CatchGame::spawnGapFor(c) + 1000.0f * travel / CatchGame::fallSpeedFor(c + d);
      float arriveEarlier = 1000.0f * travel / CatchGame::fallSpeedFor(c);
      CHECK_MSG(arriveLater - arriveEarlier >= 250.0f,  // a frame's slack either side
                "a berry arrives before the one ahead at " + std::to_string(c) + " caught, " + std::to_string(d) + " more");
    }
  }
}

// Where on a good berry the bot stands: anywhere a catch is possible, so within CATCH_DX - 5
// of its x, a fixed pseudo-random offset per berry (R1-3 of #129: a bot that always stood on
// the berry's exact x was never off the plan's position, so the rotten berries' margin from
// it, AVOID, was never needed and could be cut down without a test noticing).
static int catchBotOff(const CatchGame::Berry &b) {
  uint32_t h = ((uint32_t)b.x * 2654435761u) >> 16;
  return (int)(h % (2 * (CatchGame::CATCH_DX - 5) + 1)) - (CatchGame::CATCH_DX - 5);
}

// The player's side of the plan: reacts only to berries that have been visible for 250 ms,
// steers to the next one to arrive when it is good (off-centre, see catchBotOff) and holds
// still when it is rotten.
static void catchBotSteer(CatchGame &c) {
  if (!c.steered) c.touch(233);  // a first touch, wherever
  const CatchGame::Berry *next = nullptr;
  float nextEta = 1e9f;
  for (const CatchGame::Berry &b : c.berries) {
    if (!b.live || b.y >= CatchGame::CATCH_Y) continue;
    if ((b.y - CatchGame::SPAWN_Y) / b.vy < 0.25f) continue;
    float eta = (CatchGame::CATCH_Y - b.y) / b.vy;
    if (eta < nextEta) { nextEta = eta; next = &b; }
  }
  if (!next) return;
  if (next->kind == CatchGame::ROTTEN) c.touch((int)(c.petX + 0.5f));
  else c.touch(next->x + catchBotOff(*next));
}

TEST(catch, the_spawner_plans_good_berries_within_reach_and_rotten_ones_out_of_it) {
  int goods = 0, rottens = 0;
  for (uint32_t seed = 1; seed <= 12; seed++) {
    CatchGame c;
    c.reset(seed, 0, false);
    GameRng jitter(seed * 13 + 5);
    for (int i = 0; i < 20000 && !c.done(); i++) {
      int16_t px = c.planX;
      float pt = c.planT;
      catchBotSteer(c);
      c.step(40 + jitter.below(61));
      if (c.planT == pt) continue;  // nothing spawned in this step
      CHECK_MSG(c.planT > pt, "a berry arrives before the one planned ahead of it");
      const CatchGame::Berry *nb = nullptr;
      for (const CatchGame::Berry &b : c.berries)
        if (b.live && b.y == CatchGame::SPAWN_Y) nb = &b;
      CHECK(nb != nullptr);
      if (!nb) break;
      CHECK_RANGE(nb->x, (int16_t)CatchGame::X_MIN, (int16_t)CatchGame::X_MAX);
      int dist = nb->x > px ? nb->x - px : px - nb->x;
      if (nb->kind == CatchGame::ROTTEN) {
        rottens++;
        // out of reach of a pet standing anywhere it caught the last good berry: farther from
        // the plan than two catch widths (a literal, not AVOID itself), as catchBotSteer's
        // off-centre stands need
        CHECK_MSG(dist > 2 * CatchGame::CATCH_DX, "a rotten berry within reach of the plan: " + std::to_string(dist) + " px");
        CHECK(dist >= CatchGame::AVOID);
        CHECK_EQ(c.planX, px);  // the plan stays; only its time moves on
      } else {
        goods++;
        float room = CatchGame::PLAN_SPEED * (c.planT - pt) / 1000.0f;
        CHECK_MSG((float)dist <= room + 1.0f, "a good berry out of the plan's reach: " + std::to_string(dist) + " px");
        CHECK_EQ(c.planX, nb->x);  // and it is the new plan
      }
    }
  }
  CHECK(goods > 500 && rottens > 50);
}

// Fairness at every level: a player who only reacts to berries that have been up for 250 ms,
// at the pet's real speed, on frames of 40 to 100 ms, catches every good berry and never a
// rotten one up to the cap, for many seeds.
static void catchBotRun(uint32_t seed, uint32_t minDt, uint32_t maxDt, bool favKnown, int &rottenSeen, int &rottenEarly) {
  CatchGame c;
  c.reset(seed * 7919u, (uint8_t)(seed % 3), favKnown);
  GameRng jitter(seed * 17 + 3);
  uint16_t caughtBefore = 0;
  for (int i = 0; i < 200000 && !c.done(); i++) {
    catchBotSteer(c);
    c.step(minDt + jitter.below(maxDt - minDt + 1));
    for (const CatchGame::Berry &b : c.berries)
      if (b.live && b.kind == CatchGame::ROTTEN && b.y == CatchGame::SPAWN_Y) {
        rottenSeen++;
        if (c.caught < CatchGame::CLEAN_CATCHES) rottenEarly++;
      }
    caughtBefore = c.caught;
  }
  CHECK_MSG(!c.lost, "seed " + std::to_string(seed) + " lost after " + std::to_string(caughtBefore) + " caught");
  CHECK_MSG(c.done() && c.caught == CatchGame::MAX_CATCHES,
            "seed " + std::to_string(seed) + " ended at " + std::to_string(c.caught) + " caught");
  if (!favKnown) CHECK_EQ(c.result(), (uint16_t)CatchGame::MAX_CATCHES);
}

TEST(catch, a_player_who_reacts_to_what_was_up_for_250_ms_catches_every_good_berry_and_no_rotten_one) {
  int rottenSeen = 0, rottenEarly = 0;
  for (uint32_t seed = 1; seed <= 24; seed++) catchBotRun(seed, 40, 100, false, rottenSeen, rottenEarly);
  CHECK_MSG(rottenSeen > 500, "the bot never met rotten berries: " + std::to_string(rottenSeen));
  CHECK_EQ(rottenEarly, 0);  // none before CLEAN_CATCHES berries are caught
}

TEST(catch, the_same_player_on_slow_frames_still_keeps_up) {
  int rottenSeen = 0, rottenEarly = 0;
  for (uint32_t seed = 31; seed <= 40; seed++) catchBotRun(seed, 85, 100, false, rottenSeen, rottenEarly);
  CHECK_EQ(rottenEarly, 0);
}

static CatchGame catchWithBerry(int x, uint8_t kind, uint8_t fav = 0, bool known = false, bool steer = true) {
  CatchGame c;
  c.reset(1, fav, known);
  c.nextSpawn = 1000000;  // sin bayas nuevas
  c.petX = 233;
  c.targetX = 233;
  if (steer) c.touch(233);  // the player has touched: what the pet catches counts
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
  CHECK(!c.done());  // in the warm-up, which this is, it costs nothing
}

TEST(catch, un_paso_largo_no_atraviesa_la_linea_sin_atrapar) {
  CatchGame c = catchWithBerry(233, CatchGame::GREEN);
  c.berries[0].y = CatchGame::CATCH_Y - 10;
  c.berries[0].vy = 260;
  c.step(250);  // 65 px de golpe: pasa la linea y la de perdida
  CHECK_EQ(c.score, (int16_t)1);
}

TEST(catch, until_the_player_steers_the_pet_eats_what_falls_on_it_for_nothing) {
  CatchGame c = catchWithBerry(233, CatchGame::RED, 0, false, false);
  CHECK(!c.steered);
  c.step(85);
  CHECK(c.ate);  // eaten, and shown
  CHECK(c.takeEvents() & GEV_GOOD);
  CHECK_EQ(c.score, (int16_t)0);  // but not the player's doing
  CHECK_EQ(c.caught, (uint16_t)0);
  CHECK(c.warm);  // and it doesn't end the warm-up
  CHECK(!c.done());
  c.touch(233);  // the first steer
  CHECK(c.steered);
  CHECK(c.warm);
  c.berries[0].live = true; c.berries[0].x = 233; c.berries[0].y = CatchGame::CATCH_Y - 1; c.berries[0].vy = 200;
  c.berries[0].kind = CatchGame::RED;
  c.step(85);
  CHECK_EQ(c.score, (int16_t)1);  // now it counts
  CHECK(!c.warm);
}

TEST(catch, in_the_warm_up_a_good_berry_that_lands_costs_nothing_and_never_ends_the_game) {
  CatchGame c;
  c.reset(7, 0, false);
  c.nextSpawn = 1000000;  // only the berries below
  c.touch(100);  // the player steers the pet to one side...
  for (int round = 0; round < 6; round++) {  // ...and berries land on the other
    c.berries[0].live = true;
    c.berries[0].x = 366;
    c.berries[0].y = CatchGame::SPAWN_Y;
    c.berries[0].vy = 300;
    c.berries[0].kind = (uint8_t)(round % 3);
    for (int i = 0; i < 40; i++) c.step(100);
    CHECK(!c.berries[0].live);  // it landed
    CHECK(c.warm);
    CHECK(!c.done());
  }
  CHECK(c.steered);
  CHECK_EQ(c.score, (int16_t)0);
}

TEST(catch, after_the_warm_up_a_good_berry_that_reaches_the_ground_ends_the_game_with_what_was_caught) {
  CatchGame c = catchWithBerry(233, CatchGame::GREEN);
  c.step(85);  // the first catch ends the warm-up
  CHECK(!c.warm);
  CHECK(!c.done());
  c.berries[1].live = true; c.berries[1].x = 366; c.berries[1].y = CatchGame::CATCH_Y - 1;
  c.berries[1].vy = 200; c.berries[1].kind = CatchGame::RED;
  c.step(85);  // 17 px: past the mouth line, a long way short of the ground
  CHECK(c.berries[1].live);
  CHECK(!c.done());
  for (int i = 0; i < 20 && !c.done(); i++) c.step(85);
  CHECK(c.done());
  CHECK(c.lost);
  CHECK(c.berries[1].y >= CatchGame::GONE_Y);
  CHECK_EQ(c.result(), (uint16_t)1);  // it keeps what it caught
}

TEST(catch, after_the_warm_up_a_rotten_berry_ends_the_game_and_no_longer_costs_points) {
  CatchGame c = catchWithBerry(233, CatchGame::ROTTEN);
  c.warm = false;
  c.score = 5;
  c.caught = 5;
  c.step(85);
  CHECK(c.done());
  CHECK(c.hurting());
  CHECK(c.takeEvents() & GEV_BAD);
  CHECK_EQ(c.score, (int16_t)5);  // none taken off
  CHECK_EQ(c.result(), (uint16_t)5);
}

TEST(catch, a_rotten_berry_that_lands_uncaught_is_no_miss) {
  CatchGame c = catchWithBerry(366, CatchGame::ROTTEN);
  c.warm = false;
  c.score = 5;
  c.caught = 5;
  for (int i = 0; i < 30; i++) c.step(85);
  CHECK(!c.berries[0].live);
  CHECK(!c.done());
  CHECK_EQ(c.score, (int16_t)5);
}

TEST(catch, a_known_favourite_scores_2_but_is_one_berry_and_the_cap_ends_the_game_at_400_points) {
  CatchGame c = catchWithBerry(233, CatchGame::BLUE, CatchGame::BLUE, true);
  c.warm = false;
  c.score = 398;
  c.caught = CatchGame::MAX_CATCHES - 1;
  CHECK(!c.done());
  c.step(85);
  CHECK_EQ(c.score, (int16_t)400);
  CHECK_EQ(c.caught, CatchGame::MAX_CATCHES);
  CHECK(c.done());
  CHECK_EQ((int)c.result(), (int)GAME_REC_MAX[GAME_CATCH]);
}

TEST(catch, la_baya_favorita_vale_doble_solo_si_ya_se_conoce) {
  CatchGame c = catchWithBerry(233, CatchGame::BLUE, CatchGame::BLUE, true);
  c.step(85);
  CHECK_EQ(c.score, (int16_t)2);
  CHECK_EQ(c.caught, (uint16_t)1);
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

TEST(catch, there_is_no_timer_and_a_finished_game_deals_no_more_berries) {
  CatchGame c;
  c.reset(2, 0, false);
  for (int i = 0; i < 6000; i++) c.step(100);  // ten minutes, nobody touching: berries only
  CHECK(!c.done());
  CHECK(c.t >= 600000);
  CatchGame d;
  d.reset(2, 0, false);
  d.lost = true;  // over
  d.nextSpawn = 0;
  d.step(100);
  int live = 0;
  for (auto &b : d.berries) live += b.live;
  CHECK_EQ(live, 0);  // a finished game deals no more berries...
  CHECK_EQ(d.t, (uint32_t)0);  // ...and its clock stops
}

// ---------------------------------------------------------------- Peekaboo
TEST(peek, the_time_up_and_the_pause_ramp_by_hits_towards_a_floor) {
  CHECK_EQ(PeekGame::upMsFor(0), (uint32_t)1500);
  CHECK_EQ(PeekGame::upMsFor(19), (uint32_t)750);
  CHECK_EQ(PeekGame::upMsFor(20), (uint32_t)740);
  CHECK_EQ(PeekGame::upMsFor(39), (uint32_t)650);
  CHECK_EQ(PeekGame::upMsFor(99), (uint32_t)590);
  CHECK_EQ(PeekGame::upMsFor(254), (uint32_t)565);
  CHECK_EQ(PeekGame::gapMsFor(0), (uint32_t)500);
  CHECK_EQ(PeekGame::gapMsFor(19), (uint32_t)300);
  CHECK_EQ(PeekGame::gapMsFor(255), (uint32_t)300);
  for (uint16_t i = 1; i <= 255; i++) {
    CHECK_MSG(PeekGame::upMsFor((uint8_t)i) <= PeekGame::upMsFor((uint8_t)(i - 1)), "it stays up longer at " + std::to_string(i));
    CHECK_MSG(PeekGame::gapMsFor((uint8_t)i) <= PeekGame::gapMsFor((uint8_t)(i - 1)), "the pause grows at " + std::to_string(i));
    CHECK_MSG(PeekGame::upMsFor((uint8_t)i) >= 550, "under the 550 ms floor at " + std::to_string(i));
    CHECK(PeekGame::gapMsFor((uint8_t)i) >= 300);
  }
}

// Steps until the pet is up / has ducked, at most 30 s of game time: a game that stops short
// (a broken rule ending it early) fails its checks instead of hanging the suite.
static void stepToUp(PeekGame &p, uint32_t ms = 10) {
  for (uint32_t i = 0; i < 30000 / ms && !p.up() && !p.done(); i++) p.step(ms);
}
static void stepToDuck(PeekGame &p, uint32_t ms = 10) {
  for (uint32_t i = 0; i < 30000 / ms && p.up() && !p.done(); i++) p.step(ms);
}
static PeekGame peekUp(uint32_t seed = 1) {
  PeekGame p;
  p.reset(seed);
  stepToUp(p);
  CHECK(p.up());
  return p;
}
// a bush whose centre is outside the one that is up (so a tap there is the wrong bush)
static uint8_t otherBush(const PeekGame &p) {
  for (uint8_t b = 0; b < PeekGame::BUSHES; b++)
    if (b != p.bush && !PeekGame::inBush(p.bush, PeekGame::BX[b], PeekGame::BY[b])) return b;
  return PeekGame::BUSHES;
}
static void tapUp(PeekGame &p) { p.touchDown(PeekGame::BX[p.bush], PeekGame::BY[p.bush] - 60); }

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

TEST(peek, before_the_first_hit_nothing_ends_the_game_and_the_pace_stays_slow) {
  PeekGame p;
  p.reset(3);
  int ups = 0;
  bool wasUp = false;
  for (int i = 0; i < 6000; i++) {  // a minute of pop-ups nobody taps
    p.step(10);
    if (p.up() && !wasUp) {
      ups++;
      CHECK_EQ(p.phaseEnd - p.phaseStart, (uint32_t)1500);  // the warm-up's misses don't speed it up
    }
    wasUp = p.up();
  }
  CHECK(!p.done());
  CHECK(ups >= 20);
  // a wrong bush only rustles it
  stepToUp(p);
  uint8_t o = otherBush(p);
  CHECK(o < PeekGame::BUSHES);
  p.touchDown(PeekGame::BX[o], PeekGame::BY[o]);
  CHECK(p.rustling(o));
  CHECK(!p.done());
  CHECK(!(p.takeEvents() & GEV_BAD));
  // and so does a tap on a bush while none is up
  stepToDuck(p);
  p.touchDown(PeekGame::BX[0], PeekGame::BY[0]);
  CHECK(p.rustling(0));
  CHECK(!p.done());
}

TEST(peek, after_the_first_hit_a_pop_up_that_ducks_back_untapped_ends_the_game) {
  PeekGame p = peekUp();
  tapUp(p);
  CHECK_EQ(p.score, (uint8_t)1);
  stepToDuck(p);  // it ducked after the hit: that is no miss
  CHECK(!p.done());
  stepToUp(p);  // the next one, not tapped
  CHECK(!p.done());
  stepToDuck(p);
  CHECK(p.done());
  CHECK(p.missed);
  CHECK(p.takeEvents() & GEV_BAD);
  CHECK_EQ(p.result(), (uint16_t)1);  // what was hit is kept
  p.touchDown(PeekGame::BX[0], PeekGame::BY[0]);
  CHECK_EQ(p.result(), (uint16_t)1);  // and over is over
}

TEST(peek, after_the_first_hit_a_tap_on_another_bush_or_on_any_bush_while_none_is_up_ends_the_game) {
  PeekGame p = peekUp();
  tapUp(p);
  uint8_t o = otherBush(p);
  p.touchDown(PeekGame::BX[o], PeekGame::BY[o]);  // the wrong bush, while one is up
  CHECK(p.done());
  CHECK(p.missed);
  CHECK(p.takeEvents() & GEV_BAD);
  CHECK_EQ(p.result(), (uint16_t)1);

  PeekGame q = peekUp(2);
  tapUp(q);
  stepToDuck(q);  // ducked: none up
  CHECK(!q.done());
  q.touchDown(PeekGame::BX[2], PeekGame::BY[2]);  // any bush
  CHECK(q.done());
  CHECK(q.missed);
}

TEST(peek, a_second_tap_on_the_bush_just_hit_and_a_tap_outside_every_bush_do_nothing) {
  PeekGame p = peekUp();
  tapUp(p);
  tapUp(p);  // the same bush again, in the duck: not a miss, not a point
  CHECK(!p.done());
  CHECK_EQ(p.score, (uint8_t)1);
  for (uint8_t b = 0; b < PeekGame::BUSHES; b++) CHECK(!PeekGame::inBush(b, 233, 120));
  p.touchDown(233, 120);
  p.touchDown(5, 5);
  CHECK(!p.done());
  stepToDuck(p);
  p.touchDown(233, 120);  // none up, but not on a bush
  CHECK(!p.done());
}

// a game with bush `b` up (the seed that deals it first, searched for)
static PeekGame peekUpAt(uint8_t b) {
  for (uint32_t seed = 1; seed < 200; seed++) {
    PeekGame p;
    p.reset(seed);
    stepToUp(p);
    if (p.up() && p.bush == b) return p;
  }
  CHECK_MSG(false, "no seed pops bush " + std::to_string(b) + " up first");
  return peekUp();
}

// R1-1 of #129: the bushes' hit areas overlap (bush 0 and bush 2, bush 1 and bush 4, bush 3
// with the upper two), so a second tap on the pet just hit can lie in another bush's area
// too. It is the same pet: never a miss, whichever area it is also in.
TEST(peek, a_second_tap_on_the_pet_just_hit_is_never_a_miss_where_two_bushes_overlap) {
  // the two points the review named: the head of the pet out of bush 2 (also in bush 0's
  // area), and the lower part of bush 0 (also in bush 2's)
  int hx = PeekGame::BX[2], hy = PeekGame::BY[2] - 90;
  int lx = PeekGame::BX[0] - 40, ly = PeekGame::BY[0] + 20;
  CHECK(PeekGame::inBush(2, hx, hy) && PeekGame::inBush(0, hx, hy));
  CHECK(PeekGame::inBush(0, lx, ly) && PeekGame::inBush(2, lx, ly));
  PeekGame a = peekUpAt(2);
  tapUp(a);
  a.touchDown(hx, hy);
  CHECK(!a.done());
  CHECK_EQ(a.score, (uint8_t)1);
  PeekGame b = peekUpAt(0);
  tapUp(b);
  b.touchDown(lx, ly);
  CHECK(!b.done());
  CHECK_EQ(b.score, (uint8_t)1);
  // every point of the bush up that is also inside another bush's area, for every bush
  int overlaps = 0;
  for (uint8_t up = 0; up < PeekGame::BUSHES; up++) {
    for (int y = 0; y < 466; y += 5) {
      for (int x = 0; x < 466; x += 5) {
        if (!PeekGame::inBush(up, x, y)) continue;
        bool also = false;
        for (uint8_t o = 0; o < PeekGame::BUSHES; o++) also |= o != up && PeekGame::inBush(o, x, y);
        if (!also) continue;
        overlaps++;
        PeekGame p = peekUpAt(up);
        tapUp(p);
        p.touchDown(x, y);
        CHECK_MSG(!p.done() && p.score == 1, "bush " + std::to_string(up) + " up and hit, a second tap at " +
                                                 std::to_string(x) + "," + std::to_string(y));
      }
    }
  }
  CHECK(overlaps > 50);  // the areas do overlap: the loop above checked something
}

// ...and for BOUNCE_MS after a hit, a second tap on the bush hit is the same tap even once
// the pet ducked (a hit just before the time up ducks it at once), but a deliberate one
// later is a miss
TEST(peek, a_double_tap_right_after_the_duck_is_not_a_miss_but_a_tap_after_the_bounce_is) {
  for (int late = 0; late < 2; late++) {
    PeekGame p = peekUp(4);
    while (p.t + 2 < p.phaseEnd) p.step(1);  // the last moments of the time up
    int x = PeekGame::BX[p.bush], y = PeekGame::BY[p.bush] - 60;
    p.touchDown(x, y);
    CHECK_EQ(p.score, (uint8_t)1);
    p.step(2);
    CHECK(!p.up());  // ducked: none up
    p.step(late ? PeekGame::BOUNCE_MS - 2 : PeekGame::BOUNCE_MS - 3);
    p.touchDown(x, y);  // 1 ms under BOUNCE_MS after the hit, or exactly BOUNCE_MS
    CHECK_EQ(p.done(), late == 1);
    CHECK_EQ(p.missed, late == 1);
    CHECK_EQ(p.result(), (uint16_t)1);
  }
}

// R1-6 of #129: a tap made before phaseEnd but read after the frame that stepped past it (the
// loop reads the touch, then steps the game; see the top of game_peek.h) is still a hit for
// SETTLE_MS, and the untapped miss ends the game only after that, in the warm-up or not.
TEST(peek, an_untapped_pop_up_is_a_miss_only_once_the_settle_has_passed_and_a_tap_in_it_is_a_hit) {
  for (int warm = 0; warm < 2; warm++) {
    PeekGame p = peekUp(6), q = peekUp(6);
    if (!warm) {  // one hit first, then the next pop-up
      for (PeekGame *g : { &p, &q }) {
        tapUp(*g);
        stepToDuck(*g);
        stepToUp(*g);
        CHECK(!g->done());
      }
    }
    uint8_t before = p.score;
    // the frame that steps just past phaseEnd: ducked on screen, still tappable
    p.step(p.phaseEnd - p.t + 1);
    CHECK(p.up() && !p.done());
    tapUp(p);
    CHECK_EQ(p.score, (uint8_t)(before + 1));
    CHECK(!p.done());
    CHECK(p.phaseEnd >= p.t + PeekGame::DUCK_AFTER_HIT_MS);  // and it nods before it ducks
    // untapped: not a miss at the last millisecond of the settle, a miss at its end
    q.step(q.phaseEnd - q.t + PeekGame::SETTLE_MS - 1);
    CHECK(!q.done());
    q.step(1);
    CHECK_EQ(q.done(), warm == 0);  // the warm-up has no miss: it ducks, the next one comes
    CHECK_EQ(q.missed, warm == 0);
    if (warm) CHECK(!q.up());
  }
}

TEST(peek, a_tap_counts_through_the_whole_time_up_even_at_the_floor) {
  for (int tapAt = 0; tapAt < 2; tapAt++) {
    PeekGame p;
    p.reset(5);
    p.score = 200;  // up for about 570 ms, with the first hit long past
    stepToUp(p, 1);
    uint32_t up = p.phaseEnd - p.phaseStart;
    CHECK_EQ(up, PeekGame::upMsFor(200));
    if (tapAt)
      for (int i = 0; i < 3000 && p.t + 1 < p.phaseEnd; i++) p.step(1);  // the last millisecond
    tapUp(p);
    CHECK_EQ(p.score, (uint8_t)201);
  }
}

TEST(peek, a_player_who_taps_each_pop_up_after_350_ms_reaches_the_cap_never_twice_on_one_bush_running) {
  for (uint32_t seed = 1; seed <= 24; seed++) {
    PeekGame p;
    p.reset(seed * 331);
    GameRng jitter(seed * 7 + 1);
    uint8_t prev = PeekGame::BUSHES;
    bool wasUp = false;
    int pops = 0;
    for (int i = 0; i < 400000 && !p.done(); i++) {
      p.step(40 + jitter.below(61));
      if (p.up() && !wasUp) {
        pops++;
        CHECK_MSG(p.bush != prev, "the same bush twice running");
        prev = p.bush;
        CHECK_EQ(p.phaseEnd - p.phaseStart, PeekGame::upMsFor(p.score));
      }
      wasUp = p.up();
      if (p.up() && !p.hit && p.t - p.phaseStart >= 350) tapUp(p);
    }
    CHECK_MSG(p.done() && !p.missed && p.score == 255, "seed " + std::to_string(seed) + " ended at " + std::to_string(p.score));
    CHECK_EQ(pops, 255);
  }
}

TEST(peek, the_255th_hit_ends_the_game_by_itself) {
  PeekGame p = peekUp(9);
  p.score = 254;
  tapUp(p);
  CHECK(p.done());
  CHECK(!p.missed);
  CHECK_EQ(p.result(), (uint16_t)255);
  CHECK(p.takeEvents() & GEV_GOOD);
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
  CHECK(!(s.takeEvents() & GEV_BAD));
  listen(s);
  CHECK(!s.busy());
  CHECK(s.playersTurn());
}

TEST(song, before_the_first_tune_is_repeated_a_wrong_note_costs_nothing_and_plays_the_tune_again) {
  SongGame s;
  s.reset(8);
  for (int miss = 0; miss < 12; miss++) {  // as many as the player likes
    std::vector<uint8_t> heard = listen(s);
    CHECK_EQ(heard.size(), (size_t)2);  // the same tune
    CHECK_EQ(s.len, (uint8_t)2);
    press(s, s.seq[0]);
    press(s, (uint8_t)((s.seq[1] + 1) % SongGame::PADS));  // wrong
    CHECK(!s.done());
    CHECK_EQ((int)s.phase, (int)SongGame::WRONG);
    CHECK(s.takeEvents() & GEV_BAD);
    CHECK_EQ(s.best, (uint8_t)0);
  }
}

TEST(song, after_the_first_tune_one_wrong_note_ends_it_with_the_longest_tune_as_the_score) {
  SongGame s;
  s.reset(9);
  listen(s);
  press(s, s.seq[0]);
  press(s, s.seq[1]);  // the first tune, repeated
  CHECK_EQ(s.best, (uint8_t)2);
  CHECK(!s.warm());
  listen(s);  // 3 notes
  press(s, s.seq[0]);
  press(s, s.seq[1]);
  press(s, (uint8_t)((s.seq[2] + 1) % SongGame::PADS));  // one wrong note
  CHECK(s.done());
  CHECK(s.missed);
  CHECK(!s.won);
  CHECK(s.takeEvents() & GEV_BAD);
  CHECK_EQ(s.result(), (uint16_t)2);
  press(s, s.seq[0]);  // over is over
  CHECK_EQ(s.result(), (uint16_t)2);
}

// the tune grows a note a round with no win at 12, up to the 100 notes that win
TEST(song, repeating_a_100_note_tune_wins) {
  SongGame s;
  s.reset(3);
  for (uint8_t len = 2; len <= SongGame::MAX_LEN; len++) {
    CHECK(!s.done());
    std::vector<uint8_t> heard = listen(s);
    CHECK_EQ(heard.size(), (size_t)len);
    for (uint8_t i = 0; i < len; i++) press(s, s.seq[i]);
    CHECK_EQ(s.best, len);
    if (len == 12) CHECK(!s.won && !s.done());  // 12 is no longer the end
  }
  CHECK(s.done());
  CHECK(s.won);
  CHECK(!s.missed);
  CHECK(s.takeEvents() & GEV_WIN);
  CHECK_EQ(s.result(), (uint16_t)100);
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
  press(s, (uint8_t)((s.seq[1] + 1) % SongGame::PADS));  // falla en el calentamiento
  CHECK_EQ((int)s.phase, (int)SongGame::WRONG);
  CHECK(s.busy());
  listen(s);
  press(s, s.seq[0]);
  press(s, s.seq[1]);  // acierta la ronda
  CHECK_EQ((int)s.phase, (int)SongGame::PAUSE);
  CHECK(s.busy());
  listen(s);
  press(s, (uint8_t)((s.seq[0] + 1) % SongGame::PADS));  // falla: se acabo
  CHECK(s.done());
  CHECK(!s.busy());
}

TEST(song, the_notes_keep_getting_shorter_towards_a_floor) {
  CHECK_EQ(SongGame::noteMsFor(2), (uint32_t)500);
  CHECK_EQ(SongGame::noteMsFor(5), (uint32_t)425);
  CHECK_EQ(SongGame::noteMsFor(8), (uint32_t)350);
  CHECK_EQ(SongGame::noteMsFor(9), (uint32_t)333);
  CHECK_EQ(SongGame::noteMsFor(12), (uint32_t)300);
  CHECK_EQ(SongGame::noteMsFor(24), (uint32_t)250);
  CHECK_EQ(SongGame::noteMsFor(100), (uint32_t)212);
  for (uint8_t n = 3; n <= SongGame::MAX_LEN; n++) {
    CHECK_MSG(SongGame::noteMsFor(n) <= SongGame::noteMsFor(n - 1), "a note is longer at " + std::to_string(n));
    CHECK(SongGame::noteMsFor(n) >= 200);
  }
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

// ---------------------------------------------------------------- revision 2
// R2-1: en Berry Catch el bicho empieza en el centro y atrapa bayas el solo; sin
// ningun toque no es una partida suya y no puntua (ni bate el record)
static void runToEnd(MiniGames &m, uint32_t &now) {
  for (int i = 0; i < 2000 && !m.over(); i++) m.frame(now += 85);
}

TEST(minigames, sin_ningun_toque_la_partida_no_puntua_aunque_el_bicho_atrape_bayas) {
  bool caughtAlone = false;
  for (uint32_t seed = 1; seed <= 20; seed++) {
    MiniGames m;
    uint32_t now = 0;
    m.rng.seedWith(seed);
    m.begin(GAME_CATCH, now, 0, false);
    runToEnd(m, now);
    CHECK(m.over());
    if (m.catcher.ate) caughtAlone = true;  // eaten and shown, for nothing
    CHECK_MSG(m.catcher.result() == 0, "the game itself scores a berry nobody steered for, seed " + std::to_string(seed));
    CHECK_MSG(m.score() == 0, "puntua sin tocar, semilla " + std::to_string(seed));
  }
  CHECK_MSG(caughtAlone, "el caso no se da: el bicho nunca atrapa nada solo");
}

TEST(minigames, con_un_solo_toque_la_partida_puntua_lo_atrapado) {
  bool scored = false;
  for (uint32_t seed = 1; seed <= 20 && !scored; seed++) {
    MiniGames m;
    uint32_t now = 0;
    m.rng.seedWith(seed);
    m.begin(GAME_CATCH, now, 0, false);
    while (m.flow.phase == GP_TITLE) m.frame(now += 85);
    m.touch(true, 233, 350, now);  // apoya y suelta en el sitio: el bicho no se mueve
    m.touch(false, 233, 350, now + 20);
    runToEnd(m, now);
    CHECK_EQ(m.score(), m.catcher.result());
    if (m.score() > 0) scored = true;
  }
  CHECK(scored);
}

// R2-3: en Peekaboo y Berry Song solo cuenta apoyar el dedo; deslizarlo hasta el
// arbusto o el boton no golpea ni toca nota
TEST(minigames, en_Peekaboo_deslizar_hasta_el_arbusto_no_golpea) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_PEEK, now, 0, false);
  for (int i = 0; i < 400 && !m.peek.up(); i++) m.frame(now += 50);
  CHECK(m.peek.up());
  for (uint8_t b = 0; b < PeekGame::BUSHES; b++) CHECK(!PeekGame::inBush(b, 233, 120));
  m.touch(true, 233, 120, now);  // apoya lejos de los arbustos
  int bx = PeekGame::BX[m.peek.bush], by = PeekGame::BY[m.peek.bush];
  m.touch(true, bx, by - 60, now + 20);  // y desliza hasta el que asoma
  m.touch(true, bx, by - 60, now + 40);
  CHECK_EQ(m.peek.score, (uint8_t)0);
}

TEST(minigames, en_Berry_Song_deslizar_hasta_un_boton_no_toca_nota) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_SONG, now, 0, false);
  for (int i = 0; i < 400 && !m.song.playersTurn(); i++) m.frame(now += 50);
  CHECK(m.song.playersTurn());
  CHECK_EQ(SongGame::padAt(233, 200), SongGame::NO_PAD);
  m.touch(true, 233, 200, now);  // apoya fuera de los botones
  uint8_t p = m.song.seq[0];
  m.touch(true, SongGame::PX[p], SongGame::PY[p], now + 20);  // y desliza hasta el bueno
  m.touch(true, SongGame::PX[p], SongGame::PY[p], now + 40);
  CHECK_EQ(m.song.k, (uint8_t)0);
  CHECK(!m.song.done());
}

TEST(minigames, una_partida_sin_tocar_tras_otra_jugada_tampoco_puntua) {
  MiniGames m;  // el sketch usa un solo MiniGames para todas las partidas
  uint32_t now = 0;
  m.rng.seedWith(3);
  m.begin(GAME_CATCH, now, 0, false);
  while (m.flow.phase == GP_TITLE) m.frame(now += 85);
  m.touch(true, 233, 350, now);
  m.touch(false, 233, 350, now + 20);
  runToEnd(m, now);
  bool caughtAlone = false;
  for (uint32_t seed = 1; seed <= 20; seed++) {
    m.rng.seedWith(seed);
    m.begin(GAME_CATCH, now, 0, false);  // la siguiente, sin tocar
    runToEnd(m, now);
    if (m.catcher.ate) caughtAlone = true;
    CHECK_MSG(m.catcher.result() == 0, "the game scores what the pet ate alone");
    CHECK_MSG(m.score() == 0, "hereda la actividad de la partida anterior");
  }
  CHECK(caughtAlone);
}

// ---------------------------------------------------------------- the screen goes dark (#94)
// PWR or the 1.75C's pocket lock with a game open (MiniGames::endDark): the game ends at once
// and keeps its score; the sketch pays it with Pet::playGame when endDark() says so.

TEST(minigames, a_touched_game_the_screen_turns_off_ends_now_and_pays_its_score) {
  bool scored = false;
  for (uint32_t seed = 1; seed <= 20 && !scored; seed++) {
    MiniGames m;
    uint32_t now = 0;
    m.rng.seedWith(seed);
    m.begin(GAME_CATCH, now, 0, false);
    playUntil(m, now);
    m.touch(true, 233, 350, now);  // one tap in place: the pet catches on its own after it
    m.touch(false, 233, 350, now + 20);
    for (int i = 0; i < 120 && !m.over(); i++) m.frame(now += 85);  // ~10 s of the 30
    if (m.over() || m.catcher.result() == 0) continue;
    scored = true;
    uint16_t before = m.score();
    CHECK(m.endDark());              // paid: the score so far
    CHECK(m.over());                 // and the game is over now, not left running
    CHECK_EQ(m.score(), before);     // with the score it had
    CHECK(!m.endDark());             // a second dark end pays nothing again
    m.frame(now += 85);
    CHECK_EQ(m.score(), before);     // nothing moves on after it
  }
  CHECK_MSG(scored, "no seed caught a berry in the first 10 s");
}

TEST(minigames, a_touched_game_scoring_0_still_pays_when_the_screen_turns_off) {
  MiniGames m;
  uint32_t now = 0;
  m.begin(GAME_KEEPY, now, 0, false);
  playUntil(m, now);
  m.touch(true, 20, 233, now);  // far from the ball: a miss, but the player played
  m.touch(false, 20, 233, now + 20);
  m.frame(now += 85);
  CHECK(m.played);
  CHECK_EQ(m.score(), (uint16_t)0);
  CHECK(m.endDark());  // as its own end pays a 0 (playGame(g, 0))
  CHECK(m.over());
}

TEST(minigames, an_untouched_game_the_screen_turns_off_pays_nothing) {
  MiniGames title;  // on its title card
  title.begin(GAME_KEEPY, 0, 0, false);
  CHECK(!title.endDark());
  CHECK(title.over());

  bool caughtAlone = false;  // in play, never touched: the pet's own catches don't count
  for (uint32_t seed = 1; seed <= 20 && !caughtAlone; seed++) {
    MiniGames m;
    uint32_t now = 0;
    m.rng.seedWith(seed);
    m.begin(GAME_CATCH, now, 0, false);
    playUntil(m, now);
    for (int i = 0; i < 120 && !m.over(); i++) m.frame(now += 85);
    if (m.over() || !m.catcher.ate) continue;
    caughtAlone = true;
    CHECK(!m.endDark());
    CHECK(m.over());
  }
  CHECK_MSG(caughtAlone, "no seed had the pet catch a berry alone in 10 s");
}

TEST(minigames, a_game_already_over_is_not_paid_again_when_the_screen_turns_off) {
  MiniGames m;  // played to its end: the sketch paid it then (finishGame) and shows the result
  uint32_t now = 0;
  m.rng.seedWith(3);
  m.begin(GAME_CATCH, now, 0, false);
  playUntil(m, now);
  m.touch(true, 233, 350, now);
  m.touch(false, 233, 350, now + 20);
  runToEnd(m, now);
  CHECK(m.over());
  CHECK(m.played);
  CHECK(!m.endDark());
  CHECK(m.over());
}
