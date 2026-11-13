#include "Arduino.h"
#include "Preferences.h"
#include "../../audio.h"
#include <deque>
#include <map>
#include <vector>
#include <stdarg.h>

// ---------------- reloj falso ----------------
static uint32_t gMillis = 0;
static uint32_t gMicrosFrac = 0;  // microsegundos por debajo del milisegundo actual
uint32_t millis() { return gMillis; }
uint32_t micros() { return gMillis * 1000u + gMicrosFrac; }
void mockSetMillis(uint32_t ms) { gMillis = ms; gMicrosFrac = 0; }
void mockAdvanceMillis(uint32_t ms) { gMillis += ms; }
void delay(uint32_t ms) { gMillis += ms; }
void delayMicroseconds(uint32_t us) {
  gMicrosFrac += us;
  gMillis += gMicrosFrac / 1000u;
  gMicrosFrac %= 1000u;
}

// ---------------- random determinista ----------------
// LCG propio: reproducible entre plataformas (no depende de rand()).
static uint32_t gSeed = 12345;
static bool gForced = false;
static long gForcedValue = 0;

void randomSeed(unsigned long seed) { gSeed = seed ? (uint32_t)seed : 1; }
void mockForceRandom(long value) { gForced = true; gForcedValue = value; }
void mockClearForcedRandom() { gForced = false; }

static uint32_t nextRand() {
  gSeed = gSeed * 1664525u + 1013904223u;
  return gSeed >> 1;
}

long random(long howbig) {
  if (howbig <= 0) return 0;
  if (gForced) return gForcedValue < howbig ? gForcedValue : howbig - 1;
  return (long)(nextRand() % (uint32_t)howbig);
}

long random(long howsmall, long howbig) {
  if (howsmall >= howbig) return howsmall;
  return howsmall + random(howbig - howsmall);
}

// ---------------- Serial ----------------
MockSerial Serial;
static FILE *gSerialOut = nullptr;  // nullptr = callada (por defecto en los tests)
static std::deque<uint8_t> gSerialIn;
void mockSerialSilence(bool quiet) { gSerialOut = quiet ? nullptr : stdout; }
void mockSerialOutput(FILE *f) { gSerialOut = f; }
void mockSerialFeed(const char *text) {
  for (const char *p = text; *p; p++) gSerialIn.push_back((uint8_t)*p);
}

size_t MockSerial::write(uint8_t c) {
  if (gSerialOut) fputc(c, gSerialOut);
  return 1;
}
int MockSerial::available() { return (int)gSerialIn.size(); }
int MockSerial::read() {
  if (gSerialIn.empty()) return -1;
  int c = gSerialIn.front();
  gSerialIn.pop_front();
  return c;
}
int MockSerial::peek() { return gSerialIn.empty() ? -1 : gSerialIn.front(); }
// Sin espera: en placa readBytes()/readStringUntil() esperan hasta el timeout; aqui
// la entrada ya esta entera en la cola o no va a llegar.
size_t MockSerial::readBytes(uint8_t *buf, size_t n) {
  size_t k = 0;
  while (k < n && !gSerialIn.empty()) { buf[k++] = gSerialIn.front(); gSerialIn.pop_front(); }
  return k;
}
String MockSerial::readStringUntil(char term) {
  std::string s;
  while (!gSerialIn.empty()) {
    char c = (char)gSerialIn.front();
    gSerialIn.pop_front();
    if (c == term) break;
    s += c;
  }
  return String(s);
}

