#include "png.h"
#include <stdio.h>
#include <string.h>

uint32_t pngCrc32(const uint8_t *data, size_t n, uint32_t crc) {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    ready = true;
  }
  crc = ~crc;
  for (size_t i = 0; i < n; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

uint32_t pngAdler32(const uint8_t *data, size_t n, uint32_t adler) {
  uint32_t a = adler & 0xFFFF, b = adler >> 16;
  for (size_t i = 0; i < n; i++) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

namespace {

// Escritura de bits LSB primero, como pide deflate (RFC 1951 3.1.1).
struct BitWriter {
  std::vector<uint8_t> &out;
  uint32_t acc = 0;
  int nbits = 0;
  explicit BitWriter(std::vector<uint8_t> &o) : out(o) {}
  void bits(uint32_t v, int n) {  // n bits de v, el menos significativo primero
    acc |= v << nbits;
    nbits += n;
    while (nbits >= 8) {
      out.push_back((uint8_t)acc);
      acc >>= 8;
      nbits -= 8;
    }
  }
  void huff(uint32_t code, int len) {  // los codigos Huffman van con el bit mas significativo primero
    uint32_t rev = 0;
    for (int i = 0; i < len; i++) rev |= ((code >> i) & 1) << (len - 1 - i);
    bits(rev, len);
  }
  void flush() {
    if (nbits > 0) out.push_back((uint8_t)acc);
    acc = 0;
    nbits = 0;
  }
};

// codigos fijos de literal/longitud (RFC 1951 3.2.6)
void litLen(BitWriter &bw, int v) {
  if (v <= 143) bw.huff(0x30 + v, 8);
  else if (v <= 255) bw.huff(0x190 + (v - 144), 9);
  else if (v <= 279) bw.huff(v - 256, 7);
  else bw.huff(0xC0 + (v - 280), 8);
}

const uint16_t LEN_BASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint8_t LEN_EXTRA[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };

void match(BitWriter &bw, int len) {  // longitud 3..258, distancia 1
  int i = 28;
  while (LEN_BASE[i] > len) i--;
  litLen(bw, 257 + i);
  if (LEN_EXTRA[i]) bw.bits((uint32_t)(len - LEN_BASE[i]), LEN_EXTRA[i]);
  bw.huff(0, 5);  // codigo de distancia 0 = distancia 1, sin bits extra
}

void put32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back((uint8_t)(x >> 24));
  v.push_back((uint8_t)(x >> 16));
  v.push_back((uint8_t)(x >> 8));
  v.push_back((uint8_t)x);
}

void chunk(std::vector<uint8_t> &png, const char *type, const std::vector<uint8_t> &data) {
  put32(png, (uint32_t)data.size());
  size_t start = png.size();
  png.insert(png.end(), type, type + 4);
  png.insert(png.end(), data.begin(), data.end());
  put32(png, pngCrc32(&png[start], png.size() - start));
}

}  // namespace

std::vector<uint8_t> pngZlibCompress(const uint8_t *data, size_t n) {
  std::vector<uint8_t> out;
  out.push_back(0x78);  // deflate, ventana de 32 KB
  out.push_back(0x01);  // nivel minimo; (0x78*256+0x01) % 31 == 0
  BitWriter bw(out);
  bw.bits(1, 1);  // BFINAL
  bw.bits(1, 2);  // BTYPE = 01, Huffman fijo
  size_t i = 0;
  while (i < n) {
    if (i > 0) {
      size_t run = 0;
      while (i + run < n && run < 258 && data[i + run] == data[i - 1]) run++;
      if (run >= 3) {
        match(bw, (int)run);
        i += run;
        continue;
      }
    }
    litLen(bw, data[i]);
    i++;
  }
  litLen(bw, 256);  // fin de bloque
  bw.flush();
  put32(out, pngAdler32(data, n));
  return out;
}

std::vector<uint8_t> pngEncodeRgb565(const uint16_t *px, int w, int h, bool roundMask) {
  const size_t stride = (size_t)w * 3;
  std::vector<uint8_t> prev(stride, 0), cur(stride), raw;
  raw.reserve((stride + 1) * (size_t)h);
  std::vector<uint8_t> sub(stride), up(stride);
  const int cx2 = w - 1, cy2 = h - 1;               // centro *2 (evita medios pixeles)
  const long r2 = (long)(w < h ? w : h) * (w < h ? w : h);  // (diametro)^2 = (2r)^2
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint16_t c = px[(size_t)y * w + x];
      if (roundMask) {
        long dx = 2L * x - cx2, dy = 2L * y - cy2;
        if (dx * dx + dy * dy > r2) c = 0;
      }
      uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
      uint8_t g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
      uint8_t b = (uint8_t)((c & 0x1F) * 255 / 31);
      cur[(size_t)x * 3] = r;
      cur[(size_t)x * 3 + 1] = g;
      cur[(size_t)x * 3 + 2] = b;
    }
    // filtro por fila: Sub (1) o Up (2), el que deje mas ceros
    size_t zSub = 0, zUp = 0;
    for (size_t i = 0; i < stride; i++) {
      sub[i] = (uint8_t)(cur[i] - (i >= 3 ? cur[i - 3] : 0));
      up[i] = (uint8_t)(cur[i] - prev[i]);
      zSub += sub[i] == 0;
      zUp += up[i] == 0;
    }
    bool useUp = y > 0 && zUp > zSub;
    raw.push_back(useUp ? 2 : 1);
    const std::vector<uint8_t> &f = useUp ? up : sub;
    raw.insert(raw.end(), f.begin(), f.end());
    prev.swap(cur);
  }
  std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
  std::vector<uint8_t> ihdr;
  put32(ihdr, (uint32_t)w);
  put32(ihdr, (uint32_t)h);
  ihdr.push_back(8);  // bits por canal
  ihdr.push_back(2);  // RGB
  ihdr.push_back(0);  // deflate
  ihdr.push_back(0);  // filtros estandar
  ihdr.push_back(0);  // sin entrelazado
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", pngZlibCompress(raw.data(), raw.size()));
  chunk(png, "IEND", std::vector<uint8_t>());
  return png;
}

bool pngWriteRgb565(const std::string &path, const uint16_t *px, int w, int h, bool roundMask) {
  std::vector<uint8_t> png = pngEncodeRgb565(px, w, h, roundMask);
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
  ok = (fclose(f) == 0) && ok;
  return ok;
}
