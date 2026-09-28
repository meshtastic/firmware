// Structural checks for the Greek OLED fonts ArialMT_Plain_{10,16,24}_GR in
// src/graphics/fonts/OLEDDisplayFontsGR.cpp (compiled only with OLED_GR).
//
// With OLED_GR set, ScreenFonts.h swaps FONT_SMALL/MEDIUM/LARGE for these tables and
// Screen::customFontTableLookup() (src/graphics/Screen.h) turns UTF-8 Greek into
// CP-1253 slots of them, with Ώ moved to 0xAA so 0xBF can stay the ¿ fallback glyph.
// OLEDDisplay draws an empty jump entry as nothing, so every slot the decoder can emit
// must hold a glyph, in every size, and the ASCII range must match the default font so
// that the UI outside of Greek text does not change.
//
// Regressions guarded: the 16pt and 24pt tables shipping as one-glyph stubs (every
// medium/large string rendered blank); accented letters (ά έ ή ί ό ύ ώ ΐ ΰ Ϊ Ϋ ϊ ϋ and
// the capital tonos forms) left as empty slots; Ώ put back at 0xBF, which would make
// every unconvertible character render as Ώ; ASCII glyphs drifting from upstream.
//
// The decoder itself is not exercised here: Screen.cpp is linked into native tests
// without OLED_GR, so including Screen.h with it would give two different definitions
// of the inline customFontTableLookup().
#include "TestUtil.h"
#include <OLEDDisplayFonts.h>
#include <unity.h>

#define OLED_GR 1
#include "graphics/fonts/OLEDDisplayFontsGR.cpp"

#include <cstddef>
#include <cstdint>

struct Table {
    const uint8_t *data;
    size_t len;
    const uint8_t *upstream;
    uint8_t height;
};

static const Table TABLES[] = {
    {ArialMT_Plain_10_GR, sizeof(ArialMT_Plain_10_GR), ArialMT_Plain_10, 13},
    {ArialMT_Plain_16_GR, sizeof(ArialMT_Plain_16_GR), ArialMT_Plain_16, 19},
    {ArialMT_Plain_24_GR, sizeof(ArialMT_Plain_24_GR), ArialMT_Plain_24, 28},
};

static const uint8_t HEADER = 4;
static const uint8_t FIRST = 0x20;
static const uint16_t COUNT = 0xE0;
static const uint16_t EMPTY = 0xFFFF;

static const uint8_t *entry(const uint8_t *font, uint8_t code)
{
    return font + HEADER + 4 * (code - font[2]);
}

static uint16_t offsetOf(const uint8_t *font, uint8_t code)
{
    const uint8_t *e = entry(font, code);
    return (uint16_t)((e[0] << 8) | e[1]);
}

static const uint8_t *bitmapOf(const uint8_t *font, uint8_t code)
{
    return font + HEADER + 4 * font[3] + offsetOf(font, code);
}

// Every slot Screen::customFontTableLookup() can return for Greek input.
static bool isGreekSlot(uint8_t code)
{
    switch (code) {
    case 0xA1: // ΅
    case 0xA2: // Ά
    case 0xAA: // Ώ
    case 0xB4: // ΄
    case 0xB8: // Έ
    case 0xB9: // Ή
    case 0xBA: // Ί
    case 0xBC: // Ό
    case 0xBE: // Ύ
        return true;
    default:
        return code >= 0xC0 && code <= 0xFE && code != 0xD2;
    }
}

void setUp(void) {}
void tearDown(void) {}

void test_header_matches_upstream_metrics()
{
    for (const Table &t : TABLES) {
        TEST_ASSERT_EQUAL_UINT8(t.upstream[0], t.data[0]);
        TEST_ASSERT_EQUAL_UINT8(t.height, t.data[1]);
        TEST_ASSERT_EQUAL_UINT8(FIRST, t.data[2]);
        TEST_ASSERT_EQUAL_UINT8(COUNT, t.data[3]);
    }
}

void test_every_glyph_lies_inside_the_table()
{
    for (const Table &t : TABLES) {
        const size_t dataStart = HEADER + 4 * COUNT;
        for (uint16_t code = FIRST; code < FIRST + COUNT; code++) {
            uint16_t off = offsetOf(t.data, (uint8_t)code);
            if (off == EMPTY)
                continue;
            TEST_ASSERT_TRUE_MESSAGE(dataStart + off + entry(t.data, (uint8_t)code)[2] <= t.len, "glyph past end of table");
        }
    }
}

void test_greek_slots_have_glyphs_in_every_size()
{
    for (const Table &t : TABLES) {
        for (uint16_t code = FIRST; code < FIRST + COUNT; code++) {
            if (!isGreekSlot((uint8_t)code))
                continue;
            const uint8_t *e = entry(t.data, (uint8_t)code);
            TEST_ASSERT_NOT_EQUAL_MESSAGE(EMPTY, offsetOf(t.data, (uint8_t)code), "empty Greek slot");
            TEST_ASSERT_TRUE(e[2] > 0);
            TEST_ASSERT_TRUE(e[3] > 0);
        }
    }
}

void test_ascii_and_fallback_glyph_match_upstream()
{
    for (const Table &t : TABLES) {
        for (uint16_t code = FIRST; code <= 0xBF; code++) {
            if (code > 0x7E && code != 0xBF)
                continue;
            const uint8_t *ours = entry(t.data, (uint8_t)code);
            const uint8_t *theirs = entry(t.upstream, (uint8_t)code);
            TEST_ASSERT_EQUAL_UINT8(theirs[2], ours[2]);
            TEST_ASSERT_EQUAL_UINT8(theirs[3], ours[3]);
            if (ours[2] > 0)
                TEST_ASSERT_EQUAL_UINT8_ARRAY(bitmapOf(t.upstream, (uint8_t)code), bitmapOf(t.data, (uint8_t)code), ours[2]);
        }
    }
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_header_matches_upstream_metrics);
    RUN_TEST(test_every_glyph_lies_inside_the_table);
    RUN_TEST(test_greek_slots_have_glyphs_in_every_size);
    RUN_TEST(test_ascii_and_fallback_glyph_match_upstream);
    exit(UNITY_END());
}

void loop() {}
