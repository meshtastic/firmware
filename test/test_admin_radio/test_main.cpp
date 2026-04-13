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
#include "modules/AdminModule.h"
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

// -----------------------------------------------------------------------
// Shadow tables for testing (preset lists → profiles → regions → lookup)
// -----------------------------------------------------------------------

// A minimal preset list with only one entry
static const meshtastic_Config_LoRaConfig_ModemPreset TEST_PRESETS_SINGLE[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
    MODEM_PRESET_END,
};

// A preset list that includes all turbo variants only
static const meshtastic_Config_LoRaConfig_ModemPreset TEST_PRESETS_TURBO_ONLY[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO,
    MODEM_PRESET_END,
};

// A restricted list simulating a hypothetical tight-regulation region
static const meshtastic_Config_LoRaConfig_ModemPreset TEST_PRESETS_RESTRICTED[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE,
    MODEM_PRESET_END,
};

// Mirrors PROFILE_STD but with non-zero spacing/padding for testing
static const RegionProfile TEST_PROFILE_SPACED = {
    TEST_PRESETS_SINGLE,
    /* spacing */ 0.025f,
    /* padding */ 0.010f,
    /* audioPermitted */ true,
    /* licensedOnly */ false,
    /* textThrottle */ 0,
    /* positionThrottle */ 0,
    /* telemetryThrottle */ 0,
    /* overrideSlot */ 0,
};

// A licensed-only profile for testing access control
static const RegionProfile TEST_PROFILE_LICENSED = {
    TEST_PRESETS_RESTRICTED,
    /* spacing */ 0.0f,
    /* padding */ 0.0f,
    /* audioPermitted */ false,
    /* licensedOnly */ true,
    /* textThrottle */ 5,
    /* positionThrottle */ 10,
    /* telemetryThrottle */ 10,
    /* overrideSlot */ 3,
};

// Turbo-only profile
static const RegionProfile TEST_PROFILE_TURBO = {
    TEST_PRESETS_TURBO_ONLY,
    /* spacing */ 0.0f,
    /* padding */ 0.0f,
    /* audioPermitted */ true,
    /* licensedOnly */ false,
    /* textThrottle */ 0,
    /* positionThrottle */ 0,
    /* telemetryThrottle */ 0,
    /* overrideSlot */ 0,
};

static const RegionInfo testRegions[] = {
    // A wide US-like region with spacing + padding
    {meshtastic_Config_LoRaConfig_RegionCode_US, 902.0f, 928.0f, 100, 30, false, false, &TEST_PROFILE_SPACED, "TEST_US_SPACED"},

    // A narrow band simulating tight EU regulation
    {meshtastic_Config_LoRaConfig_RegionCode_EU_868, 869.4f, 869.65f, 10, 14, false, false, &TEST_PROFILE_LICENSED,
     "TEST_EU_LICENSED"},

    // A wide-LoRa region with turbo-only presets
    {meshtastic_Config_LoRaConfig_RegionCode_LORA_24, 2400.0f, 2483.5f, 100, 10, false, true, &TEST_PROFILE_TURBO,
     "TEST_LORA24_TURBO"},

    // Sentinel — must be last
    {meshtastic_Config_LoRaConfig_RegionCode_UNSET, 902.0f, 928.0f, 100, 30, false, false, &TEST_PROFILE_SPACED, "TEST_UNSET"},
};

static const RegionInfo *getTestRegion(meshtastic_Config_LoRaConfig_RegionCode code)
{
    const RegionInfo *r = testRegions;
    while (r->code != meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
        if (r->code == code)
            return r;
        r++;
    }
    return r; // Returns the UNSET sentinel
}

// -----------------------------------------------------------------------
// Shadow table tests
// -----------------------------------------------------------------------

static void test_shadowTable_spacedProfileHasNonZeroSpacing()
{
    const RegionInfo *r = getTestRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    TEST_ASSERT_EQUAL_STRING("TEST_US_SPACED", r->name);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.025f, r->profile->spacing);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.010f, r->profile->padding);
}

static void test_shadowTable_licensedProfileFlagsCorrect()
{
    const RegionInfo *r = getTestRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    TEST_ASSERT_TRUE(r->profile->licensedOnly);
    TEST_ASSERT_FALSE(r->profile->audioPermitted);
    TEST_ASSERT_EQUAL(3, r->profile->overrideSlot);
}

