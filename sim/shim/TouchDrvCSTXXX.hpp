// Simulador: driver falso del CST9217 (SensorLib). Devuelve el dedo del guion
// (gSimTouch, ya en coordenadas de la UI: el espejado de setMirrorXY lo da por hecho).
#pragma once
#include <Arduino.h>
#include <Wire.h>

class TouchDrvCST92xx {
public:
  void setPins(int rst, int irq) { (void)rst; (void)irq; }
  bool begin(TwoWire &wire, uint8_t addr, int sda, int scl);
  void reset() {}
  void setMaxCoordinates(uint16_t, uint16_t) {}
  void setMirrorXY(bool, bool) {}
  uint8_t getPoint(int16_t *x, int16_t *y, uint8_t maxPoints = 1);
  void sleep();
};
