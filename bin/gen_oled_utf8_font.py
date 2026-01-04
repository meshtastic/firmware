#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Generate UTF-8 bitmap font header for SSD1306 OLED.

Features
- Input TTF/TTC (default: Windows SimSun simsun.ttc)
- Sizes: 12x12, 16x16, 24x24 (monospace grid)
- Charset sources: GB2312 or GBK full scan; optional extra from UTF-8 text file; optional Japanese Kana ranges
- Exclude ASCII by default
- Output C header with:
  - uint16_t map[]  // Unicode code points (sorted)
  - uint32_t offset[] // start offsets per glyph (offset[count] is end)
  - uint8_t data[] // packed in SSD1306 column-major, 8-pixel pages

Usage Examples
- GB2312 + Kana, 16x16, SimSun TTC index 0:
  python bin/gen_oled_utf8_font.py --size 16 --charset gb2312 --kana \
    --ttf "C:/Windows/Fonts/simsun.ttc" --ttc-index 0 \
    --output src/graphics/fonts/utf8_16x16.h --prefix utf8_16x16_

- GBK + Kana + custom text set:
  python bin/gen_oled_utf8_font.py --size 24 --charset gbk --kana --txt data/needed.txt \
    --ttf "C:/Windows/Fonts/simsun.ttc" --ttc-index 0 \
    --output src/graphics/fonts/utf8_24x24.h --prefix utf8_24x24_

