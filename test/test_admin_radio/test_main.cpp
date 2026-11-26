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
 *  6. RadioInterface::frequencySlotCount() on real region tables
 *  7. ClientNotification.type on the paths that do not set it as a literal at the send site:
 *     AdminModule's channel-warning queue (queueChannelWarning()/flushChannelWarnings()) and
 *     RadioInterface::checkConfigRegion(), which hands its failure type to validateConfigRegion().
 *     Clients key off the type to localize or act on a notification instead of matching its
 *     text, so a path that drops it sends UNSET and the client falls back to text matching.
 */

#include "Channels.h"
#include "FSCommon.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "TestUtil.h"
#include "graphics/draw/MenuHandler.h"
#include "mesh/Channels.h"
#include "mesh/CryptoEngine.h" // crypto global: the tests swap in a stub engine to drive key derivation
#include "mesh/Router.h"       // router global: allocErrorResponse() allocates the reply through it
#include "modules/AdminModule.h"
#include "modules/NodeInfoModule.h"
#include <ErriezCRC32.h> // crc32Buffer(), for the my_node_num == crc32(public_key) invariant
#include <pb_decode.h>
#include <pb_encode.h>
#include <string>
#include <unity.h>
#include <vector>

#include "meshtastic/config.pb.h"
#include "support/AdminModuleTestShim.h"

// Every client notification the AdminModule emits flows through sendClientNotification();
// capture each formatted message so the warning/coalescing tests can assert on the exact
// set of messages produced by a sequence of admin messages. This shadows test/support/MockMeshService.h's
// release-only stub because these tests need to inspect the captured message text, not just avoid leaks.
static std::vector<std::string> capturedWarnings;
static std::vector<meshtastic_ClientNotification_NotificationType> capturedTypes; // parallel to capturedWarnings
static std::vector<meshtastic_LogRecord_Level> capturedLevels;                    // parallel to capturedWarnings

class MockMeshService : public MeshService
{
  public:
    void sendClientNotification(meshtastic_ClientNotification *n) override
    {
        capturedWarnings.push_back(n->message);
        capturedTypes.push_back(n->type);
        capturedLevels.push_back(n->level);
        releaseClientNotificationToPool(n);
    }
};

static MockMeshService *mockMeshService;

// -----------------------------------------------------------------------
// getRegion() tests
// -----------------------------------------------------------------------

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

static void test_validateConfigRegion_unknownCodeReturnsFalse()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = (meshtastic_Config_LoRaConfig_RegionCode)255;

    devicestate.owner.is_licensed = false;

    // Unknown code is not in the regions table; getRegion() returns the UNSET sentinel,
    // whose .code != 255, so validateConfigRegion should reject it.
    TEST_ASSERT_FALSE(RadioInterface::validateConfigRegion(cfg));
}

static void test_validateConfigRegion_anotherUnknownCodeReturnsFalse()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = (meshtastic_Config_LoRaConfig_RegionCode)99;

    devicestate.owner.is_licensed = true;

    // Unknown code should be rejected even when owner is licensed.
    TEST_ASSERT_FALSE(RadioInterface::validateConfigRegion(cfg));
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
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO,    meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO,
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
    // EU_868 has PRESETS_EU_868 which excludes the 500 kHz turbo presets
    // (SHORT_TURBO, LONG_TURBO, MEDIUM_TURBO)
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.use_preset = true;

    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::validateConfigLora(cfg), "SHORT_TURBO should be invalid for EU_868");

    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO;
    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::validateConfigLora(cfg), "LONG_TURBO should be invalid for EU_868");

    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO;
    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::validateConfigLora(cfg), "MEDIUM_TURBO should be invalid for EU_868");
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

static void test_validateConfigLora_unsetRegionAcceptsAnyRealPreset()
{
    // UNSET is "no region chosen yet", not a regulatory domain, so it must not invalidate
    // a preset the user already picked - whichever region that preset belongs to.
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    cfg.use_preset = true;

    const meshtastic_Config_LoRaConfig_ModemPreset realPresets[] = {
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,   meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO,
        meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST,   meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_TINY_FAST,
    };
    for (auto preset : realPresets) {
        cfg.modem_preset = preset;
        char msg[64];
        snprintf(msg, sizeof(msg), "preset %d should be valid for UNSET", (int)preset);
        TEST_ASSERT_TRUE_MESSAGE(RadioInterface::validateConfigLora(cfg), msg);
    }

    // A value no region offers is still invalid, so the clamp can repair it.
    cfg.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)99;
    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::validateConfigLora(cfg), "bogus preset should be invalid for UNSET");
}

static void test_isKnownModemPreset_matchesRegionTable()
{
    // Every preset some region offers is "known"...
    TEST_ASSERT_TRUE(isKnownModemPreset(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST));
    TEST_ASSERT_TRUE(isKnownModemPreset(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO));
    TEST_ASSERT_TRUE(isKnownModemPreset(meshtastic_Config_LoRaConfig_ModemPreset_LITE_SLOW));
    TEST_ASSERT_TRUE(isKnownModemPreset(meshtastic_Config_LoRaConfig_ModemPreset_TINY_SLOW));

    // ...and nothing else is, including the retired VERY_LONG_SLOW enum value.
    TEST_ASSERT_FALSE(isKnownModemPreset(meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW));
    TEST_ASSERT_FALSE(isKnownModemPreset((meshtastic_Config_LoRaConfig_ModemPreset)99));
}

static void test_validateConfigLora_allPresetsValidForLORA24()
{
    // LORA_24 uses PROFILE_STD (10 presets) with wideLora=true
    meshtastic_Config_LoRaConfig_ModemPreset stdPresets[] = {
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,     meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,   meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO,    meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO,
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

// NodeDB::clampLoadedLoraConfig() runs at boot, before onConfigChanged() renames a legacy "Default"
// primary to "". If it hashed "Default" as a custom channel name, an out-of-range channel_num would be
// clamped to that hash's slot instead of the preset's default slot, moving the node off its mesh.
static void test_clampLoadedLoraConfig_legacyDefaultPrimaryGetsThePresetSlot()
{
    channels.initDefaults();
    strncpy(channelFile.channels[0].settings.name, "Default", sizeof(channelFile.channels[0].settings.name));
    config.has_lora = true;
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    meshtastic_Config_LoRaConfig unset = config.lora;
    const uint32_t presetSlot = RadioInterface::resolveFrequencySlot(unset, "");
    config.lora.channel_num = 200; // more than US LongFast's 104 slots

    nodeDB->clampLoadedLoraConfig();

    // The bad pin is cleared, and the renamed primary resolves to the preset's slot, not hash("Default")
    TEST_ASSERT_EQUAL_UINT32(0, config.lora.channel_num);
    TEST_ASSERT_EQUAL_STRING("", channelFile.channels[0].settings.name);
    TEST_ASSERT_EQUAL_UINT32(presetSlot,
                             RadioInterface::resolveFrequencySlot(config.lora, channelFile.channels[0].settings.name));
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
    // UNSET's default preset is LONG_FAST; a value no region offers clamps to it
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    cfg.use_preset = true;
    cfg.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)99;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
}

static void test_clampConfigLora_unsetRegionKeepsRealPreset()
{
    // The boot-time clamp (NodeDB::loadFromDisk) runs on every boot. While the region is
    // unset it must leave a real preset alone rather than rewriting it to LONG_FAST.
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, cfg.modem_preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_UNSET, cfg.region);
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
// Region-locked preset swap tests (EU_868 / EU_866 / EU_N_868 trio)
// -----------------------------------------------------------------------

static void test_clampConfigLora_narrowPresetOnEU866SwapsToEUN868()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_866;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_EU_N_868, cfg.region);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST, cfg.modem_preset);
}

static void test_clampConfigLora_litePresetOnEU868SwapsToEU866()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LITE_SLOW;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_EU_866, cfg.region);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LITE_SLOW, cfg.modem_preset);
}

static void test_clampConfigLora_eu868PresetOnEUN868SwapsToEU868()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_N_868;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_EU_868, cfg.region);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
}

static void test_clampConfigLora_litePresetOnUSDoesNotSwap()
{
    // Previous region is not one of the swappable trio, so the preset clamps to the
    // region default instead of swapping regions.
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST;

    RadioInterface::clampConfigLora(cfg);

    const RegionInfo *us = getRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, cfg.region);
    TEST_ASSERT_EQUAL(us->getDefaultPreset(), cfg.modem_preset);
}

static void test_clampConfigLora_narrowPresetOnHam125cmDoesNotSwap()
{
    // ITU2_125CM shares the NARROW presets, so they are valid there and nothing changes
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_ITU2_125CM;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_ITU2_125CM, cfg.region);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW, cfg.modem_preset);
}

static void resetUnnamedPrimaryChannel()
{
    channels.initDefaults();
    channels.resolvePrimaryIndex();
    channels.getByIndex(channels.getPrimaryIndex()).settings.name[0] = '\0';
}

static void test_clampConfigLora_longSlowOnJPClampedToLongFast()
{
    // Regulatory migration of removed JP LONG_SLOW clamps to LONG_FAST at INFO without Critical Fault #7.
    resetUnnamedPrimaryChannel();
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_JP;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
    TEST_ASSERT_EQUAL(meshtastic_CriticalErrorCode_NONE, error_code);
    TEST_ASSERT_EQUAL_INT(1, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_PRESET_INVALID_FOR_REGION, capturedTypes[0]);
    TEST_ASSERT_EQUAL(meshtastic_LogRecord_Level_INFO, capturedLevels[0]);
}

static void test_clampConfigLora_longModerateOnJPClampedToLongFast()
{
    // Regulatory migration of removed JP LONG_MODERATE clamps to LONG_FAST at INFO without Critical Fault #7.
    resetUnnamedPrimaryChannel();
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_JP;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
    TEST_ASSERT_EQUAL(meshtastic_CriticalErrorCode_NONE, error_code);
    TEST_ASSERT_EQUAL_INT(1, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_PRESET_INVALID_FOR_REGION, capturedTypes[0]);
    TEST_ASSERT_EQUAL(meshtastic_LogRecord_Level_INFO, capturedLevels[0]);
}

static void test_validateConfigLora_invalidPresetIsQuiet()
{
    // Pure validation (clamp == false) must return false without emitting client notifications or
    // latching a critical error so local callers can defer cleanly to clampConfigLora().
    resetUnnamedPrimaryChannel();

    for (auto preset :
         {meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE}) {
        meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
        cfg.region = meshtastic_Config_LoRaConfig_RegionCode_JP;
        cfg.use_preset = true;
        cfg.modem_preset = preset;

        TEST_ASSERT_FALSE(RadioInterface::validateConfigLora(cfg));
        TEST_ASSERT_EQUAL(meshtastic_CriticalErrorCode_NONE, error_code);
        TEST_ASSERT_EQUAL_INT(0, (int)capturedTypes.size());
    }

    meshtastic_Config_LoRaConfig euCfg = meshtastic_Config_LoRaConfig_init_zero;
    euCfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    euCfg.use_preset = true;
    euCfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
    TEST_ASSERT_FALSE(RadioInterface::validateConfigLora(euCfg));
    TEST_ASSERT_EQUAL(meshtastic_CriticalErrorCode_NONE, error_code);
    TEST_ASSERT_EQUAL_INT(0, (int)capturedTypes.size());
}

static void test_clampConfigLora_jpPresetMigrationRepairsSlot()
{
    // JP LONG_MODERATE (default slot 14) and LONG_SLOW (up to slot 24) ran at 125 kHz (24 slots).
    // Migrating to 250 kHz LONG_FAST (12 slots) invalidates slots 13..24 solely because bandwidth
    // doubled: the derived slot repair must stay informational (no Critical Fault #7), clear the
    // failed pin to 0 (Auto), and resolve the fallback slot from the pending migrated preset
    // rather than stale global config.lora.
    resetUnnamedPrimaryChannel();
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_JP;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE;

    // Case 1: LONG_MODERATE with its legacy default hashed slot (14) -> clears pin to 0, resolves to LONG_FAST slot 12.
    meshtastic_Config_LoRaConfig modCfg = meshtastic_Config_LoRaConfig_init_zero;
    modCfg.region = meshtastic_Config_LoRaConfig_RegionCode_JP;
    modCfg.use_preset = true;
    modCfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE;
    modCfg.channel_num = 14;

    RadioInterface::clampConfigLora(modCfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, modCfg.modem_preset);
    TEST_ASSERT_EQUAL_UINT32(0, modCfg.channel_num);
    TEST_ASSERT_TRUE(RadioInterface::uses_default_frequency_slot);
    TEST_ASSERT_FALSE(RadioInterface::uses_custom_channel_name);
    TEST_ASSERT_EQUAL_UINT32(12, RadioInterface::resolveFrequencySlot(modCfg, ""));
    TEST_ASSERT_EQUAL(meshtastic_CriticalErrorCode_NONE, error_code);
    TEST_ASSERT_EQUAL_INT(2, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_PRESET_INVALID_FOR_REGION, capturedTypes[0]);
    TEST_ASSERT_EQUAL(meshtastic_LogRecord_Level_INFO, capturedLevels[0]);
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_CHANNEL_NUM_INVALID, capturedTypes[1]);
    TEST_ASSERT_EQUAL(meshtastic_LogRecord_Level_INFO, capturedLevels[1]);

    // Case 2: LONG_SLOW at upper valid pre-migration slot (24) -> clears pin to 0, resolves to LONG_FAST slot 12 at INFO.
    capturedWarnings.clear();
    capturedTypes.clear();
    capturedLevels.clear();

    meshtastic_Config_LoRaConfig slowCfg = meshtastic_Config_LoRaConfig_init_zero;
    slowCfg.region = meshtastic_Config_LoRaConfig_RegionCode_JP;
    slowCfg.use_preset = true;
    slowCfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    slowCfg.channel_num = 24;

    RadioInterface::clampConfigLora(slowCfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, slowCfg.modem_preset);
    TEST_ASSERT_EQUAL_UINT32(0, slowCfg.channel_num);
    TEST_ASSERT_TRUE(RadioInterface::uses_default_frequency_slot);
    TEST_ASSERT_FALSE(RadioInterface::uses_custom_channel_name);
    TEST_ASSERT_EQUAL_UINT32(12, RadioInterface::resolveFrequencySlot(slowCfg, ""));
    TEST_ASSERT_EQUAL(meshtastic_CriticalErrorCode_NONE, error_code);
    TEST_ASSERT_EQUAL_INT(2, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_LogRecord_Level_INFO, capturedLevels[0]);
    TEST_ASSERT_EQUAL(meshtastic_LogRecord_Level_INFO, capturedLevels[1]);
}

