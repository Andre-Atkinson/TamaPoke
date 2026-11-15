// The player's name (owner.h, #23): which names the fonts can draw, how they are encoded
// for each font, and the NVS key with its range check on load. The names here are
// placeholders; the real one is only ever set on the board.
#include "framework.h"
#include "../owner.h"
#include "../i18n.h"
#include <Preferences.h>
#include <string.h>
#include <string>

// UTF-8 for one code point (the tests build names from code points)
static std::string u8(uint32_t cp) {
  std::string s;
  if (cp < 0x80) s += (char)cp;
  else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
  else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
  return s;
}

static void putRaw(const char *v) {
  Preferences p;
  p.begin("tamapoke", false);
  p.putString("owner", v);
  p.end();
}

// ---------------------------------------------------------------- UTF-8
TEST(Owner, utf8_decodes_one_to_four_bytes) {
  uint32_t cp = 0;
  CHECK_EQ(ownerUtf8("A", &cp), 1); CHECK_EQ(cp, 0x41u);
  CHECK_EQ(ownerUtf8("\xC3\xA9", &cp), 2); CHECK_EQ(cp, 0xE9u);
  CHECK_EQ(ownerUtf8("\xE3\x81\x82", &cp), 3); CHECK_EQ(cp, 0x3042u);
  CHECK_EQ(ownerUtf8("\xF0\x9F\x98\x80", &cp), 4); CHECK_EQ(cp, 0x1F600u);
}

TEST(Owner, utf8_refuses_broken_sequences) {
  uint32_t cp = 0;
  CHECK_EQ(ownerUtf8("\xA9", &cp), 0);              // a lone continuation byte
  CHECK_EQ(ownerUtf8("\xC3", &cp), 0);              // cut short by the terminator
  CHECK_EQ(ownerUtf8("\xC3" "A", &cp), 0);          // not followed by a continuation
  CHECK_EQ(ownerUtf8("\xC1\xA9", &cp), 0);          // overlong (C0/C1 leads)
  CHECK_EQ(ownerUtf8("\xE0\x80\x80", &cp), 0);      // overlong three bytes
  CHECK_EQ(ownerUtf8("\xF0\x80\x80\x80", &cp), 0);  // overlong four bytes
  CHECK_EQ(ownerUtf8("\xED\xA0\x80", &cp), 0);      // a surrogate
  CHECK_EQ(ownerUtf8("\xF4\x90\x80\x80", &cp), 0);  // past U+10FFFF
  CHECK_EQ(ownerUtf8("\xF5\x80\x80\x80", &cp), 0);
  CHECK_EQ(ownerUtf8("\xFF", &cp), 0);
}

// ---------------------------------------------------------------- which names
TEST(Owner, plain_names_and_no_name_are_fine) {
  CHECK_EQ(ownerNameCheck(""), OWNER_OK);
  CHECK_EQ(ownerNameCheck("Player"), OWNER_OK);
  CHECK_EQ(ownerNameCheck("Player Two"), OWNER_OK);
  CHECK_EQ(ownerNameCheck("O'Player-2.0"), OWNER_OK);
  CHECK_EQ(ownerNameCheck(" !~"), OWNER_BAD_SPACE);
  CHECK_EQ(ownerNameCheck("a !~"), OWNER_OK);  // every printable ASCII character draws
}

TEST(Owner, every_accented_letter_the_latin_font_has_is_fine) {
  CHECK_EQ((int)OWNER_LETTER_COUNT, 31);
  for (const OwnerLetter &l : OWNER_LETTERS) {
    std::string n = "X" + u8(l.cp);
    CHECK_MSG(ownerNameCheck(n.c_str()) == OWNER_OK, "refused U+" + std::to_string(l.cp));
  }
}

TEST(Owner, letters_the_fonts_cannot_draw_are_refused) {
  // accented capitals and letters CP437 lacks, ae and sharp s (in CP437, but no accented
  // letters and no one-letter plain form for the CJK fonts), symbols, other scripts, control characters
  const uint32_t refused[] = { 0xC1, 0xCD, 0xD3, 0xDA, 0xC0, 0xC8, 0xE3, 0xF5, 0xE6, 0xC6, 0xDF, 0xA1, 0xB0,
                               0x0153, 0x3042, 0xAC00, 0x1F600, 0x09, 0x0A, 0x7F, 0x80, 0x9F, 0xA0, 0xAD };
  for (uint32_t cp : refused) {
    std::string n = "Ab" + u8(cp) + "c";
    uint32_t bad = 0;
    CHECK_MSG(ownerNameCheck(n.c_str(), &bad) == OWNER_BAD_CHAR, "accepted U+" + std::to_string(cp));
    CHECK_EQ(bad, cp);
  }
  CHECK_EQ(ownerNameCheck("Ab\x01"), OWNER_BAD_CHAR);
}

