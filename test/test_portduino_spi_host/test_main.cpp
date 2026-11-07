// Unit tests for spiHostFromDevicePath in src/platform/portduino/PortduinoGlue.cpp.
//
// Regression coverage for the operator-precedence bug that packed a display's
// SPI device as `x + y << 4` (i.e. `(x + y) << 4`) while HardwareSPI decodes the
// low nibble as the bus and the high nibble as the device. Under that packing
// `spidev1.0` became `/dev/spidev0.1`, `spidev0.1` collided with it, and no
// non-zero bus could be expressed - so a display configured on SPI1 wrote every
// frame to the radio's SPI0. The parser also replaces the old 14-character
// string check, which silently skipped multi-digit buses/devices.
//
// It also pins the framework's per-host registry contract: host 0 is the global
// SPI object, other hosts get a stable cached instance, and a negative host is
// refused. That registry is what LovyanGFX reaches through the weak external
// lgfx_get_spi_instance() hook.
//
// The packed value from spiHostFromDevicePath() is checked against local
// helpers that encode the expected nibble layout (bus = host & 0x0F,
// device = (host >> 4) & 0x0F). These tests do not call HardwareSPI, so a
// change to its decoder does not fail them.
//
// The config-loader tests pin two cross-file state rules: a radio path outside
// the packed range is left as the -1 sentinel (initLoRa() then opens it in its
// own instance instead of host 0), and clearing a touchscreen override makes it
// follow the display host again rather than keeping the old one.

#include "Arduino.h"
#include "PortduinoGlue.h"
#include "TestUtil.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <unity.h>

static int decodedBus(int host)
{
    return host & 0x0F;
}

static int decodedDevice(int host)
{
    return (host >> 4) & 0x0F;
}

void setUp(void) {}
void tearDown(void) {}

void test_spidev1_0_is_bus1_device0()
{
    const int host = spiHostFromDevicePath("/dev/spidev1.0");
    TEST_ASSERT_GREATER_OR_EQUAL(0, host);
    TEST_ASSERT_EQUAL_INT(1, decodedBus(host));
    TEST_ASSERT_EQUAL_INT(0, decodedDevice(host));
}

void test_spidev0_1_is_bus0_device1()
{
    const int host = spiHostFromDevicePath("/dev/spidev0.1");
    TEST_ASSERT_GREATER_OR_EQUAL(0, host);
    TEST_ASSERT_EQUAL_INT(0, decodedBus(host));
    TEST_ASSERT_EQUAL_INT(1, decodedDevice(host));
}

void test_spidev1_0_and_spidev0_1_do_not_collide()
{
    // Direct regression: both were 16 (0x10) before the fix.
    const int oneZero = spiHostFromDevicePath("/dev/spidev1.0");
    const int zeroOne = spiHostFromDevicePath("/dev/spidev0.1");
    TEST_ASSERT_GREATER_OR_EQUAL(0, oneZero);
    TEST_ASSERT_GREATER_OR_EQUAL(0, zeroOne);
    TEST_ASSERT_NOT_EQUAL(oneZero, zeroOne);
}

void test_spidev1_1_is_bus1_device1()
{
    const int host = spiHostFromDevicePath("/dev/spidev1.1");
    TEST_ASSERT_GREATER_OR_EQUAL(0, host);
    TEST_ASSERT_EQUAL_INT(1, decodedBus(host));
    TEST_ASSERT_EQUAL_INT(1, decodedDevice(host));
}

void test_spidev2_3_is_bus2_device3()
{
    const int host = spiHostFromDevicePath("/dev/spidev2.3");
    TEST_ASSERT_GREATER_OR_EQUAL(0, host);
    TEST_ASSERT_EQUAL_INT(2, decodedBus(host));
    TEST_ASSERT_EQUAL_INT(3, decodedDevice(host));
}

void test_path_without_dev_prefix_is_accepted()
{
    // The loader normalises a missing /dev/ before calling, but accepting both
    // keeps the helper usable by the emitter and by tests.
    const int host = spiHostFromDevicePath("spidev3.2");
    TEST_ASSERT_GREATER_OR_EQUAL(0, host);
    TEST_ASSERT_EQUAL_INT(3, decodedBus(host));
    TEST_ASSERT_EQUAL_INT(2, decodedDevice(host));
}

void test_two_digit_bus_is_rejected()
{
    // HardwareSPI turns the bus nibble into a single character with + '0', so
    // 10 would decode as ':' and name the wrong device.
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev10.0"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev15.0"));
}

void test_bus_nine_is_the_highest_accepted()
{
    const int host = spiHostFromDevicePath("/dev/spidev9.7");
    TEST_ASSERT_GREATER_OR_EQUAL(0, host);
    TEST_ASSERT_EQUAL_INT(9, decodedBus(host));
    TEST_ASSERT_EQUAL_INT(7, decodedDevice(host));
}

