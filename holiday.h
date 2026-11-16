// Holidays and the player's birthday (#26): on a special day the scene is decorated and the
// pet greets the player by name in a speech bubble (bubble.h, #23).
//
// The idea comes from Mu-Muu/TamaPoke (MIT; holiday.h and holiday.cpp, and the seasonal
// decorations in scene.ino), rewritten here offline, without their lunar calendar or any
// network feature.
//
//  - The days (the owner's decisions on #26): Christmas, with the tree up all December and
//    the greeting on 25 Dec; New Year (1 Jan); Valentine's Day (14 Feb); Easter Sunday,
//    worked out for each year; Halloween (31 Oct); and the player's birthday. No Mother's or
//    Father's Day, so no rules by country.
//  - The date is the clock's, in local time: the RTC keeps local wall-clock time as if it
//    were UTC (bedtime.h), so a day runs from midnight to midnight, epoch / 86400, as for
//    the seasons and the standby clock's date (civilDate(), weather.h). Without a trusted
//    clock (never set, or reseeded after a power cut and not set since: bedEdge.stale())
//    no day is special: no decorations and no greeting.
//  - The birthday is the player's, set from the owner's computer with the serial command
//    `BDAY <month> <day>` (`BDAY 0 0` clears it). It never goes in the repo, which is public.
//    It lives in NVS namespace `tamapoke`, key "bday" (month * 100 + day), and is checked
//    when it is read. 29 Feb shows on 28 Feb in a year without one.
//  - The birthday wins over a holiday on the same day for the greeting; the decorations of
//    both show (the balloons beside the Christmas tree, for example).
//  - Looks only: nothing here changes care or rewards.
//
// Pure logic (no hardware, no clock reads), tested on the PC (test/test_holiday.cpp).
// holiday.cpp keeps the stored birthday; scene.cpp draws the decorations and bubble.cpp
// says the greeting.
#pragma once
#include <stdint.h>
#include "weather.h"  // CivilDate, civilDate()

// ---------------------------------------------------------------- the calendar
inline bool holidayLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

// The days in month `m` (1-12) of year `y`; 0 for any other month.
inline uint8_t holidayMonthDays(int y, int m) {
  static const uint8_t DAYS[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m < 1 || m > 12) return 0;
  return (uint8_t)(m == 2 && holidayLeapYear(y) ? 29 : DAYS[m]);
}

struct MonthDay {
  uint8_t month;  // 1-12
  uint8_t day;    // 1-31
};

// Easter Sunday of year `y` in the Gregorian calendar (the "anonymous" algorithm, in Meeus,
// Astronomical Algorithms). Always between 22 March and 25 April.
inline MonthDay easterSunday(int y) {
  int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4;
  int f = (b + 8) / 25, g = (b - f + 1) / 3;
  int h = (19 * a + b - d - g + 15) % 30;
  int i = c / 4, k = c % 4;
  int l = (32 + 2 * e + 2 * i - h - k) % 7;
  int m = (a + 11 * h + 22 * l) / 451;
  int n = h + l - 7 * m + 114;
  MonthDay r;
  r.month = (uint8_t)(n / 31);
  r.day = (uint8_t)(n % 31 + 1);
  return r;
}

// ---------------------------------------------------------------- the holidays
// What a date is. HOL_DECEMBER is every December day but Christmas Day: the tree is up, but
// there is no greeting.
enum Holiday : uint8_t {
  HOL_NONE, HOL_DECEMBER, HOL_CHRISTMAS, HOL_NEW_YEAR, HOL_VALENTINE, HOL_EASTER, HOL_HALLOWEEN, HOLIDAYS
};
// each day's name in BDAY? (serialcmd.cpp)
inline const char *const HOLIDAY_KEYS[HOLIDAYS] = {
  "none", "december", "christmas", "newyear", "valentine", "easter", "halloween",
};