static void test_shadowTable_presetCountMatchesExpected()
{
    const RegionInfo *spaced = getTestRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    TEST_ASSERT_EQUAL(1, spaced->getNumPresets());

    const RegionInfo *licensed = getTestRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    TEST_ASSERT_EQUAL(2, licensed->getNumPresets());

    const RegionInfo *turbo = getTestRegion(meshtastic_Config_LoRaConfig_RegionCode_LORA_24);
    TEST_ASSERT_EQUAL(2, turbo->getNumPresets());
}

static void test_shadowTable_defaultPresetIsFirstInList()
{
    const RegionInfo *spaced = getTestRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, spaced->getDefaultPreset());

    const RegionInfo *licensed = getTestRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, licensed->getDefaultPreset());

    const RegionInfo *turbo = getTestRegion(meshtastic_Config_LoRaConfig_RegionCode_LORA_24);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, turbo->getDefaultPreset());
}

static void test_shadowTable_channelSpacingWithPadding()
{
    // Verify channel count when spacing + padding are non-zero
    const RegionInfo *r = getTestRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    float bw = modemPresetToBwKHz(r->getDefaultPreset(), r->wideLora);
    float channelSpacing = r->profile->spacing + (r->profile->padding * 2) + (bw / 1000.0f);

    // spacing=0.025, padding=0.010*2=0.020, bw=250kHz=0.250
    // channelSpacing = 0.025 + 0.020 + 0.250 = 0.295 MHz
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.295f, channelSpacing);

    uint32_t numChannels = (uint32_t)(((r->freqEnd - r->freqStart + r->profile->spacing) / channelSpacing) + 0.5f);
    // (928 - 902 + 0.025) / 0.295 = 88.2 → 88
    TEST_ASSERT_EQUAL_UINT32(88, numChannels);
}

static void test_shadowTable_turboOnlyOnWideLora()
{
    const RegionInfo *r = getTestRegion(meshtastic_Config_LoRaConfig_RegionCode_LORA_24);
    TEST_ASSERT_TRUE(r->wideLora);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, r->getDefaultPreset());

    // Verify wide-LoRa bandwidth for SHORT_TURBO
    float bw = modemPresetToBwKHz(r->getDefaultPreset(), r->wideLora);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1625.0f, bw); // 1625 kHz in wide mode
}

static void test_shadowTable_unknownCodeFallsToSentinel()
{
    const RegionInfo *r = getTestRegion((meshtastic_Config_LoRaConfig_RegionCode)200);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_UNSET, r->code);
    TEST_ASSERT_EQUAL_STRING("TEST_UNSET", r->name);
}

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

static void test_validateConfigLora_bogusPresetRejected()
{
    // A fabricated preset value not in any list should be rejected
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.use_preset = true;
    cfg.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)99;

    TEST_ASSERT_FALSE(RadioInterface::validateConfigLora(cfg));
}

static void test_validateConfigLora_unsetRegionOnlyAcceptsLongFast()
{
    // UNSET uses PROFILE_UNDEF which has only LONG_FAST
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    cfg.use_preset = true;

    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    TEST_ASSERT_TRUE_MESSAGE(RadioInterface::validateConfigLora(cfg), "LONG_FAST should be valid for UNSET");

    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::validateConfigLora(cfg), "MEDIUM_FAST should be invalid for UNSET");

    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::validateConfigLora(cfg), "SHORT_TURBO should be invalid for UNSET");
}

static void test_validateConfigLora_allPresetsValidForLORA24()
{
    // LORA_24 uses PROFILE_STD (9 presets) with wideLora=true
    meshtastic_Config_LoRaConfig_ModemPreset stdPresets[] = {
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,     meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,   meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO,
    };

    for (size_t i = 0; i < sizeof(stdPresets) / sizeof(stdPresets[0]); i++) {
        meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
        cfg.region = meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
        cfg.use_preset = true;
        cfg.modem_preset = stdPresets[i];
        TEST_ASSERT_TRUE_MESSAGE(RadioInterface::validateConfigLora(cfg), "Expected valid preset for LORA_24");
    }
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
    TEST_ASSERT_EQUAL(eu868->getDefaultPreset(), cfg.modem_preset);
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
    float expectedBw = modemPresetToBwKHz(eu868->getDefaultPreset(), eu868->wideLora);
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

static void test_clampConfigLora_bogusPresetOnUnsetClampedToLongFast()
{
    // UNSET uses PROFILE_UNDEF with only LONG_FAST; any other preset should clamp to it
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    cfg.use_preset = true;
    cfg.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)99;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
}