Notes
- For TTC collections (e.g., simsun.ttc), use --ttc-index to pick a face.
- Ensure Pillow (PIL) is installed: pip install Pillow
"""

import argparse
import sys
import os
from typing import List, Tuple, Set

try:
    from PIL import Image, ImageDraw, ImageFont
except Exception as e:
    print("[ERROR] Pillow is required. Install with: pip install Pillow", file=sys.stderr)
    raise


def build_charset_gb2312() -> Set[str]:
    chars: Set[str] = set()
    # GB2312 valid ranges: high byte 0xA1..0xF7, low byte 0xA1..0xFE
    for b1 in range(0xA1, 0xF8):
        for b2 in range(0xA1, 0xFF):
            try:
                s = bytes([b1, b2]).decode('gb2312')
                if s:
                    chars.add(s)
            except Exception:
                pass
    return chars


def build_charset_gbk() -> Set[str]:
    chars: Set[str] = set()
    # GBK valid ranges: lead 0x81..0xFE, trail 0x40..0xFE, excluding 0x7F
    for b1 in range(0x81, 0xFF):
        for b2 in range(0x40, 0xFF):
            if b2 == 0x7F:
                continue
            try:
                s = bytes([b1, b2]).decode('gbk')
                if s:
                    chars.add(s)
            except Exception:
                pass
    return chars


def add_kana(chars: Set[str]) -> None:
    ranges = [
        (0x3040, 0x309F),  # Hiragana
        (0x30A0, 0x30FF),  # Katakana
        (0x31F0, 0x31FF),  # Katakana Phonetic Extensions
        (0xFF66, 0xFF9D),  # Halfwidth Katakana
    ]
    for a, b in ranges:
        for cp in range(a, b + 1):
            try:
                ch = chr(cp)
                chars.add(ch)
            except Exception:
                pass


def add_from_txt(chars: Set[str], path: str) -> None:
    data = open(path, 'r', encoding='utf-8').read()
    for ch in data:
        # skip typical control chars
        if ch == '\n' or ch == '\r' or ch == '\t' or ch == '\x00':
            continue
        chars.add(ch)


def filter_charset(chars: Set[str], include_ascii: bool) -> List[int]:
    cps: Set[int] = set()
    for ch in chars:
        if not ch:
            continue
        cp = ord(ch)
        # Skip surrogates, noncharacters, and control range by default
        if 0xD800 <= cp <= 0xDFFF:
            continue
        if 0xFDD0 <= cp <= 0xFDEF or (cp & 0xFFFE) == 0xFFFE:
            continue
        if not include_ascii and 0x20 <= cp <= 0x7E:
            # Exclude ASCII printable unless asked
            continue
        cps.add(cp)
    return sorted(cps)


def render_glyph_bitmap(cp: int, font: ImageFont.FreeTypeFont, N: int, threshold: int = 128) -> List[int]:
    """
    Render a single code point to NxN 1bpp, then pack into SSD1306 bytes.
    Returns byte array of length N * ceil(N/8) in column-major, page height=8.
    """
    ch = chr(cp)
    # Compute glyph bbox
    try:
        bbox = font.getbbox(ch)
    except Exception:
        # Fallback path for older PIL: render large then crop getbbox()
        bbox = None

    img = Image.new('L', (N, N), 0)
    draw = ImageDraw.Draw(img)

    if bbox is None:
        # Fallback: draw onto a larger canvas to infer bbox
        tmp = Image.new('L', (N * 3, N * 3), 0)
        ImageDraw.Draw(tmp).text((N, N), ch, font=font, fill=255)
        tbbox = tmp.getbbox()
        if not tbbox:
            # empty glyph, return zeros
            return [0] * (N * ((N + 7) // 8))
        w = tbbox[2] - tbbox[0]
        h = tbbox[3] - tbbox[1]
        x = (N - w) // 2
        y = (N - h) // 2
        # paste cropped region into N×N
        crop = tmp.crop(tbbox)
        img.paste(crop, (x, y))
    else:
        gx0, gy0, gx1, gy1 = bbox
        gw = max(0, gx1 - gx0)
        gh = max(0, gy1 - gy0)
        x = (N - gw) // 2
        y = (N - gh) // 2
        # position text such that its bbox aligns at (x,y)
        draw.text((x - gx0, y - gy0), ch, font=font, fill=255)

    # Threshold to 1bpp
    img = img.convert('L')
    pix = img.load()
    H = N
    W = N
    pages = (H + 7) // 8
    out: List[int] = []
    for x in range(W):
        for p in range(pages):
            byte = 0
            for bit in range(8):
                y = p * 8 + bit
                if y >= H:
                    break
                v = pix[x, y]
                on = 1 if v >= threshold else 0
                byte |= (on << bit)  # LSB = top row within page
            out.append(byte)
    return out


def generate_header(prefix: str, N: int, cps: List[int], glyph_bytes: List[List[int]]) -> str:
    guard = (prefix.upper() + "GUARD").replace('-', '_').replace(' ', '_')
    count = len(cps)
    pages = (N + 7) // 8
    stride = N * pages

    def fmt_array_u16(values: List[int], per=12) -> str:
        chunks = []
        for i in range(0, len(values), per):
            line = ", ".join(f"0x{v:04X}" for v in values[i:i+per])
            chunks.append("    " + line)
        return (",\n".join(chunks)) if chunks else ""

    def fmt_array_u32(values: List[int], per=8) -> str:
        chunks = []
        for i in range(0, len(values), per):
            line = ", ".join(f"0x{v:08X}" for v in values[i:i+per])
            chunks.append("    " + line)
        return (",\n".join(chunks)) if chunks else ""

    def fmt_array_u8(values: List[int], per=16) -> str:
        chunks = []
        for i in range(0, len(values), per):
            line = ", ".join(f"0x{v:02X}" for v in values[i:i+per])
            chunks.append("    " + line)
        return (",\n".join(chunks)) if chunks else ""

    # offsets: i * stride (monospace). Keep generality in case future variable width/encode.
    offsets = [i * stride for i in range(count)] + [count * stride]

    # flatten data
    data: List[int] = []
    for gb in glyph_bytes:
        data.extend(gb)

    header = []
    header.append("#pragma once")
    header.append("#include <Arduino.h>")
    header.append("")
    header.append(f"#define {prefix}HEIGHT {N}")
    header.append(f"#define {prefix}WIDTH {N}")
    header.append(f"#define {prefix}COUNT {count}")
    header.append("")
    header.append(f"static const uint16_t {prefix}map[{count}] PROGMEM = {{")
    header.append(fmt_array_u16(cps))
    header.append("};")
    header.append("")
    # header.append(f"static const uint32_t {prefix}offset[{count + 1}] PROGMEM = {{")
    # header.append(fmt_array_u32(offsets))
    # header.append("};")
    # header.append("")
    header.append(f"static const uint8_t {prefix}data[{len(data)}] PROGMEM = {{")
    header.append(fmt_array_u8(data))
    header.append("};")
    header.append("")
    # Optional descriptor for convenient passing
    header.append("typedef struct {\n    const uint16_t* map;\n    const uint8_t* data;\n    uint16_t count;\n    uint8_t w;\n    uint8_t h;\n} FontUTF8;\n") #\n    const uint32_t* offset;
    header.append(f"static const FontUTF8 {prefix}font = {{ {prefix}map, {prefix}data, {prefix}COUNT, {prefix}WIDTH, {prefix}HEIGHT }};") #{prefix}offset, 
    header.append("")
    return "\n".join(header) + "\n"


def main():
    ap = argparse.ArgumentParser(description="Generate UTF-8 OLED font header (SSD1306 layout)")
    ap.add_argument('--ttf', default=r'C:/Windows/Fonts/simsun.ttc', help='Path to TTF/TTC font (default: SimSun)')
    ap.add_argument('--ttc-index', type=int, default=0, help='Face index in TTC (if applicable)')
    ap.add_argument('--size', type=int, required=True, choices=[12, 16, 24], help='Monospace box size (pixels)')
    ap.add_argument('--charset', choices=['gb2312', 'gbk'], default='gb2312', help='Base charset')
    ap.add_argument('--txt', help='UTF-8 text file to include unique characters from')
    ap.add_argument('--kana', action='store_true', help='Include Japanese Hiragana/Katakana')
    ap.add_argument('--include-ascii', action='store_true', help='Include ASCII 0x20-0x7E (default off)')
    ap.add_argument('--output', required=True, help='Output header file path')
    ap.add_argument('--prefix', required=True, help='Symbol prefix, e.g., utf8_16x16_')
    args = ap.parse_args()

    # Build charset
    chars: Set[str] = set()
    if args.charset == 'gb2312':
        print('[INFO] Building GB2312 charset...')
        chars |= build_charset_gb2312()
    elif args.charset == 'gbk':
        print('[INFO] Building GBK charset...')
        chars |= build_charset_gbk()

    if args.txt:
        print(f"[INFO] Including characters from: {args.txt}")
        add_from_txt(chars, args.txt)

    if args.kana:
        print('[INFO] Including Japanese Kana blocks')
        add_kana(chars)

    cps = filter_charset(chars, include_ascii=args.include_ascii)
    print(f"[INFO] Unique code points: {len(cps)}")

    if len(cps) == 0:
        print('[ERROR] No characters selected. Abort.')
        sys.exit(1)

    # Load font
    font_path = args.ttf
    if not os.path.exists(font_path):
        print(f"[ERROR] Font not found: {font_path}", file=sys.stderr)
        sys.exit(1)

    try:
        font = ImageFont.truetype(font_path, args.size, index=args.ttc_index, layout_engine=ImageFont.Layout.BASIC)
    except Exception:
        # Older Pillow may not support layout_engine arg
        font = ImageFont.truetype(font_path, args.size, index=args.ttc_index)

    N = args.size
    pages = (N + 7) // 8
    stride = N * pages

    glyph_bytes: List[List[int]] = []
    skipped = 0
    for i, cp in enumerate(cps):
        data = render_glyph_bitmap(cp, font, N)
        if sum(data) == 0:
            # Empty glyph; likely missing; skip but keep track
            skipped += 1
            # still include as empty cell to preserve indexing (map references existing cps)
            # If you prefer to drop empty glyphs, uncomment below and also drop cp from cps list.
            # continue
        glyph_bytes.append(data)
        if (i + 1) % 500 == 0:
            print(f"[INFO] Rendered {i+1}/{len(cps)} glyphs...")

    print(f"[INFO] Render finished. Empty glyphs: {skipped}")
    total_bytes = len(cps) * stride
    print(f"[INFO] Data bytes: {total_bytes} (each glyph {stride} bytes, {pages} pages)")

    # Normalize prefix for C identifiers
    prefix = args.prefix
    if not prefix.endswith('_'):
        prefix = prefix + '_'

    header = generate_header(prefix, N, cps, glyph_bytes)

    out_path = args.output
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(header)

    print(f"[OK] Header generated: {out_path}")


if __name__ == '__main__':
    main()