static void test_clampConfigLora_jpMigrationPreservesValidSlot()
{
    // A pre-migration JP LONG_SLOW slot that still fits in LONG_FAST (e.g. slot 3 <= 12) must be
    // preserved untouched with only the single preset-migration INFO notification.
    resetUnnamedPrimaryChannel();
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_JP;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    cfg.channel_num = 3;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
    TEST_ASSERT_EQUAL_UINT32(3, cfg.channel_num);
    TEST_ASSERT_EQUAL(meshtastic_CriticalErrorCode_NONE, error_code);
    TEST_ASSERT_EQUAL_INT(1, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_PRESET_INVALID_FOR_REGION, capturedTypes[0]);
    TEST_ASSERT_EQUAL(meshtastic_LogRecord_Level_INFO, capturedLevels[0]);
}

static void test_clampConfigLora_jpMigrationStillErrorsBadSlot()
{
    // Slots that were already invalid before migration (>24 for JP 125 kHz, or paired with a
    // fabricated preset enum) must retain ERROR reporting and INVALID_RADIO_SETTING.
    resetUnnamedPrimaryChannel();

    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_JP;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    cfg.channel_num = 25;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.channel_num);
    TEST_ASSERT_TRUE(RadioInterface::uses_default_frequency_slot);
    TEST_ASSERT_FALSE(RadioInterface::uses_custom_channel_name);
    TEST_ASSERT_EQUAL_UINT32(12, RadioInterface::resolveFrequencySlot(cfg, ""));
    TEST_ASSERT_EQUAL(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING, error_code);
    TEST_ASSERT_EQUAL_INT(2, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_CHANNEL_NUM_INVALID, capturedTypes[1]);
    TEST_ASSERT_EQUAL(meshtastic_LogRecord_Level_ERROR, capturedLevels[1]);

    // Fabricated enum values are not known presets and must not downgrade an out-of-range slot.
    error_code = meshtastic_CriticalErrorCode_NONE;
    capturedWarnings.clear();
    capturedTypes.clear();
    capturedLevels.clear();

    meshtastic_Config_LoRaConfig bogus = meshtastic_Config_LoRaConfig_init_zero;
    bogus.region = meshtastic_Config_LoRaConfig_RegionCode_JP;
    bogus.use_preset = true;
    bogus.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)99;
    bogus.channel_num = 20;

    RadioInterface::clampConfigLora(bogus);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, bogus.modem_preset);
    TEST_ASSERT_EQUAL_UINT32(0, bogus.channel_num);
    TEST_ASSERT_TRUE(RadioInterface::uses_default_frequency_slot);
    TEST_ASSERT_FALSE(RadioInterface::uses_custom_channel_name);
    TEST_ASSERT_EQUAL_UINT32(12, RadioInterface::resolveFrequencySlot(bogus, ""));
    TEST_ASSERT_EQUAL(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING, error_code);
    TEST_ASSERT_EQUAL_INT(2, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_CHANNEL_NUM_INVALID, capturedTypes[1]);
    TEST_ASSERT_EQUAL(meshtastic_LogRecord_Level_ERROR, capturedLevels[1]);
}

static void test_validateConfigLora_siblingLockedPresetStillFailsValidation()
{
    // Validation (no clamp) must keep failing so callers route into clampConfigLora,
    // which performs the region swap.
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_866;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST;

    TEST_ASSERT_FALSE(RadioInterface::validateConfigLora(cfg));
}

// -----------------------------------------------------------------------
// RegionInfo preset list integrity tests
// -----------------------------------------------------------------------

static void test_presetsStd_hasTenEntries()
{
    // PROFILE_STD should have exactly 10 presets (adds MEDIUM_TURBO to the turbo cluster)
    const RegionInfo *us = getRegion(meshtastic_Config_LoRaConfig_RegionCode_US);
    TEST_ASSERT_EQUAL(10, us->getNumPresets());
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
// RadioInterface::frequencySlotCount() on real region tables
// -----------------------------------------------------------------------

// EU_868 fits exactly one 250 kHz LONG_FAST channel, and EU_866's PROFILE_LITE carries non-zero spacing and
// padding (0.4 + 2*0.0375 MHz around a 125 kHz LITE_FAST channel: 2.4 / 0.6 = 4 slots). A change to the spacing
// or padding terms of the slot formula moves the EU_866 count; US/LONG_FAST = 104 is pinned in test_mesh_beacon.
static void test_frequencySlotCount_realRegions()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;

    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    TEST_ASSERT_EQUAL_UINT32(1, RadioInterface::frequencySlotCount(cfg));

    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_866;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST;
    TEST_ASSERT_EQUAL_UINT32(4, RadioInterface::frequencySlotCount(cfg));
}

// -----------------------------------------------------------------------
// handleSetConfig fromOthers dispatch tests
// -----------------------------------------------------------------------

// AdminModuleTestShim comes from test/support - the friend seam AdminModule.h declares.
static AdminModuleTestShim *testAdmin;
static NodeDB *savedNodeDB;
static NodeDB *replacementNodeDB;
static NodeInfoModule *savedNodeInfoModule;
static meshtastic_DeviceState savedDeviceState;
static meshtastic_User savedOwner;
static meshtastic_LocalConfig savedConfig;
static meshtastic_ChannelFile savedChannelFile;
static meshtastic_CriticalErrorCode savedErrorCode;
// Only the ham dispatcher test installs a router (allocErrorResponse() allocates through it).
// Saved/torn down for every test so a failed assertion's longjmp cannot leave one dangling.
static Router *savedRouter;
static Router *hamMockRouter;

// Called from setUp/tearDown for every test, not opted into by a handful. A shared NodeDB plus
// unrestored config/owner/devicestate/channelFile means each test inherits whatever its
// predecessors left, and the admin handlers under test write all four.
static void replaceAdminRadioGlobals()
{
    savedNodeDB = nodeDB;
    savedNodeInfoModule = nodeInfoModule;
    savedRouter = router;
    savedDeviceState = devicestate;
    savedOwner = owner;
    savedConfig = config;
    savedChannelFile = channelFile;
    savedErrorCode = error_code;
    error_code = meshtastic_CriticalErrorCode_NONE;
    replacementNodeDB = new NodeDB();
    nodeDB = replacementNodeDB;
}

// Defined with the crypto stub below; tearDown must undo an install even when a failed assertion
// longjmped out of the test body before it could.
static void dropRestoreCryptoStub();

static void restoreAdminRadioGlobals()
{
    dropRestoreCryptoStub();
    nodeInfoModule = savedNodeInfoModule;
    nodeDB = savedNodeDB;
    router = savedRouter;
    delete hamMockRouter;
    hamMockRouter = nullptr;
    delete replacementNodeDB;
    replacementNodeDB = nullptr;
    devicestate = savedDeviceState;
    owner = savedOwner;
    config = savedConfig;
    channelFile = savedChannelFile;
    error_code = savedErrorCode;
    initRegion();
}

static void installEncryptedAndAdminChannels()
{
    channels.initDefaults();
    meshtastic_Channel admin = meshtastic_Channel_init_zero;
    admin.index = 1;
    admin.role = meshtastic_Channel_Role_SECONDARY;
    admin.has_settings = true;
    strncpy(admin.settings.name, Channels::adminChannel, sizeof(admin.settings.name));
    admin.settings.psk.size = 16;
    memset(admin.settings.psk.bytes, 0xA5, admin.settings.psk.size);
    channels.setChannel(admin);

    meshtastic_Channel secondary = meshtastic_Channel_init_zero;
    secondary.index = 2;
    secondary.role = meshtastic_Channel_Role_SECONDARY;
    secondary.has_settings = true;
    strncpy(secondary.settings.name, "private", sizeof(secondary.settings.name));
    secondary.settings.psk.size = 32;
    memset(secondary.settings.psk.bytes, 0x5A, secondary.settings.psk.size);
    channels.setChannel(secondary);
}

static void assertLicensedChannelsSanitized()
{
    TEST_ASSERT_EQUAL(0, channels.getByIndex(0).settings.psk.size);
    TEST_ASSERT_EQUAL(meshtastic_Channel_Role_DISABLED, channels.getByIndex(1).role);
    TEST_ASSERT_EQUAL(0, channels.getByIndex(1).settings.psk.size);
    TEST_ASSERT_EQUAL(0, channels.getByIndex(2).settings.psk.size);
}

static void test_handleSetOwner_persistsLicensedChannelSanitation()
{
    owner = meshtastic_User_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    installEncryptedAndAdminChannels();

    meshtastic_User licensed = meshtastic_User_init_zero;
    licensed.is_licensed = true;
    testAdmin->deferSaves();
    nodeInfoModule = reinterpret_cast<NodeInfoModule *>(1); // reloadOwner(false) only checks presence
    testAdmin->handleSetOwner(licensed);

    TEST_ASSERT_TRUE(testAdmin->savedSegments() & SEGMENT_CHANNELS);
    assertLicensedChannelsSanitized();

    uint8_t encoded[meshtastic_ChannelFile_size];
    const size_t encodedSize = pb_encode_to_bytes(encoded, sizeof(encoded), &meshtastic_ChannelFile_msg, &channelFile);
    TEST_ASSERT_GREATER_THAN(0, encodedSize);
    meshtastic_ChannelFile reloaded = meshtastic_ChannelFile_init_zero;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(encoded, encodedSize, &meshtastic_ChannelFile_msg, &reloaded));
    channelFile = reloaded;
    assertLicensedChannelsSanitized();
    TEST_ASSERT_FALSE_MESSAGE(channels.ensureLicensedOperation(), "sanitized reload must not trigger another persistence write");
}

// -----------------------------------------------------------------------
// handleSetHamMode() name assembly: the ham long_name rides behind the call
// sign with the "//" separator hams already use on the air.
// -----------------------------------------------------------------------

// Licensing a node touches channels, the NodeDB and the owner struct; an UNSET region keeps the
// keygen/identity-migration path out of these name-only assertions.
static void primeHamModeTest()
{
    owner = meshtastic_User_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    channels.initDefaults();
    nodeInfoModule = reinterpret_cast<NodeInfoModule *>(1); // reloadOwner(false) only checks presence
    testAdmin->deferSaves();
}

static void test_handleSetHamMode_appendsLongNameToCallSign()
{
    primeHamModeTest();

    meshtastic_HamParameters p = meshtastic_HamParameters_init_zero;
    strncpy(p.call_sign, "KD2ABC", sizeof(p.call_sign) - 1);
    strncpy(p.short_name, "ABC", sizeof(p.short_name) - 1);
    strncpy(p.long_name, "Attic Heltec", sizeof(p.long_name) - 1);
    TEST_ASSERT_TRUE(testAdmin->handleSetHamMode(p));

    TEST_ASSERT_EQUAL_STRING("KD2ABC//Attic Heltec", owner.long_name);
    TEST_ASSERT_EQUAL_STRING("ABC", owner.short_name);
    TEST_ASSERT_TRUE(owner.is_licensed);
}

// The widest pair the proto can carry (7 + 2 + 14) still has to arrive whole, or the operator
// silently loses the tail of the name they typed.
static void test_handleSetHamMode_widestPairSurvivesTheLongNameCap()
{
    primeHamModeTest();

    meshtastic_HamParameters p = meshtastic_HamParameters_init_zero;
    strncpy(p.call_sign, "KD2ABCD", sizeof(p.call_sign) - 1);
    strncpy(p.long_name, "Attic Heltec 3", sizeof(p.long_name) - 1);
    TEST_ASSERT_TRUE(testAdmin->handleSetHamMode(p));

    TEST_ASSERT_EQUAL_STRING("KD2ABCD//Attic Heltec 3", owner.long_name);
    TEST_ASSERT_LESS_OR_EQUAL(MAX_LONG_NAME_BYTES, strlen(owner.long_name));
}

static void test_handleSetHamMode_omittedLongNameKeepsCallSignAlone()
{
    primeHamModeTest();

    meshtastic_HamParameters p = meshtastic_HamParameters_init_zero;
    strncpy(p.call_sign, "KD2ABC", sizeof(p.call_sign) - 1);
    testAdmin->handleSetHamMode(p);

    TEST_ASSERT_EQUAL_STRING("KD2ABC", owner.long_name);
    TEST_ASSERT_TRUE(owner.is_licensed);
}

// long_name is optional both ways a client can leave it empty: a whitespace-only one is dropped
// (no dangling "//" on the air) instead of costing the operator the whole licensing request.
static void test_handleSetHamMode_blankLongNameIsIgnoredNotRejected()
{
    primeHamModeTest();

    meshtastic_HamParameters p = meshtastic_HamParameters_init_zero;
    strncpy(p.call_sign, "KD2ABC", sizeof(p.call_sign) - 1);
    strncpy(p.long_name, "   ", sizeof(p.long_name) - 1);
    testAdmin->handleSetHamMode(p);

    TEST_ASSERT_EQUAL_STRING("KD2ABC", owner.long_name);
    TEST_ASSERT_TRUE(owner.is_licensed);
}

