#include "MeshRadio.h"
#include "RadioInterface.h"
#include "TestUtil.h"
#include <unity.h>

#include "meshtastic/config.pb.h"

static void test_bwCodeToKHz_specialMappings()
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 31.25f, bwCodeToKHz(31));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 62.5f, bwCodeToKHz(62));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 203.125f, bwCodeToKHz(200));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 406.25f, bwCodeToKHz(400));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 812.5f, bwCodeToKHz(800));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1625.0f, bwCodeToKHz(1600));
}

static void test_bwCodeToKHz_passthrough()
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 125.0f, bwCodeToKHz(125));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 250.0f, bwCodeToKHz(250));
}

static void test_bootstrapLoRaConfigFromPreset_noopWhenUsePresetFalse()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = false;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    cfg.bandwidth = 123;
    cfg.spread_factor = 8;

    RadioInterface::bootstrapLoRaConfigFromPreset(cfg);

    TEST_ASSERT_EQUAL_UINT16(123, cfg.bandwidth);
    TEST_ASSERT_EQUAL_UINT32(8, cfg.spread_factor);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, cfg.modem_preset);
}

static void test_bootstrapLoRaConfigFromPreset_setsDerivedFields_nonWideRegion()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;

    RadioInterface::bootstrapLoRaConfigFromPreset(cfg);

    TEST_ASSERT_EQUAL_UINT16(250, cfg.bandwidth);
    TEST_ASSERT_EQUAL_UINT32(9, cfg.spread_factor);
}

static void test_bootstrapLoRaConfigFromPreset_setsDerivedFields_wideRegion()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;

    RadioInterface::bootstrapLoRaConfigFromPreset(cfg);

    TEST_ASSERT_EQUAL_UINT16(800, cfg.bandwidth);
    TEST_ASSERT_EQUAL_UINT32(9, cfg.spread_factor);
}

static void test_bootstrapLoRaConfigFromPreset_fallsBackIfBandwidthExceedsRegionSpan()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    RadioInterface::bootstrapLoRaConfigFromPreset(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
    TEST_ASSERT_EQUAL_UINT16(250, cfg.bandwidth);
    TEST_ASSERT_EQUAL_UINT32(11, cfg.spread_factor);
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_bwCodeToKHz_specialMappings);
    RUN_TEST(test_bwCodeToKHz_passthrough);
    RUN_TEST(test_bootstrapLoRaConfigFromPreset_noopWhenUsePresetFalse);
    RUN_TEST(test_bootstrapLoRaConfigFromPreset_setsDerivedFields_nonWideRegion);
    RUN_TEST(test_bootstrapLoRaConfigFromPreset_setsDerivedFields_wideRegion);
    RUN_TEST(test_bootstrapLoRaConfigFromPreset_fallsBackIfBandwidthExceedsRegionSpan);
    exit(UNITY_END());
}

void loop() {}
