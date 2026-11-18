// Guion del simulador: lectura de los ficheros de pasos (sin tocar el firmware, para
// poder probarlo en test/). Un paso por linea; '#' empieza un comentario.
//
//   lang pt                  idioma antes de arrancar (en pt)
//   clock 2026-11-13 10:00   hora del reloj al arrancar ("none" = sin hora)
//   battery 40 | none        bateria del PMU falso (%), "none" = sin bateria
//   usb on|off               USB enchufado (con USB no hay light sleep)
//   charging on|off          cargando (solo cuenta con USB)
//   pmu ok|none              none: the AXP2101 doesn't answer at boot (USB reads as
//                            present, no battery); before the first step only
//   tap X Y                  toque corto en (X,Y)
//   hold X Y DUR [SHOT]      mantener el dedo DUR en (X,Y); with SHOT, also save SHOT.png
//                            at the end of the hold, the finger still down (#78).
//                            tools/gate.sh counts only `shot` lines in screens.txt, so a
//                            hold's shot isn't counted there
//   swipe left|right|up|down deslizar desde el centro
//   swipe X0 Y0 X1 Y1 [DUR]  deslizar entre dos puntos (DUR >= 160ms, y < ~750ms para que cuente; 240ms)
//   press pwr [N]            N pulsaciones cortas de PWR (3 = bloqueo de bolsillo)
//   press boot [N]           N pulsaciones de BOOT (1 = silenciar)
//   wait DUR                 dejar correr el firmware DUR (alias: advance)
//   serial LINEA             mandar una linea por la consola serie (HATCH, SPEC 25...)
//   say LINE                 show a speech bubble now (#23, bubble.h): hungry bath tired
//                            bored sick morning stars milk hi happy pat love-name love,
//                            the berry garden's thirsty ripe (#115), the
//                            natures' lines (#86) NATURE-1 NATURE-2 NATURE-3 for each of
//                            easygoing greedy playful tidy lazy (playful-1...), and the
//                            special days' greetings (#26) christmas newyear valentine
//                            easter halloween birthday (with the name when one is set). It is laid
//                            out as if it had come up by itself and still ends at once on
//                            a screen that is not free (asleep, dimmed, another screen...)
//                            A pet's own naming line (NATURE-1) counts as said for the hatch
//                            line after a hatch (#86), as one that came by itself; milk does
//                            not count for the warm-milk hint (#13), and the serial log has
//                            no "say" line for it
//   shot NOMBRE              guardar NOMBRE.png con lo que muestra el panel
//   nature NAME              the pet's nature now (#25, nature.h): easygoing greedy playful
//                            tidy lazy. In RAM, as if it had hatched with it (the next
//                            save keeps it). Only the simulator has this.
//   poops N                  the poops on the ground now (0-3, #86: a tidy pet strolls clear
//                            of them). In RAM, as if they had come by themselves. Only the
//                            simulator has this.
//   idle ACT                 the pet's next idle activity (#22: stand look walk rest doze hop
//                            ball gesture seek), at once on a free, lit main screen; ignored
//                            if its sprite can't show it. Only the simulator has this.
//   train STR STA SPD        the pet's training points (#76: strength, stamina, speed, each
//                            0-100), set at once, as if earned; the ribbons follow at the next
//                            medal check (the minute tick). Only the simulator has this.
//   game NAME                the next Play deals this minigame (#94), one of keepy, catch, peek
//                            or song, in place of the bag's pick. Only the simulator has this.
//   expect TEXT              a check on the serial output (#99): some line printed since the
//                            previous check contains TEXT. '*' in TEXT stands for any run of
//                            characters, so "expect Play: peek score=* (screen off)" passes
//                            whatever the score. TEXT is the rest of the line, trimmed; '#'
//                            starts a comment here as on every step but serial, so TEXT can't
//                            hold a '#'. Checks in a row share one window: the lines printed
//                            since the check before them (the first window starts at boot and
//                            holds what setup() printed). A failed check prints its line and
//                            the lines it looked at; the run goes on to the end (shots and
//                            serial.log complete) and exits 1. Once the PMU has powered the
//                            board off, the steps left are skipped but the checks still run,
//                            on what was printed. tools/gate.sh runs every script in
//                            sim/scripts/ that has a check, on both boards.
//   expect-not TEXT          the opposite: no line printed since the previous check contains
//                            TEXT (same matching, same window). A typo makes it pass whatever
//                            happens, so pair it with an expect in the same window where one fits.
//   expect-once TEXT         exactly one line printed since the previous check contains TEXT
//                            (same matching, same window): an ending paid once, not twice.
//
// Duraciones: 500ms, 2s, 5m, 2h, 1d y combinaciones (1h30m); un numero solo = ms.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