// The call sign is required, unlike the two optional name fields: an empty one would license a
// node that never identifies itself, and once a long_name is set it would compose to a dangling
// "//Attic Heltec".
static void test_handleSetHamMode_blankCallSignIsRejected()
{
    primeHamModeTest();

    meshtastic_HamParameters missing = meshtastic_HamParameters_init_zero;
    strncpy(missing.long_name, "Attic Heltec", sizeof(missing.long_name) - 1);
    TEST_ASSERT_FALSE(testAdmin->handleSetHamMode(missing));

    TEST_ASSERT_EQUAL_STRING("", owner.long_name);
    TEST_ASSERT_FALSE(owner.is_licensed);

    primeHamModeTest();

    meshtastic_HamParameters whitespace = meshtastic_HamParameters_init_zero;
    strncpy(whitespace.call_sign, "   ", sizeof(whitespace.call_sign) - 1);
    TEST_ASSERT_FALSE(testAdmin->handleSetHamMode(whitespace));

    TEST_ASSERT_EQUAL_STRING("", owner.long_name);
    TEST_ASSERT_FALSE(owner.is_licensed);
}

// short_name is optional too, so a blank one keeps whatever the node was already called instead of
// blanking it - licensing the node must not cost the operator their existing short name.
static void test_handleSetHamMode_blankShortNameKeepsTheExistingOne()
{
    for (const char *blank : {"", "  "}) {
        primeHamModeTest();
        strncpy(owner.short_name, "OLD", sizeof(owner.short_name) - 1);

        meshtastic_HamParameters p = meshtastic_HamParameters_init_zero;
        strncpy(p.call_sign, "KD2ABC", sizeof(p.call_sign) - 1);
        strncpy(p.short_name, blank, sizeof(p.short_name) - 1);
        TEST_ASSERT_TRUE(testAdmin->handleSetHamMode(p));

        TEST_ASSERT_EQUAL_STRING("OLD", owner.short_name);
        TEST_ASSERT_EQUAL_STRING("KD2ABC", owner.long_name);
        TEST_ASSERT_TRUE(owner.is_licensed);
    }
}

// A rejection has to reach the client, not just the log: allocErrorResponse() builds the reply
// through the router, so this is the one ham test that needs one.
class HamModeMockRouter : public Router
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }
};

// Pull the Routing error out of the ack/nak a handler queued in myReply.
static bool decodeRoutingError(meshtastic_MeshPacket *reply, meshtastic_Routing_Error &out)
{
    if (!reply || reply->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return false;
    if (reply->decoded.portnum != meshtastic_PortNum_ROUTING_APP)
        return false;
    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    if (!pb_decode_from_bytes(reply->decoded.payload.bytes, reply->decoded.payload.size, &meshtastic_Routing_msg, &routing))
        return false;
    if (routing.which_variant != meshtastic_Routing_error_reason_tag)
        return false;
    out = routing.error_reason;
    return true;
}

// Handler-level rejection is invisible to a want_response client on its own: with no reply queued,
// handleReceivedProtobuf() falls through to its generic "ACK" and answers Routing_Error_NONE, so the
// app reports ham mode as enabled on a node that changed nothing. The dispatcher has to say
// BAD_REQUEST before that fallback runs.
static void test_handleSetHamMode_blankCallSignRepliesBadRequest()
{
    primeHamModeTest();
    hamMockRouter = new HamModeMockRouter();
    router = hamMockRouter;

    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_set_ham_mode_tag;
    strncpy(m.set_ham_mode.long_name, "Attic Heltec", sizeof(m.set_ham_mode.long_name) - 1);

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = 0; // local client, so the passkey gate is bypassed and the switch body runs
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.want_response = true;
    testAdmin->handleReceivedProtobuf(mp, &m);

    meshtastic_Routing_Error err = meshtastic_Routing_Error_NONE;
    TEST_ASSERT_TRUE_MESSAGE(decodeRoutingError(testAdmin->reply(), err), "a rejected request must queue an error reply");
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_BAD_REQUEST, err);
    TEST_ASSERT_FALSE(owner.is_licensed);
    testAdmin->drainReply();
}

// The other half of the pair: an accepted request still answers Routing_Error_NONE. Asserting both
// sides is the point - NONE is what the rejection path used to borrow, so a test that only checked
// the reject case could pass against a handler that answered NONE to everything.
static void test_handleSetHamMode_acceptedRequestAcksSuccess()
{
    primeHamModeTest();
    hamMockRouter = new HamModeMockRouter();
    router = hamMockRouter;

    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_set_ham_mode_tag;
    strncpy(m.set_ham_mode.call_sign, "KD2ABC", sizeof(m.set_ham_mode.call_sign) - 1);
    strncpy(m.set_ham_mode.long_name, "Attic Heltec", sizeof(m.set_ham_mode.long_name) - 1);

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = 0;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.want_response = true;
    testAdmin->handleReceivedProtobuf(mp, &m);

    meshtastic_Routing_Error err = meshtastic_Routing_Error_BAD_REQUEST;
    TEST_ASSERT_TRUE_MESSAGE(decodeRoutingError(testAdmin->reply(), err), "want_response must be answered");
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, err);
    TEST_ASSERT_EQUAL_STRING("KD2ABC//Attic Heltec", owner.long_name);
    TEST_ASSERT_TRUE(owner.is_licensed);
    testAdmin->drainReply();
}

// The LORA_24 gate in handleReceivedProtobuf must ask the interface the router actually holds. It used
// to ask RadioLibInterface::instance, which is null for any interface that is not a local RadioLib chip
// (meshtasticd's transparent radio drives another node's chip), so LORA_24 was refused there even when
// the radio behind it does 2.4 GHz.
class WideLoraGateRadio : public RadioInterface
{
  public:
    explicit WideLoraGateRadio(bool wide) : wide(wide) {}
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }
    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override { return 100; }
    bool wideLora() override { return wide; }

  private:
    bool wide;
};

// Send set_config{region LORA_24} from a local client through a router whose interface reports
// `radioIsWide`, and return the Routing error the dispatcher answered with.
static meshtastic_Routing_Error setLora24Through(bool radioIsWide)
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    initRegion();
    testAdmin->deferSaves();
    hamMockRouter = new HamModeMockRouter();
    hamMockRouter->addInterface(std::unique_ptr<RadioInterface>(new WideLoraGateRadio(radioIsWide)));
    router = hamMockRouter;

    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_set_config_tag;
    m.set_config.which_payload_variant = meshtastic_Config_lora_tag;
    m.set_config.payload_variant.lora.region = meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
    m.set_config.payload_variant.lora.use_preset = true;
    m.set_config.payload_variant.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = 0;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.want_response = true;
    testAdmin->handleReceivedProtobuf(mp, &m);

    meshtastic_Routing_Error err = meshtastic_Routing_Error_MAX_RETRANSMIT; // neither answer the test expects
    TEST_ASSERT_TRUE_MESSAGE(decodeRoutingError(testAdmin->reply(), err), "want_response must be answered");
    testAdmin->drainReply();
    return err;
}

static void test_setConfig_lora24_acceptedWhenTheRouterInterfaceDoes2_4GHz()
{
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, setLora24Through(true));
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_LORA_24, config.lora.region);
}

// The fail-closed half: an interface without 2.4 GHz still gets BAD_REQUEST and keeps its region.
static void test_setConfig_lora24_rejectedWhenTheRouterInterfaceCannot()
{
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_BAD_REQUEST, setLora24Through(false));
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, config.lora.region);
}

// A PKC admin request with want_ack and no want_response is answered by the module's ROUTING reply in place
// of the ACK. That reply must still encode: PKC never wraps a ROUTING packet, so a reply left marked
// pki_encrypted fails perhapsEncode and the sender hears nothing.
static void expectPkcAdminRoutingReplyEncodes(meshtastic_AdminMessage &m)
{
    static constexpr NodeNum REMOTE_ADMIN = 0x0B0B0B0B;
    myNodeInfo.my_node_num = 0x0A0A0A0A;
    channels.initDefaults();
    channels.onConfigChanged();
    config.security.admin_key_count = 1;
    config.security.admin_key[0].size = 32;
    memset(config.security.admin_key[0].bytes, 0xAA, 32);

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = REMOTE_ADMIN;
    mp.to = myNodeInfo.my_node_num;
    mp.id = 0x5151;
    mp.channel = 0; // a PKC packet arrives with channel hash 0, which resolves to the primary channel
    mp.want_ack = true;
    mp.pki_encrypted = true;
    mp.public_key.size = 32;
    memset(mp.public_key.bytes, 0xAA, 32);
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.want_response = false;
    meshtastic_AdminMessage session = meshtastic_AdminMessage_init_zero;
    testAdmin->setPassKey(&session); // a state-changing request must carry a live session key
    m.session_passkey = session.session_passkey;
    testAdmin->handleReceivedProtobuf(mp, &m);

    meshtastic_MeshPacket *reply = testAdmin->reply();
    TEST_ASSERT_NOT_NULL_MESSAGE(reply, "the request must queue a routing reply");
    TEST_ASSERT_EQUAL(meshtastic_PortNum_ROUTING_APP, reply->decoded.portnum);
    meshtastic_Routing_Error err = meshtastic_Routing_Error_NONE;
    TEST_ASSERT_TRUE(decodeRoutingError(reply, err));
    TEST_ASSERT_NOT_EQUAL_MESSAGE(meshtastic_Routing_Error_ADMIN_BAD_SESSION_KEY, err, "the request must pass the session gate");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Routing_Error_NONE, perhapsEncode(reply), "the routing reply was dropped at encode");
    testAdmin->drainReply();
}

static void test_pkcAdminBadRequest_replyEncodes()
{
    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_get_channel_request_tag;
    m.get_channel_request = MAX_NUM_CHANNELS + 2; // out of range, so the handler answers BAD_REQUEST
    expectPkcAdminRoutingReplyEncodes(m);
}

static void test_pkcAdminBackupPreferencesReply_encodes()
{
    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_backup_preferences_tag;
    m.backup_preferences = meshtastic_AdminMessage_BackupLocation_FLASH;
    expectPkcAdminRoutingReplyEncodes(m);
}

static void test_bootDefense_sanitizesStaleLicensedChannelsOnce()
{
    owner = meshtastic_User_init_zero;
    owner.is_licensed = true;
    installEncryptedAndAdminChannels();

    TEST_ASSERT_TRUE(channels.ensureLicensedOperation());
    assertLicensedChannelsSanitized();
    TEST_ASSERT_FALSE_MESSAGE(channels.ensureLicensedOperation(), "boot sanitation must be idempotent");
}

static void test_restorePreferences_sanitizesLicensedBackupBeforeReturn()
{
    NodeDB *savedNodeDB = nodeDB;
    nodeDB = new NodeDB();
    const meshtastic_DeviceState savedDeviceState = devicestate;
    const meshtastic_ChannelFile savedChannelFile = channelFile;

    owner = meshtastic_User_init_zero;
    owner.is_licensed = true;
    installEncryptedAndAdminChannels();
    TEST_ASSERT_TRUE(nodeDB->backupPreferences(meshtastic_AdminMessage_BackupLocation_FLASH));

    owner.is_licensed = false;
    channels.initDefaults();
    TEST_ASSERT_TRUE(
        nodeDB->restorePreferences(meshtastic_AdminMessage_BackupLocation_FLASH, SEGMENT_DEVICESTATE | SEGMENT_CHANNELS));
    TEST_ASSERT_TRUE(owner.is_licensed);
    assertLicensedChannelsSanitized();
    TEST_ASSERT_FALSE_MESSAGE(channels.ensureLicensedOperation(), "restored licensed channels must remain sanitized");

    devicestate = savedDeviceState;
    channelFile = savedChannelFile;
    nodeDB->saveToDisk(SEGMENT_DEVICESTATE | SEGMENT_CHANNELS);
    FSCom.remove(backupFileName);
    delete nodeDB;
    nodeDB = savedNodeDB;
}

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

static void test_handleSetConfig_persistsLicensedFirstRegionIdentity()
{
    owner = meshtastic_User_init_zero;
    owner.is_licensed = true;
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    initRegion();

    testAdmin->deferSaves();
    const meshtastic_Config c =
        makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_US, true, meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
    testAdmin->handleSetConfig(c, false);

    const int expectedSegments = SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_DEVICESTATE | SEGMENT_NODEDATABASE;
    TEST_ASSERT_EQUAL_INT(expectedSegments, testAdmin->savedSegments());
    TEST_ASSERT_EQUAL(32, config.security.private_key.size);
    TEST_ASSERT_EQUAL(32, owner.public_key.size);
}

// Unlicensed twin of the test above. Without the re-derivation the node signs broadcasts every receiver
// drops (verifyFirstContactNodeInfo: crc32(user.public_key) != from).
static void test_handleSetConfig_persistsUnlicensedFirstRegionIdentity()
{
    owner = meshtastic_User_init_zero;
    owner.is_licensed = false;
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    initRegion();

    testAdmin->deferSaves();
    const meshtastic_Config c =
        makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_US, true, meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
    testAdmin->handleSetConfig(c, false);

    const int expectedSegments = SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_DEVICESTATE | SEGMENT_NODEDATABASE;
    TEST_ASSERT_EQUAL_INT(expectedSegments, testAdmin->savedSegments());
    TEST_ASSERT_EQUAL(32, config.security.private_key.size);
    TEST_ASSERT_EQUAL(32, config.security.public_key.size);
    TEST_ASSERT_EQUAL(32, owner.public_key.size);
    // The invariant: a node's mesh address is derived from its identity key.
    TEST_ASSERT_EQUAL_UINT32(crc32Buffer(config.security.public_key.bytes, config.security.public_key.size),
                             nodeDB->getNodeNum());
}

static void test_handleSetConfig_fromOthers_invalidPresetRejected()
{
    // Baseline is not the region default (LONG_FAST), so a clamp cannot pass for a rejection
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    initRegion();

    // Build an admin set_config with an invalid preset for EU_868
    meshtastic_Config c = makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_EU_868, true,
                                            meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO);

    testAdmin->handleSetConfig(c, true); // fromOthers = true

    // fromOthers=true: invalid preset should be rejected, old preset preserved
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, config.lora.modem_preset);
}

