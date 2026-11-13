// Simulador: la libreria Arduino_GFX real (lienzo, primitivas, fuentes) compilada en
// el host, con el bus QSPI y el panel CO5300 sustituidos por piezas falsas que
// guardan el frame volcado y los comandos de energia/brillo en gSimPanel (simhw.h).
#pragma once
#include <Arduino.h>
#include "Arduino_GFX.h"
#include "canvas/Arduino_Canvas.h"

// Bus QSPI: solo interpreta los comandos de energia y brillo del CO5300
// (SLPIN/SLPOUT/DISPOFF/DISPON/0x51) que manda runPanelProgram().
class Arduino_ESP32QSPI : public Arduino_DataBus {
public:
  Arduino_ESP32QSPI(int8_t cs, int8_t sck, int8_t d0, int8_t d1, int8_t d2, int8_t d3,
                    bool is_shared_interface = false) {
    (void)cs; (void)sck; (void)d0; (void)d1; (void)d2; (void)d3; (void)is_shared_interface;
  }
  bool begin(int32_t speed = GFX_NOT_DEFINED, int8_t dataMode = GFX_NOT_DEFINED) override {
    (void)speed; (void)dataMode;
    return true;
  }
  void beginWrite() override {}
  void endWrite() override {}
  void writeCommand(uint8_t c) override;
  void writeCommand16(uint16_t c) override { writeCommand((uint8_t)c); }
  void writeCommandBytes(uint8_t *data, uint32_t len) override { while (len--) writeCommand(*data++); }
  void write(uint8_t d) override;
  void write16(uint16_t d) override { write((uint8_t)(d >> 8)); }
  void writeRepeat(uint16_t, uint32_t) override {}
  void writePixels(uint16_t *, uint32_t) override {}
  void writeBytes(uint8_t *, uint32_t) override {}
};

// Panel: recibe el framebuffer entero en cada flush() del lienzo.
class Arduino_CO5300 : public Arduino_G {
public:
  Arduino_CO5300(Arduino_DataBus *bus, int8_t rst = GFX_NOT_DEFINED, uint8_t r = 0,
                 int16_t w = 466, int16_t h = 466, uint8_t col_offset1 = 0, uint8_t row_offset1 = 0,
                 uint8_t col_offset2 = 0, uint8_t row_offset2 = 0)
      : Arduino_G(w, h) {
    (void)bus; (void)rst; (void)r; (void)col_offset1; (void)row_offset1; (void)col_offset2; (void)row_offset2;
  }
  bool begin(int32_t speed = GFX_NOT_DEFINED) override;
  void setBrightness(uint8_t brightness);
  void displayOn();
  void displayOff();
  void drawBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t, uint16_t, uint16_t) override {}
  void drawIndexedBitmap(int16_t, int16_t, uint8_t *, uint16_t *, int16_t, int16_t, int16_t = 0) override {}
  void draw3bitRGBBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t) override {}
  void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) override;
  void draw24bitRGBBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t) override {}
};
