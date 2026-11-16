// Holidays and the player's birthday (holiday.h, #26): the calendar, Easter, which day is
// special, the birthday (29 Feb, the BDAY command, the NVS key and its range check) and how
// often the pet greets. The dates here are placeholders; the real birthday is only ever set
// on the board.
#include "framework.h"
#include "../holiday.h"
#include <Preferences.h>
#include <string>

// A local epoch for a date and time, worked out without civilDate() (Howard Hinnant's
// days_from_civil, the inverse), so the tests do not check civilDate() against itself.
static uint32_t at(int y, int m, int d, int hh = 12, int mm = 0, int ss = 0) {
  y -= m <= 2;
  int era = y / 400, yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int days = era * 146097 + doe - 719468;
  return (uint32_t)days * 86400u + (uint32_t)(hh * 3600 + mm * 60 + ss);
}

// Easter by another method (Oudin's, as in python-dateutil's easter()), to check
// easterSunday() across the centuries rather than on a handful of dates.
static MonthDay easterOudin(int y) {
  int g = y % 19, c = y / 100;
  int h = (c - c / 4 - (8 * c + 13) / 25 + 19 * g + 15) % 30;
  int i = h - (h / 28) * (1 - (h / 28) * (29 / (h + 1)) * ((21 - g) / 11));
  int j = (y + y / 4 + i + 2 - c + c / 4) % 7;
  int p = i - j;
  MonthDay r;
  r.day = (uint8_t)(1 + (p + 27 + (p + 6) / 40) % 31);
  r.month = (uint8_t)(3 + (p + 26) / 30);
  return r;
}

static Birthday bd(int m, int d) {
  Birthday b;
  b.month = (uint8_t)m;
  b.day = (uint8_t)d;
  return b;
}

static void putRaw(uint16_t v) {
  Preferences p;
  p.begin("tamapoke", false);
  p.putUShort("bday", v);
  p.end();
}

// ---------------------------------------------------------------- the calendar
TEST(Holiday, leap_years_follow_the_gregorian_rule) {
  CHECK(holidayLeapYear(2024));
  CHECK(holidayLeapYear(2028));
  CHECK(holidayLeapYear(2000));  // divisible by 400
  CHECK(!holidayLeapYear(2026));
  CHECK(!holidayLeapYear(2027));
  CHECK(!holidayLeapYear(2100));  // divisible by 100, not 400
  CHECK(!holidayLeapYear(1900));
}

TEST(Holiday, month_lengths) {
  CHECK_EQ(holidayMonthDays(2028, 2), 29);
  CHECK_EQ(holidayMonthDays(2027, 2), 28);
  CHECK_EQ(holidayMonthDays(2100, 2), 28);
  CHECK_EQ(holidayMonthDays(2027, 4), 30);
  CHECK_EQ(holidayMonthDays(2027, 12), 31);
  CHECK_EQ(holidayMonthDays(2027, 0), 0);
  CHECK_EQ(holidayMonthDays(2027, 13), 0);
}

// ---------------------------------------------------------------- Easter
TEST(Holiday, easter_on_known_dates) {
  struct { int y, m, d; } K[] = {
    { 1818, 3, 22 }, { 2000, 4, 23 }, { 2008, 3, 23 }, { 2011, 4, 24 }, { 2018, 4, 1 },  { 2019, 4, 21 },
    { 2024, 3, 31 }, { 2025, 4, 20 }, { 2026, 4, 5 },  { 2027, 3, 28 }, { 2038, 4, 25 }, { 2285, 3, 22 },
  };
  for (auto &k : K) {
    MonthDay e = easterSunday(k.y);
    CHECK_MSG(e.month == k.m && e.day == k.d, std::to_string(k.y));
  }
}

