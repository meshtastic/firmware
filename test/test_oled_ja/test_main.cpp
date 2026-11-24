// Checks for the Japanese OLED glyph table (src/graphics/fonts/KanjiFontK8x12.cpp), its lookup
// (src/graphics/KanjiFont.h) and the kinsoku line-break rules (src/graphics/JapaneseLineBreak.h),
// all compiled only with OLED_JA.
//
// With OLED_JA set, EmoteRenderer draws every character found in the table as an 8x12 XBM, so the
// table must stay sorted for the binary search, cover kana, JIS level 1 kanji and half-width
// katakana, and keep half-width glyphs inside the left half of the cell, since they advance by
// only 4 px. The lookup decodes UTF-8 straight out of message payloads, which are opaque bytes, so
// truncated or malformed sequences must miss rather than read past the buffer.
//
// The table and headers are included directly with OLED_JA defined: the native test build compiles
// src/ without it, so these definitions exist only in this suite.
#include "TestUtil.h"
#include <unity.h>

#define OLED_JA 1
#include "graphics/JapaneseLineBreak.h"
#include "graphics/KanjiFont.h"
#include "graphics/fonts/KanjiFontK8x12.cpp"

#include <cstring>

using namespace graphics;

void setUp(void) {}
void tearDown(void) {}

static size_t indexOf(uint16_t cp)
{
    for (size_t i = 0; i < KanjiFontData::KANJI_COUNT; i++) {
        if (KanjiFontData::KANJI_CODES[i] == cp)
            return i;
    }
    return SIZE_MAX;
}

void test_table_sizes_agree()
{
    const size_t count = KanjiFontData::KANJI_COUNT;
    TEST_ASSERT_EQUAL_UINT32(count, sizeof(KanjiFontData::KANJI_CODES) / sizeof(KanjiFontData::KANJI_CODES[0]));
    TEST_ASSERT_EQUAL_UINT32(count * KanjiFontData::GLYPH_BYTES, sizeof(KanjiFontData::KANJI_BITMAPS));
}

void test_codes_are_strictly_ascending()
{
    for (size_t i = 1; i < KanjiFontData::KANJI_COUNT; i++)
        TEST_ASSERT_TRUE_MESSAGE(KanjiFontData::KANJI_CODES[i - 1] < KanjiFontData::KANJI_CODES[i], "binary search needs order");
}

void test_every_code_finds_its_own_bitmap()
{
    for (size_t i = 0; i < KanjiFontData::KANJI_COUNT; i++) {
        const uint8_t *glyph = KanjiFont::findGlyph(KanjiFontData::KANJI_CODES[i]);
        TEST_ASSERT_EQUAL_PTR(&KanjiFontData::KANJI_BITMAPS[i * KanjiFontData::GLYPH_BYTES], glyph);
    }
}

void test_covers_kana_kanji_and_half_width_katakana()
{
    for (uint16_t cp = 0x3041; cp <= 0x3093; cp++) // ぁ..ん
        TEST_ASSERT_NOT_NULL(KanjiFont::findGlyph(cp));
    for (uint16_t cp = 0x30A1; cp <= 0x30F6; cp++) // ァ..ヶ
        TEST_ASSERT_NOT_NULL(KanjiFont::findGlyph(cp));
    for (uint16_t cp = 0xFF61; cp <= 0xFF9F; cp++) // ｡..ﾟ
        TEST_ASSERT_NOT_NULL(KanjiFont::findGlyph(cp));
    const uint16_t present[] = {0x4E9C /* 亜 */, 0x6F22 /* 漢 */, 0x5B57 /* 字 */, 0x8155 /* 腕 */, 0x3001 /* 、 */,
                                0x3002 /* 。 */, 0x300C /* 「 */, 0x30FC /* ー */, 0xFF01 /* ！ */, 0x3005 /* 々 */};
    for (uint16_t cp : present)
        TEST_ASSERT_NOT_NULL(KanjiFont::findGlyph(cp));
}

void test_leaves_latin_quotes_and_level_2_kanji_to_the_default_font()
{
    TEST_ASSERT_NULL(KanjiFont::findGlyph('A'));
    TEST_ASSERT_NULL(KanjiFont::findGlyph(0x00E9));  // é
    TEST_ASSERT_NULL(KanjiFont::findGlyph(0x2019));  // ' stays narrow
    TEST_ASSERT_NULL(KanjiFont::findGlyph(0x5F0C));  // 弌, JIS level 2
    TEST_ASSERT_NULL(KanjiFont::findGlyph(0x1F600)); // emoji, outside the BMP table
}

void test_half_width_katakana_fit_the_left_half_and_advance_4()
{
    for (uint16_t cp = 0xFF61; cp <= 0xFF9F; cp++) {
        const size_t i = indexOf(cp);
        TEST_ASSERT_NOT_EQUAL(SIZE_MAX, i);
        for (int row = 0; row < KanjiFontData::GLYPH_BYTES; row++)
            TEST_ASSERT_EQUAL_HEX8(0, KanjiFontData::KANJI_BITMAPS[i * KanjiFontData::GLYPH_BYTES + row] & 0xF0);
        TEST_ASSERT_EQUAL_INT(4, KanjiFont::advanceOf(cp));
    }
    TEST_ASSERT_EQUAL_INT(8, KanjiFont::advanceOf(0x3042)); // あ
    TEST_ASSERT_EQUAL_INT(8, KanjiFont::advanceOf(0xFF60)); // full-width ｠, just below the half-width block
    TEST_ASSERT_EQUAL_INT(8, KanjiFont::advanceOf(0xFFA0));
}

