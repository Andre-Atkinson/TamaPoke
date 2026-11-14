// The main screen's scene and pet drawing (#41: moved out of TamaPoke.ino): the sky and
// the biome, the pet with its PMD animation and walk, the bath, poops, the attention
// bubble, the header, bars and buttons, the endings and their buttons, the battery glyph
// and the clock and streak badges. render() in TamaPoke.ino puts them together.
#pragma once
#include <stdint.h>
#include "sdmon.h"    // PmdMon, PmdAct, PMD_*
#include "species.h"  // Species
