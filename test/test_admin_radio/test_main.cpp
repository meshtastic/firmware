/**
 * Tests for the radio configuration validation and clamping functions
 * introduced in the radio_interface_cherrypick branch.
 *
 * Targets:
 *  1. getRegion()
 *  2. RadioInterface::validateConfigRegion()
 *  3. RadioInterface::validateConfigLora()
 *  4. RadioInterface::clampConfigLora()
 *  5. RegionInfo preset lists (PRESETS_STD, PRESETS_EU_868, PRESETS_UNDEF)
 *  6. Channel spacing calculation (placeholder for future protobuf changes)
 */

#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "TestUtil.h"
#include <unity.h>

#include "meshtastic/config.pb.h"

class MockMeshService : public MeshService
{
  public:
    void sendClientNotification(meshtastic_ClientNotification *n) override { releaseClientNotificationToPool(n); }
};

static MockMeshService *mockMeshService;

// -----------------------------------------------------------------------
// getRegion() tests
// -----------------------------------------------------------------------
extern const RegionInfo *getRegion(meshtastic_Config_LoRaConfig_RegionCode code);

static void test_getRegion_returnsCorrectRegion_US()
{
    const RegionInfo *r = getRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, r->code);
    TEST_ASSERT_EQUAL_STRING("US", r->name);
}

static void test_getRegion_returnsCorrectRegion_EU868()
{
    const RegionInfo *r = getRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_EU_868, r->code);
    TEST_ASSERT_EQUAL_STRING("EU_868", r->name);
}

static void test_getRegion_returnsCorrectRegion_LORA24()
{
    const RegionInfo *r = getRegion(meshtastic_Config_LoRaConfig_RegionCode_LORA_24);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_LORA_24, r->code);
    TEST_ASSERT_TRUE(r->wideLora);
}

static void test_getRegion_unsetCodeReturnsUnsetEntry()
{
    const RegionInfo *r = getRegion(meshtastic_Config_LoRaConfig_RegionCode_UNSET);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_UNSET, r->code);
    TEST_ASSERT_EQUAL_STRING("UNSET", r->name);
}

static void test_getRegion_unknownCodeFallsToUnset()
{
    // A code not in the table should iterate to the UNSET sentinel
    const RegionInfo *r = getRegion((meshtastic_Config_LoRaConfig_RegionCode)255);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_UNSET, r->code);
}

// -----------------------------------------------------------------------
// validateConfigRegion() tests
// -----------------------------------------------------------------------

static void test_validateConfigRegion_validRegionReturnsTrue()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;

    // Ensure owner is not licensed (should not matter for non-licensed-only regions)
    devicestate.owner.is_licensed = false;

    TEST_ASSERT_TRUE(RadioInterface::validateConfigRegion(cfg));
}

static void test_validateConfigRegion_unsetRegionReturnsTrue()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;

    devicestate.owner.is_licensed = false;

    // UNSET region has licensedOnly=false, so should pass
    TEST_ASSERT_TRUE(RadioInterface::validateConfigRegion(cfg));
}

// Note: There are currently no regions with licensedOnly=true in the table.
// When HAM regions are added, uncomment and adapt the test below.
//
// static void test_validateConfigRegion_licensedOnlyRegionRejectedWhenUnlicensed()
// {
//     meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
//     cfg.region = meshtastic_Config_LoRaConfig_RegionCode_HAM_REGION;
//     devicestate.owner.is_licensed = false;
//     TEST_ASSERT_FALSE(RadioInterface::validateConfigRegion(cfg));
// }
//
// static void test_validateConfigRegion_licensedOnlyRegionAcceptedWhenLicensed()
// {
//     meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
//     cfg.region = meshtastic_Config_LoRaConfig_RegionCode_HAM_REGION;
//     devicestate.owner.is_licensed = true;
//     TEST_ASSERT_TRUE(RadioInterface::validateConfigRegion(cfg));
// }

