// The player's birthday (holiday.h, #26): the one stored copy and its NVS key.
#include <Arduino.h>
#include <Preferences.h>
#include "holiday.h"

static Birthday gBday;  // checked; month 0 = none

// Like ownerLoad(), a Preferences handle of its own on the pet's namespace. isKey() first,
// so a missing key is not an error; a key of another type reads as 0 (none).
void birthdayLoad() {
  Preferences p;
  p.begin("tamapoke", true);  // read-only
  uint16_t v = p.isKey("bday") ? p.getUShort("bday", 0) : 0;
  p.end();
  gBday = birthdayUnpack(v);  // the range check: anything but a real date is none
}

Birthday birthday() { return gBday; }

bool birthdaySet(Birthday b) {
  if (b.month && !birthdayValid(b.month, b.day)) return false;  // bdayParse() never passes one
  Preferences p;
  p.begin("tamapoke", false);
  bool ok = true;
  if (b.month) ok = p.putUShort("bday", birthdayPack(b)) > 0;
  else if (p.isKey("bday")) ok = p.remove("bday");
  p.end();
  if (!ok) return false;  // the stored birthday, and so this copy, are unchanged
  gBday = b.month ? b : Birthday();
  return true;
}