// ---------------- pines e interrupciones ----------------
static std::map<uint8_t, int> gPinLevel;
static std::map<uint8_t, void (*)(void)> gPinIsr;
void pinMode(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) {
  std::map<uint8_t, int>::iterator it = gPinLevel.find(pin);
  return it == gPinLevel.end() ? HIGH : it->second;
}
void digitalWrite(uint8_t pin, uint8_t val) { gPinLevel[pin] = val ? HIGH : LOW; }
void attachInterrupt(uint8_t pin, void (*isr)(void), int) { gPinIsr[pin] = isr; }
void detachInterrupt(uint8_t pin) { gPinIsr.erase(pin); }
void mockPinSet(uint8_t pin, int level) { gPinLevel[pin] = level ? HIGH : LOW; }
bool mockPinIsrAttached(uint8_t pin) { return gPinIsr.count(pin) && gPinIsr[pin]; }
bool mockPinFireIsr(uint8_t pin) {
  if (!mockPinIsrAttached(pin)) return false;
  gPinIsr[pin]();
  return true;
}

// ---------------- ESP32 ----------------
static uint32_t gCpuMhz = 240;
bool setCpuFrequencyMhz(uint32_t mhz) { gCpuMhz = mhz; return true; }
uint32_t getCpuFrequencyMhz() { return gCpuMhz; }

EspClass ESP;
static void (*gRestartHook)() = nullptr;
void mockSetRestartHook(void (*hook)()) { gRestartHook = hook; }
void EspClass::restart() {
  if (gRestartHook) gRestartHook();
  fprintf(stderr, "ESP.restart() on the host: stopping\n");
  abort();
}

// ---------------- audio (sin sonido) ----------------
// Mismo estado y mismas claves NVS que audio.cpp ("snd", "vol"), sin ES8311 ni I2S:
// la pantalla de ajustes muestra lo mismo que en placa. Los efectos solo se cuentan.
static bool gAudioOn = true;
static uint8_t gAudioVol = 0;
static bool gAudioSleeping = false;
static int gLastSfx = -1;
static uint32_t gSfxCount = 0;
void audioBegin() {
  Preferences p;
  p.begin("tamapoke", true);
  gAudioOn = p.getBool("snd", true);
  gAudioVol = p.getUChar("vol", 0) % VOL_LEVELS;
  p.end();
}
void sfxPlay(uint8_t id) {
  if (!gAudioOn || gAudioSleeping || id >= SFX_COUNT) return;
  gLastSfx = id;
  gSfxCount++;
}
void audioSetEnabled(bool on) {
  gAudioOn = on;
  Preferences p;
  p.begin("tamapoke", false);
  p.putBool("snd", on);
  p.end();
}
bool audioEnabled() { return gAudioOn; }
void audioSetVolume(uint8_t level) {
  gAudioVol = level % VOL_LEVELS;
  Preferences p;
  p.begin("tamapoke", false);
  p.putUChar("vol", gAudioVol);
  p.end();
}
uint8_t audioVolume() { return gAudioVol; }
void audioSetSleeping(bool sleeping) { gAudioSleeping = sleeping; }
bool audioBusy() { return false; }
void audioSetIdle(bool) {}
int mockLastSfx() { return gLastSfx; }
uint32_t mockSfxCount() { return gSfxCount; }

// ---------------- Preferences en memoria ----------------
typedef std::map<std::string, std::vector<uint8_t> > KV;
static std::map<std::string, KV> gStore;

void mockNvsReset() { gStore.clear(); }
size_t mockNvsKeyCount(const char *ns) {
  std::map<std::string, KV>::iterator it = gStore.find(ns);
  return it == gStore.end() ? 0 : it->second.size();
}

bool Preferences::begin(const char *name, bool readOnly) {
  ns_ = name;
  ro_ = readOnly;
  open_ = true;
  return true;
}
void Preferences::end() { open_ = false; }
bool Preferences::clear() {
  if (!open_ || ro_) return false;
  gStore[ns_].clear();
  return true;
}
bool Preferences::isKey(const char *key) {
  if (!open_) return false;
  KV &kv = gStore[ns_];
  return kv.find(key) != kv.end();
}
bool Preferences::remove(const char *key) {
  if (!open_ || ro_) return false;
  return gStore[ns_].erase(key) > 0;
}

