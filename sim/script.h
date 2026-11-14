// Guion del simulador: lectura de los ficheros de pasos (sin tocar el firmware, para
// poder probarlo en test/). Un paso por linea; '#' empieza un comentario.
//
//   lang ja                  idioma antes de arrancar (es en fr de it pt ja ko)
//   clock 2026-11-13 10:00   hora del reloj al arrancar ("none" = sin hora)
//   battery 40 | none        bateria del PMU falso (%), "none" = sin bateria
//   usb on|off               USB enchufado (con USB no hay light sleep)
//   charging on|off          cargando (solo cuenta con USB)
//   pmu ok|none              none: the AXP2101 doesn't answer at boot (USB reads as
//                            present, no battery); before the first step only
//   tap X Y                  toque corto en (X,Y)
//   hold X Y DUR             mantener el dedo DUR en (X,Y)
//   swipe left|right|up|down deslizar desde el centro
//   swipe X0 Y0 X1 Y1 [DUR]  deslizar entre dos puntos (DUR >= 160ms, y < ~750ms para que cuente; 240ms)
//   press pwr [N]            N pulsaciones cortas de PWR (3 = bloqueo de bolsillo)
//   press boot [N]           N pulsaciones de BOOT (1 = silenciar)
//   wait DUR                 dejar correr el firmware DUR (alias: advance)
//   serial LINEA             mandar una linea por la consola serie (HATCH, SPEC 25...)
//   shot NOMBRE              guardar NOMBRE.png con lo que muestra el panel
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
  OP_WAIT, OP_SERIAL, OP_SHOT,
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
size_t simPreBootEnd(const SimScript &script);
