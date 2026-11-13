// Simulador: fs::FS (LittleFS en la 1.75C, SD_MMC en la 1.75) sobre una carpeta del
// Mac, por defecto tools/sdcard (la que empaquetan build_spritefs.sh y send_sd.py).
// Solo lectura. En la 1.75C se ocultan los ps*.bin: su imagen LittleFS no los lleva
// (no caben en 32 MB) y el firmware cae al sprite normal, igual que en placa.
#include "simhw.h"
#include <FS.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <algorithm>

static std::string gRoot = "tools/sdcard";
static bool gHideShiny = false;
void simFsSetRoot(const char *dir) { gRoot = dir ? dir : ""; }
void simFsHideShiny(bool hide) { gHideShiny = hide; }

LittleFSFS LittleFS;
SDMMCFS SD_MMC;

namespace fs {

struct FileImpl {
  FILE *fp = nullptr;
  bool dir = false;
  std::string vpath;                 // ruta como la ve el firmware (/mons/p001.bin)
  std::string base;                  // nombre (p001.bin)
  std::vector<std::string> entries;  // directorio: hijos ordenados
  size_t next = 0;
  size_t size = 0;
  ~FileImpl() { if (fp) fclose(fp); }
};

}  // namespace fs

static bool hidden(const std::string &vpath) {
  if (!gHideShiny) return false;
  size_t slash = vpath.rfind('/');
  std::string name = slash == std::string::npos ? vpath : vpath.substr(slash + 1);
  return name.size() > 2 && name[0] == 'p' && name[1] == 's' && isdigit((unsigned char)name[2]);
}

static bool safePath(const char *p) { return p && p[0] == '/' && !strstr(p, ".."); }

static std::string hostPath(const char *vpath) { return gRoot + vpath; }

static std::shared_ptr<fs::FileImpl> openImpl(const char *vpath) {
  if (!safePath(vpath) || hidden(vpath)) return nullptr;
  std::string hp = hostPath(vpath);
  struct stat st;
  if (stat(hp.c_str(), &st) != 0) return nullptr;
  std::shared_ptr<fs::FileImpl> f = std::make_shared<fs::FileImpl>();
  f->vpath = vpath;
  size_t slash = f->vpath.rfind('/');
  f->base = f->vpath.substr(slash + 1);
  if (S_ISDIR(st.st_mode)) {
    f->dir = true;
    DIR *d = opendir(hp.c_str());
    if (!d) return nullptr;
    while (struct dirent *e = readdir(d)) {
      std::string n = e->d_name;
      if (n == "." || n == "..") continue;
      std::string child = f->vpath + (f->vpath == "/" ? "" : "/") + n;
      if (!hidden(child)) f->entries.push_back(child);
    }
    closedir(d);
    std::sort(f->entries.begin(), f->entries.end());
    return f;
  }
  f->fp = fopen(hp.c_str(), "rb");
  if (!f->fp) return nullptr;
  f->size = (size_t)st.st_size;
  return f;
}

namespace fs {

File::operator bool() const { return impl_ != nullptr; }
size_t File::size() const { return impl_ ? impl_->size : 0; }
size_t File::read(uint8_t *buf, size_t n) {
  if (!impl_ || !impl_->fp) return 0;
  return fread(buf, 1, n, impl_->fp);
}
int File::read() {
  uint8_t b;
  return read(&b, 1) == 1 ? b : -1;
}
size_t File::write(const uint8_t *, size_t) { return 0; }  // solo lectura
bool File::seek(uint32_t pos) { return impl_ && impl_->fp && fseek(impl_->fp, pos, SEEK_SET) == 0; }
size_t File::position() const { return impl_ && impl_->fp ? (size_t)ftell(impl_->fp) : 0; }
int File::available() const { return impl_ && impl_->fp ? (int)(impl_->size - position()) : 0; }
const char *File::name() const { return impl_ ? impl_->base.c_str() : ""; }
const char *File::path() const { return impl_ ? impl_->vpath.c_str() : ""; }
bool File::isDirectory() const { return impl_ && impl_->dir; }
File File::openNextFile(const char *) {
  while (impl_ && impl_->dir && impl_->next < impl_->entries.size()) {
    std::shared_ptr<FileImpl> f = openImpl(impl_->entries[impl_->next++].c_str());
    if (f) return File(f);
  }
  return File();
}
void File::close() { impl_.reset(); }

File FS::open(const char *path, const char *mode, bool) {
  if (!mode || mode[0] != 'r') return File();  // solo lectura: PUT contesta ERR
  return File(openImpl(path));
}
bool FS::exists(const char *path) { return openImpl(path) != nullptr; }
bool FS::mkdir(const char *path) { return exists(path); }

}  // namespace fs

static uint64_t folderBytes(const std::string &dir) {
  uint64_t total = 0;
  DIR *d = opendir(dir.c_str());
  if (!d) return 0;
  while (struct dirent *e = readdir(d)) {
    std::string n = e->d_name;
    if (n == "." || n == "..") continue;
    struct stat st;
    std::string p = dir + "/" + n;
    if (stat(p.c_str(), &st) != 0 || hidden(p)) continue;
    total += S_ISDIR(st.st_mode) ? folderBytes(p) : (uint64_t)st.st_size;
  }
  closedir(d);
  return total;
}

static bool rootMounts() {
  struct stat st;
  return stat(gRoot.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool LittleFSFS::begin(bool, const char *, uint8_t, const char *) { return rootMounts(); }
uint64_t LittleFSFS::totalBytes() { return 0x1CE0000ULL; }  // particion "storage" de la 1.75C
uint64_t LittleFSFS::usedBytes() { return folderBytes(gRoot); }

bool SDMMCFS::begin(const char *, bool, bool) { return rootMounts(); }
uint64_t SDMMCFS::cardSize() { return 8ULL * 1024 * 1024 * 1024; }
uint64_t SDMMCFS::totalBytes() { return cardSize(); }
uint64_t SDMMCFS::usedBytes() { return folderBytes(gRoot); }
