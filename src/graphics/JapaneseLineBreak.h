#pragma once
#ifdef OLED_JA
// Japanese line-break rules (kinsoku) for message wrapping: whether a break may fall between two characters.
#include "graphics/KanjiFont.h"
#include <stddef.h>
#include <stdint.h>

namespace graphics
{
namespace JapaneseLineBreak
{

// Code point of the UTF-8 character at s[0..len); 0 at the end of the text or on a malformed sequence.
static inline uint32_t peekCodePoint(const char *s, size_t len)
{
    uint32_t cp = 0;
    return (len > 0 && KanjiFont::decodeUtf8(s, 0, len, cp) > 0) ? cp : 0;
}

static inline bool isInList(uint32_t cp, const uint16_t *list, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (list[i] == cp)
            return true;
    }
    return false;
}

// Opening brackets and quotes must not end a line.
static inline bool isNoBreakAfter(uint32_t cp)
{
    static const uint16_t OPENING[] = {0x2018, 0x201C, 0x3008, 0x300A, 0x300C, 0x300E, 0x3010, 0x3014,
                                       0x3016, 0xFF08, 0xFF3B, 0xFF5B, 0xFF62, '(',    '['};
    return isInList(cp, OPENING, sizeof(OPENING) / sizeof(OPENING[0]));
}

// Closing brackets, punctuation, small kana, the long-vowel mark and units must not start a line.
static inline bool isNoBreakBefore(uint32_t cp)
{
    static const uint16_t CLOSING[] = {0x2019, 0x201D, 0x2025, 0x2026, 0x2030, 0x2103, 0x3001, 0x3002, 0x3005, 0x3009,
                                       0x300B, 0x300D, 0x300F, 0x3011, 0x3015, 0x3017, 0x309B, 0x309C, 0x309D, 0x309E,
                                       0x30FB, 0x30FC, 0x30FD, 0x30FE, 0xFF01, 0xFF05, 0xFF09, 0xFF0C, 0xFF0E, 0xFF1A,
                                       0xFF1B, 0xFF1F, 0xFF3D, 0xFF5D, 0xFF61, 0xFF63, 0xFF64, 0xFF65, 0xFF70, 0xFF9E,
                                       0xFF9F, '!',    ')',    ',',    '.',    ':',    ';',    '?',    ']'};
    static const uint16_t SMALL_KANA[] = {0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085,
                                          0x3087, 0x308E, 0x3095, 0x3096, 0x30A1, 0x30A3, 0x30A5, 0x30A7,
                                          0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE, 0x30F5, 0x30F6};
    if (cp >= 0xFF67 && cp <= 0xFF6F) // half-width small kana
        return true;
    return isInList(cp, CLOSING, sizeof(CLOSING) / sizeof(CLOSING[0])) ||
           isInList(cp, SMALL_KANA, sizeof(SMALL_KANA) / sizeof(SMALL_KANA[0]));
}

static inline bool isFullWidthAlnum(uint32_t cp)
{
    return (cp >= 0xFF10 && cp <= 0xFF19) || (cp >= 0xFF21 && cp <= 0xFF3A) || (cp >= 0xFF41 && cp <= 0xFF5A);
}

// True when a line may break between the character `cur` and the one starting at next[0..nextLen).
static inline bool canBreakBetween(uint32_t cur, const char *next, size_t nextLen)
{
    const uint32_t nextCp = peekCodePoint(next, nextLen);
    if (nextCp == 0)
        return true;
    if (KanjiFont::isInvisibleModifier(nextCp)) // variation selectors and skin tones stay with the character before them
        return false;
    if (isNoBreakAfter(cur) || isNoBreakBefore(nextCp))
        return false;
    return !(isFullWidthAlnum(cur) && isFullWidthAlnum(nextCp));
}

} // namespace JapaneseLineBreak
} // namespace graphics

#endif // OLED_JA
