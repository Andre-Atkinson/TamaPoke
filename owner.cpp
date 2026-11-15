// The player's name (owner.h): the one stored copy and its NVS key.
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "i18n.h"
#include "owner.h"

static char gOwner[OWNER_NAME_BYTES] = "";  // checked, UTF-8; "" = no name

// Like loadLang() and setLang(), a Preferences handle of its own on the pet's namespace.
void ownerLoad() {
  gOwner[0] = 0;
  Preferences p;
  p.begin("tamapoke", true);  // read-only
  // One byte more than a valid name needs: a longer stored string either does not fit
  // (the real getString() then returns 0 and leaves buf alone) or comes back cut to
  // OWNER_NAME_BYTES bytes (the test shim), and either way it fails the check below.
  // isKey() first, so a missing key is not an error.
  char buf[OWNER_NAME_BYTES + 1] = "";
  if (p.isKey("owner")) p.getString("owner", buf, sizeof buf);
  p.end();
  buf[sizeof buf - 1] = 0;
  if (ownerNameCheck(buf) == OWNER_OK) strcpy(gOwner, buf);  // checked: fits gOwner
}

const char *ownerName() { return gOwner; }

OwnerCheck ownerSet(const char *utf8, uint32_t *bad) {
  OwnerCheck c = ownerNameCheck(utf8, bad);
  if (c != OWNER_OK) return c;
  Preferences p;
  p.begin("tamapoke", false);
  bool ok = true;
  if (utf8[0]) ok = p.putString("owner", utf8) > 0;
  else if (p.isKey("owner")) ok = p.remove("owner");
  p.end();
  if (!ok) return OWNER_NOT_SAVED;  // the stored name, and so this copy, are unchanged
  strcpy(gOwner, utf8);  // checked: at most OWNER_NAME_BYTES - 1 bytes
  return OWNER_OK;
}

void ownerNameForActiveFont(char *out, size_t outSize) {
  ownerNameForFont(gOwner, LANG_IS_CJK(gLang), out, outSize);
}
