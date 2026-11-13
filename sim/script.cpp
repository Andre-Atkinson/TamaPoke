#include "script.h"
#include <ctype.h>
#include <stdlib.h>
#include <sstream>

static const char *const LANG_CODES[] = { "es", "en", "fr", "de", "it", "pt", "ja", "ko" };
static const int LANG_N = (int)(sizeof LANG_CODES / sizeof LANG_CODES[0]);

int simLangIndex(const std::string &code) {
  for (int i = 0; i < LANG_N; i++)
    if (code == LANG_CODES[i]) return i;
  return -1;
}

const char *simLangCode(int index) { return index >= 0 && index < LANG_N ? LANG_CODES[index] : "?"; }

static bool allDigits(const std::string &s) {
  if (s.empty()) return false;
  for (char c : s)
    if (!isdigit((unsigned char)c)) return false;
  return true;
}

bool simParseDuration(const std::string &s, uint64_t &ms) {
  if (s.empty()) return false;
  if (allDigits(s)) {
    if (s.size() > 15) return false;
    ms = strtoull(s.c_str(), nullptr, 10);
    return true;
  }
  uint64_t total = 0;
  size_t i = 0;
  int lastRank = 5;  // las unidades deben ir de mayor a menor: 1h30m, no 30m1h
  while (i < s.size()) {
    size_t j = i;
    while (j < s.size() && isdigit((unsigned char)s[j])) j++;
    if (j == i || j - i > 9) return false;
    uint64_t n = strtoull(s.substr(i, j - i).c_str(), nullptr, 10);
    size_t k = j;
    while (k < s.size() && isalpha((unsigned char)s[k])) k++;
    std::string unit = s.substr(j, k - j);
    uint64_t mult;
    int rank;
    if (unit == "d") { mult = 86400000ULL; rank = 4; }
    else if (unit == "h") { mult = 3600000ULL; rank = 3; }
    else if (unit == "m") { mult = 60000ULL; rank = 2; }
    else if (unit == "s") { mult = 1000ULL; rank = 1; }
    else if (unit == "ms") { mult = 1ULL; rank = 0; }
    else return false;
    if (rank >= lastRank) return false;
    lastRank = rank;
    total += n * mult;
    i = k;
  }
  ms = total;
  return true;
}

// dias desde 1970-01-01 (algoritmo de Howard Hinnant, calendario gregoriano)
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

static bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

bool simParseClock(const std::string &s, uint32_t &epoch) {
  if (allDigits(s)) {
    if (s.size() > 10) return false;
    uint64_t v = strtoull(s.c_str(), nullptr, 10);
    if (v > 0xFFFFFFFFULL) return false;
    epoch = (uint32_t)v;
    return true;
  }
  // forma exacta: YYYY-MM-DD HH:MM o YYYY-MM-DDTHH:MM, con :SS opcional
  static const char SHAPE[] = "dddd-dd-dd?dd:dd:dd";
  if (s.size() != 16 && s.size() != 19) return false;
  for (size_t i = 0; i < s.size(); i++) {
    char want = SHAPE[i], got = s[i];
    if (want == 'd' ? !isdigit((unsigned char)got) : want == '?' ? (got != ' ' && got != 'T') : got != want)
      return false;
  }
  auto num = [&](size_t at, size_t len) { return atoi(s.substr(at, len).c_str()); };
  int y = num(0, 4), mo = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2);
  int sec = s.size() == 19 ? num(17, 2) : 0;
  static const int DIM[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (y < 1970 || y > 2105 || mo < 1 || mo > 12 || h > 23 || h < 0 || mi < 0 || mi > 59 || sec < 0 || sec > 59)
    return false;
  int dim = DIM[mo - 1] + (mo == 2 && leap(y) ? 1 : 0);
  if (d < 1 || d > dim) return false;
  int64_t e = daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + sec;
  if (e < 0 || e > 0xFFFFFFFFLL) return false;
  epoch = (uint32_t)e;
  return true;
}

static std::vector<std::string> words(const std::string &line) {
  std::vector<std::string> w;
  std::istringstream in(line);
  std::string t;
  while (in >> t) w.push_back(t);
  return w;
}

static bool toInt(const std::string &s, int32_t &v, int32_t lo, int32_t hi) {
  if (s.empty()) return false;
  size_t i = (s[0] == '-') ? 1 : 0;
  if (i == s.size() || s.size() > 10) return false;
  for (; i < s.size(); i++)
    if (!isdigit((unsigned char)s[i])) return false;
  long long x = strtoll(s.c_str(), nullptr, 10);
  if (x < lo || x > hi) return false;
  v = (int32_t)x;
  return true;
}

static bool onOff(const std::string &s, int32_t &v) {
  if (s == "on") { v = 1; return true; }
  if (s == "off") { v = 0; return true; }
  return false;
}

