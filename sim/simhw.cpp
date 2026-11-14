// Simulador: piezas falsas de la placa (panel CO5300, bus QSPI, CST9217 por I2C y
// por SensorLib, light sleep, FreeRTOS) y el estado que mueve el guion (simhw.h).
#include "simhw.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>
#include <esp_sleep.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <deque>
#include <vector>
#include "../pin_config.h"

SimPanel gSimPanel;
SimTouch gSimTouch;
TwoWire Wire;

// ---------------- panel CO5300 y bus QSPI ----------------
bool simPanelVisible() {
  return !gSimPanel.sleeping && gSimPanel.displayOn && gSimPanel.brightness > 0;
}

void Arduino_ESP32QSPI::writeCommand(uint8_t c) {
  gSimPanel.lastCmd = c;
  if (c == 0x10) gSimPanel.sleeping = true;        // SLPIN
  else if (c == 0x11) gSimPanel.sleeping = false;  // SLPOUT
  else if (c == 0x28) gSimPanel.displayOn = false; // DISPOFF
  else if (c == 0x29) gSimPanel.displayOn = true;  // DISPON
}

void Arduino_ESP32QSPI::write(uint8_t d) {
  if (gSimPanel.lastCmd == 0x51) gSimPanel.brightness = d;  // brillo
}

bool Arduino_CO5300::begin(int32_t) {
  gSimPanel.sleeping = false;  // la secuencia de arranque de la libreria acaba en SLPOUT + DISPON
  gSimPanel.displayOn = true;
  return true;
}
void Arduino_CO5300::setBrightness(uint8_t b) { gSimPanel.brightness = b; }
void Arduino_CO5300::displayOn() { gSimPanel.displayOn = true; }
void Arduino_CO5300::displayOff() { gSimPanel.displayOn = false; }

void Arduino_CO5300::draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) {
  for (int r = 0; r < h; r++) {
    int py = y + r;
    if (py < 0 || py >= SIM_H) continue;
    for (int c = 0; c < w; c++) {
      int px = x + c;
      if (px >= 0 && px < SIM_W) gSimPanel.fb[py * SIM_W + px] = bitmap[r * w + c];
    }
  }
  gSimPanel.flushes++;
}

// ---------------- tactil CST9217 ----------------
// INT del chip: un flanco por informe nuevo (al apoyar, mover y levantar el dedo).
// Con el chip dormido (touch.sleep()) no hay informes, ni INT, ni respuesta I2C.
void simTouchSet(bool down, int16_t x, int16_t y) {
  bool changed = down != gSimTouch.down || (down && (x != gSimTouch.x || y != gSimTouch.y));
  if (down && !gSimTouch.down) gSimTouch.readSincePress = false;  // a new press
  gSimTouch.down = down;
  if (down) { gSimTouch.x = x; gSimTouch.y = y; }
  if (changed && gSimTouch.ready && !gSimTouch.asleep) mockPinFireIsr(TP_INT);
}

bool TouchDrvCST92xx::begin(TwoWire &, uint8_t, int, int) {
  gSimTouch.asleep = false;
  gSimTouch.ready = true;
  return true;
}

uint8_t TouchDrvCST92xx::getPoint(int16_t *x, int16_t *y, uint8_t) {
  if (!gSimTouch.ready || gSimTouch.asleep || !gSimTouch.down) return 0;
  *x = gSimTouch.x;
  *y = gSimTouch.y;
  gSimTouch.contactReads++;
  if (!gSimTouch.readSincePress) {
    gSimTouch.readSincePress = true;
    gSimTouch.firstReadX = gSimTouch.x;
    gSimTouch.firstReadY = gSimTouch.y;
  }
  return 1;
}

void TouchDrvCST92xx::sleep() { gSimTouch.asleep = true; }

// ---------------- I2C ----------------
static const uint8_t kTouchAddr = 0x5A;
static bool touchAcks() { return gSimTouch.ready && !gSimTouch.asleep; }