static void test_handleSetConfig_fromLocal_invalidPresetClamped()
{
    // Baseline is not the region default (LONG_FAST), so a rejection cannot pass for a clamp
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
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

static void test_handleSetConfig_fromOthers_invalidChannelNumFullyRejected()
{
    // Rejecting a remote config must reject ALL of it: an invalid channel_num must not
    // leak into config.lora alongside the restored region/preset.
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    config.lora.channel_num = 0;
    initRegion();

    meshtastic_Config c =
        makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_US, true, meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
    c.payload_variant.lora.channel_num = 5000; // far beyond US slot count

    testAdmin->handleSetConfig(c, true); // fromOthers = true

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, config.lora.region);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, config.lora.modem_preset);
    TEST_ASSERT_EQUAL_UINT32(0, config.lora.channel_num);
}

// clampBandwidthCode: an unset (0) bandwidth code maps to the default; any other code is left as-is.
static void test_clampBandwidthCode_zeroMapsToDefaultOthersUnchanged()
{
    TEST_ASSERT_NOT_EQUAL_UINT16(0, clampBandwidthCode(0)); // the point of the fix: 0 must not stay 0
    TEST_ASSERT_EQUAL_UINT16(bwKHzToCode(LORA_BW_DEFAULT_KHZ), clampBandwidthCode(0));
    TEST_ASSERT_EQUAL_UINT16(250, clampBandwidthCode(250));
    TEST_ASSERT_EQUAL_UINT16(125, clampBandwidthCode(125));
    TEST_ASSERT_EQUAL_UINT16(31, clampBandwidthCode(31));
}

// A custom (non-preset) config that leaves bandwidth at its proto zero-value must not persist as 0.
// Pre-fix it slipped past validateConfigLora() and the radio silently ran at the default while
// get_config still reported bandwidth 0. It is now coerced to the default code on ingest.
static void test_handleSetConfig_fromLocal_customBandwidthZeroClampedToDefault()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    initRegion();

    meshtastic_Config c =
        makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_US, false, meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
    c.payload_variant.lora.spread_factor = 11;
    c.payload_variant.lora.coding_rate = 5;
    c.payload_variant.lora.bandwidth = 0; // the footgun: unset custom bandwidth

    testAdmin->handleSetConfig(c, false); // fromOthers = false (local client)

    TEST_ASSERT_FALSE(config.lora.use_preset);
    TEST_ASSERT_NOT_EQUAL_UINT16(0, config.lora.bandwidth); // must not persist as 0
    TEST_ASSERT_EQUAL_UINT16(bwKHzToCode(LORA_BW_DEFAULT_KHZ), config.lora.bandwidth);
}

// Remote admin (fromOthers) is subject to the same ingest clamp: a custom bandwidth 0 from another
// node is normalized to the default rather than persisted as 0 (it does not weaken the wholesale
// rejection of configs that actually fail validation - a 0 bandwidth already passed validation).
static void test_handleSetConfig_fromOthers_customBandwidthZeroClampedToDefault()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    initRegion();

    meshtastic_Config c =
        makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_US, false, meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
    c.payload_variant.lora.spread_factor = 11;
    c.payload_variant.lora.coding_rate = 5;
    c.payload_variant.lora.bandwidth = 0;

    testAdmin->handleSetConfig(c, true); // fromOthers = true

    TEST_ASSERT_FALSE(config.lora.use_preset);
    TEST_ASSERT_EQUAL_UINT16(bwKHzToCode(LORA_BW_DEFAULT_KHZ), config.lora.bandwidth);
}

// In preset mode bandwidth 0 is the norm (the preset supplies it); the ingest clamp must leave it
// untouched so preset configs still read back bandwidth 0.
static void test_handleSetConfig_fromLocal_presetBandwidthZeroLeftUntouched()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    initRegion();

    meshtastic_Config c =
        makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_US, true, meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
    c.payload_variant.lora.bandwidth = 0;

    testAdmin->handleSetConfig(c, false);

    TEST_ASSERT_TRUE(config.lora.use_preset);
    TEST_ASSERT_EQUAL_UINT16(0, config.lora.bandwidth);
}

// A custom (non-preset) config with an already-valid bandwidth must be preserved verbatim.
static void test_handleSetConfig_fromLocal_customBandwidthNonZeroPreserved()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    initRegion();

    meshtastic_Config c =
        makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_US, false, meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
    c.payload_variant.lora.spread_factor = 11;
    c.payload_variant.lora.coding_rate = 5;
    c.payload_variant.lora.bandwidth = 125;

    testAdmin->handleSetConfig(c, false);

    TEST_ASSERT_FALSE(config.lora.use_preset);
    TEST_ASSERT_EQUAL_UINT16(125, config.lora.bandwidth);
}

// A security-config SET that omits the private key (partial/legacy client editing some other security field)
// must NOT regenerate our keypair: our NodeNum is crc32(public_key), so a new keypair would silently change
// our identity. The existing keypair has to be preserved.
static void test_handleSetConfig_security_preservesKeypairWhenPrivateOmitted()
{
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0x11, 32);
    config.security.public_key.size = 32;
    memset(config.security.public_key.bytes, 0x22, 32);

    // Incoming SET carries no private/public key, just another security field.
    meshtastic_Config c = meshtastic_Config_init_zero;
    c.which_payload_variant = meshtastic_Config_security_tag;
    c.payload_variant.security.serial_enabled = true;

    testAdmin->deferSaves();
    testAdmin->handleSetConfig(c, false);

    uint8_t expectedPriv[32];
    memset(expectedPriv, 0x11, 32);
    uint8_t expectedPub[32];
    memset(expectedPub, 0x22, 32);
    TEST_ASSERT_EQUAL_UINT(32, config.security.private_key.size);
    TEST_ASSERT_EQUAL_MEMORY(expectedPriv, config.security.private_key.bytes, 32);
    TEST_ASSERT_EQUAL_UINT(32, config.security.public_key.size);
    TEST_ASSERT_EQUAL_MEMORY(expectedPub, config.security.public_key.bytes, 32);
    // The non-key field still applies.
    TEST_ASSERT_TRUE(config.security.serial_enabled);
}

// A SET that DOES supply a full 32-byte keypair (legitimate key import) must apply it, not preserve the old one.
static void test_handleSetConfig_security_acceptsSuppliedKeypair()
{
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0x11, 32);
    config.security.public_key.size = 32;
    memset(config.security.public_key.bytes, 0x22, 32);

    meshtastic_Config c = meshtastic_Config_init_zero;
    c.which_payload_variant = meshtastic_Config_security_tag;
    c.payload_variant.security.private_key.size = 32;
    memset(c.payload_variant.security.private_key.bytes, 0x33, 32);
    c.payload_variant.security.public_key.size = 32;
    memset(c.payload_variant.security.public_key.bytes, 0x44, 32);

    testAdmin->deferSaves();
    testAdmin->handleSetConfig(c, false);

    uint8_t expectedPriv[32];
    memset(expectedPriv, 0x33, 32);
    uint8_t expectedPub[32];
    memset(expectedPub, 0x44, 32);
    TEST_ASSERT_EQUAL_MEMORY(expectedPriv, config.security.private_key.bytes, 32);
    TEST_ASSERT_EQUAL_MEMORY(expectedPub, config.security.public_key.bytes, 32);
}

// Issue #11073: "regenerate keys" sends a blank SecurityConfig holding only the new private key. Replacing
// the whole struct with it wiped the admin keys, locking the owner out of remote admin.
static void test_handleSetConfig_security_rotationPreservesAdminKeys()
{
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0x11, 32);
    config.security.public_key.size = 32;
    memset(config.security.public_key.bytes, 0x22, 32);
    config.security.admin_key_count = 2;
    config.security.admin_key[0].size = 32;
    memset(config.security.admin_key[0].bytes, 0xAA, 32);
    config.security.admin_key[1].size = 32;
    memset(config.security.admin_key[1].bytes, 0xBB, 32);
    config.security.is_managed = true;
    config.security.serial_enabled = true;
    config.security.packet_signature_policy =
        meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT;

    // Exactly what the regenerate dialog emits.
    meshtastic_Config c = meshtastic_Config_init_zero;
    c.which_payload_variant = meshtastic_Config_security_tag;
    c.payload_variant.security.private_key.size = 32;
    memset(c.payload_variant.security.private_key.bytes, 0x33, 32);
    // A generated key arrives clamped; an unclamped one would be clamped in place on derivation.
    c.payload_variant.security.private_key.bytes[0] &= 0xF8;
    c.payload_variant.security.private_key.bytes[31] = (c.payload_variant.security.private_key.bytes[31] & 0x7F) | 0x40;
    uint8_t expectedPriv[32];
    memcpy(expectedPriv, c.payload_variant.security.private_key.bytes, 32);

    testAdmin->deferSaves();
    testAdmin->handleSetConfig(c, false);

    TEST_ASSERT_EQUAL_UINT(32, config.security.private_key.size);
    TEST_ASSERT_EQUAL_MEMORY(expectedPriv, config.security.private_key.bytes, 32);

    uint8_t expectedAdmin0[32], expectedAdmin1[32];
    memset(expectedAdmin0, 0xAA, 32);
    memset(expectedAdmin1, 0xBB, 32);
    TEST_ASSERT_EQUAL_UINT(2, config.security.admin_key_count);
    TEST_ASSERT_EQUAL_UINT(32, config.security.admin_key[0].size);
    TEST_ASSERT_EQUAL_MEMORY(expectedAdmin0, config.security.admin_key[0].bytes, 32);
    TEST_ASSERT_EQUAL_UINT(32, config.security.admin_key[1].size);
    TEST_ASSERT_EQUAL_MEMORY(expectedAdmin1, config.security.admin_key[1].bytes, 32);
    TEST_ASSERT_TRUE(config.security.is_managed);
    TEST_ASSERT_TRUE(config.security.serial_enabled);
    TEST_ASSERT_EQUAL(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT,
                      config.security.packet_signature_policy);
}

// The escape hatch: a SET that leaves the private key alone still clears admin keys.
static void test_handleSetConfig_security_clearsAdminKeysWhenKeypairUnchanged()
{
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0x11, 32);
    config.security.public_key.size = 32;
    memset(config.security.public_key.bytes, 0x22, 32);
    config.security.admin_key_count = 1;
    config.security.admin_key[0].size = 32;
    memset(config.security.admin_key[0].bytes, 0xAA, 32);

    // Same private key we already hold, empty admin key list.
    meshtastic_Config c = meshtastic_Config_init_zero;
    c.which_payload_variant = meshtastic_Config_security_tag;
    c.payload_variant.security.private_key.size = 32;
    memset(c.payload_variant.security.private_key.bytes, 0x11, 32);
    c.payload_variant.security.public_key.size = 32;
    memset(c.payload_variant.security.public_key.bytes, 0x22, 32);

    testAdmin->deferSaves();
    testAdmin->handleSetConfig(c, false);

    TEST_ASSERT_EQUAL_UINT(0, config.security.admin_key_count);
    TEST_ASSERT_EQUAL_UINT(0, config.security.admin_key[0].size);
}

// No low-entropy private key is published, so stand in for the engine to derive a blacklisted public
// key on demand. hash() is left real: the blacklist lookup runs through it.
static const uint8_t COMPROMISED_PUBLIC_KEY[32] = {0xac, 0xaf, 0x8c, 0x1c, 0x3c, 0x1c, 0x37, 0xac, 0x4f, 0x03, 0xa1,
                                                   0xe9, 0xfc, 0x37, 0x23, 0x29, 0xc8, 0xa3, 0x5d, 0x7f, 0x05, 0x26,
                                                   0xeb, 0x00, 0xbd, 0x26, 0xb8, 0x2e, 0xb1, 0x94, 0x7d, 0x24};

class RestoreDerivingCryptoEngine : public CryptoEngine
{
  public:
    bool regenerateSucceeds = true;
    bool derivesLowEntropy = true;
    const uint8_t *derivedKey = nullptr; // when set, regeneratePublicKey() yields exactly this key
    bool regeneratePublicKey(uint8_t *pubKey, uint8_t *privKey) override
    {
        if (!regenerateSucceeds)
            return false;
        if (derivedKey)
            memcpy(pubKey, derivedKey, 32);
        else if (derivesLowEntropy)
            memcpy(pubKey, COMPROMISED_PUBLIC_KEY, 32);
        else
            memset(pubKey, 0x7C, 32);
        return true;
    }
    bool mintsLowEntropy = false;
    std::vector<const uint8_t *> mintQueue; // keys generateKeyPair() hands out first, in order
    void generateKeyPair(uint8_t *pubKey, uint8_t *privKey) override
    {
        if (!mintQueue.empty()) {
            memcpy(pubKey, mintQueue.front(), 32);
            mintQueue.erase(mintQueue.begin());
        } else if (mintsLowEntropy)
            memcpy(pubKey, COMPROMISED_PUBLIC_KEY, 32);
        else
            memset(pubKey, 0x5E, 32);
        memset(privKey, 0x5F, 32);
    }
};

static CryptoEngine *savedCrypto;
static RestoreDerivingCryptoEngine *restoreCrypto;

// Installed here and torn down in restoreAdminRadioGlobals(), not at the end of the test body: a failed
// TEST_ASSERT longjmps straight out, which would leave later tests running against a freed stub.
static RestoreDerivingCryptoEngine *installRestoreCrypto()
{
    savedCrypto = crypto;
    restoreCrypto = new RestoreDerivingCryptoEngine();
    crypto = restoreCrypto;
    return restoreCrypto;
}

static void dropRestoreCryptoStub()
{
    if (!restoreCrypto)
        return;
    crypto = savedCrypto;
    delete restoreCrypto;
    restoreCrypto = nullptr;
}

// Arms a bare private-key restore: region set so keygen runs, private key present, public key absent.
static meshtastic_Config makeBareKeyRestoreConfig()
{
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    meshtastic_Config c = meshtastic_Config_init_zero;
    c.which_payload_variant = meshtastic_Config_security_tag;
    c.payload_variant.security.private_key.size = 32;
    memset(c.payload_variant.security.private_key.bytes, 0x11, 32);
    return c;
}

static bool capturedWarningsContain(const char *needle)
{
    for (const std::string &w : capturedWarnings)
        if (w.find(needle) != std::string::npos)
            return true;
    return false;
}