TEST(Holiday, easter_agrees_with_another_method_and_stays_in_its_window) {
  for (int y = 1583; y <= 4099; y++) {
    MonthDay e = easterSunday(y), o = easterOudin(y);
    CHECK_MSG(e.month == o.month && e.day == o.day, std::to_string(y));
    // between 22 March and 25 April, and a Sunday
    CHECK_MSG((e.month == 3 && e.day >= 22) || (e.month == 4 && e.day <= 25), std::to_string(y));
    if (y >= 1970 && y <= 2105) CHECK_EQ((int)civilDate(at(y, e.month, e.day)).weekday, 0);
  }
}

// ---------------------------------------------------------------- which day is special
TEST(Holiday, the_fixed_days) {
  CHECK_EQ(holidayOn(2026, 12, 25), (uint8_t)HOL_CHRISTMAS);
  CHECK_EQ(holidayOn(2027, 1, 1), (uint8_t)HOL_NEW_YEAR);
  CHECK_EQ(holidayOn(2027, 2, 14), (uint8_t)HOL_VALENTINE);
  CHECK_EQ(holidayOn(2027, 10, 31), (uint8_t)HOL_HALLOWEEN);
  // the days either side are ordinary
  CHECK_EQ(holidayOn(2027, 1, 2), (uint8_t)HOL_NONE);
  CHECK_EQ(holidayOn(2027, 2, 13), (uint8_t)HOL_NONE);
  CHECK_EQ(holidayOn(2027, 2, 15), (uint8_t)HOL_NONE);
  CHECK_EQ(holidayOn(2027, 10, 30), (uint8_t)HOL_NONE);
  CHECK_EQ(holidayOn(2027, 11, 1), (uint8_t)HOL_NONE);
  CHECK_EQ(holidayOn(2027, 11, 30), (uint8_t)HOL_NONE);
}

TEST(Holiday, the_tree_is_up_all_december_and_christmas_is_the_25th) {
  for (int d = 1; d <= 31; d++)
    CHECK_EQ(holidayOn(2026, 12, (uint8_t)d), (uint8_t)(d == 25 ? HOL_CHRISTMAS : HOL_DECEMBER));
}

TEST(Holiday, easter_sunday_every_year_the_clock_can_hold) {
  for (int y = 2025; y <= 2099; y++) {  // clockEpochValid()'s years
    MonthDay e = easterSunday(y);
    CHECK_EQ(holidayOn(y, e.month, e.day), (uint8_t)HOL_EASTER);
    // Saturday and Monday are not Easter
    uint32_t sun = at(y, e.month, e.day);
    CivilDate sat = civilDate(sun - 86400), mon = civilDate(sun + 86400);
    CHECK_EQ(holidayOn(sat.year, sat.month, sat.day), (uint8_t)HOL_NONE);
    CHECK_EQ(holidayOn(mon.year, mon.month, mon.day), (uint8_t)HOL_NONE);
  }
}

TEST(Holiday, days_turn_at_local_midnight_across_the_year_boundary) {
  Birthday none;
  CHECK_EQ(holidayAt(at(2026, 11, 30, 23, 59, 59), none).day, (uint8_t)HOL_NONE);
  CHECK_EQ(holidayAt(at(2026, 12, 1, 0, 0, 0), none).day, (uint8_t)HOL_DECEMBER);
  CHECK_EQ(holidayAt(at(2026, 12, 24, 23, 59, 59), none).day, (uint8_t)HOL_DECEMBER);
  CHECK_EQ(holidayAt(at(2026, 12, 25, 0, 0, 0), none).day, (uint8_t)HOL_CHRISTMAS);
  CHECK_EQ(holidayAt(at(2026, 12, 25, 23, 59, 59), none).day, (uint8_t)HOL_CHRISTMAS);
  CHECK_EQ(holidayAt(at(2026, 12, 26, 0, 0, 0), none).day, (uint8_t)HOL_DECEMBER);
  CHECK_EQ(holidayAt(at(2026, 12, 31, 23, 59, 59), none).day, (uint8_t)HOL_DECEMBER);
  CHECK_EQ(holidayAt(at(2027, 1, 1, 0, 0, 0), none).day, (uint8_t)HOL_NEW_YEAR);
  CHECK_EQ(holidayAt(at(2027, 1, 1, 23, 59, 59), none).day, (uint8_t)HOL_NEW_YEAR);
  CHECK_EQ(holidayAt(at(2027, 1, 2, 0, 0, 0), none).day, (uint8_t)HOL_NONE);
  CHECK_EQ(holidayAt(at(2027, 10, 31, 0, 0, 0), none).day, (uint8_t)HOL_HALLOWEEN);
  CHECK_EQ(holidayAt(at(2027, 10, 31, 23, 59, 59), none).day, (uint8_t)HOL_HALLOWEEN);
  CHECK_EQ(holidayAt(at(2027, 11, 1, 0, 0, 0), none).day, (uint8_t)HOL_NONE);
}

