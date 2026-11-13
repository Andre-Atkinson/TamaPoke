// Shim de la clase String de Arduino sobre std::string. Solo los metodos que usan
// el firmware (consola serie de TamaPoke.ino y sdmon.cpp) y algunos habituales.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string>

class String {
public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  explicit String(char c) : s_(1, c) {}
  explicit String(int v, unsigned char base = 10) { fromLong(v, base); }
  explicit String(unsigned int v, unsigned char base = 10) { fromULong(v, base); }
  explicit String(long v, unsigned char base = 10) { fromLong(v, base); }
  explicit String(unsigned long v, unsigned char base = 10) { fromULong(v, base); }

  const char *c_str() const { return s_.c_str(); }
  unsigned int length() const { return (unsigned int)s_.size(); }
  char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
  char operator[](unsigned int i) const { return charAt(i); }

  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *o) { s_ += (o ? o : ""); return *this; }
  String &operator+=(char c) { s_ += c; return *this; }
  bool concat(const String &o) { s_ += o.s_; return true; }

  bool equals(const String &o) const { return s_ == o.s_; }
  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *o) const { return s_ == (o ? o : ""); }
  bool operator!=(const String &o) const { return !(*this == o); }
  bool operator!=(const char *o) const { return !(*this == o); }

  bool startsWith(const String &p) const { return s_.compare(0, p.s_.size(), p.s_) == 0; }
  bool endsWith(const String &p) const {
    return p.s_.size() <= s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
  }
  int indexOf(char c, unsigned int from = 0) const { return pos(s_.find(c, from)); }
  int indexOf(const String &p, unsigned int from = 0) const { return pos(s_.find(p.s_, from)); }
  int lastIndexOf(char c) const { return pos(s_.rfind(c)); }
  int lastIndexOf(const String &p) const { return pos(s_.rfind(p.s_)); }
  String substring(unsigned int from) const { return from < s_.size() ? String(s_.substr(from)) : String(); }
  String substring(unsigned int from, unsigned int to) const {
    if (from > to) { unsigned int t = from; from = to; to = t; }
    if (from >= s_.size()) return String();
    return String(s_.substr(from, to - from));
  }
  long toInt() const { return strtol(s_.c_str(), nullptr, 10); }
  void trim() {
    size_t a = s_.find_first_not_of(" \t\r\n\f\v");
    if (a == std::string::npos) { s_.clear(); return; }
    size_t b = s_.find_last_not_of(" \t\r\n\f\v");
    s_ = s_.substr(a, b - a + 1);
  }
  void toUpperCase() { for (char &c : s_) c = (char)toupper((unsigned char)c); }
  void toLowerCase() { for (char &c : s_) c = (char)tolower((unsigned char)c); }

  friend String operator+(const String &a, const String &b) { return String(a.s_ + b.s_); }
  friend String operator+(const char *a, const String &b) { return String(std::string(a ? a : "") + b.s_); }
  friend String operator+(const String &a, const char *b) { return String(a.s_ + (b ? b : "")); }

private:
  std::string s_;
  static int pos(size_t p) { return p == std::string::npos ? -1 : (int)p; }
  void fromLong(long v, unsigned char base) {
    if (v < 0 && base == 10) { fromULong((unsigned long)(-(v + 1)) + 1, base); s_.insert(s_.begin(), '-'); }
    else fromULong((unsigned long)v, base);
  }
  void fromULong(unsigned long v, unsigned char base) {
    if (base < 2 || base > 36) base = 10;
    char buf[72];
    int i = 71;
    buf[i] = 0;
    do { int d = (int)(v % base); buf[--i] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= base; } while (v && i > 0);
    s_ = buf + i;
  }
};
