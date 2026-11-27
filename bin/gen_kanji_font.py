#!/usr/bin/env python3
"""Convert k8x12S.bdf into a C++ glyph table for the OLED_JA renderer.

Output: sorted uint16 code points and 12-byte XBM bitmaps (8x12 cell, LSB = leftmost pixel).
Half-width katakana keep their 4-dot glyphs in the left half of the cell.
Usage: gen_kanji_font.py k8x12S.bdf OUT_DIR [--range level1|level2|joyo] [--joyo FILE]
"""

import argparse
import os
import sys

CELL_W = 8
CELL_H = 12


def parse_bdf(path):
    glyphs = {}
    ascent = None
    cur = None
    with open(path, encoding="latin-1") as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            key = t[0]
            if key == "FONT_ASCENT":
                ascent = int(t[1])
            elif key == "STARTCHAR":
                cur = {}
            elif cur is None:
                continue
            elif key == "ENCODING":
                cur["cp"] = int(t[1])
            elif key == "DWIDTH":
                cur["dw"] = int(t[1])
            elif key == "BBX":
                cur["bbx"] = tuple(int(v) for v in t[1:5])
            elif key == "BITMAP":
                cur["rows"] = []
            elif key == "ENDCHAR":
                glyphs[cur["cp"]] = cur
                cur = None
            elif "rows" in cur:
                cur["rows"].append(int(t[0], 16))
    if ascent is None:
        sys.exit("FONT_ASCENT missing")
    return glyphs, ascent


def jis_level(cp):
    """Return 1 or 2 for JIS X 0208 level-1/level-2 kanji, 0 otherwise."""
    try:
        b = chr(cp).encode("euc_jp")
    except UnicodeEncodeError:
        return 0
    if len(b) != 2:
        return 0
    if 0xB0 <= b[0] <= 0xCF:
        return 1
    if 0xD0 <= b[0] <= 0xF4:
        return 2
    return 0


def is_kanji(cp):
    return 0x4E00 <= cp <= 0x9FFF


def want_non_kanji(cp, g):
    if 0xFF61 <= cp <= 0xFF9F:  # half-width katakana
        return True
    # Otherwise full-width glyphs only; ASCII and Latin-1 stay on the default font.
    if g["dw"] != CELL_W or cp < 0x100:
        return False
    if 0x0370 <= cp <= 0x052F:  # Greek, Cyrillic
        return False
    if 0x2500 <= cp <= 0x257F:  # box drawing
        return False
    if (
        0x2010 <= cp <= 0x2027
    ):  # dashes and quotes: leave to the default font so English text stays narrow
        return False
    return True


def to_cell(g, ascent):
    """Place the glyph in an 8x12 cell and return 12 XBM bytes."""
    w, h, ox, oy = g["bbx"]
    top = ascent - (oy + h)
    rowbytes = (w + 7) // 8
    cell = [0] * CELL_H
    for i, raw in enumerate(g["rows"]):
        y = top + i
        if not 0 <= y < CELL_H:
            continue
        bits = raw >> (rowbytes * 8 - CELL_W) if rowbytes > 1 else raw
        bits = (bits >> ox) & 0xFF  # BDF: bit 7 = leftmost
        cell[y] = int(f"{bits:08b}"[::-1], 2)  # XBM: bit 0 = leftmost
    return cell


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bdf")
    ap.add_argument("out_dir")
    ap.add_argument("--range", choices=["level1", "level2", "joyo"], default="level1")
    ap.add_argument("--joyo", help="text file listing joyo kanji (any layout)")
    args = ap.parse_args()

    glyphs, ascent = parse_bdf(args.bdf)
    joyo = set()
    if args.range == "joyo":
        if not args.joyo:
            sys.exit("--joyo FILE is required for --range joyo")
        with open(args.joyo, encoding="utf-8") as f:
            joyo = {ord(c) for c in f.read() if is_kanji(ord(c))}

    codes = []
    for cp in sorted(glyphs):
        g = glyphs[cp]
        if is_kanji(cp):
            lv = jis_level(cp)
            ok = (
                (args.range == "level1" and lv == 1)
                or (args.range == "level2" and lv in (1, 2))
                or cp in joyo
            )
        else:
            ok = want_non_kanji(cp, g)
        if ok and cp <= 0xFFFF:
            codes.append(cp)

    os.makedirs(args.out_dir, exist_ok=True)
    with open(
        os.path.join(args.out_dir, "KanjiFontK8x12.h"),
        "w",
        encoding="utf-8",
        newline="\n",
    ) as f:
        f.write(HEADER)
    with open(
        os.path.join(args.out_dir, "KanjiFontK8x12.cpp"),
        "w",
        encoding="utf-8",
        newline="\n",
    ) as f:
        f.write(SOURCE_HEAD.format(rng=args.range, count=len(codes)))
        f.write("const uint16_t KANJI_CODES[] PROGMEM = {\n")
        for i in range(0, len(codes), 15):
            f.write("    " + ", ".join(f"0x{c:04X}" for c in codes[i : i + 15]) + ",\n")
        f.write("};\n\nconst uint8_t KANJI_BITMAPS[] PROGMEM = {\n")
        for cp in codes:
            cell = to_cell(glyphs[cp], ascent)
            f.write(
                "    " + ", ".join(f"0x{b:02X}" for b in cell) + f", // {chr(cp)}\n"
            )
        f.write(
            "};\n\n} // namespace KanjiFontData\n} // namespace graphics\n\n#endif\n"
        )
    size = len(codes) * (2 + CELL_H)
    print(f"{len(codes)} glyphs, {size} bytes ({args.range})")


HEADER = """#pragma once
#ifdef OLED_JA
// Glyph table generated by bin/gen_kanji_font.py from k8x12S (c) Num Kadoma, free software.
#include <stdint.h>
#ifdef ARDUINO
#include <Arduino.h>
#elif !defined(PROGMEM)
#define PROGMEM
#endif

namespace graphics
{
namespace KanjiFontData
{
static constexpr int GLYPH_WIDTH = 8;
static constexpr int GLYPH_HEIGHT = 12;
static constexpr int GLYPH_BYTES = 12;

extern const uint16_t KANJI_CODES[] PROGMEM;
extern const uint8_t KANJI_BITMAPS[] PROGMEM;
extern const uint16_t KANJI_COUNT;
} // namespace KanjiFontData
} // namespace graphics

#endif
"""

SOURCE_HEAD = """#ifdef OLED_JA
// Generated by bin/gen_kanji_font.py (range: {rng}); do not edit by hand.
// Font: k8x12S 2021-05-05 (c) 2015-2021 Num Kadoma, https://littlelimit.net/k8x12.htm
#include "KanjiFontK8x12.h"

namespace graphics
{{
namespace KanjiFontData
{{
const uint16_t KANJI_COUNT = {count};

"""

if __name__ == "__main__":
    main()