// -----------------------------------------------------------------------
// validateConfigLora() tests
// -----------------------------------------------------------------------

static void test_validateConfigLora_validPresetForUS()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    TEST_ASSERT_TRUE(RadioInterface::validateConfigLora(cfg));
}

static void test_validateConfigLora_allStdPresetsValidForUS()
{
    meshtastic_Config_LoRaConfig_ModemPreset stdPresets[] = {
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,     meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,   meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO,
    };

    for (size_t i = 0; i < sizeof(stdPresets) / sizeof(stdPresets[0]); i++) {
        meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
        cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
        cfg.use_preset = true;
        cfg.modem_preset = stdPresets[i];
        TEST_ASSERT_TRUE_MESSAGE(RadioInterface::validateConfigLora(cfg), "Expected valid preset for US");
    }
}

static void test_validateConfigLora_turboPresetsInvalidForEU868()
{
    // EU_868 has PRESETS_EU_868 which excludes SHORT_TURBO and LONG_TURBO
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.use_preset = true;

    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::validateConfigLora(cfg), "SHORT_TURBO should be invalid for EU_868");

    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO;
    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::validateConfigLora(cfg), "LONG_TURBO should be invalid for EU_868");
}

static void test_validateConfigLora_validPresetsForEU868()
{
    meshtastic_Config_LoRaConfig_ModemPreset eu868Presets[] = {
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,     meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,   meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE,
    };

    for (size_t i = 0; i < sizeof(eu868Presets) / sizeof(eu868Presets[0]); i++) {
        meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
        cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
        cfg.use_preset = true;
        cfg.modem_preset = eu868Presets[i];
        TEST_ASSERT_TRUE_MESSAGE(RadioInterface::validateConfigLora(cfg), "Expected valid preset for EU_868");
    }
}

static void test_validateConfigLora_customBandwidthTooWideForEU868()
{
    // EU_868 spans 869.4 - 869.65 = 0.25 MHz = 250 kHz
    // A 500 kHz custom BW should be rejected
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.use_preset = false;
    cfg.bandwidth = 500;
    cfg.spread_factor = 11;
    cfg.coding_rate = 5;

    TEST_ASSERT_FALSE(RadioInterface::validateConfigLora(cfg));
}

static void test_validateConfigLora_customBandwidthFitsUS()
{
    // US spans 902 - 928 = 26 MHz, so 250 kHz BW fits easily
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.use_preset = false;
    cfg.bandwidth = 250;
    cfg.spread_factor = 11;
    cfg.coding_rate = 5;

    TEST_ASSERT_TRUE(RadioInterface::validateConfigLora(cfg));
}

static void test_validateConfigLora_customBandwidthFitsEU868()
{
    // EU_868 spans 250 kHz, 125 kHz BW should fit
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.use_preset = false;
    cfg.bandwidth = 125;
    cfg.spread_factor = 12;
    cfg.coding_rate = 8;

    TEST_ASSERT_TRUE(RadioInterface::validateConfigLora(cfg));
}

// -----------------------------------------------------------------------
// clampConfigLora() tests
// -----------------------------------------------------------------------

static void test_clampConfigLora_invalidPresetClampedToDefault()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO; // not in EU_868 preset list

    RadioInterface::clampConfigLora(cfg);

    const RegionInfo *eu868 = getRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    TEST_ASSERT_EQUAL(eu868->defaultPreset, cfg.modem_preset);
}

static void test_clampConfigLora_validPresetUnchanged()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, cfg.modem_preset);
}

static void test_clampConfigLora_customBwTooWideClampedToDefaultBw()
{
    // EU_868 span is 250kHz. A 500kHz custom BW should be clamped to default preset BW.
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.use_preset = false;
    cfg.bandwidth = 500;
    cfg.spread_factor = 11;
    cfg.coding_rate = 5;

    RadioInterface::clampConfigLora(cfg);

    const RegionInfo *eu868 = getRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    float expectedBw = modemPresetToBwKHz(eu868->defaultPreset, eu868->wideLora);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedBw, (float)cfg.bandwidth);
}

