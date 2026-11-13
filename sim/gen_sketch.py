#!/usr/bin/env python3
"""Convierte TamaPoke.ino en un .cpp compilable en el host, como hace arduino-cli.

El preprocesador de Arduino declara un prototipo de cada funcion del sketch antes de
la primera definicion, para que se puedan llamar antes de definirlas. Este script hace
lo mismo sin tocar el .ino:

  * busca las definiciones de funcion de nivel superior (empiezan en la columna 0);
  * escribe sus prototipos (sin argumentos por defecto) justo antes de la primera
    definicion que no este dentro de un #if, cada uno envuelto en las mismas
    condiciones #if/#elif/#else que su definicion;
  * anade #line para que los errores del compilador apunten a las lineas del .ino.

  python3 sim/gen_sketch.py TamaPoke.ino salida.cpp
"""
import os
import re
import sys

# tipo [* &] nombre(parametros) { ...   (sin '=' ni ';' antes del parentesis)
DEF_RE = re.compile(r'^(?P<head>[A-Za-z_][^=;(){}#]*?\b(?P<name>[A-Za-z_]\w*))\s*'
                    r'\((?P<params>[^(){}]*)\)\s*(?:const\s*)?\{')
NOT_FUNCS = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'else', 'do'}
NOT_HEADS = ('struct', 'class', 'enum', 'union', 'namespace', 'typedef', 'template')


def strip_defaults(params):
    """Quita los '= valor' de cada parametro (prototipo sin argumentos por defecto)."""
    out, depth, cur = [], 0, ''
    for ch in params:
        if ch in '(<[':
            depth += 1
        elif ch in ')>]':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    if cur.strip() or out:
        out.append(cur)
    return ', '.join(re.sub(r'\s*=.*$', '', p.strip(), flags=re.S) for p in out)


def find_functions(lines):
    """[(indice_linea, prototipo, condiciones)] de las definiciones de nivel superior.

    condiciones es la pila de directivas (#if.. y sus #elif/#else) activa en esa linea.
    """
    stack, funcs = [], []
    in_comment = False
    for i, raw in enumerate(lines):
        line = raw.rstrip('\n')
        stripped = line.strip()
        if in_comment:
            if '*/' in line:
                in_comment = False
            continue
        if stripped.startswith('/*') and '*/' not in stripped:
            in_comment = True
            continue
        if stripped.startswith('#'):
            d = stripped[1:].strip()
            if d.startswith(('if', 'ifdef', 'ifndef')) and not d.startswith('include'):
                stack.append([stripped])
            elif d.startswith(('elif', 'else')):
                if stack:
                    stack[-1].append(stripped)
            elif d.startswith('endif'):
                if stack:
                    stack.pop()
            continue
        if not line or line[0].isspace():
            continue
        m = DEF_RE.match(line)
        if not m:
            continue
        head, name = m.group('head').strip(), m.group('name')
        if name in NOT_FUNCS or head.split()[0] in NOT_HEADS or head == name:
            continue
        proto = '%s(%s);' % (head, strip_defaults(m.group('params')))
        funcs.append((i, proto, [list(f) for f in stack]))
    return funcs


def wrap(proto, conds):
    """El prototipo dentro de sus #if (las ramas anteriores quedan vacias)."""
    out = []
    for frame in conds:
        out.extend(frame)
    out.append(proto)
    out.extend(['#endif'] * len(conds))
    return out


def generate(ino_path, lines):
    funcs = find_functions(lines)
    if not funcs:
        raise SystemExit('gen_sketch: no function definitions found in ' + ino_path)
    first = next((i for i, _, conds in funcs if not conds), None)
    if first is None:
        raise SystemExit('gen_sketch: every function is inside an #if')
    path = os.path.abspath(ino_path).replace('\\', '/')
    out = ['#include <Arduino.h>', '#line 1 "%s"' % path]
    out.extend(l.rstrip('\n') for l in lines[:first])
    seen = set()
    for _, proto, conds in funcs:
        key = (proto, tuple(tuple(f) for f in conds))
        if key in seen:
            continue
        seen.add(key)
        out.extend(wrap(proto, conds))
    out.append('#line %d "%s"' % (first + 1, path))
    out.extend(l.rstrip('\n') for l in lines[first:])
    return '\n'.join(out) + '\n'


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[-1].strip(), file=sys.stderr)
        return 2
    with open(argv[1], encoding='utf-8', errors='surrogateescape') as f:
        lines = f.readlines()
    text = generate(argv[1], lines)
    tmp = argv[2] + '.tmp'
    with open(tmp, 'w', encoding='utf-8', errors='surrogateescape') as f:
        f.write(text)
    os.replace(tmp, argv[2])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
