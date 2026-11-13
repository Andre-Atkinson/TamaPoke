// Shim de la clase Print de Arduino: print()/println()/printf() sobre write().
// La usan Serial (shim) y Arduino_GFX (la libreria real, compilada en el host).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "WString.h"

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class __FlashStringHelper;
#define F(s) ((const __FlashStringHelper *)(s))

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t *buf, size_t n) {
    size_t k = 0;
    while (n--) k += write(*buf++);
    return k;
  }
  size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
  size_t write(const char *buf, size_t n) { return write((const uint8_t *)buf, n); }

  size_t print(const char *s) { return write(s); }
  size_t print(const __FlashStringHelper *s) { return write((const char *)s); }
  size_t print(const String &s) { return write(s.c_str()); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(unsigned char v, int base = DEC) { return print((unsigned long)v, base); }
  size_t print(int v, int base = DEC) { return print((long)v, base); }
  size_t print(unsigned int v, int base = DEC) { return print((unsigned long)v, base); }
  size_t print(long v, int base = DEC) {
    if (base == DEC) return printf("%ld", v);
    return print((unsigned long)v, base);
  }
  size_t print(unsigned long v, int base = DEC) {
    if (base == HEX) return printf("%lX", v);
    if (base == OCT) return printf("%lo", v);
    if (base == BIN) {
      char buf[65];
      int i = 64;
      buf[i] = 0;
      do { buf[--i] = (char)('0' + (v & 1)); v >>= 1; } while (v && i > 0);
      return write(buf + i);
    }
    return printf("%lu", v);
  }
  size_t print(long long v) { return printf("%lld", v); }
  size_t print(unsigned long long v) { return printf("%llu", v); }
  size_t print(double v, int digits = 2) { return printf("%.*f", digits, v); }

  size_t println() { return write("\n"); }  // "\r\n" en placa; en el host, logs legibles
  template <class T> size_t println(const T &v) { size_t n = print(v); return n + println(); }
  template <class T> size_t println(const T &v, int fmt) { size_t n = print(v, fmt); return n + println(); }

  size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    char small[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if ((size_t)n < sizeof small) return write((const uint8_t *)small, (size_t)n);
    char *big = new char[(size_t)n + 1];
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    size_t k = write((const uint8_t *)big, (size_t)n);
    delete[] big;
    return k;
  }
};