static size_t rawPut(std::map<std::string, KV> &store, const std::string &ns, bool open,
                     bool ro, const char *k, const void *v, size_t len) {
  if (!open || ro) return 0;
  const uint8_t *p = (const uint8_t *)v;
  store[ns][k].assign(p, p + len);
  return len;
}

template <class T>
static size_t putScalar(std::map<std::string, KV> &store, const std::string &ns, bool open,
                        bool ro, const char *k, T v) {
  return rawPut(store, ns, open, ro, k, &v, sizeof(T));
}

template <class T>
static T getScalar(std::map<std::string, KV> &store, const std::string &ns, bool open,
                   const char *k, T def) {
  if (!open) return def;
  KV &kv = store[ns];
  KV::iterator it = kv.find(k);
  if (it == kv.end() || it->second.size() != sizeof(T)) return def;
  T out;
  memcpy(&out, &it->second[0], sizeof(T));
  return out;
}

size_t Preferences::putBool(const char *k, bool v)       { return putScalar(gStore, ns_, open_, ro_, k, (uint8_t)(v ? 1 : 0)); }
size_t Preferences::putUChar(const char *k, uint8_t v)   { return putScalar(gStore, ns_, open_, ro_, k, v); }
size_t Preferences::putChar(const char *k, int8_t v)     { return putScalar(gStore, ns_, open_, ro_, k, v); }
size_t Preferences::putShort(const char *k, int16_t v)   { return putScalar(gStore, ns_, open_, ro_, k, v); }
size_t Preferences::putUShort(const char *k, uint16_t v) { return putScalar(gStore, ns_, open_, ro_, k, v); }
size_t Preferences::putInt(const char *k, int32_t v)     { return putScalar(gStore, ns_, open_, ro_, k, v); }
size_t Preferences::putUInt(const char *k, uint32_t v)   { return putScalar(gStore, ns_, open_, ro_, k, v); }
size_t Preferences::putBytes(const char *k, const void *v, size_t len) {
  return rawPut(gStore, ns_, open_, ro_, k, v, len);
}
size_t Preferences::putString(const char *k, const char *v) {
  return rawPut(gStore, ns_, open_, ro_, k, v, strlen(v) + 1);
}

bool Preferences::getBool(const char *k, bool def)         { return getScalar<uint8_t>(gStore, ns_, open_, k, def ? 1 : 0) != 0; }
uint8_t Preferences::getUChar(const char *k, uint8_t def)  { return getScalar(gStore, ns_, open_, k, def); }
int8_t Preferences::getChar(const char *k, int8_t def)     { return getScalar(gStore, ns_, open_, k, def); }
int16_t Preferences::getShort(const char *k, int16_t def)  { return getScalar(gStore, ns_, open_, k, def); }
uint16_t Preferences::getUShort(const char *k, uint16_t def){ return getScalar(gStore, ns_, open_, k, def); }
int32_t Preferences::getInt(const char *k, int32_t def)    { return getScalar(gStore, ns_, open_, k, def); }
uint32_t Preferences::getUInt(const char *k, uint32_t def) { return getScalar(gStore, ns_, open_, k, def); }

size_t Preferences::getBytes(const char *k, void *buf, size_t maxLen) {
  if (!open_) return 0;
  KV &kv = gStore[ns_];
  KV::iterator it = kv.find(k);
  if (it == kv.end()) return 0;   // como la NVS real: deja el buffer intacto
  size_t n = it->second.size() < maxLen ? it->second.size() : maxLen;
  memcpy(buf, &it->second[0], n);
  return n;
}

size_t Preferences::getString(const char *k, char *buf, size_t maxLen) {
  if (!open_ || maxLen == 0) return 0;
  KV &kv = gStore[ns_];
  KV::iterator it = kv.find(k);
  if (it == kv.end()) { buf[0] = 0; return 0; }
  size_t n = it->second.size() < maxLen ? it->second.size() : maxLen - 1;
  memcpy(buf, &it->second[0], n);
  buf[n < maxLen ? n : maxLen - 1] = 0;
  return strlen(buf);
}
