#include "DebugConfiguration.h"
#include "TestUtil.h"
#include <unity.h>

#ifdef ARCH_PORTDUINO
#include "configuration.h"

#include "modules/SerialModule.h"

// Test that empty configuration is valid.
void test_serialConfigEmptyIsValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {};

    TEST_ASSERT_TRUE(serialConfigIsValid(config));
}

// Test that basic enabled configuration is valid.
void test_serialConfigEnabledIsValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = true};

    TEST_ASSERT_TRUE(serialConfigIsValid(config));
}

// Test that configuration with override_console_serial_port and NMEA mode is valid.
void test_serialConfigWithOverrideConsoleNmeaModeIsValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA, .override_console_serial_port = true};

    TEST_ASSERT_TRUE(serialConfigIsValid(config));
}

// Test that configuration with override_console_serial_port and CalTopo mode is valid.
void test_serialConfigWithOverrideConsoleCalTopoModeIsValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO, .override_console_serial_port = true};

    TEST_ASSERT_TRUE(serialConfigIsValid(config));
}

// Test that configuration with override_console_serial_port and MS Config mode is valid.
void test_serialConfigWithOverrideConsoleMsConfigModeIsValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = true,
                                                   .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MS_CONFIG,
                                                   .override_console_serial_port = true};

    TEST_ASSERT_TRUE(serialConfigIsValid(config));
}

// Test that configuration with override_console_serial_port and DEFAULT mode is invalid.
void test_serialConfigWithOverrideConsoleDefaultModeIsInvalid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_DEFAULT, .override_console_serial_port = true};

    TEST_ASSERT_FALSE(serialConfigIsValid(config));
}

// Test that configuration with override_console_serial_port and SIMPLE mode is invalid.
void test_serialConfigWithOverrideConsoleSimpleModeIsInvalid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_SIMPLE, .override_console_serial_port = true};

    TEST_ASSERT_FALSE(serialConfigIsValid(config));
}

// Test that configuration with override_console_serial_port and TEXTMSG mode is invalid.
void test_serialConfigWithOverrideConsoleTextMsgModeIsInvalid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_TEXTMSG, .override_console_serial_port = true};

    TEST_ASSERT_FALSE(serialConfigIsValid(config));
}

// Test that configuration with override_console_serial_port and PROTO mode is invalid.
void test_serialConfigWithOverrideConsoleProtoModeIsInvalid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO, .override_console_serial_port = true};

    TEST_ASSERT_FALSE(serialConfigIsValid(config));
}

// Test that various modes work without override_console_serial_port.
void test_serialConfigVariousModesWithoutOverrideAreValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = true, .override_console_serial_port = false};

    // Test DEFAULT mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_DEFAULT;
    TEST_ASSERT_TRUE(serialConfigIsValid(config));

    // Test SIMPLE mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_SIMPLE;
    TEST_ASSERT_TRUE(serialConfigIsValid(config));

    // Test TEXTMSG mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_TEXTMSG;
    TEST_ASSERT_TRUE(serialConfigIsValid(config));

    // Test PROTO mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO;
    TEST_ASSERT_TRUE(serialConfigIsValid(config));

    // Test NMEA mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA;
    TEST_ASSERT_TRUE(serialConfigIsValid(config));

    // Test CALTOPO mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO;
    TEST_ASSERT_TRUE(serialConfigIsValid(config));
}

// --- TEXTMSG payload sanitizing ---

void test_textMsgCrLfOnlyIsNotSent(void)
{
    char buf[8] = "\r\n";

    TEST_ASSERT_EQUAL_size_t(0, sanitizeTextMessagePayload(buf, 2));
}

void test_textMsgWhitespaceOnlyIsNotSent(void)
{
    char buf[16] = "  \t \r\n";

    TEST_ASSERT_EQUAL_size_t(0, sanitizeTextMessagePayload(buf, 6));
}

// The floating RX pin at boot: one noise byte must not become a message.
void test_textMsgLoneNoiseByteIsNotSent(void)
{
    char buf[8] = "\xFF";

    TEST_ASSERT_EQUAL_size_t(0, sanitizeTextMessagePayload(buf, 1));
}

void test_textMsgPlainTextUnchanged(void)
{
    char buf[16] = "hello";

    TEST_ASSERT_EQUAL_size_t(5, sanitizeTextMessagePayload(buf, 5));
    TEST_ASSERT_EQUAL_MEMORY("hello", buf, 5);
}