void test_every_glyph_has_ink()
{
    for (size_t i = 0; i < KanjiFontData::KANJI_COUNT; i++) {
        if (KanjiFontData::KANJI_CODES[i] == 0x3000) // ideographic space
            continue;
        uint8_t any = 0;
        for (int row = 0; row < KanjiFontData::GLYPH_BYTES; row++)
            any |= KanjiFontData::KANJI_BITMAPS[i * KanjiFontData::GLYPH_BYTES + row];
        TEST_ASSERT_NOT_EQUAL(0, any);
    }
}

void test_find_glyph_at_decodes_utf8_and_reports_width()
{
    size_t charLen = 0;
    int advance = 0;
    const char *kan = "\xE6\xBC\xA2"; // 漢
    TEST_ASSERT_EQUAL_PTR(KanjiFont::findGlyph(0x6F22), KanjiFont::findGlyphAt(kan, 0, 3, charLen, advance));
    TEST_ASSERT_EQUAL_UINT32(3, charLen);
    TEST_ASSERT_EQUAL_INT(8, advance);

    const char *ha = "a\xEF\xBE\x8A"; // aﾊ
    TEST_ASSERT_NOT_NULL(KanjiFont::findGlyphAt(ha, 1, 4, charLen, advance));
    TEST_ASSERT_EQUAL_UINT32(3, charLen);
    TEST_ASSERT_EQUAL_INT(4, advance);
}

void test_find_glyph_at_misses_on_malformed_or_truncated_input()
{
    size_t charLen = 99;
    int advance = 99;
    TEST_ASSERT_NULL(KanjiFont::findGlyphAt("A", 0, 1, charLen, advance));
    TEST_ASSERT_NULL(KanjiFont::findGlyphAt("\xE6\xBC", 0, 2, charLen, advance));         // truncated 漢
    TEST_ASSERT_NULL(KanjiFont::findGlyphAt("\xE6\x41\xA2", 0, 3, charLen, advance));     // bad continuation
    TEST_ASSERT_NULL(KanjiFont::findGlyphAt("\xA2", 0, 1, charLen, advance));             // stray continuation
    TEST_ASSERT_NULL(KanjiFont::findGlyphAt("\xF0\x9F\x98\x80", 0, 4, charLen, advance)); // 😀
    TEST_ASSERT_EQUAL_UINT32(99, charLen);                                                // untouched on a miss
    TEST_ASSERT_EQUAL_INT(99, advance);
}

static bool canBreak(const char *cur, const char *next)
{
    return JapaneseLineBreak::canBreakBetween(JapaneseLineBreak::peekCodePoint(cur), next);
}

void test_kinsoku_rules()
{
    TEST_ASSERT_TRUE(canBreak("あ", "い"));
    TEST_ASSERT_TRUE(canBreak("あ", "")); // end of text
    TEST_ASSERT_FALSE(canBreak("「", "皆"));
    TEST_ASSERT_FALSE(canBreak("（", "土"));
    TEST_ASSERT_FALSE(canBreak("様", "」"));
    TEST_ASSERT_FALSE(canBreak("は", "、"));
    TEST_ASSERT_FALSE(canBreak("は", "。"));
    TEST_ASSERT_FALSE(canBreak("は", "！"));
    TEST_ASSERT_FALSE(canBreak("は", "!"));
    TEST_ASSERT_FALSE(canBreak("ち", "ょ"));
    TEST_ASSERT_FALSE(canBreak("ち", "っ"));
    TEST_ASSERT_FALSE(canBreak("メ", "ー"));
    TEST_ASSERT_FALSE(canBreak("ﾒ", "ｯ"));
    TEST_ASSERT_FALSE(canBreak("２", "３"));
    TEST_ASSERT_FALSE(canBreak("３", "℃"));
    TEST_ASSERT_TRUE(canBreak("３", "で"));
}

void test_peek_code_point_rejects_malformed_bytes()
{
    TEST_ASSERT_EQUAL_UINT32(0x6F22, JapaneseLineBreak::peekCodePoint("\xE6\xBC\xA2"));
    TEST_ASSERT_EQUAL_UINT32(0, JapaneseLineBreak::peekCodePoint("\xE6\x41\xA2"));
    TEST_ASSERT_EQUAL_UINT32(0, JapaneseLineBreak::peekCodePoint("\xE6"));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_table_sizes_agree);
    RUN_TEST(test_codes_are_strictly_ascending);
    RUN_TEST(test_every_code_finds_its_own_bitmap);
    RUN_TEST(test_covers_kana_kanji_and_half_width_katakana);
    RUN_TEST(test_leaves_latin_quotes_and_level_2_kanji_to_the_default_font);
    RUN_TEST(test_half_width_katakana_fit_the_left_half_and_advance_4);
    RUN_TEST(test_every_glyph_has_ink);
    RUN_TEST(test_find_glyph_at_decodes_utf8_and_reports_width);
    RUN_TEST(test_find_glyph_at_misses_on_malformed_or_truncated_input);
    RUN_TEST(test_kinsoku_rules);
    RUN_TEST(test_peek_code_point_rejects_malformed_bytes);
    exit(UNITY_END());
}

void loop() {}