// A restored private key deriving a blacklisted public key is rejected and rotated at set time, and
// the client is told why - not left to discover it after the next reboot.
static void test_handleSetConfig_security_lowEntropyRestoreWarnsAndRotates()
{
    installRestoreCrypto();

    const meshtastic_Config c = makeBareKeyRestoreConfig();
    testAdmin->deferSaves();
    testAdmin->handleSetConfig(c, false);

    TEST_ASSERT_TRUE(nodeDB->keyIsLowEntropy);
    TEST_ASSERT_EQUAL_UINT(32, config.security.public_key.size);
    TEST_ASSERT_TRUE(memcmp(COMPROMISED_PUBLIC_KEY, config.security.public_key.bytes, 32) != 0);
    TEST_ASSERT_FALSE(nodeDB->checkLowEntropyPublicKey(config.security.public_key));
    TEST_ASSERT_TRUE(capturedWarningsContain(LOW_ENTROPY_RESTORE_WARNING));
}

// A restore carrying a whole blacklisted pair must not skip validation just because it populated the
// public key too - that path reaches neither keygen branch, so the weak identity used to be kept.
static void test_handleSetConfig_security_lowEntropyFullKeypairRestoreIsRejected()
{
    installRestoreCrypto();

    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    meshtastic_Config c = meshtastic_Config_init_zero;
    c.which_payload_variant = meshtastic_Config_security_tag;
    c.payload_variant.security.private_key.size = 32;
    memset(c.payload_variant.security.private_key.bytes, 0x11, 32);
    c.payload_variant.security.public_key.size = 32;
    memcpy(c.payload_variant.security.public_key.bytes, COMPROMISED_PUBLIC_KEY, 32);

    testAdmin->deferSaves();
    testAdmin->handleSetConfig(c, false);

    TEST_ASSERT_EQUAL_UINT(32, config.security.public_key.size);
    TEST_ASSERT_TRUE(memcmp(COMPROMISED_PUBLIC_KEY, config.security.public_key.bytes, 32) != 0);
    TEST_ASSERT_FALSE(nodeDB->checkLowEntropyPublicKey(config.security.public_key));
    TEST_ASSERT_TRUE(capturedWarningsContain(LOW_ENTROPY_RESTORE_WARNING));
}

// A blacklisted public key whose private key derives a clean one is only re-derived - the user's key
// does stick, so the "a new secure key was generated" warning would be a lie here.
static void test_handleSetConfig_security_reDerivedCleanKeyDoesNotWarn()
{
    installRestoreCrypto()->derivesLowEntropy = false;

    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    meshtastic_Config c = meshtastic_Config_init_zero;
    c.which_payload_variant = meshtastic_Config_security_tag;
    c.payload_variant.security.private_key.size = 32;
    memset(c.payload_variant.security.private_key.bytes, 0x11, 32);
    c.payload_variant.security.public_key.size = 32;
    memcpy(c.payload_variant.security.public_key.bytes, COMPROMISED_PUBLIC_KEY, 32);

    testAdmin->deferSaves();
    testAdmin->handleSetConfig(c, false);

    // The supplied private key survives, and the blacklisted public key is replaced by its derivation.
    uint8_t expectedPriv[32];
    memset(expectedPriv, 0x11, 32);
    TEST_ASSERT_EQUAL_MEMORY(expectedPriv, config.security.private_key.bytes, 32);
    TEST_ASSERT_FALSE(nodeDB->checkLowEntropyPublicKey(config.security.public_key));
    TEST_ASSERT_TRUE(memcmp(COMPROMISED_PUBLIC_KEY, config.security.public_key.bytes, 32) != 0);
    TEST_ASSERT_FALSE(capturedWarningsContain(LOW_ENTROPY_RESTORE_WARNING));
}

// A replacement that is itself blacklisted leaves no identity behind - persisting a known-weak key
// would defeat the rejection this whole path exists for.
static void test_handleSetConfig_security_blacklistedMintLeavesNoKey()
{
    installRestoreCrypto()->mintsLowEntropy = true;

    const meshtastic_Config c = makeBareKeyRestoreConfig();
    testAdmin->deferSaves();
    testAdmin->handleSetConfig(c, false);

    TEST_ASSERT_EQUAL_UINT(0, config.security.private_key.size);
    TEST_ASSERT_EQUAL_UINT(0, config.security.public_key.size);
    TEST_ASSERT_FALSE(capturedWarningsContain(LOW_ENTROPY_RESTORE_WARNING));
}

// factory_reset_config keeps the private key and clears the public one, so the entry check sees no key
// and the boot-time derive path used to adopt whatever it produced - including a known-weak key.
static void test_generateCryptoKeyPair_derivedFromStoredPrivateIsChecked()
{
    installRestoreCrypto();
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0x11, 32);
    config.security.public_key.size = 0; // as installDefaultConfig(preserveKey = true) leaves it

    TEST_ASSERT_TRUE(nodeDB->generateCryptoKeyPair());

    TEST_ASSERT_TRUE(nodeDB->keyIsLowEntropy);
    TEST_ASSERT_TRUE(memcmp(COMPROMISED_PUBLIC_KEY, config.security.public_key.bytes, 32) != 0);
    TEST_ASSERT_FALSE(nodeDB->checkLowEntropyPublicKey(config.security.public_key));
}

// Same clear-and-fail on the boot path: a stored private key that derives nothing must not leave both
// sizes at 32, claiming a pair the node never got.
static void test_generateCryptoKeyPair_failedDerivationFromStoredPrivateClearsKeySizes()
{
    installRestoreCrypto()->regenerateSucceeds = false;
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0x11, 32);
    config.security.public_key.size = 0;

    TEST_ASSERT_FALSE(nodeDB->generateCryptoKeyPair());

    TEST_ASSERT_EQUAL_UINT(0, config.security.private_key.size);
    TEST_ASSERT_EQUAL_UINT(0, config.security.public_key.size);
}

// --- Key-derived node numbers in the reserved range (#12439) ---
//
// Under test, in src/mesh/NodeDB.cpp: NodeDB::keyYieldsReservedNodeNum(), NodeDB::generateCryptoKeyPair()
// (through generateBlacklistCheckedKeyPair() and derivePublicKeyFromPrivate()), NodeDB::ensurePkiIdentity()
// and NodeDB::createNewIdentity().
//
// my_node_num is crc32(public_key), and nothing checked that against the reserved numbers 0-9 and
// broadcast (0xFFFFFFFF). Number 0 is the one that hurts: the first send asserts on from == 0, and
// every reboot re-derives the same key and number, so the node crash-loops until it gets a new key.
// Number 1 collides with NODENUM_BROADCAST_NO_LORA. The number can't be rerolled - it must equal the
// key's CRC - so these pin that such a key is redrawn when minted and replaced when derived (the
// recovery path for a node already stuck), on boot, on admin keygen, and on the first-region path.
//
// The keys are 28 bytes of 0xA5 plus a 4-byte suffix solved to force the CRC32 (CRC32 is affine over
// GF(2)), since a real key landing there takes ~2^32/5 draws.
static const uint8_t KEY_CRC_0[32] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
                                      0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
                                      0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0x98, 0xb5, 0x42, 0xed};
static const uint8_t KEY_CRC_1[32] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
                                      0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
                                      0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xd9, 0xb3, 0x33, 0x36};
static const uint8_t KEY_CRC_9[32] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
                                      0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
                                      0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0x94, 0x9e, 0x0e, 0x5b};
static const uint8_t KEY_CRC_10[32] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
                                       0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
                                       0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0x16, 0x92, 0xec, 0xed};
static const uint8_t KEY_CRC_BROADCAST[32] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
                                              0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
                                              0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xfa, 0x40, 0x64, 0x7f};

static meshtastic_Config_SecurityConfig_public_key_t publicKeyOf(const uint8_t *bytes)
{
    meshtastic_Config_SecurityConfig_public_key_t key = {};
    key.size = 32;
    memcpy(key.bytes, bytes, 32);
    return key;
}

// Our own number must be a usable one and must match the key it came from.
static void assertUsableIdentity()
{
    TEST_ASSERT_EQUAL_UINT(32, config.security.public_key.size);
    TEST_ASSERT_FALSE(NodeDB::keyYieldsReservedNodeNum(config.security.public_key));
    TEST_ASSERT_EQUAL_HEX32(crc32Buffer(config.security.public_key.bytes, 32), nodeDB->getNodeNum());
    TEST_ASSERT_TRUE(nodeDB->getNodeNum() >= 10 && nodeDB->getNodeNum() != NODENUM_BROADCAST);
}

// The vectors themselves, and both edges: 0, 1, 9 and broadcast are reserved; 10 and an ordinary CRC are not.
static void test_keyYieldsReservedNodeNum_matchesTheReservedRange()
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000, crc32Buffer(KEY_CRC_0, 32));
    TEST_ASSERT_EQUAL_HEX32(0x00000001, crc32Buffer(KEY_CRC_1, 32));
    TEST_ASSERT_EQUAL_HEX32(0x00000009, crc32Buffer(KEY_CRC_9, 32));
    TEST_ASSERT_EQUAL_HEX32(0x0000000A, crc32Buffer(KEY_CRC_10, 32));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, crc32Buffer(KEY_CRC_BROADCAST, 32));
    TEST_ASSERT_TRUE(NodeDB::keyYieldsReservedNodeNum(publicKeyOf(KEY_CRC_0)));
    TEST_ASSERT_TRUE(NodeDB::keyYieldsReservedNodeNum(publicKeyOf(KEY_CRC_1)));
    TEST_ASSERT_TRUE(NodeDB::keyYieldsReservedNodeNum(publicKeyOf(KEY_CRC_9)));
    TEST_ASSERT_TRUE(NodeDB::keyYieldsReservedNodeNum(publicKeyOf(KEY_CRC_BROADCAST)));
    TEST_ASSERT_FALSE(NodeDB::keyYieldsReservedNodeNum(publicKeyOf(KEY_CRC_10)));
    uint8_t ordinary[32];
    memset(ordinary, 0xA5, 32); // CRC 0x775F2F85
    TEST_ASSERT_FALSE(NodeDB::keyYieldsReservedNodeNum(publicKeyOf(ordinary)));
}

// A freshly minted key whose CRC is 0 is drawn again rather than adopted as node 0.
static void test_generateCryptoKeyPair_mintedKeyForNodeZeroIsRedrawn()
{
    installRestoreCrypto()->mintQueue = {KEY_CRC_0};
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    TEST_ASSERT_TRUE(nodeDB->generateCryptoKeyPair());

    TEST_ASSERT_TRUE(memcmp(KEY_CRC_0, config.security.public_key.bytes, 32) != 0);
    assertUsableIdentity();
}

// The crash-loop recovery: a stored private key deriving node 0 is replaced at boot, not re-adopted.
static void test_generateCryptoKeyPair_storedKeyForNodeZeroIsReplacedAtBoot()
{
    installRestoreCrypto()->derivedKey = KEY_CRC_0;
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0x11, 32);
    config.security.public_key.size = 32;
    memcpy(config.security.public_key.bytes, KEY_CRC_0, 32); // as a stuck node has it saved

    TEST_ASSERT_TRUE(nodeDB->generateCryptoKeyPair());

    uint8_t storedPriv[32];
    memset(storedPriv, 0x11, 32);
    TEST_ASSERT_TRUE(memcmp(storedPriv, config.security.private_key.bytes, 32) != 0);
    assertUsableIdentity();
}

// Every reserved number is refused, not just 0: 1 is NODENUM_BROADCAST_NO_LORA, 9 the top of the
// range, 0xFFFFFFFF broadcast. Each is minted in turn ahead of a clean key.
static void test_generateCryptoKeyPair_everyReservedMintIsRedrawn()
{
    installRestoreCrypto()->mintQueue = {KEY_CRC_1, KEY_CRC_9, KEY_CRC_BROADCAST};
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    TEST_ASSERT_TRUE(nodeDB->generateCryptoKeyPair());

    TEST_ASSERT_TRUE(restoreCrypto->mintQueue.empty());
    assertUsableIdentity();
}

// The range ends at 9: a minted key for node 10, the first usable number, is kept on the first draw.
static void test_generateCryptoKeyPair_mintedKeyForNodeTenIsKept()
{
    installRestoreCrypto()->mintQueue = {KEY_CRC_10};
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    TEST_ASSERT_TRUE(nodeDB->generateCryptoKeyPair());

    TEST_ASSERT_EQUAL_MEMORY(KEY_CRC_10, config.security.public_key.bytes, 32);
    TEST_ASSERT_EQUAL_HEX32(10, nodeDB->getNodeNum());
}

// The draw is bounded at four: if every attempt is reserved, no key is kept rather than a reserved one.
static void test_generateCryptoKeyPair_onlyReservedMintsLeaveNoKey()
{
    installRestoreCrypto()->mintQueue = {KEY_CRC_0, KEY_CRC_0, KEY_CRC_0, KEY_CRC_0, KEY_CRC_0, KEY_CRC_0};
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    TEST_ASSERT_FALSE(nodeDB->generateCryptoKeyPair());

    TEST_ASSERT_EQUAL_UINT(2, restoreCrypto->mintQueue.size()); // exactly four of the six drawn
    TEST_ASSERT_EQUAL_UINT(0, config.security.private_key.size);
    TEST_ASSERT_EQUAL_UINT(0, config.security.public_key.size);
}

// Leaving our old number must remove only our own row: the removal compacts the next row into slot 0, and
// a remote node numbered 9 (legal before reserved numbers grew to 0-9) is not ours.
static void test_generateCryptoKeyPair_ordinaryRotationKeepsRemoteNodeNine()
{
    installRestoreCrypto();
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    const NodeNum oldSelf = 0x0A0B0C0D;
    meshtastic_NodeInfoLite self = meshtastic_NodeInfoLite_init_zero;
    self.num = oldSelf;
    meshtastic_NodeInfoLite remoteNine = meshtastic_NodeInfoLite_init_zero;
    remoteNine.num = 9;
    // Earlier tests can leave the table full, so lay out exactly [self, remote 9] rather than inserting.
    nodeDB->meshNodes->at(0) = self;
    nodeDB->meshNodes->at(1) = remoteNine;
    nodeDB->numMeshNodes = 2;
    myNodeInfo.my_node_num = oldSelf;

    TEST_ASSERT_TRUE(nodeDB->generateCryptoKeyPair()); // mints the clean 0x5E key: an ordinary rotation

    TEST_ASSERT_NULL(nodeDB->getMeshNode(oldSelf));
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(9));
    assertUsableIdentity();
}

