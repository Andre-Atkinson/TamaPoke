#!/usr/bin/env python3
"""Copia las fuentes U8g2 que usa TamaPoke.ino desde la libreria U8g2 instalada.

clib/u8g2_fonts.c pesa ~40 MB (todas las fuentes de U8g2); compilarlo entero en el
host tarda mucho. Este script saca solo los arrays pedidos, tal cual, a un .c
pequeno, quitando la macro U8G2_FONT_SECTION (en el host no hay secciones).

  python3 sim/extract_fonts.py <ruta a u8g2_fonts.c> salida.c NOMBRE [NOMBRE...]
"""
import os
import re
import sys


def extract(src_bytes, names):
    out = [b'/* generado por sim/extract_fonts.py desde U8g2 clib/u8g2_fonts.c */',
           b'#include <stdint.h>']
    for name in names:
        start = re.search(rb'^const uint8_t ' + re.escape(name.encode()) + rb'\[\d+\][^\n]*=',
                          src_bytes, re.M)
        if not start:
            raise SystemExit('extract_fonts: %s not found' % name)
        # el array acaba en la primera linea que termina en '";' (una cadena puede
        # contener ';', asi que no vale buscar el primer ';')
        end = start.end()
        while True:
            nl = src_bytes.find(b'\n', end)
            if nl < 0:
                raise SystemExit('extract_fonts: unterminated array ' + name)
            if src_bytes[end:nl].rstrip().endswith(b'";'):
                break
            end = nl + 1
        block = src_bytes[start.start():nl]
        block = re.sub(rb'\s*U8G2_FONT_SECTION\("[^"]*"\)', b'', block, count=1)
        out.append(block)
    return b'\n'.join(out) + b'\n'


def main(argv):
    if len(argv) < 4:
        print(__doc__.strip().splitlines()[-1].strip(), file=sys.stderr)
        return 2
    with open(argv[1], 'rb') as f:
        data = f.read()
    text = extract(data, argv[3:])
    tmp = argv[2] + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(text)
    os.replace(tmp, argv[2])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
