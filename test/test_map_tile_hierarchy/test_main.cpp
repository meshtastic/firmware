// Verifies MTB1 pixel interpretation and coordinate projection when a sparse map
// tile is absent. This guards MSB-first, white-is-one rendering and the quadrant
// math that prevents blank blocks between overlapping high-detail map regions.
#include "TestUtil.h"
#include <unity.h>

#ifndef MESHTASTIC_INCLUDE_INKHUD
#define MESHTASTIC_INCLUDE_INKHUD
#endif
#include "../../src/graphics/niche/InkHUD/Applets/Bases/Map/MapTileUtils.h"

using namespace NicheGraphics::InkHUD::MapTileUtils;

void setUp(void) {}
void tearDown(void) {}

void test_mtb1_pixels_are_msb_first(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x80, mtb1PixelMask(0));
    TEST_ASSERT_EQUAL_HEX8(0x01, mtb1PixelMask(7));
    TEST_ASSERT_EQUAL_HEX8(0x80, mtb1PixelMask(8));
    TEST_ASSERT_EQUAL_UINT32(0, mtb1PixelOffset(0, 0));
    TEST_ASSERT_EQUAL_UINT32(TILE_SIZE, mtb1PixelOffset(8, 0));
}

void test_mtb1_pixels_use_native_eink_polarity(void)
{
    TEST_ASSERT_TRUE(mtb1PixelIsWhite(0x80, 0));
    TEST_ASSERT_FALSE(mtb1PixelIsWhite(0x00, 0));
    TEST_ASSERT_TRUE(mtb1PixelIsWhite(0x01, 7));
}

void test_ancestorPixel_preserves_pixels_at_same_zoom(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, ancestorPixel(123, 0, 0));
    TEST_ASSERT_EQUAL_UINT16(255, ancestorPixel(123, 255, 0));
}

void test_ancestorPixel_selects_the_correct_parent_quadrant(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, ancestorPixel(8, 0, 1));
    TEST_ASSERT_EQUAL_UINT16(127, ancestorPixel(8, 255, 1));
    TEST_ASSERT_EQUAL_UINT16(128, ancestorPixel(9, 0, 1));
    TEST_ASSERT_EQUAL_UINT16(255, ancestorPixel(9, 255, 1));
}

void test_ancestorPixel_projects_across_multiple_zoom_levels(void)
{
    TEST_ASSERT_EQUAL_UINT16(216, ancestorPixel(35436, 0, 7));
    TEST_ASSERT_EQUAL_UINT16(217, ancestorPixel(35436, 255, 7));
    TEST_ASSERT_EQUAL_UINT16(255, ancestorPixel(11, 255, 2));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    RUN_TEST(test_mtb1_pixels_are_msb_first);
    RUN_TEST(test_mtb1_pixels_use_native_eink_polarity);
    RUN_TEST(test_ancestorPixel_preserves_pixels_at_same_zoom);
    RUN_TEST(test_ancestorPixel_selects_the_correct_parent_quadrant);
    RUN_TEST(test_ancestorPixel_projects_across_multiple_zoom_levels);

    exit(UNITY_END());
}

void loop() {}