TEST(Holiday, without_a_trusted_clock_no_day_is_special) {
  HolidayToday t = holidayAt(0, bd(1, 1));
  CHECK_EQ(t.day, (uint8_t)HOL_NONE);
  CHECK(!t.birthday);
  CHECK_EQ(t.greet, (uint8_t)GREET_NONE);
}

TEST(Holiday, each_day_has_its_greeting_and_december_has_none_but_christmas) {
  Birthday none;
  CHECK_EQ(holidayAt(at(2026, 12, 25), none).greet, (uint8_t)GREET_CHRISTMAS);
  CHECK_EQ(holidayAt(at(2026, 12, 24), none).greet, (uint8_t)GREET_NONE);
  CHECK_EQ(holidayAt(at(2026, 12, 31), none).greet, (uint8_t)GREET_NONE);
  CHECK_EQ(holidayAt(at(2027, 1, 1), none).greet, (uint8_t)GREET_NEW_YEAR);
  CHECK_EQ(holidayAt(at(2027, 2, 14), none).greet, (uint8_t)GREET_VALENTINE);
  CHECK_EQ(holidayAt(at(2027, 3, 28), none).greet, (uint8_t)GREET_EASTER);  // Easter 2027
  CHECK_EQ(holidayAt(at(2027, 10, 31), none).greet, (uint8_t)GREET_HALLOWEEN);
  CHECK_EQ(holidayAt(at(2027, 6, 15), none).greet, (uint8_t)GREET_NONE);
  for (int i = 0; i < HOLIDAYS; i++) CHECK(HOLIDAY_KEYS[i] && HOLIDAY_KEYS[i][0]);
}

// ---------------------------------------------------------------- the birthday
TEST(Holiday, the_birthday_greets_and_wins_over_a_holiday_but_both_decorate) {
  HolidayToday t = holidayAt(at(2027, 6, 15), bd(6, 15));
  CHECK(t.birthday);
  CHECK_EQ(t.day, (uint8_t)HOL_NONE);
  CHECK_EQ(t.greet, (uint8_t)GREET_BIRTHDAY);
  t = holidayAt(at(2026, 12, 25), bd(12, 25));  // on Christmas Day
  CHECK(t.birthday);
  CHECK_EQ(t.day, (uint8_t)HOL_CHRISTMAS);  // the tree stays up
  CHECK_EQ(t.greet, (uint8_t)GREET_BIRTHDAY);
  t = holidayAt(at(2026, 12, 10), bd(12, 10));  // in December
  CHECK_EQ(t.day, (uint8_t)HOL_DECEMBER);
  CHECK_EQ(t.greet, (uint8_t)GREET_BIRTHDAY);
  t = holidayAt(at(2027, 3, 28), bd(3, 28));  // on Easter Sunday 2027
  CHECK_EQ(t.day, (uint8_t)HOL_EASTER);
  CHECK_EQ(t.greet, (uint8_t)GREET_BIRTHDAY);
  t = holidayAt(at(2027, 6, 16), bd(6, 15));  // the day after
  CHECK(!t.birthday);
  CHECK_EQ(t.greet, (uint8_t)GREET_NONE);
  CHECK(!holidayAt(at(2027, 6, 15), Birthday()).birthday);  // none set
}