void TwoWire::beginTransmission(uint8_t addr) {
  addr_ = addr;
  txLen_ = 0;
}

size_t TwoWire::write(uint8_t b) {
  if (txLen_ < sizeof tx_) tx_[txLen_++] = b;
  return 1;
}

uint8_t TwoWire::endTransmission(bool) {
  if (addr_ != kTouchAddr || !touchAcks()) return 2;  // NACK de direccion
  if (txLen_ >= 2) { reg_[0] = tx_[0]; reg_[1] = tx_[1]; }
  return 0;
}

// Informe crudo del registro 0xD0 del CST9217: coordenadas nativas (sin espejar)
// codificadas como las lee decodeTrainingReport(); 0xAB en el byte 6 = informe completo.
size_t TwoWire::requestFrom(uint8_t addr, size_t n, bool) {
  rxLen_ = rxPos_ = 0;
  if (addr != kTouchAddr || !touchAcks()) return 0;
  uint8_t b[15] = {};
  b[6] = 0xAB;
  if (reg_[0] == 0xD0 && gSimTouch.down) {
    int rx = 466 - gSimTouch.x, ry = 466 - gSimTouch.y;
    b[0] = 0x06;  // punto 0, contacto
    b[1] = (uint8_t)(rx >> 4);
    b[2] = (uint8_t)(ry >> 4);
    b[3] = (uint8_t)(((rx & 0x0F) << 4) | (ry & 0x0F));
    b[4] = 0;
    b[5] = 1;     // un punto
  }
  rxLen_ = n < sizeof b ? n : sizeof b;
  memcpy(rx_, b, rxLen_);
  return rxLen_;
}

// ---------------- botones ----------------
void simBootButton(bool down) { mockPinSet(0 /* GPIO0, BOOT en las dos placas */, down ? LOW : HIGH); }

// ---------------- light sleep ----------------
static uint64_t gSleepUs = 0;
static uint32_t gLightSleeps = 0;
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us) { gSleepUs = us; return ESP_OK; }
esp_err_t esp_sleep_enable_gpio_wakeup(void) { return ESP_OK; }
esp_err_t esp_light_sleep_start(void) {
  uint32_t ms = (uint32_t)(gSleepUs / 1000);
  delay(ms ? ms : 1);
  gLightSleeps++;
  return ESP_OK;
}
uint32_t simLightSleeps() { return gLightSleeps; }

// ---------------- FreeRTOS minimo ----------------
struct SimTask { TaskFunction_t fn; void *arg; };
struct SimQueue { size_t itemSize, length; std::deque<std::vector<uint8_t> > items; };
struct SimSemaphore { int unused; };

BaseType_t xTaskCreate(TaskFunction_t fn, const char *, uint32_t, void *arg, UBaseType_t, TaskHandle_t *out) {
  SimTask *t = new SimTask{ fn, arg };
  if (out) *out = t;
  return pdPASS;
}
void vTaskDelay(TickType_t ticks) { delay(ticks); }

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize) {
  SimQueue *q = new SimQueue();
  q->itemSize = itemSize;
  q->length = length;
  return q;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t) {
  if (!q || q->items.size() >= q->length) return pdFALSE;
  const uint8_t *p = (const uint8_t *)item;
  q->items.push_back(std::vector<uint8_t>(p, p + q->itemSize));
  return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t) {
  if (!q || q->items.empty()) return pdFALSE;
  memcpy(item, &q->items.front()[0], q->itemSize);
  q->items.pop_front();
  return pdTRUE;
}
BaseType_t xQueueReset(QueueHandle_t q) {
  if (q) q->items.clear();
  return pdPASS;
}
void vQueueDelete(QueueHandle_t q) { delete q; }
SemaphoreHandle_t xSemaphoreCreateBinary() { return new SimSemaphore(); }
void vSemaphoreDelete(SemaphoreHandle_t s) { delete s; }
