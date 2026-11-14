// What both simulator programs do before the sketch's setup(): tamapoke_sim (script
// runner) and tamapoke_window (interactive). One copy, so they boot the same way.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>

struct SimBootConfig {
  int lang = -1;               // Lang index written to the "lang" key; -1: leave it unset (the firmware's default)
  uint32_t clock = 0;          // board clock at boot, local time as epoch seconds; 0 = no time
  std::string sprites;         // folder that stands in for LittleFS / the SD card
  FILE *serialOut = nullptr;   // where the firmware's Serial output goes; nullptr = dropped
};

// Empty NVS (a new game), the language, the clocks (millis() at 0, the software RTC),
// the sprites folder (the 1.75C without the shiny sprites), the ESP.restart() hook
// (prints why and exits with status 3) and the serial output. Call before setup().
void simBootPrepare(const SimBootConfig &cfg);

// Make DIR and any missing parents (like mkdir -p). False if that fails.
bool simMkdirs(const std::string &dir);

// What the panel shows right now: the framebuffer, or black when the panel is asleep,
// off or at brightness 0 (simPanelVisible()). Screenshots and the window use this.
const uint16_t *simShownPixels();
