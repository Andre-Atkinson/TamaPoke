// TamaPoke en el Mac: corre el TamaPoke.ino real con la placa simulada (simhw.h) y
// un guion de pasos (script.h), y guarda capturas PNG de lo que muestra el panel.
//
//   tamapoke_sim [--lang ja] [--clock "2026-11-13 10:00"] [--script FILE]
//                [--out DIR] [--sprites DIR] [--serial-log FILE] [--square] [--quiet]
//
// tools/sim.sh compila y llama a este programa.
#include <Arduino.h>
#include <Preferences.h>
#include <sys/stat.h>
#include <errno.h>
#include <fstream>
#include <sstream>
#include <string>
#include "png.h"
#include "runner.h"
#include "script.h"
#include "simhw.h"
#include "../board.h"

void setup();
void loop();

namespace {

struct Options {
  int lang = -1;  // -1: la del guion, o la de serie (ingles)
  bool clockGiven = false;
  uint32_t clock = 0;
  std::string script, out = "shots", sprites = "tools/sdcard", serialLog;
  bool square = false, quiet = false;
};

// Tiempos de los gestos (ms de reloj falso). Los umbrales de handleTouch(): un toque
// dura < 1500 ms y se mueve < 40 px; un deslizamiento > 80 px en < 800 ms.
const uint32_t TAP_DOWN_MS = 80;
const uint32_t SETTLE_MS = 400;      // tras soltar: el gesto se resuelve y se dibuja otro frame
const uint32_t PWR_SPACING_MS = 120; // entre pulsaciones de PWR: el bucle sondea el PKEY cada 40 ms
const uint32_t PWR_RESOLVE_MS = 350 + 250;  // PwrButton::GAP_MS y un frame
const uint32_t BOOT_DOWN_MS = 100;
const uint32_t BOOT_GAP_MS = 150;
const uint32_t BOOT_RESOLVE_MS = 350 + 250; // BootButton::DOUBLE_MS y un frame

void usage() {
  fprintf(stderr,
          "usage: tamapoke_sim [--lang CODE] [--clock \"YYYY-MM-DD HH:MM\"|none] [--script FILE]\n"
          "                    [--out DIR] [--sprites DIR] [--serial-log FILE] [--square] [--quiet]\n"
          "  languages: es en fr de it pt ja ko (default: the script's, else en)\n"
          "  the default clock is 2026-11-13 10:00; none = boot with no time (1.75C after a power cut)\n");
}

bool parseArgs(int argc, char **argv, Options &o) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](std::string &dst) {
      if (i + 1 >= argc) return false;
      dst = argv[++i];
      return true;
    };
    std::string v;
    if (a == "--lang") {
      if (!next(v) || simLangIndex(v) < 0) { fprintf(stderr, "bad --lang\n"); return false; }
      o.lang = simLangIndex(v);
    } else if (a == "--clock") {
      if (!next(v)) return false;
      o.clockGiven = true;
      if (v == "none") o.clock = 0;
      else if (!simParseClock(v, o.clock)) { fprintf(stderr, "bad --clock '%s'\n", v.c_str()); return false; }
    } else if (a == "--script") {
      if (!next(o.script)) return false;
    } else if (a == "--out") {
      if (!next(o.out)) return false;
    } else if (a == "--sprites") {
      if (!next(o.sprites)) return false;
    } else if (a == "--serial-log") {
      if (!next(o.serialLog)) return false;
    } else if (a == "--square") {
      o.square = true;
    } else if (a == "--quiet") {
      o.quiet = true;
    } else if (a == "-h" || a == "--help") {
      usage();
      exit(0);
    } else {
      fprintf(stderr, "unknown option '%s'\n", a.c_str());
      return false;
    }
  }
  return true;
}

bool mkdirs(const std::string &dir) {
  std::string cur;
  std::stringstream ss(dir);
  std::string part;
  if (!dir.empty() && dir[0] == '/') cur = "/";
  while (std::getline(ss, part, '/')) {
    if (part.empty()) continue;
    cur += part + "/";
    if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
  }
  return true;
}

uint64_t run(uint64_t ms) {
  return simRunFor(ms, [] { loop(); }, [] { return gSimPower.poweredOff; });
}

void onRestart() {
  fprintf(stderr, "sim: the firmware called ESP.restart() (WIPE); stopping here\n");
  exit(3);
}

}  // namespace

