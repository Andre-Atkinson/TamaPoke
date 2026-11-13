// Simulador: la microSD de la 1.75 original es la carpeta de sprites del repo.
#pragma once
#include <FS.h>

class SDMMCFS : public fs::FS {
public:
  bool setPins(int clk, int cmd, int d0) { (void)clk; (void)cmd; (void)d0; return true; }
  bool begin(const char *mountpoint = "/sdcard", bool mode1bit = false, bool formatOnFail = false);
  uint64_t cardSize();
  uint64_t totalBytes();
  uint64_t usedBytes();
};
extern SDMMCFS SD_MMC;
