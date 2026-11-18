#pragma once
#include <Arduino.h>

// Sprite animado TPK1 (formato heredado, camino de respaldo). El proyecto usa
// PMD/TPK2 (PmdMon) para todo; esta ruta queda inactiva si no hay NNN.bin en la SD.
// Los datos indexados viven en PSRAM; la paleta es RGB565.
struct SdMon {
  bool loaded = false;
  uint16_t w = 0, h = 0, frames = 0, frameMs = 100;
  uint8_t scale = 2;       // factor de zoom entero al dibujar
  uint16_t palCount = 0;
  uint16_t pal[256];
  uint8_t *data = nullptr;  // frames * w * h indices (0xFF = transparente)

  bool load(uint8_t dexNum, bool shiny = false);
  void unload();
};

// acciones de los sprites PMD (formato TPK2)
enum : uint8_t {
  PMD_IDLE = 0, PMD_WALKL, PMD_WALKR, PMD_SLEEP, PMD_EAT, PMD_HURT,
  PMD_ATTACK, PMD_POSE, PMD_HOP, PMD_NOD, PMD_BREATH, PMD_SIT,
  PMD_NACTS
};

struct PmdAct {
  uint8_t w = 0, h = 0, frames = 0;
  uint8_t base = 0;  // fila+1 del pixel mas bajo (anclar por los pies, no el lienzo)
  uint8_t top = 0;   // primera fila del frame 0 con contenido (h si esta vacio): lo alto de la cabeza (#73)
  uint16_t ms[24];
  const uint8_t *data = nullptr;  // frames * w * h en el blob
  // Where an accessory sits on each frame (#116, accFindHead() in accessory.h, with accfix.h's
  // skip): 3 bytes a frame, the head's top row, then the first column and the width of the
  // stretch under it (width 0: none). nullptr when PSRAM had no room: then no accessory shows.
  const uint8_t *head = nullptr;
};

// The scale drawPmdActM() draws an action whose canvas is actH tall at, for a sprite whose
// IDLE canvas is idleH tall, at most maxS: IDLE fills about 170 px, never under 2, and a
// taller canvas steps down until it is at most 250 px (ATTACK's and HOP's have room for the
// move, so they often draw smaller than IDLE). The rhythm punch's lunge draws ATTACK at
// IDLE's (punchlunge.h, #90).
inline uint8_t pmdScaleFor(uint8_t idleH, uint8_t actH, uint8_t maxS) {
  uint8_t s = idleH ? 170 / idleH : 5;
  if (s < 2) s = 2;
  if (s > maxS) s = maxS;
  while (s > 2 && actH * s > 250) s--;
  return s;
}

// sprite PMD multi-accion cargado de la SD a PSRAM
struct PmdMon {
  bool loaded = false;
  uint16_t palCount = 0;
  uint16_t pal[256];
  uint8_t *blob = nullptr;
  uint8_t *heads = nullptr;  // every action's PmdAct::head, one block (#116)
  uint8_t dex = 0;           // the species loaded (accfix.h's rows are per species)
  PmdAct acts[PMD_NACTS];

  bool load(uint8_t dexNum, bool shiny = false);
  void unload();
  bool has(uint8_t a) const { return loaded && a < PMD_NACTS && acts[a].frames > 0; }

private:
  void findHeads(uint8_t dexNum);
};

// miniaturas de la galeria (thumbs.bin entero en PSRAM)
struct SdThumbs {
  bool loaded = false;
  uint8_t *data = nullptr;
  uint16_t count = 0;
  uint32_t size = 0;  // bytes leidos: acota los offsets del fichero
  bool load();
  void unload();  // libera el blob: recargar tras recibir un thumbs.bin nuevo
  const uint8_t *get(int16_t dex) const;  // blob: w,h,palCount,pal[],idx[]
};
extern SdThumbs thumbs;

bool sdBegin();                 // monta SD (1.75) o LittleFS en flash (1.75C)
bool sdSerialCommand(const String &line);  // PUT/LS por USB; true si la maneja
extern bool sdReady;
extern bool sdDirty;  // true tras recibir archivos: recargar sprite