void test_device_above_signed_nibble_is_rejected()
{
    // HardwareSPI takes int8_t, so device 8..15 would decode back as a
    // negative host. Reject rather than truncate.
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev0.8"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev0.15"));
}

void test_bus_above_nibble_is_rejected()
{
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev16.0"));
}

void test_signed_whitespace_and_overflow_fields_are_rejected()
{
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev+1.0"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev 1.0"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev1. 0"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev4294967296.0"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev1.4294967296"));
}

void test_non_spidev_paths_are_rejected()
{
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath(""));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev1"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev1."));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev.1"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev1.2.3"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev1.2/extra"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev1.2 "));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev-1.0"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/spidev1.x"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("ch341"));
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev/i2c-1"));
}

void test_double_dev_prefix_is_rejected()
{
    // Pins the helper's rejection of a doubled "/dev/" prefix. This does not
    // cover the emitter and loader round trip.
    TEST_ASSERT_EQUAL_INT(-1, spiHostFromDevicePath("/dev//dev/spidev1.0"));
}

void test_registry_host_zero_is_the_global_spi()
{
    TEST_ASSERT_EQUAL_PTR(&SPI, HardwareSPI::getInstance(0));
}

void test_registry_is_stable_per_host()
{
    HardwareSPI *first = HardwareSPI::getInstance(5);
    HardwareSPI *second = HardwareSPI::getInstance(5);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_PTR(first, second);
    TEST_ASSERT_NOT_EQUAL(&SPI, first);
}

void test_registry_rejects_negative_host()
{
    TEST_ASSERT_NULL(HardwareSPI::getInstance(-1));
}

static std::string writeConfig(const char *name, const std::string &body)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path);
    out << body;
    return path.string();
}

void test_unpackable_radio_path_is_not_left_on_host_zero()
{
    const std::string path = writeConfig("meshtastic-spi-radio.yaml", "Lora:\n"
                                                                      "  Module: sim\n"
                                                                      "  spidev: spidev10.0\n");
    TEST_ASSERT_TRUE(loadConfig(path.c_str()));
    TEST_ASSERT_EQUAL_INT(-1, portduino_config.lora_spi_dev_int);
    TEST_ASSERT_EQUAL_STRING("/dev/spidev10.0", portduino_config.lora_spi_dev.c_str());
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void test_clearing_touchscreen_spidev_returns_to_the_display_host()
{
    const std::string first = writeConfig("meshtastic-spi-touch-a.yaml", "Lora:\n"
                                                                         "  Module: sim\n"
                                                                         "Display:\n"
                                                                         "  Panel: ILI9488\n"
                                                                         "  spidev: spidev1.0\n"
                                                                         "Touchscreen:\n"
                                                                         "  Module: XPT2046\n"
                                                                         "  spidev: spidev2.0\n");
    const std::string second = writeConfig("meshtastic-spi-touch-b.yaml", "Touchscreen:\n"
                                                                          "  spidev: \"\"\n");
    TEST_ASSERT_TRUE(loadConfig(first.c_str()));
    TEST_ASSERT_EQUAL_INT(1, portduino_config.display_spi_dev_int);
    TEST_ASSERT_EQUAL_INT(2, portduino_config.touchscreen_spi_dev_int);
    TEST_ASSERT_TRUE(loadConfig(second.c_str()));
    TEST_ASSERT_EQUAL_INT(portduino_config.display_spi_dev_int, portduino_config.touchscreen_spi_dev_int);
    std::error_code ec;
    std::filesystem::remove(first, ec);
    std::filesystem::remove(second, ec);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_spidev1_0_is_bus1_device0);
    RUN_TEST(test_spidev0_1_is_bus0_device1);
    RUN_TEST(test_spidev1_0_and_spidev0_1_do_not_collide);
    RUN_TEST(test_spidev1_1_is_bus1_device1);
    RUN_TEST(test_spidev2_3_is_bus2_device3);
    RUN_TEST(test_path_without_dev_prefix_is_accepted);
    RUN_TEST(test_two_digit_bus_is_rejected);
    RUN_TEST(test_bus_nine_is_the_highest_accepted);
    RUN_TEST(test_device_above_signed_nibble_is_rejected);
    RUN_TEST(test_bus_above_nibble_is_rejected);
    RUN_TEST(test_signed_whitespace_and_overflow_fields_are_rejected);
    RUN_TEST(test_non_spidev_paths_are_rejected);
    RUN_TEST(test_double_dev_prefix_is_rejected);
    RUN_TEST(test_registry_host_zero_is_the_global_spi);
    RUN_TEST(test_registry_is_stable_per_host);
    RUN_TEST(test_registry_rejects_negative_host);
    RUN_TEST(test_unpackable_radio_path_is_not_left_on_host_zero);
    RUN_TEST(test_clearing_touchscreen_spidev_returns_to_the_display_host);
    exit(UNITY_END());
}

void loop() {}