SimScript simParseScript(const std::string &text, const std::string &name) {
  SimScript out;
  std::istringstream in(text);
  std::string raw;
  int lineNo = 0;
  bool running = false;  // ya hay un paso que hace correr el firmware
  auto fail = [&](const std::string &why) {
    out.error = name + ":" + std::to_string(lineNo) + ": " + why;
    out.cmds.clear();
    return out;
  };
  while (std::getline(in, raw)) {
    lineNo++;
    // '#' empieza un comentario; "serial" lee el resto de la linea cruda (abajo),
    // porque un comando serie puede llevar '#'
    std::string line = raw.substr(0, raw.find('#'));
    std::vector<std::string> w = words(line);
    if (w.empty()) continue;
    const std::string &op = w[0];
    SimCmd c;
    c.line = lineNo;
    size_t n = w.size();
    if (op == "lang") {
      if (n != 2) return fail("usage: lang CODE");
      if (running) return fail("lang must come before the first step that runs the firmware");
      if (simLangIndex(w[1]) < 0) return fail("unknown language '" + w[1] + "' (es en fr de it pt ja ko)");
      c.op = OP_LANG;
      c.a = simLangIndex(w[1]);
      c.text = w[1];
    } else if (op == "clock") {
      if (n < 2) return fail("usage: clock YYYY-MM-DD HH:MM | none");
      if (running) return fail("clock must come before the first step that runs the firmware");
      c.op = OP_CLOCK;
      std::string v = w[1];
      for (size_t i = 2; i < n; i++) v += " " + w[i];
      uint32_t e = 0;
      if (v != "none" && !simParseClock(v, e)) return fail("bad clock '" + v + "'");
      c.ms = e;
    } else if (op == "battery") {
      if (n != 2) return fail("usage: battery PERCENT | none");
      c.op = OP_BATTERY;
      if (w[1] == "none") c.a = -1;
      else if (!toInt(w[1], c.a, 0, 100)) return fail("battery must be 0-100 or none");
    } else if (op == "usb" || op == "charging") {
      if (n != 2 || !onOff(w[1], c.a)) return fail("usage: " + op + " on|off");
      c.op = op == "usb" ? OP_USB : OP_CHARGING;
    } else if (op == "tap") {
      if (n != 3 || !toInt(w[1], c.a, 0, 465) || !toInt(w[2], c.b, 0, 465))
        return fail("usage: tap X Y (0-465)");
      c.op = OP_TAP;
      running = true;
    } else if (op == "hold") {
      if (n != 4 || !toInt(w[1], c.a, 0, 465) || !toInt(w[2], c.b, 0, 465) || !simParseDuration(w[3], c.ms))
        return fail("usage: hold X Y DURATION");
      c.op = OP_HOLD;
      running = true;
    } else if (op == "swipe") {
      c.op = OP_SWIPE;
      c.ms = 240;
      if (n == 2) {
        // desde el centro, 160 px: supera el umbral de 80 px de handleTouch()
        const int32_t C = 233, D = 80;
        if (w[1] == "left") { c.a = C + D; c.b = C; c.c = C - D; c.d = C; }
        else if (w[1] == "right") { c.a = C - D; c.b = C; c.c = C + D; c.d = C; }
        else if (w[1] == "up") { c.a = C; c.b = C + D; c.c = C; c.d = C - D; }
        else if (w[1] == "down") { c.a = C; c.b = C - D; c.c = C; c.d = C + D; }
        else return fail("usage: swipe left|right|up|down");
      } else if (n == 5 || n == 6) {
        if (!toInt(w[1], c.a, 0, 465) || !toInt(w[2], c.b, 0, 465) || !toInt(w[3], c.c, 0, 465) ||
            !toInt(w[4], c.d, 0, 465))
          return fail("usage: swipe X0 Y0 X1 Y1 [DURATION]");
        if (n == 6 && !simParseDuration(w[5], c.ms)) return fail("bad duration '" + w[5] + "'");
      } else {
        return fail("usage: swipe left|right|up|down or swipe X0 Y0 X1 Y1 [DURATION]");
      }
      running = true;
    } else if (op == "press") {
      if (n < 2 || n > 3) return fail("usage: press pwr|boot [COUNT]");
      if (w[1] == "pwr") c.op = OP_PRESS_PWR;
      else if (w[1] == "boot") c.op = OP_PRESS_BOOT;
      else return fail("usage: press pwr|boot [COUNT]");
      c.a = 1;
      if (n == 3 && !toInt(w[2], c.a, 1, 10)) return fail("press count must be 1-10");
      running = true;
    } else if (op == "wait" || op == "advance") {
      if (n != 2 || !simParseDuration(w[1], c.ms)) return fail("usage: " + op + " DURATION (500ms, 2s, 1h30m)");
      c.op = OP_WAIT;
      running = true;
    } else if (op == "serial") {
      size_t at = raw.find("serial");
      std::string rest = raw.substr(at + 6);
      size_t a = rest.find_first_not_of(" \t");
      size_t b = rest.find_last_not_of(" \t\r\n");
      if (a == std::string::npos) return fail("usage: serial LINE");
      c.op = OP_SERIAL;
      c.text = rest.substr(a, b - a + 1);
      running = true;
    } else if (op == "shot") {
      if (n != 2) return fail("usage: shot NAME");
      for (char ch : w[1])
        if (!isalnum((unsigned char)ch) && ch != '-' && ch != '_' && ch != '.')
          return fail("shot names use letters, digits, '-', '_' and '.' only");
      c.op = OP_SHOT;
      c.text = w[1];
      running = true;
    } else {
      return fail("unknown step '" + op + "'");
    }
    out.cmds.push_back(c);
  }
  return out;
}
