// Unit tests for the Portduino display panel selection.
//
// The 3.5" ILI9488 on the Luckfox Lyra sends 18-bit RGB666 over SPI. Treating it
// as a 16-bit-data panel corrupts every frame, so keep ILI9488 out of the
// dlen_16bit path. The invert test pins the config handoff: Display.Invert must
// reach portduino_config.displayInvert, which tftSetup passes to the panel as
// cfg.invert (LGFX then emits INVON after panel init).
#include "Arduino.h"
#include "TestUtil.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <unity.h>

#include "PortduinoGlue.h"

static std::string writeConfig(const std::string &body)
{
    const auto path = std::filesystem::temp_directory_path() / "meshtastic-ili9488-test.yaml";
    std::ofstream out(path);
    out << body;
    return path.string();
}

void setUp(void) {}
void tearDown(void) {}

void test_ili9486_needs_16bit_data()
{
    TEST_ASSERT_TRUE(displayPanelUses16BitData(ili9486));
}

void test_ili9488_stays_on_18bit_rgb666()
{
    TEST_ASSERT_FALSE(displayPanelUses16BitData(ili9488));
}

void test_display_invert_reaches_portduino_config()
{
    const std::string path = writeConfig("Lora:\n"
                                         "  Module: sim\n"
                                         "Display:\n"
                                         "  Panel: ILI9488\n"
                                         "  Invert: true\n");
    TEST_ASSERT_TRUE(loadConfig(path.c_str()));
    TEST_ASSERT_EQUAL_INT(ili9488, portduino_config.displayPanel);
    TEST_ASSERT_TRUE(portduino_config.displayInvert);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_ili9486_needs_16bit_data);
    RUN_TEST(test_ili9488_stays_on_18bit_rgb666);
    RUN_TEST(test_display_invert_reaches_portduino_config);
    exit(UNITY_END());
}

void loop() {}
