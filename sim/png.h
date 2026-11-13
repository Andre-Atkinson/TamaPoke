// Escritor de PNG sin dependencias (sin zlib): RGB de 8 bits, filtro Sub/Up por fila
// y deflate con codigos Huffman fijos y repeticiones a distancia 1 (las zonas lisas
// de la UI se comprimen bien sin buscar coincidencias). Para capturas del simulador.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

uint32_t pngCrc32(const uint8_t *data, size_t n, uint32_t crc = 0);
uint32_t pngAdler32(const uint8_t *data, size_t n, uint32_t adler = 1);
// flujo zlib (cabecera 0x78 0x01, un bloque deflate de Huffman fijo, Adler-32)
std::vector<uint8_t> pngZlibCompress(const uint8_t *data, size_t n);
// PNG en memoria de una imagen RGB565 w x h. mask: fuera del circulo inscrito = negro
// (lo que se ve del panel redondo).
std::vector<uint8_t> pngEncodeRgb565(const uint16_t *px, int w, int h, bool roundMask);
bool pngWriteRgb565(const std::string &path, const uint16_t *px, int w, int h, bool roundMask);