// The holiday on a date. Easter (22 Mar - 25 Apr) never falls on one of the others.
inline uint8_t holidayOn(int year, uint8_t month, uint8_t day) {
  if (month == 12) return day == 25 ? (uint8_t)HOL_CHRISTMAS : (uint8_t)HOL_DECEMBER;
  if (month == 1 && day == 1) return HOL_NEW_YEAR;
  if (month == 2 && day == 14) return HOL_VALENTINE;
  if (month == 10 && day == 31) return HOL_HALLOWEEN;
  MonthDay e = easterSunday(year);
  if (month == e.month && day == e.day) return HOL_EASTER;
  return HOL_NONE;
}

// ---------------------------------------------------------------- the birthday
struct Birthday {
  uint8_t month = 0;  // 1-12; 0: no birthday set
  uint8_t day = 0;    // 1-31
};

// A birthday the player can have: a real day of that month in some year (so 29 Feb is one).
inline bool birthdayValid(int month, int day) {
  return day >= 1 && day <= holidayMonthDays(2000, month);  // 2000: a leap year
}

// The NVS value: month * 100 + day (1225 is 25 Dec), so a dump reads plainly. 0: none.
inline uint16_t birthdayPack(Birthday b) { return (uint16_t)(b.month * 100 + b.day); }
// The range check on load: anything but a valid date (a damaged page, another firmware's
// key) is no birthday.
inline Birthday birthdayUnpack(uint16_t v) {
  Birthday b;
  int m = v / 100, d = v % 100;
  if (birthdayValid(m, d)) {
    b.month = (uint8_t)m;
    b.day = (uint8_t)d;
  }
  return b;
}

// Is the birthday on this date? 29 Feb is on 28 Feb in a year without a 29 Feb.
inline bool birthdayOn(Birthday b, int year, uint8_t month, uint8_t day) {
  if (!birthdayValid(b.month, b.day)) return false;
  if (b.month == 2 && b.day == 29 && !holidayLeapYear(year)) return month == 2 && day == 28;
  return month == b.month && day == b.day;
}

// The serial commands about the birthday, as handleSerial() reads them (serialcmd.cpp).
enum BdayCmd : uint8_t {
  BDAY_CMD_NONE = 0,  // not ours
  BDAY_CMD_QUERY,     // "BDAY?"
  BDAY_CMD_SET,       // "BDAY <month> <day>": the birthday to store is in `out` (0 0: clear it)
  BDAY_CMD_BAD,       // "BDAY" with anything else: refused, nothing changes
};

// Reads one serial line (already trimmed by handleSerial). Two numbers of one or two digits
// after "BDAY", separated by spaces: a valid date, or 0 0 to clear. "BDAYX" or "bday 1 2" is
// not ours; "BDAY", "BDAY 2 30", "BDAY 13 1", "BDAY 0 5" or "BDAY 1 2 3" is BDAY_CMD_BAD.
inline BdayCmd bdayParse(const char *line, Birthday &out) {
  out = Birthday();
  const char *p = line;
  for (const char *k = "BDAY"; *k; k++, p++)
    if (*p != *k) return BDAY_CMD_NONE;
  if (p[0] == '?' && p[1] == 0) return BDAY_CMD_QUERY;
  if (*p != ' ' && *p != 0) return BDAY_CMD_NONE;
  int v[2];
  for (int n = 0; n < 2; n++) {
    if (*p != ' ') return BDAY_CMD_BAD;
    while (*p == ' ') p++;
    int digits = 0;
    v[n] = 0;
    while (*p >= '0' && *p <= '9') {
      if (++digits > 2) return BDAY_CMD_BAD;
      v[n] = v[n] * 10 + (*p++ - '0');
    }
    if (!digits) return BDAY_CMD_BAD;
  }
  if (*p) return BDAY_CMD_BAD;  // a third word, or a stray character
  if (v[0] == 0 && v[1] == 0) return BDAY_CMD_SET;  // clear
  if (!birthdayValid(v[0], v[1])) return BDAY_CMD_BAD;
  out.month = (uint8_t)v[0];
  out.day = (uint8_t)v[1];
  return BDAY_CMD_SET;
}

