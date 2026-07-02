#include "TestUtil.h"
#include "meshtastic/config.pb.h"
#include "modules/ModuleAvailability.h"
#include <Arduino.h>
#include <unity.h>

static bool expected_audio_available_for_current_build()
{
#if defined(ARCH_ESP32)
    const bool isEsp32 = true;
#else
    const bool isEsp32 = false;
#endif

#if defined(USE_SX1280)
    const bool hasSx1280 = true;
#else
    const bool hasSx1280 = false;
#endif

#if MESHTASTIC_EXCLUDE_AUDIO
    const bool audioExcluded = true;
#else
    const bool audioExcluded = false;
#endif

    return isAudioModuleAvailable(isEsp32, hasSx1280, audioExcluded, meshtastic_Config_LoRaConfig_RegionCode_LORA_24);
}

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

static void test_audio_region_wrapper_matches_current_build_for_lora24()
{
    TEST_ASSERT_EQUAL(expected_audio_available_for_current_build(),
                      isAudioModuleAvailableForRegion(meshtastic_Config_LoRaConfig_RegionCode_LORA_24));
}

static void test_audio_region_wrapper_excludes_non_lora24()
{
    TEST_ASSERT_FALSE(isAudioModuleAvailableForRegion(meshtastic_Config_LoRaConfig_RegionCode_US));
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
    RUN_TEST(test_audio_region_wrapper_matches_current_build_for_lora24);
    RUN_TEST(test_audio_region_wrapper_excludes_non_lora24);
    exit(UNITY_END());
}

void loop() {}