TEST(Owner, broken_utf8_is_refused) {
  CHECK_EQ(ownerNameCheck("Pl\xE9yer"), OWNER_BAD_UTF8);  // Latin-1, not UTF-8
  CHECK_EQ(ownerNameCheck("Player\xC3"), OWNER_BAD_UTF8);
  CHECK_EQ(ownerNameCheck("\xC3\xA9\xA9"), OWNER_BAD_UTF8);
}

TEST(Owner, at_most_12_characters_counted_as_letters_not_bytes) {
  CHECK_EQ(ownerNameCheck("Abcdefghijkl"), OWNER_OK);
  CHECK_EQ(ownerNameCheck("Abcdefghijklm"), OWNER_TOO_LONG);
  std::string twelve, thirteen;
  for (int i = 0; i < 12; i++) twelve += u8(0xE9);  // 24 bytes
  thirteen = twelve + "e";
  CHECK_EQ(ownerNameCheck(twelve.c_str()), OWNER_OK);
  CHECK_EQ((int)twelve.size(), OWNER_NAME_BYTES - 1);
  CHECK_EQ(ownerNameCheck(thirteen.c_str()), OWNER_TOO_LONG);
}

TEST(Owner, spaces_at_either_end_are_refused_inside_are_fine) {
  CHECK_EQ(ownerNameCheck(" Player"), OWNER_BAD_SPACE);
  CHECK_EQ(ownerNameCheck("Player "), OWNER_BAD_SPACE);
  CHECK_EQ(ownerNameCheck(" "), OWNER_BAD_SPACE);
  CHECK_EQ(ownerNameCheck("Pla yer"), OWNER_OK);
}

TEST(Owner, a_name_needs_a_letter) {
  CHECK_EQ(ownerNameCheck("123"), OWNER_NO_LETTER);
  CHECK_EQ(ownerNameCheck("?"), OWNER_NO_LETTER);
  CHECK_EQ(ownerNameCheck("-"), OWNER_NO_LETTER);
  CHECK_EQ(ownerNameCheck("1 2.3!"), OWNER_NO_LETTER);
  CHECK_EQ(ownerNameCheck(""), OWNER_OK);  // no name is not a name without a letter
  CHECK_EQ(ownerNameCheck("R2-D2"), OWNER_OK);
  CHECK_EQ(ownerNameCheck("a"), OWNER_OK);
  CHECK_EQ(ownerNameCheck("7z"), OWNER_OK);
  CHECK_EQ(ownerNameCheck("Zo\xC3\xAB"), OWNER_OK);    // an accented letter counts
  std::string only = "1" + u8(0xE9);                    // a lone accented letter among digits
  CHECK_EQ(ownerNameCheck(only.c_str()), OWNER_OK);
  CHECK_EQ(ownerNameCheck("@[`{"), OWNER_NO_LETTER);    // the characters next to the ASCII letter ranges
  // a stored name without a letter loads as no name (the same check)
  mockNvsReset();
  putRaw("123");
  ownerLoad();
  CHECK_STREQ(ownerName(), "");
  CHECK_EQ(ownerSet("?"), OWNER_NO_LETTER);
}

// ---------------------------------------------------------------- the serial line
static OwnerCmd parse(const char *line, std::string *name = nullptr) {
  char out[OWNER_CMD_NAME_BYTES];
  memset(out, 'x', sizeof out);  // proves the name is always terminated
  OwnerCmd c = ownerParse(line, out, sizeof out);
  if (name) *name = out;
  return c;
}

TEST(Owner, owner_alone_clears_the_name) {
  std::string n = "stale";
  CHECK_EQ(parse("OWNER", &n), OWNER_CMD_SET);
  CHECK_STREQ(n.c_str(), "");
  n = "stale";
  CHECK_EQ(parse("OWNER ", &n), OWNER_CMD_SET);
  CHECK_STREQ(n.c_str(), "");
  CHECK_EQ(parse("OWNER    ", &n), OWNER_CMD_SET);
  CHECK_STREQ(n.c_str(), "");
}

TEST(Owner, owner_with_a_name_sets_it_trimmed_at_both_ends) {
  std::string n;
  CHECK_EQ(parse("OWNER Player", &n), OWNER_CMD_SET);
  CHECK_STREQ(n.c_str(), "Player");
  CHECK_EQ(parse("OWNER   Ab  c  ", &n), OWNER_CMD_SET);
  CHECK_STREQ(n.c_str(), "Ab  c");  // inside, the spaces stay
  CHECK_EQ(parse("OWNER Zo\xC3\xAB", &n), OWNER_CMD_SET);
  CHECK_STREQ(n.c_str(), "Zo\xC3\xAB");
}

