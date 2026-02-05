#include "DebugConfiguration.h"
#include "TestUtil.h"
#include <unity.h>

#ifdef ARCH_PORTDUINO
#include "configuration.h"

#if defined(UNIT_TEST)
#define IS_RUNNING_TESTS 1
#else
#define IS_RUNNING_TESTS 0
#endif

#if (defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)) && !defined(CONFIG_IDF_TARGET_ESP32S2) &&               \
    !defined(CONFIG_IDF_TARGET_ESP32C3)
#include "modules/SerialModule.h"
#endif

#if (defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)) && !defined(CONFIG_IDF_TARGET_ESP32S2) &&               \
    !defined(CONFIG_IDF_TARGET_ESP32C3)

// Test that empty configuration is valid.
void test_serialConfigEmptyIsValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {};

    TEST_ASSERT_TRUE(SerialModule::isValidConfig(config));
}

// Test that basic enabled configuration is valid.
void test_serialConfigEnabledIsValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = true};

    TEST_ASSERT_TRUE(SerialModule::isValidConfig(config));
}

// Test that configuration with override_console_serial_port and NMEA mode is valid.
void test_serialConfigWithOverrideConsoleNmeaModeIsValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .override_console_serial_port = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA};

    TEST_ASSERT_TRUE(SerialModule::isValidConfig(config));
}

// Test that configuration with override_console_serial_port and CalTopo mode is valid.
void test_serialConfigWithOverrideConsoleCalTopoModeIsValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .override_console_serial_port = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO};

    TEST_ASSERT_TRUE(SerialModule::isValidConfig(config));
}

// Test that configuration with override_console_serial_port and DEFAULT mode is invalid.
void test_serialConfigWithOverrideConsoleDefaultModeIsInvalid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .override_console_serial_port = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_DEFAULT};

    TEST_ASSERT_FALSE(SerialModule::isValidConfig(config));
}

// Test that configuration with override_console_serial_port and SIMPLE mode is invalid.
void test_serialConfigWithOverrideConsoleSimpleModeIsInvalid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .override_console_serial_port = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_SIMPLE};

    TEST_ASSERT_FALSE(SerialModule::isValidConfig(config));
}

// Test that configuration with override_console_serial_port and TEXTMSG mode is invalid.
void test_serialConfigWithOverrideConsoleTextMsgModeIsInvalid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .override_console_serial_port = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_TEXTMSG};

    TEST_ASSERT_FALSE(SerialModule::isValidConfig(config));
}

// Test that configuration with override_console_serial_port and PROTO mode is invalid.
void test_serialConfigWithOverrideConsoleProtoModeIsInvalid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {
        .enabled = true, .override_console_serial_port = true, .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO};

    TEST_ASSERT_FALSE(SerialModule::isValidConfig(config));
}

// Test that various modes work without override_console_serial_port.
void test_serialConfigVariousModesWithoutOverrideAreValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = true, .override_console_serial_port = false};

    // Test DEFAULT mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_DEFAULT;
    TEST_ASSERT_TRUE(SerialModule::isValidConfig(config));

    // Test SIMPLE mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_SIMPLE;
    TEST_ASSERT_TRUE(SerialModule::isValidConfig(config));

    // Test TEXTMSG mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_TEXTMSG;
    TEST_ASSERT_TRUE(SerialModule::isValidConfig(config));

    // Test PROTO mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_PROTO;
    TEST_ASSERT_TRUE(SerialModule::isValidConfig(config));

    // Test NMEA mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA;
    TEST_ASSERT_TRUE(SerialModule::isValidConfig(config));

    // Test CALTOPO mode
    config.mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_CALTOPO;
    TEST_ASSERT_TRUE(SerialModule::isValidConfig(config));
}

#endif // Architecture check

void setup()
{
    initializeTestEnvironment();

#if (defined(ARCH_ESP32) || defined(ARCH_NRF52) || defined(ARCH_RP2040)) && !defined(CONFIG_IDF_TARGET_ESP32S2) &&               \
    !defined(CONFIG_IDF_TARGET_ESP32C3)
    UNITY_BEGIN();
    RUN_TEST(test_serialConfigEmptyIsValid);
    RUN_TEST(test_serialConfigEnabledIsValid);
    RUN_TEST(test_serialConfigWithOverrideConsoleNmeaModeIsValid);
    RUN_TEST(test_serialConfigWithOverrideConsoleCalTopoModeIsValid);
    RUN_TEST(test_serialConfigWithOverrideConsoleDefaultModeIsInvalid);
    RUN_TEST(test_serialConfigWithOverrideConsoleSimpleModeIsInvalid);
    RUN_TEST(test_serialConfigWithOverrideConsoleTextMsgModeIsInvalid);
    RUN_TEST(test_serialConfigWithOverrideConsoleProtoModeIsInvalid);
    RUN_TEST(test_serialConfigVariousModesWithoutOverrideAreValid);
    exit(UNITY_END());
#else
    LOG_WARN("This test requires ESP32, NRF52, or RP2040 architecture");
    UNITY_BEGIN();
    UNITY_END();
#endif
}
#else
void setup()
{
    initializeTestEnvironment();
    LOG_WARN("This test requires the ARCH_PORTDUINO variant");
    UNITY_BEGIN();
    UNITY_END();
}
#endif
void loop() {}