static void test_clampConfigLora_invalidPresetOnLORA24ClampedToDefault()
{
    // LORA_24 uses PROFILE_STD; a bogus preset should clamp to LONG_FAST (first in PRESETS_STD)
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
    cfg.use_preset = true;
    cfg.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)99;

    RadioInterface::clampConfigLora(cfg);

    const RegionInfo *lora24 = getRegion(meshtastic_Config_LoRaConfig_RegionCode_LORA_24);
    TEST_ASSERT_EQUAL(lora24->getDefaultPreset(), cfg.modem_preset);
}

// -----------------------------------------------------------------------
// RegionInfo preset list integrity tests
// -----------------------------------------------------------------------

static void test_presetsStd_hasNineEntries()
{
    // PROFILE_STD should have exactly 9 presets
    const RegionInfo *us = getRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    TEST_ASSERT_EQUAL(9, us->getNumPresets());
    TEST_ASSERT_EQUAL_PTR(PROFILE_STD.presets, us->getAvailablePresets());
}

static void test_presetsEU868_hasSevenEntries()
{
    const RegionInfo *eu = getRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    TEST_ASSERT_EQUAL(7, eu->getNumPresets());
    TEST_ASSERT_EQUAL_PTR(PROFILE_EU868.presets, eu->getAvailablePresets());
}

static void test_presetsUndef_hasOneEntry()
{
    const RegionInfo *unset = getRegion(meshtastic_Config_LoRaConfig_RegionCode_UNSET);
    TEST_ASSERT_EQUAL(1, unset->getNumPresets());
    TEST_ASSERT_EQUAL_PTR(PROFILE_UNDEF.presets, unset->getAvailablePresets());
}

static void test_defaultPresetIsInAvailablePresets()
{
    // For every region, the defaultPreset must appear in its own availablePresets list
    const RegionInfo *r = regions;
    while (true) {
        bool found = false;
        for (size_t i = 0; i < r->getNumPresets(); i++) {
            if (r->getAvailablePresets()[i] == r->getDefaultPreset()) {
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
        TEST_ASSERT_TRUE_MESSAGE(r->getNumPresets() > 0, "numPresets must be > 0");
        TEST_ASSERT_NOT_NULL(r->getAvailablePresets());

        if (r->code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
            break;
        r++;
    }
}

static void test_onlyLORA24HasWideLora()
{
    // Verify that LORA_24 is the only region with wideLora=true
    const RegionInfo *r = regions;
    while (true) {
        char msg[80];
        if (r->code == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) {
            snprintf(msg, sizeof(msg), "Region %s should have wideLora=true", r->name);
            TEST_ASSERT_TRUE_MESSAGE(r->wideLora, msg);
        } else {
            snprintf(msg, sizeof(msg), "Region %s should have wideLora=false", r->name);
            TEST_ASSERT_FALSE_MESSAGE(r->wideLora, msg);
        }

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
    float channelSpacing = us->profile->spacing + (us->profile->padding * 2) + (bw / 1000.0f);
    uint32_t numChannels = (uint32_t)(((us->freqEnd - us->freqStart + us->profile->spacing) / channelSpacing) + 0.5f);

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
    float channelSpacing = eu->profile->spacing + (eu->profile->padding * 2) + (bw / 1000.0f);
    uint32_t numChannels = (uint32_t)(((eu->freqEnd - eu->freqStart + eu->profile->spacing) / channelSpacing) + 0.5f);

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
// handleSetConfig fromOthers dispatch tests
// -----------------------------------------------------------------------

class AdminModuleTestShim : public AdminModule
{
  public:
    using AdminModule::handleSetConfig;
};

static AdminModuleTestShim *testAdmin;

static meshtastic_Config makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode region, bool usePreset,
                                           meshtastic_Config_LoRaConfig_ModemPreset preset)
{
    meshtastic_Config c = meshtastic_Config_init_zero;
    c.which_payload_variant = meshtastic_Config_lora_tag;
    c.payload_variant.lora.region = region;
    c.payload_variant.lora.use_preset = usePreset;
    c.payload_variant.lora.modem_preset = preset;
    return c;
}

static void test_handleSetConfig_fromOthers_invalidPresetRejected()
{
    // Set up a known-good baseline in the global config
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    initRegion();

    // Build an admin set_config with an invalid preset for EU_868
    meshtastic_Config c = makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_EU_868, true,
                                            meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO);

    testAdmin->handleSetConfig(c, true); // fromOthers = true

    // fromOthers=true: invalid preset should be rejected, old preset preserved
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, config.lora.modem_preset);
}

static void test_handleSetConfig_fromLocal_invalidPresetClamped()
{
    // Set up a known-good baseline
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    initRegion();

    // Build an admin set_config with an invalid preset for EU_868
    meshtastic_Config c = makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_EU_868, true,
                                            meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO);

    testAdmin->handleSetConfig(c, false); // fromOthers = false (local client)

    // fromOthers=false: invalid preset should be clamped to the region's default
    const RegionInfo *eu868 = getRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    TEST_ASSERT_EQUAL(eu868->getDefaultPreset(), config.lora.modem_preset);
}

static void test_handleSetConfig_fromOthers_validPresetAccepted()
{
    // Set up baseline
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    initRegion();

    // Build an admin set_config with a valid preset for EU_868
    meshtastic_Config c = makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_EU_868, true,
                                            meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST);

    testAdmin->handleSetConfig(c, true); // fromOthers = true

    // Valid preset should be accepted regardless of fromOthers
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, config.lora.modem_preset);
}