// The first-region path mints through CryptoEngine::ensurePkiKeys(), not NodeDB's checked keygen, and
// switches number without a reboot - the route the issue traced to the node-0 crash.
static void test_ensurePkiIdentity_mintedKeyForNodeZeroIsRedrawn()
{
    installRestoreCrypto()->mintQueue = {KEY_CRC_0};
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    owner.is_licensed = false;

    nodeDB->ensurePkiIdentity();

    TEST_ASSERT_TRUE(memcmp(KEY_CRC_0, config.security.public_key.bytes, 32) != 0);
    TEST_ASSERT_EQUAL_MEMORY(config.security.public_key.bytes, owner.public_key.bytes, 32);
    assertUsableIdentity();
}

// keyIsLowEntropy survives from a boot-time regeneration, and generateCryptoKeyPair returns early on
// an unset region without clearing it. The restore warning must stay gated on this keygen running.
static void test_handleSetConfig_security_staleLowEntropyFlagDoesNotWarn()
{
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    initRegion();
    nodeDB->keyIsLowEntropy = true;

    meshtastic_Config c = meshtastic_Config_init_zero;
    c.which_payload_variant = meshtastic_Config_security_tag;
    c.payload_variant.security.private_key.size = 32;
    memset(c.payload_variant.security.private_key.bytes, 0x11, 32);

    testAdmin->deferSaves();
    testAdmin->handleSetConfig(c, false);

    TEST_ASSERT_FALSE(capturedWarningsContain(LOW_ENTROPY_RESTORE_WARNING));
}

// A private key that derives nothing usable must not leave sizes claiming a 32-byte pair behind:
// that state gets persisted, and every later keygen re-derives from the same dead key.
static void test_handleSetConfig_security_failedDerivationClearsKeySizes()
{
    installRestoreCrypto()->regenerateSucceeds = false;

    const meshtastic_Config c = makeBareKeyRestoreConfig();
    testAdmin->deferSaves();
    testAdmin->handleSetConfig(c, false);

    TEST_ASSERT_EQUAL_UINT(0, config.security.private_key.size);
    TEST_ASSERT_EQUAL_UINT(0, config.security.public_key.size);
    TEST_ASSERT_FALSE(capturedWarningsContain(LOW_ENTROPY_RESTORE_WARNING));
}

static void test_regionInfo_supportsPreset()
{
    const RegionInfo *eu868 = getRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    TEST_ASSERT_TRUE(eu868->supportsPreset(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST));
    TEST_ASSERT_FALSE(eu868->supportsPreset(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO));
    TEST_ASSERT_FALSE(eu868->supportsPreset(meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST));

    const RegionInfo *eu866 = getRegion(meshtastic_Config_LoRaConfig_RegionCode_EU_866);
    TEST_ASSERT_TRUE(eu866->supportsPreset(meshtastic_Config_LoRaConfig_ModemPreset_LITE_SLOW));
    TEST_ASSERT_FALSE(eu866->supportsPreset(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST));

    // UNSET enforces nothing (the radio is silent regardless), so it supports every real
    // preset - not just the LONG_FAST its own profile advertises as the default.
    const RegionInfo *unset = getRegion(meshtastic_Config_LoRaConfig_RegionCode_UNSET);
    TEST_ASSERT_TRUE(unset->supportsPreset(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST));
    TEST_ASSERT_TRUE(unset->supportsPreset(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO));
    TEST_ASSERT_TRUE(unset->supportsPreset(meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST));
    TEST_ASSERT_FALSE(unset->supportsPreset((meshtastic_Config_LoRaConfig_ModemPreset)99));
}

static void test_checkConfigRegion_quietCheckReportsReason()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    TEST_ASSERT_TRUE(RadioInterface::checkConfigRegion(cfg));

    cfg.region = (meshtastic_Config_LoRaConfig_RegionCode)254;
    char err[160] = {0};
    TEST_ASSERT_FALSE(RadioInterface::checkConfigRegion(cfg, err, sizeof(err)));
    TEST_ASSERT_TRUE_MESSAGE(strlen(err) > 0, "Expected a failure reason in errBuf");
}

static void test_checkConfigRegion_allowsProspectiveLicensedOwner()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_ITU1_2M;
    devicestate.owner.is_licensed = false;

    TEST_ASSERT_FALSE(RadioInterface::checkConfigRegion(cfg));
    TEST_ASSERT_TRUE(RadioInterface::checkConfigRegion(cfg, nullptr, 0, true));
}

static void test_handleSetConfig_fromOthers_siblingLockedPresetSwapsRegion()
{
    // Baseline: EU_866 (LITE profile)
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_866;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST;
    initRegion();

    // Remote admin keeps the region but selects a NARROW preset (locked to EU_N_868)
    meshtastic_Config c = makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_EU_866, true,
                                            meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST);

    testAdmin->handleSetConfig(c, true); // fromOthers = true

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_EU_N_868, config.lora.region);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST, config.lora.modem_preset);

    // Restore the region table pointer for subsequent tests
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    initRegion();
}

static void test_handleSetConfig_fromOthers_lockedPresetFromNonTrioRegionRejected()
{
    // Baseline: US is not one of the swappable trio, so a LITE preset must be rejected
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    initRegion();

    meshtastic_Config c =
        makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_US, true, meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST);

    testAdmin->handleSetConfig(c, true); // fromOthers = true

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, config.lora.region);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, config.lora.modem_preset);
}

static void test_handleSetConfig_presetChosenBeforeRegionSurvives()
{
    // A fresh device: the user picks a preset in the app before choosing a region. The
    // unset region must not clamp that choice back to LONG_FAST.
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    initRegion();

    meshtastic_Config c = makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_UNSET, true,
                                            meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST);

    testAdmin->handleSetConfig(c, false);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_UNSET, config.lora.region);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, config.lora.modem_preset);
}

static void test_handleSetConfig_unsettingRegionKeepsPreset()
{
    // Clearing the region is a valid request in its own right. It must take effect (and
    // disable tx) without discarding the config because the preset outlives the region.
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
    config.lora.tx_enabled = true;
    initRegion();

    meshtastic_Config c = makeLoraSetConfig(meshtastic_Config_LoRaConfig_RegionCode_UNSET, true,
                                            meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO);
    c.payload_variant.lora.tx_enabled = true;

    testAdmin->handleSetConfig(c, false);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_UNSET, config.lora.region);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, config.lora.modem_preset);
    TEST_ASSERT_FALSE_MESSAGE(config.lora.tx_enabled, "unsetting the region must disable tx");

    // Restore the region table pointer for subsequent tests
    initRegion();
}

// -----------------------------------------------------------------------
// Channel-configuration warning + coalescing tests
//
// These exercise the real incoming-admin-message path (handleReceivedProtobuf):
// begin_edit_settings / set_channel / commit_edit_settings. Warnings raised while a
// transaction is open must be deferred and collapsed into a single notification at
// commit; outside a transaction each save emits its own single message immediately.
// -----------------------------------------------------------------------

static const uint8_t DEFAULT_KEY[] = {0x01};      // the well-known "default" PSK (AQ==)
static const uint8_t CUSTOM_KEY[] = {0x42, 0x17}; // any non-default key

// Count captured warnings whose text contains substr.
static int warningsContaining(const char *substr)
{
    int n = 0;
    for (const auto &w : capturedWarnings)
        if (w.find(substr) != std::string::npos)
            n++;
    return n;
}

static meshtastic_Channel makeChannel(int8_t index, meshtastic_Channel_Role role, const char *name, const uint8_t *psk,
                                      size_t pskLen)
{
    meshtastic_Channel ch = meshtastic_Channel_init_zero;
    ch.index = index;
    ch.role = role;
    ch.has_settings = true;
    strncpy(ch.settings.name, name, sizeof(ch.settings.name) - 1);
    ch.settings.psk.size = pskLen;
    for (size_t i = 0; i < pskLen; i++)
        ch.settings.psk.bytes[i] = psk[i];
    return ch;
}

// Dispatch one admin message as if it arrived from a local (from==0) client, which bypasses
// the passkey/authorization gates so the switch body runs.
static void sendAdmin(meshtastic_AdminMessage &m)
{
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = 0;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag; // required: handler drops non-decoded packets
    testAdmin->handleReceivedProtobuf(mp, &m);
}

static void sendSetChannel(const meshtastic_Channel &ch)
{
    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_set_channel_tag;
    m.set_channel = ch;
    sendAdmin(m);
}

static void sendBeginEdit()
{
    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_begin_edit_settings_tag;
    m.begin_edit_settings = true;
    sendAdmin(m);
}

static void sendCommitEdit()
{
    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_commit_edit_settings_tag;
    m.commit_edit_settings = true;
    sendAdmin(m);
}

// An admin message that changes nothing. It answers, so drain the reply or the packet pool leaks.
static void sendGetDeviceMetadata()
{
    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_get_device_metadata_request_tag;
    m.get_device_metadata_request = true;
    sendAdmin(m);
    testAdmin->drainReply();
}

// Preset = LongFast on US, unlicensed owner. "LongFast" is the display name we compare against.
static void usePresetLongFast()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    initRegion();
    owner.is_licensed = false;
}

static void test_warn_singleChannel_variantName_oneSpecificMessage()
{
    usePresetLongFast();
    // Name is a case/space variant of the preset with the default key: a single name issue.
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "long fast", DEFAULT_KEY, 1));
    TEST_ASSERT_EQUAL_INT(1, (int)capturedWarnings.size());
    TEST_ASSERT_EQUAL_INT(1, warningsContaining("looks like a mistype of 'LongFast'"));
}

static void test_warn_singleChannel_nameAndPsk_collapsedToCatchAll()
{
    usePresetLongFast();
    // Variant name AND a non-default key: two issues on one channel collapse to one catch-all.
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "long fast", CUSTOM_KEY, 2));
    TEST_ASSERT_EQUAL_INT(1, (int)capturedWarnings.size());
    TEST_ASSERT_EQUAL_INT(1, warningsContaining("There may be name and PSK issues on channel 0"));
}

static void test_warn_cleanChannel_noMessage()
{
    usePresetLongFast();
    // Exact preset name + default key: nothing to warn about.
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "LongFast", DEFAULT_KEY, 1));
    TEST_ASSERT_EQUAL_INT(0, (int)capturedWarnings.size());
}

static void test_warn_transaction_multipleChannels_singleCoalescedMessage()
{
    usePresetLongFast();
    sendBeginEdit();
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "long fast", DEFAULT_KEY, 1));
    sendSetChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "long fast", DEFAULT_KEY, 1));
    // Nothing emitted yet - warnings are deferred until commit.
    TEST_ASSERT_EQUAL_INT(0, (int)capturedWarnings.size());

    sendCommitEdit();
    // Exactly one message, naming both channels.
    TEST_ASSERT_EQUAL_INT(1, (int)capturedWarnings.size());
    TEST_ASSERT_EQUAL_INT(1, warningsContaining("There may be name issues on channels 0, 1"));
}

static void test_warn_transaction_singleChannel_keepsSpecificMessage()
{
    usePresetLongFast();
    sendBeginEdit();
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "long fast", DEFAULT_KEY, 1));
    TEST_ASSERT_EQUAL_INT(0, (int)capturedWarnings.size());

    sendCommitEdit();
    // One flagged channel: the specific message verbatim, not the plural catch-all.
    TEST_ASSERT_EQUAL_INT(1, (int)capturedWarnings.size());
    TEST_ASSERT_EQUAL_INT(1, warningsContaining("looks like a mistype of 'LongFast'"));
    TEST_ASSERT_EQUAL_INT(0, warningsContaining("on channels"));
}

// An idle transaction is retired by the next admin message, flushing the warnings it held.
static void test_editTransaction_abandoned_isRetiredOnNextAdminMessage()
{
    usePresetLongFast();
    sendBeginEdit();
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "long fast", DEFAULT_KEY, 1));
    // Deferred, exactly as before: nothing emitted while the transaction looks alive.
    TEST_ASSERT_EQUAL_INT(0, (int)capturedWarnings.size());
    TEST_ASSERT_TRUE(testAdmin->editTransactionOpen());

    testAdmin->ageEditTransaction();
    sendGetDeviceMetadata(); // any later admin message, from any client

    TEST_ASSERT_FALSE(testAdmin->editTransactionOpen());
    TEST_ASSERT_EQUAL_INT(1, warningsContaining("looks like a mistype of 'LongFast'"));
}

// A write arriving after abandonment is saved, not deferred to a commit that never comes.
static void test_editTransaction_abandoned_laterWriteIsNoLongerDeferred()
{
    usePresetLongFast();
    sendBeginEdit();
    testAdmin->ageEditTransaction();

    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "long fast", DEFAULT_KEY, 1));

    // The write itself retired the stale transaction, so its own warning is emitted immediately.
    TEST_ASSERT_FALSE(testAdmin->editTransactionOpen());
    TEST_ASSERT_EQUAL_INT(1, warningsContaining("looks like a mistype of 'LongFast'"));
}

// A transaction still in use is left alone: each write refreshes the window.
static void test_editTransaction_active_isNotRetired()
{
    usePresetLongFast();
    sendBeginEdit();
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "long fast", DEFAULT_KEY, 1));
    sendSetChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "long fast", DEFAULT_KEY, 1));

    TEST_ASSERT_TRUE(testAdmin->editTransactionOpen());
    TEST_ASSERT_EQUAL_INT(0, (int)capturedWarnings.size());

    sendCommitEdit();
    TEST_ASSERT_FALSE(testAdmin->editTransactionOpen());
    TEST_ASSERT_EQUAL_INT(1, warningsContaining("There may be name issues on channels 0, 1"));
}

