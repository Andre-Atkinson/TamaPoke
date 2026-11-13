// Avance del reloj falso del simulador: hace correr pasadas del bucle del firmware
// hasta que millis() llega a un instante. Separado del firmware para probarlo solo.
#pragma once
#include <Arduino.h>

// Una pasada que no adelanta el reloj (en placa siempre tarda algo) se cobra 1 ms,
// para que un bucle sin esperas no se quede girando en el mismo milisegundo.
static const uint32_t SIM_MIN_PASS_MS = 1;
// Los tramos largos se parten: la comparacion con signo de millis() solo vale
// para distancias por debajo de ~24,8 dias.
static const uint64_t SIM_CHUNK_MS = 86400000ULL;

// Corre step() hasta que han pasado `ms` milisegundos de reloj falso. El ultimo paso
// puede pasarse de largo (un delay() o light sleep no se corta), como mucho lo que
// dure una pasada. Devuelve cuantas pasadas se hicieron. stop() permite cortar antes
// (el PMU apago la placa).
template <class Step, class Stop>
uint64_t simRunFor(uint64_t ms, Step step, Stop stop) {
  uint64_t passes = 0;
  while (ms > 0) {
    uint32_t chunk = (uint32_t)(ms > SIM_CHUNK_MS ? SIM_CHUNK_MS : ms);
    uint32_t start = millis();
    uint32_t target = start + chunk;
    while ((int32_t)(millis() - target) < 0) {
      if (stop()) return passes;
      uint32_t before = millis();
      step();
      passes++;
      if (millis() == before) mockAdvanceMillis(SIM_MIN_PASS_MS);
    }
    uint32_t done = millis() - start;  // >= chunk: lo que se haya pasado cuenta
    ms = done >= ms ? 0 : ms - done;
  }
  return passes;
}

template <class Step>
uint64_t simRunFor(uint64_t ms, Step step) {
  return simRunFor(ms, step, [] { return false; });
}