// The CR/LF a terminal appends to the line goes with the surrounding whitespace.
void test_textMsgSurroundingWhitespaceTrimmed(void)
{
    char buf[16] = "  hello \r\n";

    TEST_ASSERT_EQUAL_size_t(5, sanitizeTextMessagePayload(buf, 10));
    TEST_ASSERT_EQUAL_MEMORY("hello", buf, 5);
}

void test_textMsgControlCharsStripped(void)
{
    char buf[16] = "a\x01"
                   "b\x7F"
                   "c";

    TEST_ASSERT_EQUAL_size_t(3, sanitizeTextMessagePayload(buf, 5));
    TEST_ASSERT_EQUAL_MEMORY("abc", buf, 3);
}

// Interior newline and tab survive, so pasted multi-line input stays readable.
void test_textMsgInteriorNewlineAndTabKept(void)
{
    char buf[16] = "a\n\tb";

    TEST_ASSERT_EQUAL_size_t(4, sanitizeTextMessagePayload(buf, 4));
    TEST_ASSERT_EQUAL_MEMORY("a\n\tb", buf, 4);
}

// Text in someone's own language must still send, so valid multi-byte UTF-8 survives.
void test_textMsgValidUtf8Preserved(void)
{
    // "café 🌍" - é is C3 A9, the globe is F0 9F 8C 8D
    char buf[24] = "caf\xC3\xA9 \xF0\x9F\x8C\x8D";

    TEST_ASSERT_EQUAL_size_t(10, sanitizeTextMessagePayload(buf, 10));
    TEST_ASSERT_EQUAL_MEMORY("caf\xC3\xA9 \xF0\x9F\x8C\x8D", buf, 10);
}

void test_textMsgInvalidUtf8BytesDropped(void)
{
    // 0xC3 without its continuation byte, then a bare continuation byte.
    char buf[16] = "a\xC3"
                   "b\x80"
                   "c";

    TEST_ASSERT_EQUAL_size_t(3, sanitizeTextMessagePayload(buf, 5));
    TEST_ASSERT_EQUAL_MEMORY("abc", buf, 3);
}

void test_textMsgEmptyPayloadIsNotSent(void)
{
    char buf[8] = "";

    TEST_ASSERT_EQUAL_size_t(0, sanitizeTextMessagePayload(buf, 0));
}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_serialConfigEmptyIsValid);
    RUN_TEST(test_serialConfigEnabledIsValid);
    RUN_TEST(test_serialConfigWithOverrideConsoleNmeaModeIsValid);
    RUN_TEST(test_serialConfigWithOverrideConsoleCalTopoModeIsValid);
    RUN_TEST(test_serialConfigWithOverrideConsoleMsConfigModeIsValid);
    RUN_TEST(test_serialConfigWithOverrideConsoleDefaultModeIsInvalid);
    RUN_TEST(test_serialConfigWithOverrideConsoleSimpleModeIsInvalid);
    RUN_TEST(test_serialConfigWithOverrideConsoleTextMsgModeIsInvalid);
    RUN_TEST(test_serialConfigWithOverrideConsoleProtoModeIsInvalid);
    RUN_TEST(test_serialConfigVariousModesWithoutOverrideAreValid);
    RUN_TEST(test_textMsgCrLfOnlyIsNotSent);
    RUN_TEST(test_textMsgWhitespaceOnlyIsNotSent);
    RUN_TEST(test_textMsgLoneNoiseByteIsNotSent);
    RUN_TEST(test_textMsgPlainTextUnchanged);
    RUN_TEST(test_textMsgSurroundingWhitespaceTrimmed);
    RUN_TEST(test_textMsgControlCharsStripped);
    RUN_TEST(test_textMsgInteriorNewlineAndTabKept);
    RUN_TEST(test_textMsgValidUtf8Preserved);
    RUN_TEST(test_textMsgInvalidUtf8BytesDropped);
    RUN_TEST(test_textMsgEmptyPayloadIsNotSent);
    exit(UNITY_END());
}
#else
void setup()
{
    initializeTestEnvironment();
    LOG_WARN("This test requires the ARCH_PORTDUINO variant");
    UNITY_BEGIN();
    exit(UNITY_END());
}
#endif
void loop() {}
