// SerialModule configuration validation: serialConfigIsValid() in src/modules/SerialModule.cpp.
//
// Why: an invalid serial config can take over the console UART and make the node unreachable.
// Regression guarded: override_console_serial_port accepted with a mode that writes to the
// port (DEFAULT, SIMPLE, TEXTMSG, PROTO), or rejected with a mode that is allowed there.

#include "DebugConfiguration.h"
#include "TestUtil.h"
#include <unity.h>

// Required by Unity: PlatformIO's weak defaults do not link on MinGW (PE-COFF weak externals).
void setUp(void) {}

#ifdef ARCH_PORTDUINO
#include "configuration.h"

#include "MeshRadio.h"
#include "MeshTypes.h"
#include "mesh/RadioInterface.h"
#include "modules/SerialModule.h"

// The native build has no LoRa radio, so this stands in for one whose driver claims kRadioPin.
static constexpr uint32_t kRadioPin = 47;
// A failed assertion longjmps past a stack PinRadio's destructor; don't let the next test see it.
void tearDown(void)
{
    RadioInterface::active = nullptr;
}

class PinRadio : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }
    uint32_t getPacketTime(uint32_t, bool) override { return 0; }
    bool usesPin(uint32_t pin) const override { return pin == kRadioPin; }
};

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
    PinRadio radio;
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = true, .rxd = kRadioPin, .txd = 2};
    TEST_ASSERT_FALSE(serialConfigIsValid(config));

    config.rxd = 2;
    config.txd = kRadioPin;
    TEST_ASSERT_FALSE(serialConfigIsValid(config));
}

// A disabled module never opens the UART, so the same pins must still save; that is how a user backs out.
void test_serialConfigRadioPinIsValidWhenDisabled(void)
{
    PinRadio radio;
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = false, .rxd = kRadioPin, .txd = kRadioPin};
    TEST_ASSERT_TRUE(serialConfigIsValid(config));
}

// Pin 0 means "use the default pins", not GPIO 0, so it never counts as a radio pin.
void test_serialConfigNonRadioPinsAreValid(void)
{
    PinRadio radio;
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = true, .rxd = 2, .txd = 3};
    TEST_ASSERT_TRUE(serialConfigIsValid(config));
    TEST_ASSERT_FALSE(isRadioPin(0));
    TEST_ASSERT_TRUE(isRadioPin(kRadioPin));
}

// The check asks the live radio, so a destroyed one (a failed probe in initLoRa) no longer claims its pins.
void test_radioPinsFollowTheLiveRadio(void)
{
    TEST_ASSERT_FALSE(isRadioPin(kRadioPin));
    {
        PinRadio failedProbe;
        TEST_ASSERT_TRUE(isRadioPin(kRadioPin));
    }
    TEST_ASSERT_FALSE(isRadioPin(kRadioPin));
}

// UART pins are 8-bit, so a value above 255 would wrap onto another pin (303 lands on 47, the radio pin here).
void test_serialConfigPinAbove255IsInvalidWhenEnabled(void)
{
    PinRadio radio;
    meshtastic_ModuleConfig_SerialConfig config = {.enabled = true, .rxd = 2, .txd = kRadioPin + 256};
    TEST_ASSERT_FALSE(serialConfigIsValid(config));

    config.rxd = 256;
    config.txd = 3;
    TEST_ASSERT_FALSE(serialConfigIsValid(config));

    config.enabled = false;
    TEST_ASSERT_TRUE(serialConfigIsValid(config));

    // The console override still opens the UART on rxd/txd, so it gets no exemption.
    config = {.enabled = true,
              .rxd = 2,
              .txd = 303,
              .mode = meshtastic_ModuleConfig_SerialConfig_Serial_Mode_NMEA,
              .override_console_serial_port = true};
    TEST_ASSERT_FALSE(serialConfigIsValid(config));
}

void setup()
{
    initializeTestEnvironment();
    initRegion(); // a RadioInterface reads myRegion when it is constructed

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
    RUN_TEST(test_radioPinsFollowTheLiveRadio);
    RUN_TEST(test_serialConfigPinAbove255IsInvalidWhenEnabled);
    exit(UNITY_END());
}
#else
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    LOG_WARN("This test requires the ARCH_PORTDUINO variant");
    UNITY_BEGIN();
    exit(UNITY_END());
}
#endif
void loop() {}
