// Regression tests for the alert-banner font-tag pipeline ([S]/[M]/[L] line prefixes).
//
// The BLE pairing banner (src/platform/nrf52/NRF52Bluetooth.cpp) sends
// "Bluetooth\nPIN\n[M]<pin>" with notification type pairing_pin. The [M] prefix is a
// font-change tag, never text: it must be stripped by parseBannerMessageWithFonts and,
// crucially, must also be stripped when a draw resolves a line the parsed cache doesn't
// cover (the shape of the historical bug where the pairing PIN rendered a literal "[M]").
#include "MeshTypes.h" // Include BEFORE TestUtil.h (provides NodeNum, etc.)
#include "TestUtil.h"  // initializeTestEnvironment()
#include <unity.h>

#if HAS_SCREEN // Same guard as the module under test

#include "graphics/draw/NotificationRenderer.h"
#include <cstring>

using graphics::NotificationRenderer;
using graphics::notificationTypeEnum;

static const char *BLE_PIN_MESSAGE = "Bluetooth\nPIN\n[M]123 456";

// Reset every static the tests touch, so each case starts from a known state.
void setUp(void)
{
    NotificationRenderer::alertBannerMessage[0] = '\0';
    NotificationRenderer::parseBannerMessageWithFonts("");
    NotificationRenderer::alertBannerOptions = 0;
    NotificationRenderer::current_notification_type = notificationTypeEnum::none;
}

void tearDown(void) {}

// Simulate Screen::showOverlayBanner storing and parsing a banner message.
static void showBanner(const char *message, notificationTypeEnum type, uint8_t options = 0)
{
    strncpy(NotificationRenderer::alertBannerMessage, message, 255);
    NotificationRenderer::alertBannerMessage[255] = '\0';
    NotificationRenderer::parseBannerMessageWithFonts(NotificationRenderer::alertBannerMessage);
    NotificationRenderer::alertBannerOptions = options;
    NotificationRenderer::current_notification_type = type;
}

// --- parseBannerMessageWithFonts ---

void test_pairing_message_parses_and_strips_medium_tag()
{
    showBanner(BLE_PIN_MESSAGE, notificationTypeEnum::pairing_pin);

    TEST_ASSERT_EQUAL_UINT8(3, NotificationRenderer::alertBannerLineCount);
    TEST_ASSERT_EQUAL_STRING("Bluetooth", NotificationRenderer::alertBannerLines[0]);
    TEST_ASSERT_EQUAL_STRING("PIN", NotificationRenderer::alertBannerLines[1]);
    TEST_ASSERT_EQUAL_STRING("123 456", NotificationRenderer::alertBannerLines[2]);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_DEFAULT, NotificationRenderer::alertBannerLineFonts[0]);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_DEFAULT, NotificationRenderer::alertBannerLineFonts[1]);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_MEDIUM, NotificationRenderer::alertBannerLineFonts[2]);
}

void test_small_and_large_tags_parse()
{
    showBanner("[S]small\n[L]large", notificationTypeEnum::text_banner);

    TEST_ASSERT_EQUAL_UINT8(2, NotificationRenderer::alertBannerLineCount);
    TEST_ASSERT_EQUAL_STRING("small", NotificationRenderer::alertBannerLines[0]);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_SMALL, NotificationRenderer::alertBannerLineFonts[0]);
    TEST_ASSERT_EQUAL_STRING("large", NotificationRenderer::alertBannerLines[1]);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_LARGE, NotificationRenderer::alertBannerLineFonts[1]);
}

void test_unknown_tag_is_kept_as_text()
{
    showBanner("[X]hello", notificationTypeEnum::text_banner);

    TEST_ASSERT_EQUAL_STRING("[X]hello", NotificationRenderer::alertBannerLines[0]);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_DEFAULT, NotificationRenderer::alertBannerLineFonts[0]);
}

