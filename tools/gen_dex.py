#!/usr/bin/env python3
"""Genera dex.h (tabla de la Pokedex para el firmware) desde dex_data.py.

  python3 tools/gen_dex.py
"""
import itertools
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from dex_data import DEX, TYPE_ACCENTS, CLASSIC, RARE, LEGENDARY
from dex_stats import BASE_STATS


def rgb565(hexcol):
    r, g, b = int(hexcol[1:3], 16), int(hexcol[3:5], 16), int(hexcol[5:7], 16)
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


# Egg-choice spots (#34): an egg's spots show its type accent, so two accents
# closer than this (CIE76) look alike on the shell, and one roll of three eggs
# keeps them apart. Chosen in #34: 25 parts ground/rock, poison/ghost,
# electric/ground and normal/rock (13.7 to 24.5) and leaves every other pair of
# today's accents (27.4 and up) alone.
EGG_SPOT_MIN_DE = 25


def rgb888(c565):
    """An RGB565 colour widened to 8 bits a channel by repeating its top bits."""
    r, g, b = c565 >> 11, (c565 >> 5) & 0x3F, c565 & 0x1F
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def cielab(rgb):
    """CIELAB (D65 white) of an sRGB colour."""
    def linear(v):
        v /= 255
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4

    def f(t):
        return t ** (1 / 3) if t > (6 / 29) ** 3 else t / (3 * (6 / 29) ** 2) + 4 / 29

    r, g, b = (linear(v) for v in rgb)
    x = f((0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047)
    y = f(0.2126 * r + 0.7152 * g + 0.0722 * b)
    z = f((0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883)
    return 116 * y - 16, 500 * (x - y), 200 * (y - z)


def delta_e76(a565, b565):
    """CIE76 colour difference of two RGB565 colours."""
    return math.dist(cielab(rgb888(a565)), cielab(rgb888(b565)))


def egg_spot_alike_pairs():
    """(dE, accent, accent, type, type) for every pair of type accents that look
    alike as egg spots, closest first."""
    accents = {}  # one name per RGB565 colour
    for typ, hexcol in TYPE_ACCENTS.items():
        accents.setdefault(rgb565(hexcol), typ)
    pairs = []
    for a, b in itertools.combinations(accents, 2):
        de = delta_e76(a, b)
        if de < EGG_SPOT_MIN_DE:
            pairs.append((de, a, b, accents[a], accents[b]))
    return sorted(pairs)


def emit_egg_spots(out):
    pairs = egg_spot_alike_pairs()
    out.append(
        "// Egg-choice spots (#34): an egg's spots show its type accent. Two accents whose\n"
        "// CIE76 colour difference is under EGG_SPOT_MIN_DE look alike on the shell, so\n"
        "// Pet::rollEggChoices() keeps them out of one roll. Measured on the RGB565 accent\n"
        "// (each channel widened to 8 bits by repeating its top bits) as sRGB with a D65\n"
        "// white, by tools/gen_dex.py. test/test_pet.cpp recomputes every pair.\n"
        f"#define EGG_SPOT_MIN_DE {EGG_SPOT_MIN_DE}\n")
    if not pairs:
        out.append(
            "inline bool eggSpotsLookAlike(uint16_t a, uint16_t b) { return a == b; }\n\n")
        return
    out.append("inline const uint16_t EGG_SPOT_ALIKE[][2] = {\n")
    for de, a, b, ta, tb in pairs:
        out.append(f"  {{ 0x{a:04X}, 0x{b:04X} }},  // {ta} / {tb}, dE {de:.1f}\n")
    out.append(
        "};\n\n"
        "// true when spots of accents a and b would look alike (the same accent included)\n"
        "inline bool eggSpotsLookAlike(uint16_t a, uint16_t b) {\n"
        "  if (a == b) return true;\n"
        "  for (const auto &p : EGG_SPOT_ALIKE)\n"
        "    if ((p[0] == a && p[1] == b) || (p[0] == b && p[1] == a)) return true;\n"
        "  return false;\n"
        "}\n\n")


# bioma de fondo por tipo (la luz la pone la hora real del RTC)
# 0 PRADERA, 1 PLAYA, 2 BOSQUE, 3 VOLCAN, 4 MONTANA, 5 NIEVE
TYPE_BIOME = {
    'agua': 1, 'planta': 2, 'bicho': 2, 'fuego': 3,
    'roca': 4, 'tierra': 4, 'dragon': 1, 'hielo': 5,  # los dragones gen1 (Dratini) viven en el agua
    'normal': 0, 'electrico': 0, 'lucha': 0, 'veneno': 0,
    'psiquico': 0, 'fantasma': 0,
}

# excepciones por dex# (el tipo no basta): fosiles marinos roca/agua -> playa
BIOME_OVERRIDE = {138: 1, 139: 1, 140: 1, 141: 1}  # Omanyte, Omastar, Kabuto, Kabutops


def main():
    out = []
    out.append("#pragma once\n#include <stdint.h>\n\n")
    out.append("// GENERADO por tools/gen_dex.py desde tools/dex_data.py - no editar\n\n")
    out.append("// The tables and functions are inline (C++17), not static: the linker keeps one\n// copy however many source files include this header (#41).\n\n")
    out.append("#define DEX_COUNT 151\n")
    out.append("#define DEX_EEVEE 133  // rama al azar: 134/135/136\n\n")
    out.append(
        "// rareza: 0 = solo por evolucion, 1 = comun, 2 = raro, 3 = legendario\n"
        "enum : uint8_t { R_EVO = 0, R_COMUN, R_RARO, R_LEGENDARIO };\n\n"
        "struct DexEntry {\n"
        "  const char *name;\n"
        "  uint8_t evolvesTo;    // numero de dex, 0 = forma final\n"
        "  uint8_t evolveLevel;\n"
        "  uint8_t rarity;       // sale de huevo si > 0\n"
        "  uint16_t accent;      // color RGB565 del tipo para la UI\n"
        "  uint8_t bHp, bAtk, bDef, bSpe;  // base stats reales de gen 1\n"
        "  uint8_t biome;        // 0 pradera 1 playa 2 bosque 3 volcan 4 montana 5 nieve\n"
        "};\n\n")
    # formas base = las que no son evolucion de nadie (las ramas de Eevee si lo son)
    evolved = {evo for *_, evo, _lvl in [(d[4], d[5]) for d in DEX] for evo in [_[0] for _ in [(d[4],) for d in DEX]]}
    evolved = {d[4] for d in DEX if d[4]} | {135, 136}
    rarities = []
    out.append("inline const DexEntry DEX_TBL[DEX_COUNT + 1] = {\n")
    out.append('  { "?", 0, 0, 0, 0x2946, 50, 50, 50, 50, 0 },  // 0: sin usar\n')
    for num, slug, display, typ, evo, lvl in DEX:
        acc = rgb565(TYPE_ACCENTS[typ])
        if num in evolved:
            rar = 'R_EVO'
        elif num in LEGENDARY:
            rar = 'R_LEGENDARIO'
        elif num in RARE:
            rar = 'R_RARO'
        else:
            rar = 'R_COMUN'
        rarities.append(rar)
        hp, atk, df, spe = BASE_STATS[num]
        bio = BIOME_OVERRIDE.get(num, TYPE_BIOME[typ])
        out.append(f'  {{ "{display}", {evo}, {lvl}, {rar}, 0x{acc:04X}, {hp}, {atk}, {df}, {spe}, {bio} }},  // {num} {typ}\n')
    out.append("};\n\n")

    # EN and PT (#61) both use the English names, the ones in DEX_TBL.
    out.append(
        "// Nombre de la especie. Los dos idiomas (#61) usan el ingles, como los juegos\n"
        "// en portugues.\n"
        "inline const char *dexName(int16_t dex) {\n"
        "  if (dex < 1 || dex > DEX_COUNT) return DEX_TBL[0].name;\n"
        "  return DEX_TBL[dex].name;\n"
        "}\n\n")

    emit_egg_spots(out)

    out.append("// el primer huevo de la partida: iniciales clasicos\n")
    out.append("inline const int16_t CLASSIC_DEX[] = { %s };\n" % ", ".join(map(str, CLASSIC)))
    out.append(f"#define NUM_CLASSIC_DEX {len(CLASSIC)}\n")
    from collections import Counter
    c = Counter(rarities)
    print(f"bases: {c['R_COMUN']} comunes, {c['R_RARO']} raras, {c['R_LEGENDARIO']} legendarias, {c['R_EVO']} solo-evolucion")

    path = os.path.join(os.path.dirname(__file__), '..', 'dex.h')
    open(path, 'w').write(''.join(out))
    print(f"guardado {os.path.normpath(path)} ({len(DEX)} especies)")


if __name__ == '__main__':
    main()