TEST(Owner, owner_question_mark_is_the_query) {
  std::string n = "stale";
  CHECK_EQ(parse("OWNER?", &n), OWNER_CMD_QUERY);
  CHECK_STREQ(n.c_str(), "");
}

TEST(Owner, other_lines_are_not_ours) {
  CHECK_EQ(parse("OWNERX"), OWNER_CMD_NONE);
  CHECK_EQ(parse("OWNER?x"), OWNER_CMD_NONE);
  CHECK_EQ(parse("OWNER? "), OWNER_CMD_NONE);
  CHECK_EQ(parse("owner x"), OWNER_CMD_NONE);
  CHECK_EQ(parse("OWNE"), OWNER_CMD_NONE);
  CHECK_EQ(parse("NICK OWNER"), OWNER_CMD_NONE);
  CHECK_EQ(parse(""), OWNER_CMD_NONE);
  std::string n = "stale";
  parse("OWNERX", &n);
  CHECK_STREQ(n.c_str(), "");  // nothing is left to be mistaken for a name
}

TEST(Owner, owner_space_question_mark_is_a_set_the_check_refuses) {
  std::string n;
  CHECK_EQ(parse("OWNER ?", &n), OWNER_CMD_SET);  // not the query
  CHECK_STREQ(n.c_str(), "?");
  mockNvsReset();
  CHECK_EQ(ownerSet("Player"), OWNER_OK);
  CHECK_EQ(ownerSet(n.c_str()), OWNER_NO_LETTER);
  CHECK_STREQ(ownerName(), "Player");  // the stored name is unchanged
}

TEST(Owner, a_name_line_that_does_not_fit_is_cut_and_still_refused) {
  char small[8];
  CHECK_EQ(ownerParse("OWNER Abcdefghij", small, sizeof small), OWNER_CMD_SET);
  CHECK_STREQ(small, "Abcdefg");  // cut, never overrun
  CHECK_EQ(ownerParse("OWNER Abc", small, 0), OWNER_CMD_SET);  // no room: nothing written
  std::string longName = "OWNER " + std::string(200, 'a');
  std::string n;
  CHECK_EQ(parse(longName.c_str(), &n), OWNER_CMD_SET);
  CHECK_EQ((int)n.size(), OWNER_CMD_NAME_BYTES - 1);
  CHECK_EQ(ownerNameCheck(n.c_str()), OWNER_TOO_LONG);
}

// ---------------------------------------------------------------- for each font
TEST(Owner, the_latin_font_gets_cp437_bytes_and_the_cjk_fonts_plain_letters) {
  std::string n = u8(0xD1) + "and" + u8(0xFA) + " " + u8(0xC9) + "x";  // "Nandu Ex", accented
  char out[OWNER_NAME_MAX + 1];
  ownerNameForFont(n.c_str(), false, out, sizeof out);
  CHECK_STREQ(out, "\xA5" "and\xA3 \x90x");
  ownerNameForFont(n.c_str(), true, out, sizeof out);
  CHECK_STREQ(out, "Nandu Ex");
  for (const OwnerLetter &l : OWNER_LETTERS) {
    std::string one = u8(l.cp);
    ownerNameForFont(one.c_str(), false, out, sizeof out);
    CHECK_EQ((uint8_t)out[0], l.cp437);
    CHECK_EQ(out[1], 0);
    ownerNameForFont(one.c_str(), true, out, sizeof out);
    CHECK_EQ(out[0], l.plain);
    CHECK((l.plain >= 'a' && l.plain <= 'z') || (l.plain >= 'A' && l.plain <= 'Z'));
  }
}

TEST(Owner, the_longest_name_fits_its_font_buffer) {
  std::string twelve;
  for (int i = 0; i < 12; i++) twelve += u8(0xFC);
  char out[OWNER_NAME_MAX + 1];
  memset(out, 'x', sizeof out);
  ownerNameForFont(twelve.c_str(), false, out, sizeof out);
  CHECK_EQ((int)strlen(out), OWNER_NAME_MAX);
  ownerNameForFont(twelve.c_str(), true, out, sizeof out);
  CHECK_EQ((int)strlen(out), OWNER_NAME_MAX);
  char small[4];
  ownerNameForFont("Player", false, small, sizeof small);
  CHECK_STREQ(small, "Pla");  // cut, never overrun
  ownerNameForFont("Player", false, small, 0);  // nothing written
}