// -----------------------------------------------------------------------
// Test runner
// -----------------------------------------------------------------------

void setUp(void)
{
    mockMeshService = new MockMeshService();
    service = mockMeshService;
    testAdmin = new AdminModuleTestShim();
}
void tearDown(void)
{
    service = nullptr;
    delete mockMeshService;
    mockMeshService = nullptr;
    delete testAdmin;
    testAdmin = nullptr;
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

    // Shadow table tests
    RUN_TEST(test_shadowTable_spacedProfileHasNonZeroSpacing);
    RUN_TEST(test_shadowTable_licensedProfileFlagsCorrect);
    RUN_TEST(test_shadowTable_presetCountMatchesExpected);
    RUN_TEST(test_shadowTable_defaultPresetIsFirstInList);
    RUN_TEST(test_shadowTable_channelSpacingWithPadding);
    RUN_TEST(test_shadowTable_turboOnlyOnWideLora);
    RUN_TEST(test_shadowTable_unknownCodeFallsToSentinel);

    // validateConfigLora()
    RUN_TEST(test_validateConfigLora_validPresetForUS);
    RUN_TEST(test_validateConfigLora_allStdPresetsValidForUS);
    RUN_TEST(test_validateConfigLora_turboPresetsInvalidForEU868);
    RUN_TEST(test_validateConfigLora_validPresetsForEU868);
    RUN_TEST(test_validateConfigLora_customBandwidthTooWideForEU868);
    RUN_TEST(test_validateConfigLora_customBandwidthFitsUS);
    RUN_TEST(test_validateConfigLora_customBandwidthFitsEU868);
    RUN_TEST(test_validateConfigLora_bogusPresetRejected);
    RUN_TEST(test_validateConfigLora_unsetRegionOnlyAcceptsLongFast);
    RUN_TEST(test_validateConfigLora_allPresetsValidForLORA24);

    // clampConfigLora()
    RUN_TEST(test_clampConfigLora_invalidPresetClampedToDefault);
    RUN_TEST(test_clampConfigLora_validPresetUnchanged);
    RUN_TEST(test_clampConfigLora_customBwTooWideClampedToDefaultBw);
    RUN_TEST(test_clampConfigLora_customBwValidLeftUnchanged);
    RUN_TEST(test_clampConfigLora_bogusPresetOnUnsetClampedToLongFast);
    RUN_TEST(test_clampConfigLora_invalidPresetOnLORA24ClampedToDefault);

    // RegionInfo preset list integrity
    RUN_TEST(test_presetsStd_hasNineEntries);
    RUN_TEST(test_presetsEU868_hasSevenEntries);
    RUN_TEST(test_presetsUndef_hasOneEntry);
    RUN_TEST(test_defaultPresetIsInAvailablePresets);
    RUN_TEST(test_regionFieldsAreSane);
    RUN_TEST(test_onlyLORA24HasWideLora);

    // Channel spacing (current + placeholder)
    RUN_TEST(test_channelSpacingCalculation_US_LONG_FAST);
    RUN_TEST(test_channelSpacingCalculation_EU868_LONG_FAST);
    RUN_TEST(test_channelSpacingCalculation_placeholder);

    // handleSetConfig fromOthers dispatch
    RUN_TEST(test_handleSetConfig_fromOthers_invalidPresetRejected);
    RUN_TEST(test_handleSetConfig_fromLocal_invalidPresetClamped);
    RUN_TEST(test_handleSetConfig_fromOthers_validPresetAccepted);

    exit(UNITY_END());
}

void loop() {}