void test_tag_not_at_line_start_is_kept_as_text()
{
    showBanner("PIN [M]x", notificationTypeEnum::text_banner);

    TEST_ASSERT_EQUAL_STRING("PIN [M]x", NotificationRenderer::alertBannerLines[0]);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_DEFAULT, NotificationRenderer::alertBannerLineFonts[0]);
}

void test_tag_only_line_yields_empty_text_with_font()
{
    showBanner("[L]", notificationTypeEnum::text_banner);

    TEST_ASSERT_EQUAL_STRING("", NotificationRenderer::alertBannerLines[0]);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_LARGE, NotificationRenderer::alertBannerLineFonts[0]);
}

void test_lone_bracket_line_is_kept_as_text()
{
    showBanner("[", notificationTypeEnum::text_banner);

    TEST_ASSERT_EQUAL_STRING("[", NotificationRenderer::alertBannerLines[0]);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_DEFAULT, NotificationRenderer::alertBannerLineFonts[0]);
}

// --- resolveBannerLine: what the draw code actually puts on the panel ---

void test_resolve_uses_parsed_lines_for_pairing_pin()
{
    showBanner(BLE_PIN_MESSAGE, notificationTypeEnum::pairing_pin);

    NotificationRenderer::BannerFont font = NotificationRenderer::BANNER_FONT_DEFAULT;
    const char *text = NotificationRenderer::resolveBannerLine(2, "[M]123 456", font);
    TEST_ASSERT_EQUAL_STRING("123 456", text);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_MEDIUM, font);
}

// The historical bug: the pairing banner drawn from the raw message, with the parsed-line
// cache not consulted (before the pairing_pin type was tag-aware) or not populated (a draw
// racing the parse from the BLE task). The tag must still act as a font change, not text.
void test_resolve_strips_tag_when_parsed_cache_missing()
{
    strncpy(NotificationRenderer::alertBannerMessage, BLE_PIN_MESSAGE, 255);
    NotificationRenderer::current_notification_type = notificationTypeEnum::pairing_pin;
    NotificationRenderer::alertBannerOptions = 0;
    // Deliberately no parseBannerMessageWithFonts call: cache empty.

    NotificationRenderer::BannerFont font = NotificationRenderer::BANNER_FONT_DEFAULT;
    const char *text = NotificationRenderer::resolveBannerLine(2, "[M]123 456", font);
    TEST_ASSERT_EQUAL_STRING("123 456", text);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_MEDIUM, font);
}

// Picker content can be user data (e.g. node names); it must never be tag-interpreted.
void test_resolve_leaves_picker_lines_untouched()
{
    NotificationRenderer::current_notification_type = notificationTypeEnum::node_picker;
    NotificationRenderer::alertBannerOptions = 0;

    NotificationRenderer::BannerFont font = NotificationRenderer::BANNER_FONT_LARGE;
    const char *text = NotificationRenderer::resolveBannerLine(0, "[M]allory", font);
    TEST_ASSERT_EQUAL_STRING("[M]allory", text);
    TEST_ASSERT_EQUAL(NotificationRenderer::BANNER_FONT_DEFAULT, font);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    RUN_TEST(test_pairing_message_parses_and_strips_medium_tag);
    RUN_TEST(test_small_and_large_tags_parse);
    RUN_TEST(test_unknown_tag_is_kept_as_text);
    RUN_TEST(test_tag_not_at_line_start_is_kept_as_text);
    RUN_TEST(test_tag_only_line_yields_empty_text_with_font);
    RUN_TEST(test_lone_bracket_line_is_kept_as_text);

    RUN_TEST(test_resolve_uses_parsed_lines_for_pairing_pin);
    RUN_TEST(test_resolve_strips_tag_when_parsed_cache_missing);
    RUN_TEST(test_resolve_leaves_picker_lines_untouched);

    exit(UNITY_END());
}

void loop() {}

#else // !HAS_SCREEN

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

void loop() {}

#endif // HAS_SCREEN
