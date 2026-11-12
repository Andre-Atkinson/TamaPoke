#pragma once
#include <Arduino.h>

// RTC PCF85063: hora persistente mientras la placa tenga alimentacion.
// 1.75C (BOARD_AMOLED_175C): no PCF85063, software clock (time()/settimeofday).
bool rtcBegin();
uint32_t rtcEpoch();             // segundos unix; 0 si el RTC no es valido
void rtcSetEpoch(uint32_t e);

// PMU AXP2101: estado de la bateria
bool batBegin();
void pmuEnablePanel();           // enciende BLDO1 (OLED VDD 3.3V); llamar antes de gfx->begin()
void batSetPollMs(uint32_t ms);   // cada cuanto se lee el PMU por I2C (se alarga con la pantalla apagada)
int batPercent();                // 0-100, -1 si no hay bateria conectada
bool batCharging();
int batMillivolts();               // tension de la bateria en mV, 0 si no hay lectura
bool batShouldShutdown();          // descargada hasta el limite seguro (batcurve.h): guardar y apagar
void batPowerOff();                // apagado fisico del PMU
bool usbPresent();
bool usbPresentNow();             // sin cache (una lectura I2C): para decidir si se puede dormir

// Extra fields on the 5-minute HEALTH line. 0 strips them.
// -DTAMAPOKE_CHG_DEBUG=0
#ifndef TAMAPOKE_CHG_DEBUG
#define TAMAPOKE_CHG_DEBUG 1
#endif
#if TAMAPOKE_CHG_DEBUG
void batFormatHealth(char *dst, size_t n);  // charger state, die temp, programmed currents
#else
inline void batFormatHealth(char *dst, size_t n) { if (n) dst[0] = 0; }
#endif

// boton PWR del AXP2101: pulsacion larga 4s = apagado fisico (RTC sigue vivo);
// la pulsacion corta la captura el firmware (pantalla on/off)
void pwrSetup();
bool pwrShortPressed();  // sondear en el loop (1.75C: siempre false, triple press en screenlock.h)
