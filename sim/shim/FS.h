// Simulador: fs::FS de Arduino-ESP32 sobre una carpeta del Mac (tools/sdcard).
// Solo lectura: abrir para escribir falla, asi que PUT por la consola contesta ERR
// y nunca toca los sprites del repo.
#pragma once
#include <Arduino.h>
#include <memory>
#include <string>

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {

struct FileImpl;

class File {
public:
  File() {}
  explicit File(std::shared_ptr<FileImpl> impl) : impl_(impl) {}
  explicit operator bool() const;
  size_t size() const;
  size_t read(uint8_t *buf, size_t n);
  int read();
  size_t write(const uint8_t *buf, size_t n);
  size_t write(uint8_t b) { return write(&b, 1); }
  bool seek(uint32_t pos);
  size_t position() const;
  int available() const;
  const char *name() const;
  const char *path() const;
  bool isDirectory() const;
  File openNextFile(const char *mode = FILE_READ);
  void close();

private:
  std::shared_ptr<FileImpl> impl_;
};

class FS {
public:
  File open(const char *path, const char *mode = FILE_READ, bool create = false);
  File open(const String &path, const char *mode = FILE_READ, bool create = false) {
    return open(path.c_str(), mode, create);
  }
  bool exists(const char *path);
  bool exists(const String &path) { return exists(path.c_str()); }
  bool remove(const char *path) { (void)path; return false; }  // solo lectura
  bool remove(const String &path) { return remove(path.c_str()); }
  bool mkdir(const char *path);
  bool mkdir(const String &path) { return mkdir(path.c_str()); }
  bool rmdir(const char *path) { (void)path; return false; }
};

}  // namespace fs

using fs::File;
using fs::FS;
