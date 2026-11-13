#pragma once
#include <stdint.h>

// Efectos de sonido del juego (cola, no bloqueante). El orden coincide con la
// tabla SFX de audio.cpp.
enum Sfx : uint8_t {
  SFX_TAP = 0,  // tocar / boton
  SFX_EAT,      // comer
  SFX_PLAY,     // punto del minijuego / golpe
  SFX_HEART,    // le gusta / mimo
  SFX_HATCH,    // eclosion
  SFX_EVOLVE,   // evolucion
  SFX_MEDAL,    // medalla / hito
  SFX_DENY,     // accion no permitida
  SFX_BYE,      // despedida
  SFX_LEVEL,    // sube de nivel
  SFX_NOTE0,    // Berry Song: baya roja (do)
  SFX_NOTE1,    // baya azul (mi)
  SFX_NOTE2,    // baya verde (sol)
  SFX_NOTE3,    // caramelo (do agudo)
  SFX_COUNT
};

void audioBegin();          // init ES8311 + I2S + amplificador + tarea de audio
void sfxPlay(uint8_t id);   // encola un efecto (no bloquea el loop)
void audioSetEnabled(bool on);
bool audioEnabled();
// niveles de volumen de ajustes (#21): 0 = alto (el de siempre), 1 = medio, 2 = bajo
#define VOL_LEVELS 3
void audioSetVolume(uint8_t level);  // guarda el nivel en NVS
uint8_t audioVolume();
// amplitud de la onda cuadrada para cada nivel: unos -8 dB y -17 dB respecto al alto
inline int16_t sfxAmp(uint8_t level) { return level == 1 ? 2000 : level >= 2 ? 700 : 5000; }
void audioSetSleeping(bool sleeping);  // dormida: amplificador apagado
bool audioBusy();                      // suena, hay un efecto en cola o el amp sigue retenido: no dormir la CPU
void audioSetIdle(bool idle);          // sin uso: amplificador apagado (un efecto lo enciende 4 s)