TEST(Holiday, a_29_february_birthday_shows_on_the_28th_in_other_years) {
  Birthday leap = bd(2, 29);
  CHECK(birthdayOn(leap, 2028, 2, 29));
  CHECK(!birthdayOn(leap, 2028, 2, 28));  // a leap year has its own 29th
  CHECK(birthdayOn(leap, 2027, 2, 28));
  CHECK(!birthdayOn(leap, 2027, 3, 1));
  CHECK(birthdayOn(leap, 2100, 2, 28));  // 2100 is not a leap year
  CHECK(holidayAt(at(2027, 2, 28), leap).birthday);
  CHECK(!holidayAt(at(2027, 3, 1), leap).birthday);
  CHECK(holidayAt(at(2028, 2, 29), leap).birthday);
  CHECK(!holidayAt(at(2028, 2, 28), leap).birthday);
  // a 28 February birthday is only on the 28th
  CHECK(birthdayOn(bd(2, 28), 2028, 2, 28));
  CHECK(!birthdayOn(bd(2, 28), 2028, 2, 29));
}

TEST(Holiday, a_birthday_is_a_real_day_of_its_month) {
  CHECK(birthdayValid(1, 1));
  CHECK(birthdayValid(2, 29));
  CHECK(birthdayValid(4, 30));
  CHECK(birthdayValid(12, 31));
  CHECK(!birthdayValid(0, 1));
  CHECK(!birthdayValid(13, 1));
  CHECK(!birthdayValid(1, 0));
  CHECK(!birthdayValid(1, 32));
  CHECK(!birthdayValid(2, 30));
  CHECK(!birthdayValid(4, 31));
  CHECK(!birthdayValid(-1, 5));
  CHECK(!birthdayValid(255, 1));
  // an invalid one is never on any day
  CHECK(!birthdayOn(bd(2, 30), 2027, 2, 30));
  CHECK(!birthdayOn(bd(0, 0), 2027, 1, 1));
}

TEST(Holiday, the_nvs_value_reads_back_only_as_a_real_date) {
  int valid = 0;
  for (uint32_t v = 0; v <= 0xFFFF; v++) {
    Birthday b = birthdayUnpack((uint16_t)v);
    bool real = birthdayValid((int)(v / 100), (int)(v % 100));
    if (real) {
      valid++;
      CHECK_EQ((int)b.month * 100 + b.day, (int)v);
      CHECK_EQ(birthdayPack(b), (uint16_t)v);
    } else {
      CHECK_MSG(b.month == 0 && b.day == 0, std::to_string(v));
    }
  }
  CHECK_EQ(valid, 366);  // every day of a leap year
  CHECK_EQ(birthdayPack(bd(12, 25)), (uint16_t)1225);
  CHECK_EQ(birthdayPack(bd(3, 7)), (uint16_t)307);
}

// ---------------------------------------------------------------- BDAY (serial)
TEST(Holiday, bday_sets_queries_and_clears) {
  Birthday b;
  CHECK_EQ(bdayParse("BDAY?", b), BDAY_CMD_QUERY);
  CHECK_EQ(bdayParse("BDAY 12 25", b), BDAY_CMD_SET);
  CHECK(b.month == 12 && b.day == 25);
  CHECK_EQ(bdayParse("BDAY  2   29", b), BDAY_CMD_SET);  // spaces
  CHECK(b.month == 2 && b.day == 29);
  CHECK_EQ(bdayParse("BDAY 03 07", b), BDAY_CMD_SET);  // leading zeros
  CHECK(b.month == 3 && b.day == 7);
  CHECK_EQ(bdayParse("BDAY 0 0", b), BDAY_CMD_SET);  // clears
  CHECK(b.month == 0 && b.day == 0);
  CHECK_EQ(bdayParse("BDAY 00 00", b), BDAY_CMD_SET);
  CHECK(b.month == 0 && b.day == 0);
}