static void test_clampConfigLora_customBwValidLeftUnchanged()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.use_preset = false;
    cfg.bandwidth = 125;
    cfg.spread_factor = 12;
    cfg.coding_rate = 8;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL_UINT16(125, cfg.bandwidth);
}

// -----------------------------------------------------------------------
// RegionInfo preset list integrity tests
// -----------------------------------------------------------------------

static void test_presetsStd_hasNineEntries()
{
    // PRESETS_STD should have exactly 9 presets
    const RegionInfo *us = getRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    TEST_ASSERT_EQUAL(9, us->numPresets);
    TEST_ASSERT_EQUAL_PTR(PRESETS_STD, us->availablePresets);
}

static void test_presetsEU868_hasSevenEntries()
{
    const RegionInfo *eu = getRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    TEST_ASSERT_EQUAL(7, eu->numPresets);
    TEST_ASSERT_EQUAL_PTR(PRESETS_EU_868, eu->availablePresets);
}

static void test_presetsUndef_hasOneEntry()
{
    const RegionInfo *unset = getRegion(meshtastic_Config_LoRaConfig_RegionCode_UNSET);
    TEST_ASSERT_EQUAL(1, unset->numPresets);
    TEST_ASSERT_EQUAL_PTR(PRESETS_UNDEF, unset->availablePresets);
}

static void test_defaultPresetIsInAvailablePresets()
{
    // For every region, the defaultPreset must appear in its own availablePresets list
    const RegionInfo *r = regions;
    while (true) {
        bool found = false;
        for (size_t i = 0; i < r->numPresets; i++) {
            if (r->availablePresets[i] == r->defaultPreset) {
                found = true;
                break;
            }
        }
        char msg[80];
        snprintf(msg, sizeof(msg), "Region %s defaultPreset not in availablePresets", r->name);
        TEST_ASSERT_TRUE_MESSAGE(found, msg);

        if (r->code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
            break; // UNSET is the sentinel, stop after it
        r++;
    }
}

static void test_regionFieldsAreSane()
{
    // Basic sanity check: all regions have freqEnd > freqStart and a non-null name
    const RegionInfo *r = regions;
    while (true) {
        char msg[80];
        snprintf(msg, sizeof(msg), "Region %s: freqEnd must be > freqStart", r->name);
        TEST_ASSERT_TRUE_MESSAGE(r->freqEnd > r->freqStart, msg);
        TEST_ASSERT_NOT_NULL(r->name);
        TEST_ASSERT_TRUE_MESSAGE(r->numPresets > 0, "numPresets must be > 0");
        TEST_ASSERT_NOT_NULL(r->availablePresets);

        if (r->code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
            break;
        r++;
    }
}

// -----------------------------------------------------------------------
// Channel spacing calculation (placeholder for future protobuf updates)
// -----------------------------------------------------------------------

static void test_channelSpacingCalculation_US_LONG_FAST()
{
    // Current formula: channelSpacing = spacing + (padding * 2) + (bw / 1000)
    // US: spacing=0, padding=0
    // LONG_FAST on non-wide region: bw=250 kHz
    // channelSpacing = 0 + 0 + 0.250 = 0.250 MHz
    // numChannels = round((928 - 902 + 0) / 0.250) = round(104) = 104
    const RegionInfo *us = getRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    float bw = modemPresetToBwKHz(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, us->wideLora);
    float channelSpacing = us->spacing + (us->padding * 2) + (bw / 1000.0f);
    uint32_t numChannels = (uint32_t)(((us->freqEnd - us->freqStart + us->spacing) / channelSpacing) + 0.5f);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.250f, channelSpacing);
    TEST_ASSERT_EQUAL_UINT32(104, numChannels);
}

static void test_channelSpacingCalculation_EU868_LONG_FAST()
{
    // EU_868: freqStart=869.4, freqEnd=869.65, spacing=0, padding=0
    // LONG_FAST: bw=250 kHz => channelSpacing = 0.250 MHz
    // numChannels = round((0.25 + 0) / 0.250) = 1
    const RegionInfo *eu = getRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    float bw = modemPresetToBwKHz(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, eu->wideLora);
    float channelSpacing = eu->spacing + (eu->padding * 2) + (bw / 1000.0f);
    uint32_t numChannels = (uint32_t)(((eu->freqEnd - eu->freqStart + eu->spacing) / channelSpacing) + 0.5f);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.250f, channelSpacing);
    TEST_ASSERT_EQUAL_UINT32(1, numChannels);
}

