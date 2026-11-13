// Simulador: bus I2C falso. Solo contesta el CST9217 (0x5A) mientras esta despierto,
// con el informe crudo de 0xD0 que decodeTrainingReport() espera (saco de la 1.75C).
// El resto de direcciones (QMI8658, ES8311, AXP2101, PCF85063) no responden: sus
// drivers estan sustituidos por piezas falsas que no pasan por aqui.
#pragma once
#include <Arduino.h>

class TwoWire {
public:
  bool begin(int sda = -1, int scl = -1, uint32_t freq = 0) { (void)sda; (void)scl; (void)freq; return true; }
  void setTimeOut(uint16_t) {}
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t addr);
  void beginTransmission(int addr) { beginTransmission((uint8_t)addr); }
  uint8_t endTransmission(bool sendStop = true);
  size_t write(uint8_t b);
  size_t write(const uint8_t *buf, size_t n) { size_t k = 0; while (n--) k += write(*buf++); return k; }
  size_t requestFrom(uint8_t addr, size_t n, bool sendStop = true);
  size_t requestFrom(uint8_t addr, uint8_t n) { return requestFrom(addr, (size_t)n, true); }
  size_t requestFrom(int addr, int n) { return requestFrom((uint8_t)addr, (size_t)n, true); }
  int available() { return (int)(rxLen_ - rxPos_); }
  int read() { return rxPos_ < rxLen_ ? rx_[rxPos_++] : -1; }
  size_t readBytes(uint8_t *buf, size_t n) {
    size_t k = 0;
    while (k < n && rxPos_ < rxLen_) buf[k++] = rx_[rxPos_++];
    return k;
  }

private:
  uint8_t addr_ = 0;
  uint8_t tx_[16] = {};
  size_t txLen_ = 0;
  uint8_t reg_[2] = {};  // ultimo registro direccionado (CST9217: 2 bytes)
  uint8_t rx_[32] = {};
  size_t rxLen_ = 0, rxPos_ = 0;
};
extern TwoWire Wire;
