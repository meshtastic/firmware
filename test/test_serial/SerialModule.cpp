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
