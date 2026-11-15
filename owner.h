// The player's name (#23; #26 reuses it). Set once from the owner's computer with the
// serial command `OWNER <name>` (an empty name clears it); there is no screen for it.
// It lives in NVS namespace `tamapoke`, key "owner", as UTF-8, and never in the repo.
//
// The name must draw in every language, so it is checked against the fonts the bubble
// uses (uitext.cpp):
//  - the Latin languages print the classic 5x7 font, a CP437 table: plain ASCII plus
//    the accented letters below (not the accented capitals A/I/O/U/A`/E`, or a~/o~, which
//    CP437 lacks; ae and sharp s are in CP437 but are no accented letters and have no
//    one-letter plain form for the CJK fonts, so they are left out too);
//  - Japanese and Korean print unifont_t_japanese3 / unifont_t_korean2, which have plain
//    ASCII (0x20-0x7E) and no accented Latin letter at all, so there an accented letter
//    drops to its plain one (an e with an acute shows as a plain e).
// A name also needs at least one letter (an ASCII letter or one of the accented ones), so
// "?" or "123" is no name. Anything else is refused, and a stored name that fails the
// check (a damaged page, an older or other firmware) loads as no name.
//
// The checks and the conversions are pure (test/test_owner.cpp); owner.cpp keeps the
// stored copy and talks to NVS.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define OWNER_NAME_MAX 12  // characters; "I love you, <name>!" must fit the bubble in every language
// UTF-8 bytes, worst case: each accented letter is two bytes, plus the terminator
#define OWNER_NAME_BYTES (OWNER_NAME_MAX * 2 + 1)

// The accented letters a name may use: their Unicode code point, their CP437 byte (the
// classic font's glyph) and the plain ASCII letter the CJK fonts draw instead.
struct OwnerLetter {
  uint16_t cp;
  uint8_t cp437;
  char plain;
};
inline const OwnerLetter OWNER_LETTERS[] = {
  {0x00C7, 0x80, 'C'}, {0x00FC, 0x81, 'u'}, {0x00E9, 0x82, 'e'}, {0x00E2, 0x83, 'a'},
  {0x00E4, 0x84, 'a'}, {0x00E0, 0x85, 'a'}, {0x00E5, 0x86, 'a'}, {0x00E7, 0x87, 'c'},
  {0x00EA, 0x88, 'e'}, {0x00EB, 0x89, 'e'}, {0x00E8, 0x8A, 'e'}, {0x00EF, 0x8B, 'i'},
  {0x00EE, 0x8C, 'i'}, {0x00EC, 0x8D, 'i'}, {0x00C4, 0x8E, 'A'}, {0x00C5, 0x8F, 'A'},
  {0x00C9, 0x90, 'E'}, {0x00F4, 0x93, 'o'}, {0x00F6, 0x94, 'o'}, {0x00F2, 0x95, 'o'},
  {0x00FB, 0x96, 'u'}, {0x00F9, 0x97, 'u'}, {0x00FF, 0x98, 'y'}, {0x00D6, 0x99, 'O'},
  {0x00DC, 0x9A, 'U'}, {0x00E1, 0xA0, 'a'}, {0x00ED, 0xA1, 'i'}, {0x00F3, 0xA2, 'o'},
  {0x00FA, 0xA3, 'u'}, {0x00F1, 0xA4, 'n'}, {0x00D1, 0xA5, 'N'},
};
#define OWNER_LETTER_COUNT (sizeof(OWNER_LETTERS) / sizeof(OWNER_LETTERS[0]))

// The entry for an accented code point, or nullptr.
inline const OwnerLetter *ownerLetter(uint32_t cp) {
  for (const OwnerLetter &l : OWNER_LETTERS)
    if (l.cp == cp) return &l;
  return nullptr;
}

// Can a name use this code point? Printable ASCII or one of OWNER_LETTERS.
inline bool ownerCharOk(uint32_t cp) { return (cp >= 0x20 && cp <= 0x7E) || ownerLetter(cp); }

// Decodes one UTF-8 character at s. Returns its length in bytes (1-4) and the code point
// in *cp, or 0 for a malformed, overlong or truncated sequence.
inline int ownerUtf8(const char *s, uint32_t *cp) {
  const unsigned char *p = (const unsigned char *)s;
  unsigned char c = p[0];
  int n;
  uint32_t v;
  if (c < 0x80) { *cp = c; return 1; }
  if (c >= 0xC2 && c <= 0xDF) { n = 2; v = c & 0x1F; }
  else if (c >= 0xE0 && c <= 0xEF) { n = 3; v = c & 0x0F; }
  else if (c >= 0xF0 && c <= 0xF4) { n = 4; v = c & 0x07; }
  else return 0;  // a continuation byte, an overlong lead (C0/C1) or past U+10FFFF
  for (int i = 1; i < n; i++) {
    if ((p[i] & 0xC0) != 0x80) return 0;  // includes the terminator: truncated
    v = (v << 6) | (p[i] & 0x3F);
  }
  if ((n == 3 && v < 0x800) || (n == 4 && v < 0x10000) || (v >= 0xD800 && v <= 0xDFFF) || v > 0x10FFFF) return 0;
  *cp = v;
  return n;
}

