// Simulador: la particion "storage" de la 1.75C es la carpeta de sprites del repo.
#pragma once
#include <FS.h>

class LittleFSFS : public fs::FS {
public:
  bool begin(bool formatOnFail = false, const char *basePath = "/littlefs", uint8_t maxOpen = 10,
             const char *label = "spiffs");
  uint64_t totalBytes();
  uint64_t usedBytes();
};
extern LittleFSFS LittleFS;
