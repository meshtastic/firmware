#!/usr/bin/env python3
"""Generate 12x12 emotes for OLED_JA from src/graphics/emotes.cpp.

The emote table is copied as-is; each 16x16 bitmap is downscaled (area coverage >= 40%,
OR for diagonal arrows) or replaced by a hand-drawn 12x12 glyph listed below.
Usage: gen_kanji_emotes.py src/graphics/emotes.cpp src/graphics/emotesKanji.cpp
"""

import os
import re
import shutil
import subprocess
import sys

SIZE = 12


def parse_bitmaps(src):
    out = {}
    for name, body in re.findall(
        r"const unsigned char (\w+)\[\] PROGMEM = \{(.*?)\};", src, re.S
    ):
        b = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", body)]
        if len(b) != 32:
            sys.exit(f"{name}: expected a 16x16 bitmap")
        out[name] = [
            [(b[y * 2 + x // 8] >> (x % 8)) & 1 for x in range(16)] for y in range(16)
        ]
    return out


def _span(i):
    return (i * 16) // SIZE, ((i + 1) * 16 + SIZE - 1) // SIZE


def orscale(m):
    return [
        [
            int(any(m[sy][sx] for sy in range(*_span(y)) for sx in range(*_span(x))))
            for x in range(SIZE)
        ]
        for y in range(SIZE)
    ]


def coverage(m, th=0.4):
    out = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            tot = lit = 0
            for sy in range(16):
                oy = max(0, min((sy + 1) * SIZE, (y + 1) * 16) - max(sy * SIZE, y * 16))
                for sx in range(16):
                    ox = max(
                        0, min((sx + 1) * SIZE, (x + 1) * 16) - max(sx * SIZE, x * 16)
                    )
                    tot += ox * oy
                    lit += ox * oy * m[sy][sx]
            row.append(int(lit >= th * tot))
        out.append(row)
    return out


RIGHT = [
    "......#.....",
    "......##....",
    "......###...",
    "......####..",
    ".##########.",
    ".###########",
    ".###########",
    ".##########.",
    "......####..",
    "......###...",
    "......##....",
    "......#.....",
]
THUMBUP = [
    "......##....",
    ".....#..#...",
    ".....#..#...",
    "....#..#....",
    "..##...####.",
    ".##.......#.",
    ".##.....###.",
    ".##.......#.",
    ".##.....###.",
    ".##.......#.",
    "..#......#..",
    "...######...",
]

HAND = {
    "turkey_leg": [
        ".##.........",
        "####........",
        "####........",
        ".###........",
        "...#.###....",
        "....#...##..",
        "....#.....#.",
        ".....#.....#",
        ".....#.....#",
        ".....#.....#",
        "......#...#.",
        ".......###..",
    ],
    "bowling": [
        "........##..",
        ".......#..#.",
        ".......#..#.",
        "........##..",
        "........##..",
        "..###..#..#.",
        ".#...##....#",
        "#..#..#....#",
        "#.#...#....#",
        "#.....#....#",
        ".#...#.#..#.",
        "..###...##..",
    ],
    "praying": [
        ".....##.....",
        "....#..#....",
        "....#..#....",
        "....#..#....",
        "...##..##...",
        "..#.#..#.#..",
        "..#..##..#..",
        "..#..##..#..",
        "..#..##..#..",
        ".##..##..##.",
        "###..##..###",
        "####....####",
    ],
    "fog": [
        "............",
        "..#...#...#.",
        ".#.#.#.#.#.#",
        "#...#...#...",
        "............",
        ".#...#...#..",
        "#.#.#.#.#.#.",
        "...#...#...#",
        "............",
        "..#...#...#.",
        ".#.#.#.#.#.#",
        "#...#...#...",
    ],
    "ghost": [
        "....####....",
        "..########..",
        ".##########.",
        ".##.####.##.",
        "###.####.###",
        "############",
        "####....####",
        ".###....###.",
        ".##########.",
        ".##########.",
        ".##########.",
        ".##..##..##.",
    ],
    "clipboard": [
        "....####....",
        ".####..####.",
        ".#.######.#.",
        ".#........#.",
        ".#.######.#.",
        ".#........#.",
        ".#.######.#.",
        ".#........#.",
        ".#.######.#.",
        ".#........#.",
        ".#........#.",
        ".##########.",
    ],
    "snowflake": [
        ".....#......",
        ".#..###..#..",
        "..#..#..#...",
        "...#.#.#....",
        ".#..###..#..",
        "###########.",
        ".#..###..#..",
        "...#.#.#....",
        "..#..#..#...",
        ".#..###..#..",
        ".....#......",
        "............",
    ],
}

DIAGONAL = {
    "north_east_arrow",
    "north_west_arrow",
    "south_east_arrow",
    "south_west_arrow",
}


def grid(rows):
    return [[int(c == "#") for c in r] for r in rows]


def rot_cw(m):
    return [list(r) for r in zip(*m[::-1], strict=True)]


def cardinal():
    right = grid(RIGHT)
    down = rot_cw(right)
    left = rot_cw(down)
    up = rot_cw(left)
    return {
        "rightwards_arrow": right,
        "downwards_arrow": down,
        "leftwards_arrow": left,
        "upwards_arrow": up,
        "thumbup": grid(THUMBUP),
        "thumbdown": grid(THUMBUP[::-1]),
        **{k: grid(v) for k, v in HAND.items()},
    }


def build(bitmaps):
    hand = cardinal()
    out = {}
    for name, m in bitmaps.items():
        if name in hand:
            out[name] = hand[name]
        elif name == "poo":
            m2 = coverage(m)
            m2[3][8] = m2[4][8] = 1  # close the gap in the right outline
            out[name] = m2
        elif name in DIAGONAL:
            out[name] = orscale(m)
        else:
            out[name] = coverage(m)
    return out


def xbm_bytes(m):
    out = []
    for row in m:
        v = sum(bit << x for x, bit in enumerate(row))
        out += [v & 0xFF, v >> 8]
    return out


def main():
    src_path, out_path = sys.argv[1:3]
    src = open(src_path, encoding="utf-8").read()
    small = build(parse_bitmaps(src))

    table = re.search(r"(const Emote emotes\[\] = \{\n.*?\n\};\n)", src, re.S).group(1)
    entry = re.compile(r'(\{"(?:[^"\\]|\\.)*",\s*(\w+),\s*)\w+,\s*\w+(\})')
    table = entry.sub(lambda m: f"{m.group(1)}{SIZE}, {SIZE}{m.group(3)}", table)
    missing = set(re.findall(r'\{"(?:[^"\\]|\\.)*",\s*(\w+),', table)) - set(small)
    if missing:
        sys.exit(f"no bitmap for {sorted(missing)}")

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write('#include "configuration.h"\n')
        f.write("#if HAS_SCREEN && defined(OLED_JA)\n")
        f.write(
            "// Generated by bin/gen_kanji_emotes.py from emotes.cpp; 12x12 emotes that sit on a kanji line.\n"
        )
        f.write('#include "emotes.h"\n\nnamespace graphics\n{\n\n')
        f.write(table)
        f.write(
            "\nconst int numEmotes = sizeof(emotes) / sizeof(emotes[0]);\n\n#ifndef EXCLUDE_EMOJI\n"
        )
        for name in sorted(small):
            vals = ", ".join(f"0x{b:02X}" for b in xbm_bytes(small[name]))
            f.write(f"const unsigned char {name}[] PROGMEM = {{{vals}}};\n")
        f.write("#endif\n\n} // namespace graphics\n\n#endif\n")
    style = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..",
        ".trunk",
        "configs",
        ".clang-format",
    )
    if shutil.which("clang-format") and os.path.exists(style):
        subprocess.run(
            ["clang-format", f"--style=file:{style}", "-i", out_path], check=True
        )
    print(f"{len(small)} bitmaps, {len(small) * SIZE * 2} bytes")


if __name__ == "__main__":
    main()