TEST(Holiday, bday_refuses_anything_but_a_real_date) {
  const char *BAD[] = { "BDAY",     "BDAY 12",   "BDAY 13 1",  "BDAY 2 30", "BDAY 4 31", "BDAY 0 5",
                        "BDAY 5 0", "BDAY 1 2 3", "BDAY 123 1", "BDAY 1 123", "BDAY 1 2x", "BDAY -1 2",
                        "BDAY 1,2", "BDAY 1-2",  "BDAY x y",   "BDAY 1  ",  "BDAY 0 0 0",
                        "BDAY 001 1", "BDAY 1 001", "BDAY 99999999999 1", "BDAY 1 99999999999" };
  for (const char *line : BAD) {
    Birthday b = bd(9, 9);
    CHECK_MSG(bdayParse(line, b) == BDAY_CMD_BAD, line);
    CHECK_MSG(b.month == 0 && b.day == 0, line);  // nothing to store
  }
}

TEST(Holiday, other_lines_are_not_bday) {
  const char *OTHER[] = { "", "BDA", "BDAYX", "BDAY?x", "BDAY? ", "bday 1 2", "Bday 1 2", "BDAY_1_2", "XBDAY 1 2" };
  for (const char *line : OTHER) {
    Birthday b;
    CHECK_MSG(bdayParse(line, b) == BDAY_CMD_NONE, line);
  }
}

// ---------------------------------------------------------------- the NVS key
TEST(Holiday, the_birthday_is_stored_cleared_and_loaded) {
  mockNvsReset();
  birthdayLoad();
  CHECK_EQ(birthday().month, 0);  // none set
  CHECK(birthdaySet(bd(3, 14)));
  CHECK(birthday().month == 3 && birthday().day == 14);
  Preferences p;
  p.begin("tamapoke", true);
  CHECK_EQ(p.getUShort("bday", 0), (uint16_t)314);
  p.end();
  putRaw(1225);  // proof that birthdayLoad() reads NVS rather than keeping its copy
  birthdayLoad();
  CHECK(birthday().month == 12 && birthday().day == 25);
  CHECK(birthdaySet(Birthday()));  // BDAY 0 0
  CHECK_EQ(birthday().month, 0);
  p.begin("tamapoke", true);
  CHECK(!p.isKey("bday"));  // the key is gone
  p.end();
  CHECK(birthdaySet(Birthday()));  // clearing with nothing stored is fine
  birthdayLoad();
  CHECK_EQ(birthday().month, 0);
  mockNvsReset();
}

TEST(Holiday, a_stored_value_that_is_not_a_date_loads_as_none) {
  for (uint16_t v : { (uint16_t)0, (uint16_t)1, (uint16_t)100, (uint16_t)230, (uint16_t)431, (uint16_t)1232,
                      (uint16_t)1300, (uint16_t)1301, (uint16_t)65535 }) {
    mockNvsReset();
    putRaw(1225);
    birthdayLoad();
    putRaw(v);
    birthdayLoad();
    CHECK_MSG(birthday().month == 0 && birthday().day == 0, std::to_string(v));
  }
  // a key of another type (an older or other firmware): no birthday
  mockNvsReset();
  Preferences p;
  p.begin("tamapoke", false);
  p.putString("bday", "12-25");
  p.end();
  birthdayLoad();
  CHECK_EQ(birthday().month, 0);
  mockNvsReset();
}

