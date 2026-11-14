// The screens other than the main one (#41: moved out of TamaPoke.ino): the starter and
// egg choice, the pet card's five pages, the clock and settings screen, the nickname
// keyboard and the Pokedex gallery, each with its taps and its state. render(), onTap()
// and onSwipe*() in TamaPoke.ino choose which one is open.
#pragma once
#include <stdint.h>
#include "sdmon.h"  // PmdMon
