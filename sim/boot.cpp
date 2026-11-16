// Shared pre-boot setup of the two simulator programs (see boot.h).
#include "boot.h"
#include <Arduino.h>
#include <Preferences.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits>
#include <sstream>
#include "simhw.h"
#include "../i18n.h"
#include "../board.h"

// char is unsigned on the ESP32-S3 (#29), so sim/Makefile builds both programs with
// -funsigned-char: a byte >= 0x80, such as a PT letter, then reads as on the board.
// This file is in both programs and not in the host tests, which check the same at run
// time (host.char_is_unsigned_like_the_esp32s3).
static_assert(!std::numeric_limits<char>::is_signed,
              "sim/Makefile must build with -funsigned-char: char is unsigned on the ESP32-S3");

static void onRestart() {
  fprintf(stderr, "sim: the firmware called ESP.restart() (WIPE); stopping here\n");
  exit(3);
}

void simBootPrepare(const SimBootConfig &cfg) {
  mockNvsReset();
  if (cfg.lang >= 0) setLang((Lang)cfg.lang);  // the firmware's own write: the code it stores (i18n.h)
  mockSetMillis(0);
  simClockSet(cfg.clock);
  simFsSetRoot(cfg.sprites.c_str());
  simFsHideShiny(BOARD_AMOLED_175C);
  mockSetRestartHook(onRestart);
  mockSerialOutput(cfg.serialOut);
}

bool simMkdirs(const std::string &dir) {
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

const uint16_t *simShownPixels() {
  static uint16_t black[SIM_W * SIM_H];
  return simPanelVisible() ? gSimPanel.fb : black;
}