enum OwnerCheck : uint8_t {
  OWNER_OK = 0,
  OWNER_TOO_LONG,   // more than OWNER_NAME_MAX characters
  OWNER_BAD_UTF8,   // not valid UTF-8
  OWNER_BAD_CHAR,   // a character the fonts cannot draw (*bad says which)
  OWNER_BAD_SPACE,  // starts or ends with a space (the serial line is trimmed, NVS is not)
  OWNER_NO_LETTER,  // not empty, but not one letter in it ("123", "?", "-")
  OWNER_NOT_SAVED,  // ownerSet(): the NVS write failed; the old name stays
};

// A letter a name can count: an ASCII letter or one of OWNER_LETTERS.
inline bool ownerIsLetter(uint32_t cp) {
  return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || ownerLetter(cp);
}

// Is `s` a name the bubble can draw? "" is fine (no name). *bad gets the first refused
// code point for OWNER_BAD_CHAR.
inline OwnerCheck ownerNameCheck(const char *s, uint32_t *bad = nullptr) {
  int chars = 0;
  bool letter = false;
  const char *p = s;
  while (*p) {
    uint32_t cp;
    int n = ownerUtf8(p, &cp);
    if (!n) return OWNER_BAD_UTF8;
    if (!ownerCharOk(cp)) {
      if (bad) *bad = cp;
      return OWNER_BAD_CHAR;
    }
    if (++chars > OWNER_NAME_MAX) return OWNER_TOO_LONG;
    if (ownerIsLetter(cp)) letter = true;
    p += n;
  }
  if (chars && (s[0] == ' ' || p[-1] == ' ')) return OWNER_BAD_SPACE;
  if (chars && !letter) return OWNER_NO_LETTER;
  return OWNER_OK;
}

// The serial commands about the name, as handleSerial() reads them (serialcmd.cpp).
enum OwnerCmd : uint8_t {
  OWNER_CMD_NONE = 0,  // not ours
  OWNER_CMD_QUERY,     // "OWNER?"
  OWNER_CMD_SET,       // "OWNER", "OWNER <name>": the name to set is in `name` ("" clears it)
};
#define OWNER_CMD_NAME_BYTES 64  // room for a name line; longer is cut, and is refused anyway

// Reads one serial line (already trimmed by handleSerial). "OWNER?" is a query, "OWNER" or
// "OWNER " plus text sets the name, with the spaces at both ends of the text dropped.
// Anything else ("OWNERX", "OWNER?x", "owner x") is not ours. The name is copied into
// `name` (nameSize bytes, always terminated). A name too long for it is cut, which still
// fails ownerNameCheck() (the cut is longer than OWNER_NAME_BYTES), so the cut never turns
// a refused name into an accepted one. The name is not checked here.
inline OwnerCmd ownerParse(const char *line, char *name, size_t nameSize) {
  if (nameSize) name[0] = 0;
  if (strcmp(line, "OWNER?") == 0) return OWNER_CMD_QUERY;
  if (strncmp(line, "OWNER", 5) != 0 || (line[5] != 0 && line[5] != ' ')) return OWNER_CMD_NONE;
  const char *a = line + 5;
  while (*a == ' ') a++;
  size_t n = strlen(a);
  while (n && a[n - 1] == ' ') n--;
  if (nameSize) {
    if (n > nameSize - 1) n = nameSize - 1;
    memcpy(name, a, n);
    name[n] = 0;
  }
  return OWNER_CMD_SET;
}

// A checked name in the encoding of the active font: CP437 bytes for the classic font,
// plain ASCII for a CJK font. One byte per character either way, so `out` needs
// OWNER_NAME_MAX + 1 bytes. A name that fails ownerNameCheck() gives "".
inline void ownerNameForFont(const char *utf8, bool cjk, char *out, size_t outSize) {
  if (!outSize) return;
  out[0] = 0;
  if (ownerNameCheck(utf8) != OWNER_OK) return;
  size_t k = 0;
  for (const char *p = utf8; *p && k + 1 < outSize;) {
    uint32_t cp;
    p += ownerUtf8(p, &cp);
    const OwnerLetter *l = ownerLetter(cp);
    out[k++] = !l ? (char)cp : cjk ? l->plain : (char)l->cp437;
  }
  out[k] = 0;
}

// ---- the stored name (owner.cpp) ----
void ownerLoad();                                  // setup(): read and check the NVS copy
const char *ownerName();                           // UTF-8, "" when no name is set
OwnerCheck ownerSet(const char *utf8, uint32_t *bad = nullptr);  // store ("" clears); refused names change nothing
void ownerNameForActiveFont(char *out, size_t outSize);  // ownerName() for the active language's font