// ---------------------------------------------------------------- today
// What the pet says on the day, in the order of its speech bubble lines (BUB_CHRISTMAS...,
// bubble.h).
enum Greet : uint8_t {
  GREET_NONE, GREET_CHRISTMAS, GREET_NEW_YEAR, GREET_VALENTINE, GREET_EASTER, GREET_HALLOWEEN, GREET_BIRTHDAY, GREETS
};

struct HolidayToday {
  uint8_t day = HOL_NONE;  // the holiday, for its decorations
  bool birthday = false;   // the player's birthday: the balloons too
  uint8_t greet = GREET_NONE;
};

// The day at `epoch`, local time (0: no trusted clock, sceneEpoch() in weather.h, so nothing).
// The birthday's greeting wins over a holiday's; December's other days have none.
inline HolidayToday holidayAt(uint32_t epoch, Birthday b) {
  HolidayToday t;
  if (!epoch) return t;
  CivilDate c = civilDate(epoch);
  t.day = holidayOn(c.year, c.month, c.day);
  t.birthday = birthdayOn(b, c.year, c.month, c.day);
  static const uint8_t GREET_OF[HOLIDAYS] = { GREET_NONE,      GREET_NONE,   GREET_CHRISTMAS, GREET_NEW_YEAR,
                                              GREET_VALENTINE, GREET_EASTER, GREET_HALLOWEEN };
  t.greet = t.birthday ? (uint8_t)GREET_BIRTHDAY : GREET_OF[t.day];
  return t;
}

// ---------------------------------------------------------------- the greeting
// The greeting takes the place of the next speech bubble that comes (BubbleTimer: 3 s after
// the screen wakes, then every 1-2 min), so the player gets it the first few times they look
// at the pet that day: at most HOLIDAY_GREETS a day, at least HOLIDAY_GREET_GAP_S apart, so
// a few minutes spent with the pet do not use them all up. Never in quiet hours (the bedtime
// window, or 22:00-07:00 with bedtime off: attnQuietAt, attention.h) and never without a
// trusted clock. The count is kept in RAM only: a restart on the day may greet again.
#define HOLIDAY_GREETS 3
#define HOLIDAY_GREET_GAP_S (30 * 60UL)

struct HolidayGreeter {
  // Is the greeting the next line? `epoch`: the trusted clock (0: none), `quiet`: quiet
  // hours now, `greet`: today's (holidayAt().greet).
  bool due(uint32_t epoch, bool quiet, uint8_t greet) const {
    if (!epoch || quiet || greet == GREET_NONE || greet >= GREETS) return false;
    if (epoch / 86400UL != day_) return true;  // a new day: none said yet
    // (a clock set back reads as long ago: the unsigned difference is large)
    return count_ < HOLIDAY_GREETS && epoch - last_ >= HOLIDAY_GREET_GAP_S;
  }
  // The greeting was said at `epoch`.
  void said(uint32_t epoch) {
    uint32_t d = epoch / 86400UL;
    if (d != day_) {
      day_ = d;
      count_ = 0;
    }
    count_++;
    last_ = epoch;
  }
  uint8_t count() const { return count_; }

 private:
  uint32_t day_ = 0, last_ = 0;  // day 0 is 1 Jan 1970: never a trusted clock's
  uint8_t count_ = 0;
};

// ---- the stored birthday (holiday.cpp) ----
// (Today on the scene's clock is holidayNow(), scene.h.)
void birthdayLoad();             // setup(): read and check the NVS copy
Birthday birthday();             // the stored one; month 0 when none
bool birthdaySet(Birthday b);    // store (month 0 clears); false if the flash write failed (nothing changes)
