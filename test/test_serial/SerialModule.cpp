#include "DebugConfiguration.h"
#include "TestUtil.h"
#include <unity.h>

// Required by Unity: PlatformIO's weak defaults do not link on MinGW (PE-COFF weak externals).
// Outside the guard below so both the portduino and the stub setup() get them.
void setUp(void) {}
void tearDown(void) {}

#ifdef ARCH_PORTDUINO
#include "configuration.h"

#include "meshUtils.h"
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

// An enabled module whose RX or TX is a radio pin would take the pin off the radio, so it is rejected.
void test_serialConfigRadioPinIsInvalidWhenEnabled(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = true, .rxd = UNIT_TEST_RADIO_PIN, .txd = 2};
    TEST_ASSERT_FALSE(serialConfigIsValid(config));

    config.rxd = 2;
    config.txd = UNIT_TEST_RADIO_PIN;
    TEST_ASSERT_FALSE(serialConfigIsValid(config));
}

// A disabled module never opens the UART, so the same pins must still save; that is how a user backs out.
void test_serialConfigRadioPinIsValidWhenDisabled(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = false, .rxd = UNIT_TEST_RADIO_PIN, .txd = UNIT_TEST_RADIO_PIN};
    TEST_ASSERT_TRUE(serialConfigIsValid(config));
}

// Pin 0 means "use the default pins", not GPIO 0, so it never counts as a radio pin.
void test_serialConfigNonRadioPinsAreValid(void)
{
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = true, .rxd = 2, .txd = 3};
    TEST_ASSERT_TRUE(serialConfigIsValid(config));
    TEST_ASSERT_FALSE(isRadioPin(0));
    TEST_ASSERT_TRUE(isRadioPin(UNIT_TEST_RADIO_PIN));
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
    RUN_TEST(test_serialConfigRadioPinIsInvalidWhenEnabled);
    RUN_TEST(test_serialConfigRadioPinIsValidWhenDisabled);
    RUN_TEST(test_serialConfigNonRadioPinsAreValid);
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
