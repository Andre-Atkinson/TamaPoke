// The minigames' screens and the strength-training sack (#41: moved out of
// TamaPoke.ino): starting and finishing a game, its touches and sounds, and drawing it.
// The games' own logic is in minigames.h; render() and handleTouch() call in here.
#pragma once
#include <stdint.h>
#include "minigames.h"  // GAME_COUNT
#include "species.h"    // the SPR_ICON_* sprites