int main(int argc, char **argv) {
  Options o;
  if (!parseArgs(argc, argv, o)) {
    usage();
    return 2;
  }

  SimScript script;
  if (!o.script.empty()) {
    std::ifstream f(o.script);
    if (!f) {
      fprintf(stderr, "cannot read %s\n", o.script.c_str());
      return 2;
    }
    std::stringstream buf;
    buf << f.rdbuf();
    script = simParseScript(buf.str(), o.script);
    if (!script.error.empty()) {
      fprintf(stderr, "%s\n", script.error.c_str());
      return 2;
    }
  } else {
    script = simParseScript("wait 2s\nshot boot\n", "default");
  }
  if (!mkdirs(o.out)) {
    fprintf(stderr, "cannot create %s\n", o.out.c_str());
    return 2;
  }

  // ---- preparacion antes de arrancar: NVS vacia, idioma, reloj, sprites ----
  int lang = o.lang;
  uint32_t clock = o.clockGiven ? o.clock : 0;
  bool clockSet = o.clockGiven;
  // lang, clock, battery, usb y charging que van antes del primer paso que corre el
  // firmware se aplican ya: el firmware los ve en setup()
  size_t first = simPreBootEnd(script);
  for (size_t i = 0; i < first; i++) {
    const SimCmd &c = script.cmds[i];
    switch (c.op) {
      case OP_LANG: if (o.lang < 0) lang = c.a; break;
      case OP_CLOCK: if (!o.clockGiven) { clock = (uint32_t)c.ms; clockSet = true; } break;
      case OP_BATTERY: gSimPower.batPct = c.a; break;
      case OP_USB: gSimPower.usb = c.a != 0; break;
      case OP_CHARGING: gSimPower.charging = c.a != 0; break;
      default: break;
    }
  }
  if (!clockSet) simParseClock("2026-11-13 10:00", clock);

  mockNvsReset();
  if (lang >= 0) {
    Preferences p;
    p.begin("tamapoke", false);
    p.putUChar("lang", (uint8_t)lang);
    p.end();
  }
  mockSetMillis(0);
  simClockSet(clock);
  simFsSetRoot(o.sprites.c_str());
  simFsHideShiny(BOARD_AMOLED_175C);
  mockSetRestartHook(onRestart);

  FILE *serialLog = nullptr;
  std::string logPath = o.serialLog.empty() ? o.out + "/serial.log" : o.serialLog;
  serialLog = fopen(logPath.c_str(), "w");
  if (!serialLog) {
    fprintf(stderr, "cannot write %s\n", logPath.c_str());
    return 2;
  }
  mockSerialOutput(serialLog);

  if (!o.quiet)
    printf("sim: %s, lang %s, clock %u, sprites %s\n", BOARD_AMOLED_175C ? "1.75C" : "1.75",
           lang >= 0 ? simLangCode(lang) : "default", clock, o.sprites.c_str());

  setup();

  int shots = 0;
  for (size_t i = first; i < script.cmds.size() && !gSimPower.poweredOff; i++) {
    const SimCmd &c = script.cmds[i];
    switch (c.op) {
      case OP_LANG:
      case OP_CLOCK:
        break;  // el lector los rechaza despues del primer paso que corre el firmware
      case OP_BATTERY: gSimPower.batPct = c.a; break;
      case OP_USB: gSimPower.usb = c.a != 0; break;
      case OP_CHARGING: gSimPower.charging = c.a != 0; break;
      case OP_TAP:
        simTouchSet(true, (int16_t)c.a, (int16_t)c.b);
        run(TAP_DOWN_MS);
        simTouchSet(false, 0, 0);
        run(SETTLE_MS);
        break;
      case OP_HOLD:
        simTouchSet(true, (int16_t)c.a, (int16_t)c.b);
        run(c.ms);
        simTouchSet(false, 0, 0);
        run(SETTLE_MS);
        break;
      case OP_SWIPE: {
        const int steps = 8;
        simTouchSet(true, (int16_t)c.a, (int16_t)c.b);
        for (int s = 1; s <= steps; s++) {
          run(c.ms / steps);
          simTouchSet(true, (int16_t)(c.a + (c.c - c.a) * s / steps), (int16_t)(c.b + (c.d - c.b) * s / steps));
        }
        run(40);  // el ultimo punto se lee antes de soltar
        simTouchSet(false, 0, 0);
        run(SETTLE_MS);
        break;
      }
      case OP_PRESS_PWR:
        for (int k = 0; k < c.a; k++) {
          simPwrPress();
          run(PWR_SPACING_MS);
        }
        run(PWR_RESOLVE_MS);
        break;
      case OP_PRESS_BOOT:
        for (int k = 0; k < c.a; k++) {
          simBootButton(true);
          run(BOOT_DOWN_MS);
          simBootButton(false);
          if (k + 1 < c.a) run(BOOT_GAP_MS);
        }
        run(BOOT_RESOLVE_MS);
        break;
      case OP_WAIT: {
        uint32_t f0 = gSimPanel.flushes;
        uint64_t passes = run(c.ms);
        if (!o.quiet && c.ms >= 60000)
          printf("sim: waited %llu ms (%llu loop passes, %u frames)\n", (unsigned long long)c.ms,
                 (unsigned long long)passes, gSimPanel.flushes - f0);
        break;
      }
      case OP_SERIAL: {
        std::string line = c.text + "\n";
        mockSerialFeed(line.c_str());
        run(50);
        break;
      }
      case OP_SHOT: {
        std::string path = o.out + "/" + c.text + ".png";
        static uint16_t black[SIM_W * SIM_H];
        const uint16_t *px = simPanelVisible() ? gSimPanel.fb : black;
        if (!pngWriteRgb565(path, px, SIM_W, SIM_H, !o.square)) {
          fprintf(stderr, "cannot write %s\n", path.c_str());
          return 1;
        }
        shots++;
        if (!o.quiet)
          printf("sim: %s%s\n", path.c_str(), simPanelVisible() ? "" : " (panel dark: screen off or locked)");
        break;
      }
    }
  }
  fclose(serialLog);
  if (gSimPower.poweredOff) printf("sim: the PMU powered the board off (battery at 0%% without USB)\n");
  if (!o.quiet) {
    printf("sim: done at %.1f s simulated, %d screenshot(s), serial log %s\n", millis() / 1000.0, shots, logPath.c_str());
    printf("sim: %u sound effect(s) would have played (last: %d), %u light sleep(s)\n", (unsigned)mockSfxCount(),
           mockLastSfx(), (unsigned)simLightSleeps());
  }
  return 0;
}