TEST(Holiday, a_failed_write_keeps_the_old_birthday) {
  mockNvsReset();
  CHECK(birthdaySet(bd(5, 1)));
  mockNvsFailWrites(true);
  CHECK(!birthdaySet(bd(6, 2)));
  CHECK(birthday().month == 5 && birthday().day == 1);
  CHECK(!birthdaySet(Birthday()));  // nor can it be cleared
  CHECK(birthday().month == 5 && birthday().day == 1);
  mockNvsFailWrites(false);
  birthdayLoad();
  CHECK(birthday().month == 5 && birthday().day == 1);
  CHECK(!birthdaySet(bd(2, 30)));  // never stores an invalid date
  CHECK(birthday().month == 5 && birthday().day == 1);
  mockNvsReset();
}

// ---------------------------------------------------------------- the greeting
TEST(Holiday, the_first_look_on_the_day_gets_the_greeting) {
  HolidayGreeter g;
  uint32_t e = at(2026, 12, 25, 9, 0);
  CHECK(g.due(e, false, GREET_CHRISTMAS));
  CHECK(!g.due(e, false, GREET_NONE));  // not a greeting day
  CHECK(!g.due(e, true, GREET_CHRISTMAS));  // quiet hours
  CHECK(!g.due(0, false, GREET_CHRISTMAS));  // no trusted clock
  CHECK(!g.due(e, false, GREETS));  // out of range
  g.said(e);
  CHECK(!g.due(0, false, GREET_CHRISTMAS));  // the clock lost after a greeting: still none
}

TEST(Holiday, at_most_three_a_day_30_min_apart) {
  HolidayGreeter g;
  uint32_t e = at(2026, 12, 25, 9, 0);
  g.said(e);
  CHECK_EQ(g.count(), 1);
  CHECK(!g.due(e + 60, false, GREET_CHRISTMAS));
  CHECK(!g.due(e + HOLIDAY_GREET_GAP_S - 1, false, GREET_CHRISTMAS));
  CHECK(g.due(e + HOLIDAY_GREET_GAP_S, false, GREET_CHRISTMAS));
  g.said(e + 3600);
  CHECK(!g.due(e + 3600 + HOLIDAY_GREET_GAP_S - 30, false, GREET_CHRISTMAS));
  CHECK(g.due(e + 3 * 3600, false, GREET_CHRISTMAS));
  g.said(e + 3 * 3600);
  CHECK_EQ(g.count(), 3);
  CHECK(!g.due(e + 6 * 3600, false, GREET_CHRISTMAS));  // three said: no more today
  CHECK(!g.due(at(2026, 12, 25, 23, 59, 59), false, GREET_CHRISTMAS));
}

TEST(Holiday, a_new_day_starts_the_count_again) {
  HolidayGreeter g;
  uint32_t e = at(2026, 12, 31, 12, 0);
  for (int i = 0; i < HOLIDAY_GREETS; i++) g.said(e + i * 3600);
  CHECK(!g.due(e + 5 * 3600, false, GREET_BIRTHDAY));
  // a new day starts fresh: no 30-minute gap is carried over midnight (a greeting at 23:55
  // does not hold back the next day's at 00:10; in practice midnight is quiet hours)
  uint32_t late = at(2026, 12, 31, 23, 55);
  HolidayGreeter h;
  h.said(late);
  CHECK(h.due(at(2027, 1, 1, 0, 10), false, GREET_NEW_YEAR));
  uint32_t ny = at(2027, 1, 1, 0, 10);
  CHECK(g.due(ny, false, GREET_NEW_YEAR));
  g.said(ny);
  CHECK_EQ(g.count(), 1);
}

TEST(Holiday, a_clock_set_back_does_not_block_the_greeting) {
  HolidayGreeter g;
  uint32_t e = at(2027, 2, 14, 18, 0);
  g.said(e);
  CHECK(g.due(e - 3600, false, GREET_VALENTINE));  // the same day, earlier: it can't be measured
  g.said(e - 3600);
  g.said(e);
  CHECK(!g.due(e - 7200, false, GREET_VALENTINE));  // but three is still the most a day
  CHECK(g.due(at(2027, 2, 13, 18, 0), false, GREET_VALENTINE));  // another day
}
