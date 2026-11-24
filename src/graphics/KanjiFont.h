#pragma once
#ifdef OLED_JA
// Japanese glyph lookup for OLED_JA: UTF-8 decode and binary search over KanjiFontData.
#include "graphics/fonts/KanjiFontK8x12.h"
#include <stddef.h>
#include <stdint.h>

namespace graphics
{
namespace KanjiFont
{
static constexpr int GLYPH_WIDTH = KanjiFontData::GLYPH_WIDTH;
static constexpr int GLYPH_HEIGHT = KanjiFontData::GLYPH_HEIGHT;
static constexpr int HALF_WIDTH = GLYPH_WIDTH / 2;

// Half-width katakana (U+FF61-U+FF9F) advance by half a cell; everything else by a full cell.
static inline int advanceOf(uint32_t cp)
{
    return (cp >= 0xFF61 && cp <= 0xFF9F) ? HALF_WIDTH : GLYPH_WIDTH;
}

// Decode the UTF-8 character at s[pos]; returns its byte length (0 if malformed or truncated).
static inline size_t decodeUtf8(const char *s, size_t pos, size_t len, uint32_t &cp)
{
    const uint8_t c = static_cast<uint8_t>(s[pos]);
    size_t n;
    if (c < 0x80) {
        cp = c;
        return 1;
    } else if ((c & 0xE0) == 0xC0) {
        cp = c & 0x1F;
        n = 2;
    } else if ((c & 0xF0) == 0xE0) {
        cp = c & 0x0F;
        n = 3;
    } else if ((c & 0xF8) == 0xF0) {
        cp = c & 0x07;
        n = 4;
    } else {
        return 0;
    }
    if (pos + n > len)
        return 0;
    for (size_t i = 1; i < n; i++) {
        const uint8_t cc = static_cast<uint8_t>(s[pos + i]);
        if ((cc & 0xC0) != 0x80)
            return 0;
        cp = (cp << 6) | (cc & 0x3F);
    }
    return n;
}

// Bitmap (XBM, 8x12) for a code point, or nullptr when the table lacks it.
static inline const uint8_t *findGlyph(uint32_t cp)
{
    if (cp > 0xFFFF)
        return nullptr;
    int lo = 0;
    int hi = KanjiFontData::KANJI_COUNT - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const uint16_t c = KanjiFontData::KANJI_CODES[mid];
        if (c == cp)
            return &KanjiFontData::KANJI_BITMAPS[mid * KanjiFontData::GLYPH_BYTES];
        if (c < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return nullptr;
}

static constexpr uint32_t GETA = 0x3013;             // 〓, the usual Japanese mark for a missing glyph
static constexpr uint32_t GETA_RANGE_START = 0x2E80; // CJK, kana, Hangul, full-width forms, emoji and beyond

// Variation selectors, skin tones and tags modify the character before them and draw nothing.
static inline bool isInvisibleModifier(uint32_t cp)
{
    return (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x1F3FB && cp <= 0x1F3FF) || (cp >= 0xE0000 && cp <= 0xE01EF);
}

// Glyph for the character at s[pos]; sets charLen to its byte length and advance to its width when found.
// Characters from U+2E80 up that the table lacks get 〓, so each one draws and measures as one cell.
static inline const uint8_t *findGlyphAt(const char *s, size_t pos, size_t len, size_t &charLen, int &advance)
{
    uint32_t cp;
    const size_t n = decodeUtf8(s, pos, len, cp);
    if (n < 2)
        return nullptr;
    const uint8_t *g = findGlyph(cp);
    if (!g && cp >= GETA_RANGE_START && !isInvisibleModifier(cp)) {
        g = findGlyph(GETA);
        cp = GETA;
    }
    if (g) {
        charLen = n;
        advance = advanceOf(cp);
    }
    return g;
}

} // namespace KanjiFont
} // namespace graphics

#endif