// Placeholder: when protobuf region definitions include non-zero padding/spacing,
// add tests here to verify the channel count and frequency calculations.
static void test_channelSpacingCalculation_placeholder()
{
    // TODO: Once protobuf RegionInfo entries have non-zero padding or spacing values,
    // verify:
    //  - Channel count matches expected value for each (region, preset) pair
    //  - First channel frequency = freqStart + (bw/2000) + padding
    //  - Nth channel frequency = first + (n * channelSpacing)
    //  - overrideSlot, when non-zero, forces the channel_num
    TEST_PASS_MESSAGE("Placeholder for future channel spacing tests with updated protobuf region fields");
}

// -----------------------------------------------------------------------
// Test runner
// -----------------------------------------------------------------------

void setUp(void)
{
    mockMeshService = new MockMeshService();
    service = mockMeshService;
}
void tearDown(void)
{
    service = nullptr;
    delete mockMeshService;
    mockMeshService = nullptr;
}

void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();

    UNITY_BEGIN();

    // getRegion()
    RUN_TEST(test_getRegion_returnsCorrectRegion_US);
    RUN_TEST(test_getRegion_returnsCorrectRegion_EU868);
    RUN_TEST(test_getRegion_returnsCorrectRegion_LORA24);
    RUN_TEST(test_getRegion_unsetCodeReturnsUnsetEntry);
    RUN_TEST(test_getRegion_unknownCodeFallsToUnset);

    // validateConfigRegion()
    RUN_TEST(test_validateConfigRegion_validRegionReturnsTrue);
    RUN_TEST(test_validateConfigRegion_unsetRegionReturnsTrue);

    // validateConfigLora()
    RUN_TEST(test_validateConfigLora_validPresetForUS);
    RUN_TEST(test_validateConfigLora_allStdPresetsValidForUS);
    RUN_TEST(test_validateConfigLora_turboPresetsInvalidForEU868);
    RUN_TEST(test_validateConfigLora_validPresetsForEU868);
    RUN_TEST(test_validateConfigLora_customBandwidthTooWideForEU868);
    RUN_TEST(test_validateConfigLora_customBandwidthFitsUS);
    RUN_TEST(test_validateConfigLora_customBandwidthFitsEU868);

    // clampConfigLora()
    RUN_TEST(test_clampConfigLora_invalidPresetClampedToDefault);
    RUN_TEST(test_clampConfigLora_validPresetUnchanged);
    RUN_TEST(test_clampConfigLora_customBwTooWideClampedToDefaultBw);
    RUN_TEST(test_clampConfigLora_customBwValidLeftUnchanged);

    // RegionInfo preset list integrity
    RUN_TEST(test_presetsStd_hasNineEntries);
    RUN_TEST(test_presetsEU868_hasSevenEntries);
    RUN_TEST(test_presetsUndef_hasOneEntry);
    RUN_TEST(test_defaultPresetIsInAvailablePresets);
    RUN_TEST(test_regionFieldsAreSane);

    // Channel spacing (current + placeholder)
    RUN_TEST(test_channelSpacingCalculation_US_LONG_FAST);
    RUN_TEST(test_channelSpacingCalculation_EU868_LONG_FAST);
    RUN_TEST(test_channelSpacingCalculation_placeholder);

    exit(UNITY_END());
}

void loop() {}