static void test_warn_license_noTransaction_emittedImmediately()
{
    usePresetLongFast();
    owner.is_licensed = true;
    // Setting a channel that still carries a key triggers ensureLicensedOperation() to strip it.
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "", CUSTOM_KEY, 2));
    TEST_ASSERT_EQUAL_INT(1, warningsContaining("Licensed mode activated"));
}

static void test_warn_license_transaction_coalescedToSingleMessage()
{
    usePresetLongFast();
    owner.is_licensed = true;
    sendBeginEdit();
    // Two separate triggers within one transaction (two channels with keys to strip).
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "", CUSTOM_KEY, 2));
    sendSetChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "", CUSTOM_KEY, 2));
    TEST_ASSERT_EQUAL_INT(0, (int)capturedWarnings.size());

    sendCommitEdit();
    // Collapsed to a single licensed-mode notice (and no channel warning, since names are blank).
    TEST_ASSERT_EQUAL_INT(1, warningsContaining("Licensed mode activated"));
    TEST_ASSERT_EQUAL_INT(1, (int)capturedWarnings.size());
}

// -----------------------------------------------------------------------
// ClientNotification.type tests
//
// A lone queued channel warning must keep the type it was queued with, including when it is
// held across an edit transaction; several warnings coalesced into one catch-all message must
// carry the matching CHANNEL_*_ISSUES type instead. Losing either sends the client UNSET, or a
// type that names the wrong fix.
// -----------------------------------------------------------------------

static void test_notificationType_loneChannelWarning_keepsItsType()
{
    usePresetLongFast();
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "long fast", DEFAULT_KEY, 1));
    TEST_ASSERT_EQUAL_INT(1, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_CHANNEL_NAME_PRESET_MISTYPE_CLEAR_NAME, capturedTypes[0]);
}

static void test_notificationType_transaction_loneChannelWarning_keepsItsType()
{
    usePresetLongFast();
    sendBeginEdit();
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "long fast", DEFAULT_KEY, 1));
    sendCommitEdit();
    TEST_ASSERT_EQUAL_INT(1, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_CHANNEL_NAME_PRESET_MISTYPE_CLEAR_NAME, capturedTypes[0]);
}

static void test_notificationType_transaction_coalescedWarnings_useCatchAllType()
{
    usePresetLongFast();
    sendBeginEdit();
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "long fast", DEFAULT_KEY, 1));
    sendSetChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "long fast", DEFAULT_KEY, 1));
    sendCommitEdit();
    TEST_ASSERT_EQUAL_INT(1, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_CHANNEL_NAME_ISSUES, capturedTypes[0]);
}

static void test_notificationType_transaction_coalescedPskWarnings_usePskCatchAllType()
{
    usePresetLongFast();
    sendBeginEdit();
    // Blank names resolve to the preset; a non-default key on each is a PSK issue only.
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "", CUSTOM_KEY, 2));
    sendSetChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "", CUSTOM_KEY, 2));
    sendCommitEdit();
    TEST_ASSERT_EQUAL_INT(1, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_CHANNEL_PSK_ISSUES, capturedTypes[0]);
}

static void test_notificationType_transaction_mixedWarnings_useNameAndPskCatchAllType()
{
    usePresetLongFast();
    sendBeginEdit();
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "long fast", DEFAULT_KEY, 1)); // name issue
    sendSetChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "", CUSTOM_KEY, 2));         // PSK issue
    sendCommitEdit();
    TEST_ASSERT_EQUAL_INT(1, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_CHANNEL_NAME_AND_PSK_ISSUES, capturedTypes[0]);
}

static void test_notificationType_licensedMode_deferredNoticeKeepsItsType()
{
    usePresetLongFast();
    owner.is_licensed = true;
    sendBeginEdit();
    sendSetChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "", CUSTOM_KEY, 2));
    sendCommitEdit();
    TEST_ASSERT_EQUAL_INT(1, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LICENSED_MODE_ACTIVATED, capturedTypes[0]);
}

// checkConfigRegion() is the only place that knows why a region failed; validateConfigRegion()
// sends whatever type it reports, so a missing assignment there reaches the client as UNSET.
static void test_notificationType_checkConfigRegion_reportsFailureType()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    devicestate.owner.is_licensed = false;
    meshtastic_ClientNotification_NotificationType type = meshtastic_ClientNotification_NotificationType_UNSET;

    cfg.region = (meshtastic_Config_LoRaConfig_RegionCode)254;
    TEST_ASSERT_FALSE(RadioInterface::checkConfigRegion(cfg, nullptr, 0, false, &type));
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_REGION_UNRECOGNIZED, type);

    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_ITU1_2M;
    TEST_ASSERT_FALSE(RadioInterface::checkConfigRegion(cfg, nullptr, 0, false, &type));
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_REGION_REQUIRES_LICENSE, type);
}

static void test_notificationType_validateConfigRegion_sendsFailureType()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_ITU1_2M;
    devicestate.owner.is_licensed = false;

    TEST_ASSERT_FALSE(RadioInterface::validateConfigRegion(cfg));
    TEST_ASSERT_EQUAL_INT(1, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_REGION_REQUIRES_LICENSE, capturedTypes[0]);
}

static void test_notificationType_clampConfigLora_channelNumOutOfRange()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    cfg.channel_num = 5000; // far beyond the US slot count

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL_INT(1, (int)capturedTypes.size());
    TEST_ASSERT_EQUAL(meshtastic_ClientNotification_NotificationType_LORA_CHANNEL_NUM_INVALID, capturedTypes[0]);
}

// -----------------------------------------------------------------------
// Node-DB admin metadata: favorite / ignore / mute
// -----------------------------------------------------------------------
//
// MeshService::reloadConfig() only re-derives the region and fires configChanged - which drives the
// live SX126x/RadioInterface reconfigure - when saveWhat includes SEGMENT_CONFIG or
// SEGMENT_CHANNELS. A pure node-DB metadata save must skip that reconfigure entirely. These watch
// service->configChanged directly, so widening the saveWhat mask or reordering the check is caught
// even though they run outside an edit transaction.
//
// Characterization: all three already hold on develop. They are worth pinning because that reload
// is the path implicated in the WisMesh Tag favourite-node crash, and nothing asserted it.

// Counts configChanged.notifyObservers() calls - the only externally visible signal that
// reloadConfig() took the radio-reconfigure branch.
class ConfigChangedCounter : public Observer<void *>
{
  public:
    int count = 0;

  protected:
    int onNotify(void *arg) override
    {
        count++;
        return 0;
    }
};

static const NodeNum TEST_NODE_NUM = 0x12345678;

static void test_setFavoriteNode_skipsRadioReload_butPersists()
{
    nodeDB->getOrCreateMeshNode(TEST_NODE_NUM);
    ConfigChangedCounter counter;
    counter.observe(&service->configChanged);

    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_set_favorite_node_tag;
    m.set_favorite_node = TEST_NODE_NUM;
    sendAdmin(m);

    TEST_ASSERT_EQUAL_INT(0, counter.count);
    TEST_ASSERT_TRUE(nodeInfoLiteIsFavorite(nodeDB->getMeshNode(TEST_NODE_NUM)));
}

static void test_setIgnoredNode_skipsRadioReload_butPersists()
{
    nodeDB->getOrCreateMeshNode(TEST_NODE_NUM);
    ConfigChangedCounter counter;
    counter.observe(&service->configChanged);

    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_set_ignored_node_tag;
    m.set_ignored_node = TEST_NODE_NUM;
    sendAdmin(m);

    TEST_ASSERT_EQUAL_INT(0, counter.count);
    TEST_ASSERT_TRUE(nodeInfoLiteIsIgnored(nodeDB->getMeshNode(TEST_NODE_NUM)));
}

static void test_toggleMutedNode_skipsRadioReload_butPersists()
{
    nodeDB->getOrCreateMeshNode(TEST_NODE_NUM);
    ConfigChangedCounter counter;
    counter.observe(&service->configChanged);

    meshtastic_AdminMessage m = meshtastic_AdminMessage_init_zero;
    m.which_payload_variant = meshtastic_AdminMessage_toggle_muted_node_tag;
    m.toggle_muted_node = TEST_NODE_NUM;
    sendAdmin(m);

    TEST_ASSERT_EQUAL_INT(0, counter.count);
    TEST_ASSERT_TRUE(nodeInfoLiteIsMuted(nodeDB->getMeshNode(TEST_NODE_NUM)));
}

// -----------------------------------------------------------------------
// Node menu mute toggle (graphics::menuHandler::toggleNodeMuted)
// -----------------------------------------------------------------------
//
// Reachable only since the mute branch was lifted out of its banner-callback lambda; the lambda
// runs via screen->showOverlayBanner(), so nothing in MenuHandler.cpp was testable before.

#if HAS_SCREEN
static void test_toggleNodeMuted_flipsBitAndSkipsRadioReload()
{
    nodeDB->getOrCreateMeshNode(TEST_NODE_NUM);
    ConfigChangedCounter counter;
    counter.observe(&service->configChanged);

    graphics::menuHandler::toggleNodeMuted(TEST_NODE_NUM);
    TEST_ASSERT_TRUE(nodeInfoLiteIsMuted(nodeDB->getMeshNode(TEST_NODE_NUM)));
    TEST_ASSERT_EQUAL_INT(0, counter.count);

    graphics::menuHandler::toggleNodeMuted(TEST_NODE_NUM);
    TEST_ASSERT_FALSE(nodeInfoLiteIsMuted(nodeDB->getMeshNode(TEST_NODE_NUM)));
    TEST_ASSERT_EQUAL_INT(0, counter.count);
}

static void test_toggleNodeMuted_unknownNodeDoesNothing()
{
    ConfigChangedCounter counter;
    counter.observe(&service->configChanged);

    graphics::menuHandler::toggleNodeMuted(0xDEADBEEF); // never added to the DB

    TEST_ASSERT_EQUAL_INT(0, counter.count);
    TEST_ASSERT_NULL(nodeDB->getMeshNode(0xDEADBEEF));
}

// CHARACTERIZATION OF A KNOWN DEFECT, not an endorsement. Flipping one NodeInfoLite bit currently
// calls bare nodeDB->saveToDisk(), which rewrites all five segments. saveToDisk() is not virtual,
// so the mask is observed through its effect: every prefs file reappears after being removed.
//
// A pending fix narrows this to SEGMENT_NODEDATABASE. When it lands, only nodes.proto should come
// back and this assertion is EXPECTED to change - that diff is the point, so the improvement is
// visible instead of silent.
static void test_toggleNodeMuted_currentlyRewritesEverySegment()
{
    nodeDB->getOrCreateMeshNode(TEST_NODE_NUM);

    const char *segmentFiles[] = {configFileName, moduleConfigFileName, deviceStateFileName, channelFileName,
                                  nodeDatabaseFileName};
    for (const char *f : segmentFiles)
        FSCom.remove(f);

    graphics::menuHandler::toggleNodeMuted(TEST_NODE_NUM);

    for (const char *f : segmentFiles)
        TEST_ASSERT_TRUE_MESSAGE(FSCom.exists(f), f);
}

// -----------------------------------------------------------------------
// BaseUI region chooser preset default (graphics::menuHandler::presetForRegionSelection)
// -----------------------------------------------------------------------
//
// Out-of-box US setup starts on LongTurbo. Each guard below is load-bearing: widening the rule past
// "first region ever chosen, US, no preset on record" re-presets nodes that already have an opinion.

// `region` is the region still in place when the user highlights `selected`.
static meshtastic_Config_LoRaConfig loraAt(meshtastic_Config_LoRaConfig_RegionCode region,
                                           meshtastic_Config_LoRaConfig_ModemPreset preset, bool usePreset = true)
{
    meshtastic_Config_LoRaConfig lora = meshtastic_Config_LoRaConfig_init_default;
    lora.region = region;
    lora.modem_preset = preset;
    lora.use_preset = usePreset;
    return lora;
}

#ifndef USERPREFS_LORACONFIG_MODEM_PRESET
static void test_presetForRegionSelection_firstUsSelectionDefaultsToLongTurbo()
{
    const meshtastic_Config_LoRaConfig lora =
        loraAt(meshtastic_Config_LoRaConfig_RegionCode_UNSET, meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO,
                      graphics::menuHandler::presetForRegionSelection(lora, meshtastic_Config_LoRaConfig_RegionCode_US));

    // Unusable unless US offers it: applyLoraRegion()'s reconciliation would throw it straight back.
    TEST_ASSERT_TRUE_MESSAGE(getRegion(meshtastic_Config_LoRaConfig_RegionCode_US)
                                 ->supportsPreset(meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO),
                             "US no longer supports LongTurbo");
}
#else
// A pinned preset owns the decision outright.
static void test_presetForRegionSelection_pinnedUserprefWins()
{
    const meshtastic_Config_LoRaConfig_ModemPreset pinned = USERPREFS_LORACONFIG_MODEM_PRESET;
    const meshtastic_Config_LoRaConfig lora = loraAt(meshtastic_Config_LoRaConfig_RegionCode_UNSET, pinned);

    TEST_ASSERT_EQUAL(pinned, graphics::menuHandler::presetForRegionSelection(lora, meshtastic_Config_LoRaConfig_RegionCode_US));
}
#endif

// US on a node that already has a region is a region change, not first-time setup.
static void test_presetForRegionSelection_laterUsSelectionKeepsCurrentPreset()
{
    const meshtastic_Config_LoRaConfig lora =
        loraAt(meshtastic_Config_LoRaConfig_RegionCode_EU_868, meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
                      graphics::menuHandler::presetForRegionSelection(lora, meshtastic_Config_LoRaConfig_RegionCode_US));
}

// The default is US-only; no other region's first selection is touched.
static void test_presetForRegionSelection_firstNonUsSelectionKeepsCurrentPreset()
{
    const meshtastic_Config_LoRaConfig lora =
        loraAt(meshtastic_Config_LoRaConfig_RegionCode_UNSET, meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);

    for (auto region : {meshtastic_Config_LoRaConfig_RegionCode_EU_868, meshtastic_Config_LoRaConfig_RegionCode_ANZ,
                        meshtastic_Config_LoRaConfig_RegionCode_JP})
        TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
                          graphics::menuHandler::presetForRegionSelection(lora, region));
}

