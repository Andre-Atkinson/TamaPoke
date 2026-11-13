// Estado del hardware simulado de la placa (lo que el guion del simulador mueve y lo
// que las piezas falsas de sim/shim/ leen): panel CO5300, tactil CST9217, botones
// PWR (PKEY del AXP2101) y BOOT (GPIO0), PMU (bateria/USB) y reloj de software.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define SIM_W 466
#define SIM_H 466

// ---- panel: lo que el CO5300 muestra (el ultimo frame volcado por QSPI) ----
struct SimPanel {
  uint16_t fb[SIM_W * SIM_H];  // RGB565, lo que hay en la GRAM del panel
  bool sleeping = false;       // SLPIN (0x10) / SLPOUT (0x11)
  bool displayOn = true;       // DISPOFF (0x28) / DISPON (0x29)
  uint8_t brightness = 0;      // 0x51 o setBrightness()
  uint32_t flushes = 0;        // frames recibidos
  uint8_t lastCmd = 0;         // para interpretar el byte de datos que sigue a un comando
};
extern SimPanel gSimPanel;
bool simPanelVisible();  // false: dormido, apagado o a brillo 0 (se ve negro)

// ---- tactil ----
struct SimTouch {
  bool down = false;
  int16_t x = 0, y = 0;    // coordenadas de la UI (466x466, como las da el driver ya espejado)
  bool asleep = false;     // touch.sleep(): no contesta por I2C hasta el siguiente begin()
  bool ready = false;      // begin() correcto
};
extern SimTouch gSimTouch;
// mueve el dedo; dispara el INT del CST9217 (la ISR del firmware) si el chip esta despierto
void simTouchSet(bool down, int16_t x, int16_t y);

// ---- botones ----
void simBootButton(bool down);  // GPIO0 a nivel bajo mientras esta pulsado
void simPwrPress();             // pulsacion corta del PKEY: el bit de IRQ queda latcheado

// ---- PMU y reloj ----
struct SimPower {
  int batPct = 80;         // -1 = sin bateria
  bool charging = false;
  bool usb = false;
  bool poweredOff = false;  // batPowerOff(): el AXP corto la alimentacion
};
extern SimPower gSimPower;
void simClockSet(uint32_t epoch);  // pone el reloj de software (0 = sin hora, como tras un corte)

// ---- sistema de ficheros de sprites ----
void simFsSetRoot(const char *dir);   // carpeta que hace de LittleFS/SD (tools/sdcard)
void simFsHideShiny(bool hide);       // 1.75C: la imagen LittleFS no lleva ps*.bin

// ---- audio ----
int mockLastSfx();         // ultimo efecto que habria sonado (-1 = ninguno)
uint32_t mockSfxCount();   // efectos que habrian sonado