TEST(Owner, a_name_the_check_refuses_is_never_drawn) {
  char out[OWNER_NAME_MAX + 1] = "x";
  ownerNameForFont("Abcdefghijklm", false, out, sizeof out);
  CHECK_STREQ(out, "");
  ownerNameForFont("Pl\xE9yer", true, out, sizeof out);
  CHECK_STREQ(out, "");
}

// ---------------------------------------------------------------- NVS
TEST(Owner, no_name_until_one_is_set) {
  mockNvsReset();
  ownerLoad();
  CHECK_STREQ(ownerName(), "");
}

TEST(Owner, a_name_is_stored_and_survives_a_restart) {
  mockNvsReset();
  std::string n = "Pl" + u8(0xE1) + "yer";
  CHECK_EQ(ownerSet(n.c_str()), OWNER_OK);
  CHECK_STREQ(ownerName(), n.c_str());
  ownerLoad();  // as after a restart: what NVS holds
  CHECK_STREQ(ownerName(), n.c_str());
  putRaw("Other");  // proof that ownerLoad() reads NVS rather than keeping its copy
  ownerLoad();
  CHECK_STREQ(ownerName(), "Other");
  putRaw(n.c_str());
  Preferences p;
  p.begin("tamapoke", true);
  char raw[32] = "";
  p.getString("owner", raw, sizeof raw);
  p.end();
  CHECK_STREQ(raw, n.c_str());  // stored as UTF-8, in the pet's namespace, key "owner"
}

TEST(Owner, an_empty_name_clears_it) {
  mockNvsReset();
  CHECK_EQ(ownerSet("Player"), OWNER_OK);
  CHECK_EQ(ownerSet(""), OWNER_OK);
  CHECK_STREQ(ownerName(), "");
  Preferences p;
  p.begin("tamapoke", true);
  CHECK(!p.isKey("owner"));
  p.end();
  ownerLoad();
  CHECK_STREQ(ownerName(), "");
  CHECK_EQ(ownerSet(""), OWNER_OK);  // clearing again is fine
}

TEST(Owner, a_refused_name_changes_nothing) {
  mockNvsReset();
  CHECK_EQ(ownerSet("Player"), OWNER_OK);
  uint32_t bad = 0;
  std::string n = "Pl" + u8(0xC1) + "yer";
  CHECK_EQ(ownerSet(n.c_str(), &bad), OWNER_BAD_CHAR);
  CHECK_EQ(bad, 0xC1u);
  CHECK_EQ(ownerSet("Abcdefghijklm"), OWNER_TOO_LONG);
  CHECK_STREQ(ownerName(), "Player");
  ownerLoad();
  CHECK_STREQ(ownerName(), "Player");
}

TEST(Owner, a_stored_name_that_fails_the_check_loads_as_no_name) {
  const char *badOnes[] = { "Abcdefghijklm", "Pl\xE9yer", " Player", "Player ", "Ab\x01", "Player\xC3",
                            "\xE3\x81\x82" };
  for (const char *v : badOnes) {
    mockNvsReset();
    putRaw(v);
    ownerLoad();
    CHECK_MSG(ownerName()[0] == 0, std::string("loaded a bad name: ") + v);
  }
  // longer than any name can be, in bytes: refused whatever getString() does with it
  std::string longOne;
  for (int i = 0; i < 13; i++) longOne += u8(0xE9);
  mockNvsReset();
  putRaw(longOne.c_str());
  ownerLoad();
  CHECK_STREQ(ownerName(), "");
  mockNvsReset();
  putRaw(std::string(60, 'a').c_str());
  ownerLoad();
  CHECK_STREQ(ownerName(), "");
  // a key of another type under the same name
  mockNvsReset();
  Preferences p;
  p.begin("tamapoke", false);
  p.putUInt("owner", 42);
  p.end();
  ownerLoad();
  CHECK(strlen(ownerName()) <= OWNER_NAME_BYTES - 1);
  CHECK_EQ(ownerNameCheck(ownerName()), OWNER_OK);
}

TEST(Owner, the_active_font_follows_the_language) {
  mockNvsReset();
  std::string n = "Z" + u8(0xF6) + "e";
  ownerSet(n.c_str());
  char out[OWNER_NAME_MAX + 1];
  gLang = LANG_DE;
  ownerNameForActiveFont(out, sizeof out);
  CHECK_STREQ(out, "Z\x94" "e");
  gLang = LANG_JA;
  ownerNameForActiveFont(out, sizeof out);
  CHECK_STREQ(out, "Zoe");
  gLang = LANG_KO;
  ownerNameForActiveFont(out, sizeof out);
  CHECK_STREQ(out, "Zoe");
  gLang = LANG_DEFAULT;
  ownerSet("");
}