// A preset off the install default is a preference on record (phone app, admin, preset menu).
static void test_presetForRegionSelection_respectsAPresetAlreadyChosen()
{
    const meshtastic_Config_LoRaConfig lora =
        loraAt(meshtastic_Config_LoRaConfig_RegionCode_UNSET, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
                      graphics::menuHandler::presetForRegionSelection(lora, meshtastic_Config_LoRaConfig_RegionCode_US));
}

// use_preset false means raw bandwidth/SF/CR: rewriting modem_preset only misleads the preset menu.
static void test_presetForRegionSelection_ignoresNodesOnRawModemSettings()
{
    const meshtastic_Config_LoRaConfig lora = loraAt(meshtastic_Config_LoRaConfig_RegionCode_UNSET,
                                                     meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, /*usePreset=*/false);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
                      graphics::menuHandler::presetForRegionSelection(lora, meshtastic_Config_LoRaConfig_RegionCode_US));
}
#endif // HAS_SCREEN

// -----------------------------------------------------------------------
// Test runner
// -----------------------------------------------------------------------

void setUp(void)
{
    mockMeshService = new MockMeshService();
    service = mockMeshService;
    testAdmin = new AdminModuleTestShim();
    capturedWarnings.clear();
    capturedTypes.clear();
    capturedLevels.clear();
    // Every test gets its own NodeDB and its own copy of the globals the admin handlers write.
    replaceAdminRadioGlobals();
}
void tearDown(void)
{
    restoreAdminRadioGlobals();
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
    RUN_TEST(test_handleSetOwner_persistsLicensedChannelSanitation);
    RUN_TEST(test_handleSetHamMode_appendsLongNameToCallSign);
    RUN_TEST(test_handleSetHamMode_widestPairSurvivesTheLongNameCap);
    RUN_TEST(test_handleSetHamMode_omittedLongNameKeepsCallSignAlone);
    RUN_TEST(test_handleSetHamMode_blankLongNameIsIgnoredNotRejected);
    RUN_TEST(test_handleSetHamMode_blankShortNameKeepsTheExistingOne);
    RUN_TEST(test_handleSetHamMode_blankCallSignIsRejected);
    RUN_TEST(test_handleSetHamMode_blankCallSignRepliesBadRequest);
    RUN_TEST(test_handleSetHamMode_acceptedRequestAcksSuccess);
    RUN_TEST(test_setConfig_lora24_acceptedWhenTheRouterInterfaceDoes2_4GHz);
    RUN_TEST(test_setConfig_lora24_rejectedWhenTheRouterInterfaceCannot);
    RUN_TEST(test_pkcAdminBadRequest_replyEncodes);
    RUN_TEST(test_pkcAdminBackupPreferencesReply_encodes);
    RUN_TEST(test_handleSetConfig_persistsLicensedFirstRegionIdentity);
    RUN_TEST(test_handleSetConfig_persistsUnlicensedFirstRegionIdentity);
    RUN_TEST(test_bootDefense_sanitizesStaleLicensedChannelsOnce);
    RUN_TEST(test_restorePreferences_sanitizesLicensedBackupBeforeReturn);
    RUN_TEST(test_getRegion_returnsCorrectRegion_US);
    RUN_TEST(test_getRegion_returnsCorrectRegion_EU868);
    RUN_TEST(test_getRegion_returnsCorrectRegion_LORA24);
    RUN_TEST(test_getRegion_unsetCodeReturnsUnsetEntry);
    RUN_TEST(test_getRegion_unknownCodeFallsToUnset);

    // validateConfigRegion()
    RUN_TEST(test_validateConfigRegion_validRegionReturnsTrue);
    RUN_TEST(test_validateConfigRegion_unsetRegionReturnsTrue);
    RUN_TEST(test_validateConfigRegion_unknownCodeReturnsFalse);
    RUN_TEST(test_validateConfigRegion_anotherUnknownCodeReturnsFalse);

    // validateConfigLora()
    RUN_TEST(test_validateConfigLora_validPresetForUS);
    RUN_TEST(test_validateConfigLora_allStdPresetsValidForUS);
    RUN_TEST(test_validateConfigLora_turboPresetsInvalidForEU868);
    RUN_TEST(test_validateConfigLora_validPresetsForEU868);
    RUN_TEST(test_validateConfigLora_customBandwidthTooWideForEU868);
    RUN_TEST(test_validateConfigLora_customBandwidthFitsUS);
    RUN_TEST(test_validateConfigLora_customBandwidthFitsEU868);
    RUN_TEST(test_validateConfigLora_bogusPresetRejected);
    RUN_TEST(test_validateConfigLora_unsetRegionAcceptsAnyRealPreset);
    RUN_TEST(test_isKnownModemPreset_matchesRegionTable);
    RUN_TEST(test_validateConfigLora_allPresetsValidForLORA24);

    // clampConfigLora()
    RUN_TEST(test_clampConfigLora_invalidPresetClampedToDefault);
    RUN_TEST(test_clampConfigLora_validPresetUnchanged);
    RUN_TEST(test_clampLoadedLoraConfig_legacyDefaultPrimaryGetsThePresetSlot);
    RUN_TEST(test_clampConfigLora_customBwTooWideClampedToDefaultBw);
    RUN_TEST(test_clampConfigLora_customBwValidLeftUnchanged);
    RUN_TEST(test_clampConfigLora_bogusPresetOnUnsetClampedToLongFast);
    RUN_TEST(test_clampConfigLora_unsetRegionKeepsRealPreset);
    RUN_TEST(test_clampConfigLora_invalidPresetOnLORA24ClampedToDefault);
    RUN_TEST(test_clampConfigLora_longSlowOnJPClampedToLongFast);
    RUN_TEST(test_clampConfigLora_longModerateOnJPClampedToLongFast);
    RUN_TEST(test_validateConfigLora_invalidPresetIsQuiet);
    RUN_TEST(test_clampConfigLora_jpPresetMigrationRepairsSlot);
    RUN_TEST(test_clampConfigLora_jpMigrationPreservesValidSlot);
    RUN_TEST(test_clampConfigLora_jpMigrationStillErrorsBadSlot);

    // Region-locked preset swap
    RUN_TEST(test_clampConfigLora_narrowPresetOnEU866SwapsToEUN868);
    RUN_TEST(test_clampConfigLora_litePresetOnEU868SwapsToEU866);
    RUN_TEST(test_clampConfigLora_eu868PresetOnEUN868SwapsToEU868);
    RUN_TEST(test_clampConfigLora_litePresetOnUSDoesNotSwap);
    RUN_TEST(test_clampConfigLora_narrowPresetOnHam125cmDoesNotSwap);
    RUN_TEST(test_validateConfigLora_siblingLockedPresetStillFailsValidation);

    // RegionInfo preset list integrity
    RUN_TEST(test_presetsStd_hasTenEntries);
    RUN_TEST(test_presetsEU868_hasSevenEntries);
    RUN_TEST(test_presetsUndef_hasOneEntry);
    RUN_TEST(test_defaultPresetIsInAvailablePresets);
    RUN_TEST(test_regionFieldsAreSane);
    RUN_TEST(test_onlyLORA24HasWideLora);

    // RadioInterface::frequencySlotCount()
    RUN_TEST(test_frequencySlotCount_realRegions);

    // handleSetConfig fromOthers dispatch
    RUN_TEST(test_handleSetConfig_fromOthers_invalidPresetRejected);
    RUN_TEST(test_handleSetConfig_fromLocal_invalidPresetClamped);
    RUN_TEST(test_handleSetConfig_fromOthers_validPresetAccepted);
    RUN_TEST(test_handleSetConfig_fromOthers_invalidChannelNumFullyRejected);
    RUN_TEST(test_clampBandwidthCode_zeroMapsToDefaultOthersUnchanged);
    RUN_TEST(test_handleSetConfig_fromLocal_customBandwidthZeroClampedToDefault);
    RUN_TEST(test_handleSetConfig_fromOthers_customBandwidthZeroClampedToDefault);
    RUN_TEST(test_handleSetConfig_fromLocal_presetBandwidthZeroLeftUntouched);
    RUN_TEST(test_handleSetConfig_fromLocal_customBandwidthNonZeroPreserved);
    RUN_TEST(test_handleSetConfig_security_preservesKeypairWhenPrivateOmitted);
    RUN_TEST(test_handleSetConfig_security_acceptsSuppliedKeypair);
    RUN_TEST(test_handleSetConfig_security_rotationPreservesAdminKeys);
    RUN_TEST(test_handleSetConfig_security_clearsAdminKeysWhenKeypairUnchanged);
    RUN_TEST(test_handleSetConfig_security_lowEntropyRestoreWarnsAndRotates);
    RUN_TEST(test_handleSetConfig_security_lowEntropyFullKeypairRestoreIsRejected);
    RUN_TEST(test_handleSetConfig_security_reDerivedCleanKeyDoesNotWarn);
    RUN_TEST(test_handleSetConfig_security_blacklistedMintLeavesNoKey);
    RUN_TEST(test_generateCryptoKeyPair_derivedFromStoredPrivateIsChecked);
    RUN_TEST(test_generateCryptoKeyPair_failedDerivationFromStoredPrivateClearsKeySizes);
    RUN_TEST(test_keyYieldsReservedNodeNum_matchesTheReservedRange);
    RUN_TEST(test_generateCryptoKeyPair_mintedKeyForNodeZeroIsRedrawn);
    RUN_TEST(test_generateCryptoKeyPair_storedKeyForNodeZeroIsReplacedAtBoot);
    RUN_TEST(test_generateCryptoKeyPair_everyReservedMintIsRedrawn);
    RUN_TEST(test_generateCryptoKeyPair_mintedKeyForNodeTenIsKept);
    RUN_TEST(test_generateCryptoKeyPair_onlyReservedMintsLeaveNoKey);
    RUN_TEST(test_generateCryptoKeyPair_ordinaryRotationKeepsRemoteNodeNine);
    RUN_TEST(test_ensurePkiIdentity_mintedKeyForNodeZeroIsRedrawn);
    RUN_TEST(test_handleSetConfig_security_staleLowEntropyFlagDoesNotWarn);
    RUN_TEST(test_handleSetConfig_security_failedDerivationClearsKeySizes);
    RUN_TEST(test_regionInfo_supportsPreset);
    RUN_TEST(test_checkConfigRegion_quietCheckReportsReason);
    RUN_TEST(test_checkConfigRegion_allowsProspectiveLicensedOwner);
    RUN_TEST(test_handleSetConfig_fromOthers_siblingLockedPresetSwapsRegion);
    RUN_TEST(test_handleSetConfig_fromOthers_lockedPresetFromNonTrioRegionRejected);
    RUN_TEST(test_handleSetConfig_presetChosenBeforeRegionSurvives);
    RUN_TEST(test_handleSetConfig_unsettingRegionKeepsPreset);

    // Channel-configuration warning + coalescing
    RUN_TEST(test_warn_singleChannel_variantName_oneSpecificMessage);
    RUN_TEST(test_warn_singleChannel_nameAndPsk_collapsedToCatchAll);
    RUN_TEST(test_warn_cleanChannel_noMessage);
    RUN_TEST(test_warn_transaction_multipleChannels_singleCoalescedMessage);
    RUN_TEST(test_warn_transaction_singleChannel_keepsSpecificMessage);
    RUN_TEST(test_editTransaction_abandoned_isRetiredOnNextAdminMessage);
    RUN_TEST(test_editTransaction_abandoned_laterWriteIsNoLongerDeferred);
    RUN_TEST(test_editTransaction_active_isNotRetired);
    RUN_TEST(test_warn_license_noTransaction_emittedImmediately);
    RUN_TEST(test_warn_license_transaction_coalescedToSingleMessage);

    // ClientNotification.type
    RUN_TEST(test_notificationType_loneChannelWarning_keepsItsType);
    RUN_TEST(test_notificationType_transaction_loneChannelWarning_keepsItsType);
    RUN_TEST(test_notificationType_transaction_coalescedWarnings_useCatchAllType);
    RUN_TEST(test_notificationType_transaction_coalescedPskWarnings_usePskCatchAllType);
    RUN_TEST(test_notificationType_transaction_mixedWarnings_useNameAndPskCatchAllType);
    RUN_TEST(test_notificationType_licensedMode_deferredNoticeKeepsItsType);
    RUN_TEST(test_notificationType_checkConfigRegion_reportsFailureType);
    RUN_TEST(test_notificationType_validateConfigRegion_sendsFailureType);
    RUN_TEST(test_notificationType_clampConfigLora_channelNumOutOfRange);

    // Node-DB metadata saves must not reconfigure the radio
    RUN_TEST(test_setFavoriteNode_skipsRadioReload_butPersists);
    RUN_TEST(test_setIgnoredNode_skipsRadioReload_butPersists);
    RUN_TEST(test_toggleMutedNode_skipsRadioReload_butPersists);

#if HAS_SCREEN
    // Node menu mute toggle
    RUN_TEST(test_toggleNodeMuted_flipsBitAndSkipsRadioReload);
    RUN_TEST(test_toggleNodeMuted_unknownNodeDoesNothing);
    RUN_TEST(test_toggleNodeMuted_currentlyRewritesEverySegment);

    // BaseUI region chooser preset default
#ifndef USERPREFS_LORACONFIG_MODEM_PRESET
    RUN_TEST(test_presetForRegionSelection_firstUsSelectionDefaultsToLongTurbo);
#else
    RUN_TEST(test_presetForRegionSelection_pinnedUserprefWins);
#endif
    RUN_TEST(test_presetForRegionSelection_laterUsSelectionKeepsCurrentPreset);
    RUN_TEST(test_presetForRegionSelection_firstNonUsSelectionKeepsCurrentPreset);
    RUN_TEST(test_presetForRegionSelection_respectsAPresetAlreadyChosen);
    RUN_TEST(test_presetForRegionSelection_ignoresNodesOnRawModemSettings);
#endif

    exit(UNITY_END());
}

void loop() {}
