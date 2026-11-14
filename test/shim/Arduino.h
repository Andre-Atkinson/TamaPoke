// Shim minimo de Arduino.h para ejecutar la logica del firmware en el host.
// Cubre lo que usan pet.cpp / i18n.cpp (tests) y, desde el simulador (sim/),
// el resto del nucleo Arduino-ESP32 que toca TamaPoke.ino: String, Print, pines,
// interrupciones, delay(), ESP.*. El reloj es falso y controlable para que los
// tests y el simulador sean deterministas: delay() solo adelanta ese reloj.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

#include "WString.h"
#include "Print.h"

// ---- reloj falso -----------------------------------------------------------
uint32_t millis();
uint64_t mockMillis64();  // los mismos ms sin dar la vuelta a los 49,7 dias (el millis() de 32 bits si)
uint32_t micros();
void mockSetMillis(uint32_t ms);
void mockAdvanceMillis(uint32_t ms);
void delay(uint32_t ms);  // solo adelanta el reloj falso
void delayMicroseconds(uint32_t us);
static inline void yield() {}

// ---- random determinista (misma semantica que Arduino: [0, howbig) ) -------
long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);
// fuerza el siguiente/los siguientes random() a un valor fijo (para tests)
void mockForceRandom(long value);   // todos los random() devuelven value
void mockClearForcedRandom();

// ---- Serial ----------------------------------------------------------------
// Salida: callada por defecto (tests); el simulador la manda a un fichero.
// Entrada: una cola que se llena con mockSerialFeed() (comandos de consola).
class MockSerial : public Print {
public:
  void begin(unsigned long) {}
  void setRxBufferSize(size_t) {}
  void setTxTimeoutMs(uint32_t) {}
  void setTimeout(unsigned long) {}
  size_t write(uint8_t c) override;
  using Print::write;
  int available();
  int read();
  int peek();
  size_t readBytes(uint8_t *buf, size_t n);
  size_t readBytes(char *buf, size_t n) { return readBytes((uint8_t *)buf, n); }
  String readStringUntil(char term);
  void flush() {}
  operator bool() const { return true; }
};
extern MockSerial Serial;
void mockSerialSilence(bool quiet);   // true: descarta la salida (por defecto)
void mockSerialOutput(FILE *f);       // nullptr = callada
void mockSerialFeed(const char *text);  // encola entrada como si llegara por USB

// ---- utilidades ------------------------------------------------------------
template <class T> static inline T min(T a, T b) { return a < b ? a : b; }
template <class T> static inline T max(T a, T b) { return a > b ? a : b; }
#define _min(a, b) ((a) < (b) ? (a) : (b))
#define _max(a, b) ((a) > (b) ? (a) : (b))
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
typedef uint8_t byte;
typedef bool boolean;
#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

// ---- pines e interrupciones --------------------------------------------------
#define LOW 0x0
#define HIGH 0x1
#define INPUT 0x01
#define OUTPUT 0x03
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define RISING 0x01
#define FALLING 0x02
#define CHANGE 0x03
void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t val);
static inline int digitalPinToInterrupt(int pin) { return pin; }
void attachInterrupt(uint8_t pin, void (*isr)(void), int mode);
void detachInterrupt(uint8_t pin);
// banco de pruebas: nivel que leera digitalRead() (sin fijar: HIGH, como con pull-up)
void mockPinSet(uint8_t pin, int level);
// dispara la ISR enganchada al pin, si la hay; devuelve false si no habia ninguna
bool mockPinFireIsr(uint8_t pin);
bool mockPinIsrAttached(uint8_t pin);

// ---- ESP32 -------------------------------------------------------------------
#define IRAM_ATTR
#define RTC_NOINIT_ATTR  // the board keeps it in RTC memory through soft resets; the PC has none
#define PROGMEM
#define PGM_P const char *
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define pgm_read_ptr(addr) (*(void *const *)(addr))
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define portENTER_CRITICAL_ISR(m) ((void)(m))
#define portEXIT_CRITICAL_ISR(m) ((void)(m))
static inline void *ps_malloc(size_t n) { return malloc(n); }
bool setCpuFrequencyMhz(uint32_t mhz);
uint32_t getCpuFrequencyMhz();

class EspClass {
public:
  uint32_t getFreeHeap() { return 200000; }
  uint32_t getMinFreeHeap() { return 180000; }
  [[noreturn]] void restart();
};
extern EspClass ESP;
// ESP.restart() llama a este gancho (por defecto: aborta con un mensaje)
void mockSetRestartHook(void (*hook)());