enum SimOp : uint8_t {
  OP_LANG, OP_CLOCK, OP_BATTERY, OP_USB, OP_CHARGING, OP_PMU,
  OP_TAP, OP_HOLD, OP_SWIPE, OP_PRESS_PWR, OP_PRESS_BOOT,
  OP_WAIT, OP_SERIAL, OP_SHOT, OP_IDLE, OP_SAY, OP_NATURE,
  OP_TRAIN, OP_GAME, OP_POOPS, OP_EXPECT, OP_EXPECT_NOT, OP_EXPECT_ONCE,
};

struct SimCmd {
  SimOp op;
  int line = 0;           // linea del guion (para los mensajes)
  int32_t a = 0, b = 0, c = 0, d = 0;  // coordenadas, %, cuentas, flags
  uint64_t ms = 0;        // duraciones
  std::string text;       // idioma, linea serie, nombre de la captura
};

struct SimScript {
  std::vector<SimCmd> cmds;
  std::string error;  // vacio si se leyo bien; si no "fichero:linea: motivo"
};

// "500ms", "2s", "1h30m", "250" (ms). false si no es una duracion valida.
bool simParseDuration(const std::string &s, uint64_t &ms);
// "2026-11-13 10:00", "2026-11-13T10:00[:ss]" o un epoch en segundos. La hora es
// local (el firmware guarda hora local en el RTC). false si no es valida.
bool simParseClock(const std::string &s, uint32_t &epoch);
// codigo de idioma ("ja") -> indice de Lang (i18n.h); -1 si no existe
int simLangIndex(const std::string &code);
const char *simLangCode(int index);

// Lee un guion entero. lang, clock y pmu solo valen antes del primer paso que hace correr
// el firmware; battery, usb y charging valen en cualquier momento, y los que van antes
// de ese paso ya estan puestos cuando arranca el firmware.
SimScript simParseScript(const std::string &text, const std::string &name = "script");

// Indice del primer paso que hace correr el firmware (tap, hold, swipe, press, wait,
// serial, shot): los anteriores (lang, clock, battery, usb, charging, pmu) son preparacion
// y se aplican antes de setup(). Es el mismo corte que usa el lector para rechazar un
// lang/clock/pmu tardio, asi que no pueden discrepar. cmds.size() si no hay ninguno.
// A check (expect, expect-not, expect-once) also ends the preparation: it looks at what setup() printed.
size_t simPreBootEnd(const SimScript &script);

// ---- checks on the serial output (expect, expect-not, expect-once; #99) ----

// True if `line` contains `pattern`, where each '*' in the pattern stands for any run of
// characters, none included: "Play: peek * (screen off)". The parts between the stars are
// found in order, left to right, without overlapping. A pattern of stars only matches any line.
bool simLineMatches(const std::string &line, const std::string &pattern);

// A check: expect, expect-not or expect-once.
bool simIsCheck(SimOp op);
// How many steps of the script are checks.
size_t simCheckCount(const SimScript &script);

// The serial output the checks look at. The simulator has the shim append every byte the
// firmware prints to `text` (mockSerialCapture), and tells it when a step that isn't a check
// comes (stepRuns). A check looks at the lines printed since the previous check; checks in a
// row share one window, and the first window starts at boot.
struct SimSerialWindow {
  std::string text;  // the current window: from the start of a line to the last byte printed

  // A step that isn't a check comes next (run, or skipped once the board is off). If checks
  // came just before it, their window is done and the next one starts here, at the start of
  // the line being printed: a line still open is seen whole by the next check.
  void stepRuns();

  // One check (simIsCheck; any other step: false). If it fails, `why` says what was looked
  // for since which script line, and lists the lines it looked at (none matched) or the ones
  // that matched (expect-not, or expect-once with more than one), at most 20, each cut to
  // 160 characters.
  bool check(const SimCmd &c, std::string &why);

 private:
  bool afterCheck_ = false;  // the last step was a check
  int lastCheckLine_ = 0;    // the script line of the last check
  int since_ = 0;            // the window started after this script line's check (0: boot)
};
