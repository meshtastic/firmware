#include "TestUtil.h"
#include "meshtastic/config.pb.h"
#include "modules/ModuleAvailability.h"
#include <Arduino.h>
#include <unity.h>

static void test_audio_available_only_on_esp32_sx1280_lora24()
{
    TEST_ASSERT_TRUE(isAudioModuleAvailable(true, true, false, meshtastic_Config_LoRaConfig_RegionCode_LORA_24));
}

static void test_audio_excluded_when_region_is_not_lora24()
{
    TEST_ASSERT_FALSE(isAudioModuleAvailable(true, true, false, meshtastic_Config_LoRaConfig_RegionCode_US));
}

static void test_audio_excluded_when_radio_lacks_sx1280()
{
    TEST_ASSERT_FALSE(isAudioModuleAvailable(true, false, false, meshtastic_Config_LoRaConfig_RegionCode_LORA_24));
}

static void test_audio_excluded_when_build_excludes_audio()
{
    TEST_ASSERT_FALSE(isAudioModuleAvailable(true, true, true, meshtastic_Config_LoRaConfig_RegionCode_LORA_24));
}

void setUp(void) {}

void tearDown(void) {}

void setup()
{
    testDelay(10);
    testDelay(2000);

    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_audio_available_only_on_esp32_sx1280_lora24);
    RUN_TEST(test_audio_excluded_when_region_is_not_lora24);
    RUN_TEST(test_audio_excluded_when_radio_lacks_sx1280);
    RUN_TEST(test_audio_excluded_when_build_excludes_audio);
    exit(UNITY_END());
}

void loop() {}
