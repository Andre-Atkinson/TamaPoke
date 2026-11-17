#pragma once
#include <stdint.h>
#include "pet.h"

// Starting an ending (#33): the farewell, the run-away or the release, Pet's ceremonies
// (CER_FAREWELL, CER_RUNAWAY, CER_RELEASE). Every way one starts comes through here: the
// farewell choice's button, the run-away button, the release dialog's YES and the serial
// BYE and RUN (startPetEnding() in the sketch, app.h).
//
// `closeOverlays` (closeForEnding() in the sketch) first closes everything over the scene,
// so nothing is drawn in the ending's place and nothing is left open for the egg after it:
// a minigame or an exercise ends paid for what was done (#94, #88), a bath finishes, the
// other screens close unsaved and the strips and dialogs close with nothing chosen. It runs
// before the ceremony starts because the pet takes nothing during one (Pet::playGame,
// trainStrength, trainStamina and clean() return at once): run after, a minigame's score
// would be lost. A pet that cannot end now (an egg, which a starter or egg choice is too, or
// an ending already on: the same check Pet::startFarewell(), startRunaway() and release()
// make) is left as it is, its overlays too, and so is an unknown `kind`.
// Returns whether the ending started.
inline bool startEnding(Pet &pet, uint8_t kind, void (*closeOverlays)()) {
  if (pet.isEgg() || pet.ceremony != CER_NONE) return false;
  if (kind != CER_FAREWELL && kind != CER_RUNAWAY && kind != CER_RELEASE) return false;
  closeOverlays();
  if (kind == CER_FAREWELL) pet.startFarewell();
  else if (kind == CER_RUNAWAY) pet.startRunaway();
  else pet.release();
  return pet.ceremony == kind;
}
