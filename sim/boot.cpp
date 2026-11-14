// Shared pre-boot setup of the two simulator programs (see boot.h).
#include "boot.h"
#include <Arduino.h>
#include <Preferences.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sstream>
#include "simhw.h"
#include "../board.h"

static void onRestart() {
  fprintf(stderr, "sim: the firmware called ESP.restart() (WIPE); stopping here\n");
  exit(3);
}

void simBootPrepare(const SimBootConfig &cfg) {
  mockNvsReset();
  if (cfg.lang >= 0) {
    Preferences p;
    p.begin("tamapoke", false);
    p.putUChar("lang", (uint8_t)cfg.lang);
    p.end();
  }
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
