// trunk-ignore-all(trufflehog/Lob): matches test_* function names, not credentials
#include "LR20x0Band.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "RadioInterface.h"
#include "TestUtil.h"
#include "memory/MemAudit.h"
#include <string.h>
#include <unity.h>

#include "meshtastic/config.pb.h"
#include "support/MockMeshService.h"

static MockMeshService *mockMeshService;

static void test_lr20x0BandClassification()
{
    TEST_ASSERT_FALSE(isLr20x0HighBand(906.875f));
    TEST_ASSERT_FALSE(isLr20x0HighBand(1500.0f));
    TEST_ASSERT_TRUE(isLr20x0HighBand(2400.0f));
    TEST_ASSERT_TRUE(isLr20x0HighBand(2420.71875f));
}

static void test_lr20x0BandHopDetection()
{
    TEST_ASSERT_FALSE(isLr20x0BandHop(0.0f, 2420.71875f));
    TEST_ASSERT_FALSE(isLr20x0BandHop(906.875f, 915.0f));
    TEST_ASSERT_FALSE(isLr20x0BandHop(2400.0f, 2420.71875f));
    TEST_ASSERT_TRUE(isLr20x0BandHop(906.875f, 2420.71875f));
    TEST_ASSERT_TRUE(isLr20x0BandHop(2420.71875f, 906.875f));
    // Invalid requested frequency must not look like a band hop.
    TEST_ASSERT_FALSE(isLr20x0BandHop(2420.71875f, 0.0f));
    TEST_ASSERT_FALSE(isLr20x0BandHop(906.875f, 0.0f));
    TEST_ASSERT_FALSE(isLr20x0BandHop(2420.71875f, -1.0f));
    TEST_ASSERT_FALSE(isLr20x0BandHop(906.875f, -915.0f));
}

static void test_lr20x0ReconfigurePathSelection()
{
    // LF -> HF and HF -> LF take full begin(); same-band stays incremental.
    TEST_ASSERT_EQUAL(static_cast<int>(Lr20x0ReconfigurePath::FullBegin),
                      static_cast<int>(lr20x0ReconfigurePath(906.875f, 2420.71875f)));
    TEST_ASSERT_EQUAL(static_cast<int>(Lr20x0ReconfigurePath::FullBegin),
                      static_cast<int>(lr20x0ReconfigurePath(2420.71875f, 906.875f)));
    TEST_ASSERT_EQUAL(static_cast<int>(Lr20x0ReconfigurePath::Incremental),
                      static_cast<int>(lr20x0ReconfigurePath(906.875f, 915.0f)));
    TEST_ASSERT_EQUAL(static_cast<int>(Lr20x0ReconfigurePath::Incremental),
                      static_cast<int>(lr20x0ReconfigurePath(2400.0f, 2420.71875f)));
    TEST_ASSERT_EQUAL(static_cast<int>(Lr20x0ReconfigurePath::Incremental),
                      static_cast<int>(lr20x0ReconfigurePath(0.0f, 2420.71875f)));
    TEST_ASSERT_EQUAL(static_cast<int>(Lr20x0ReconfigurePath::Incremental),
                      static_cast<int>(lr20x0ReconfigurePath(2420.71875f, 0.0f)));
    TEST_ASSERT_EQUAL(static_cast<int>(Lr20x0ReconfigurePath::Incremental),
                      static_cast<int>(lr20x0ReconfigurePath(906.875f, -1.0f)));
}

// Test shim to expose protected radio parameters set by applyModemConfig()
class TestableRadioInterface : public RadioInterface
{
  public:
    TestableRadioInterface() : RadioInterface() {}
    uint8_t getCr() const { return cr; }
    uint8_t getSf() const { return sf; }
    float getBw() const { return bw; }

    size_t beginSendingPublic(meshtastic_MeshPacket *p) { return beginSending(p); }
    meshtastic_MeshPacket *getSendingPacket() const { return sendingPacket; }
    void clearSendingPacketForTest() { sendingPacket = nullptr; }

    // Override reconfigure to call the base which invokes applyModemConfig()
    bool reconfigure() override { return RadioInterface::reconfigure(); }

    // Stubs for pure virtual methods required by RadioInterface
    uint32_t getPacketTime(uint32_t, bool) override { return 0; }
    ErrorCode send(meshtastic_MeshPacket *p) override { return ERRNO_OK; }
};

static void test_bwCodeToKHz_specialMappings()
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 7.8f, bwCodeToKHz(8));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.4f, bwCodeToKHz(10));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 15.6f, bwCodeToKHz(16));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 20.8f, bwCodeToKHz(21));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 31.25f, bwCodeToKHz(31));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 41.7f, bwCodeToKHz(42));
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

static void test_bwCodeToKHz_roundTrip()
{
    // Round-trip: bwKHzToCode(bwCodeToKHz(code)) should return the original code
    uint16_t codes[] = {8, 10, 16, 21, 31, 42, 62, 200, 400, 800, 1600};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        uint16_t code = codes[i];
        float khz = bwCodeToKHz(code);
        uint16_t result = bwKHzToCode(khz);
        TEST_ASSERT_EQUAL_UINT16(code, result);
    }
}

static void test_validateConfigLora_noopWhenUsePresetFalse()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = false;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    cfg.bandwidth = 123;
    cfg.spread_factor = 8;

    RadioInterface::validateConfigLora(cfg);

    TEST_ASSERT_EQUAL_UINT16(123, cfg.bandwidth);
    TEST_ASSERT_EQUAL_UINT32(8, cfg.spread_factor);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, cfg.modem_preset);
}

static void test_validateConfigLora_validPreset_nonWideRegion()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;

    TEST_ASSERT_TRUE(RadioInterface::validateConfigLora(cfg));
}

static void test_validateConfigLora_validPreset_wideRegion()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_LORA_24;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;

    TEST_ASSERT_TRUE(RadioInterface::validateConfigLora(cfg));
}

static void test_validateConfigLora_rejectsInvalidPresetForRegion()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    TEST_ASSERT_FALSE(RadioInterface::validateConfigLora(cfg));
}

static void test_clampConfigLora_invalidPresetClampedToDefault()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
}

static void test_clampConfigLora_validPresetUnchanged()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, cfg.modem_preset);
}

// ---------------------------------------------------------------------------
// Repairing an out-of-range frequency slot. The pin is the thing that failed, so the repair is the
// region's own rule: overrideSlot -1 hashes the preset name, 0 the channel name, >0 is that slot.
// ---------------------------------------------------------------------------

/** A region that names its own slot keeps it, whatever the channel happens to be called. */
static void test_clampSlot_regionSlotOutranksACustomName()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_ITU2_70CM; // overrideSlot 137
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW;
    cfg.channel_num = 999;

    const RadioInterface::LoraSlotVerdict verdict = RadioInterface::clampConfigLora(cfg, "NYMesh", false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(137, cfg.channel_num, "the region's slot, not the hash of the name");
    TEST_ASSERT_TRUE_MESSAGE(verdict.usesDefaultFrequencySlot, "the region's own rule leaves it on the default slot");
    TEST_ASSERT_TRUE(verdict.usesCustomChannelName);
}

/** A channel-hash region hashes the name it was handed. */
static void test_clampSlot_channelHashRegionUsesTheGivenName()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    cfg.channel_num = 999;

    meshtastic_Config_LoRaConfig derive = cfg;
    derive.channel_num = 0; // no pin in the way, so this is the derived answer
    const uint32_t expected = RadioInterface::resolveFrequencySlot(derive, "NYMesh");

    const RadioInterface::LoraSlotVerdict verdict = RadioInterface::clampConfigLora(cfg, "NYMesh", false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, cfg.channel_num, "the repair must agree with resolveFrequencySlot()");
    TEST_ASSERT_TRUE(verdict.usesDefaultFrequencySlot);
}

/** An uncustomised name takes the same path: there is no separate preset-hash case for it. */
static void test_clampSlot_defaultNamedChannelTakesTheSamePath()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    cfg.channel_num = 999;

    meshtastic_Config_LoRaConfig derive = cfg;
    derive.channel_num = 0;
    const uint32_t expected = RadioInterface::resolveFrequencySlot(derive, "LongFast");

    const RadioInterface::LoraSlotVerdict verdict = RadioInterface::clampConfigLora(cfg, "LongFast", false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, cfg.channel_num, "the preset's own name is just a channel name here");
    TEST_ASSERT_FALSE(verdict.usesCustomChannelName);
    TEST_ASSERT_TRUE(verdict.usesDefaultFrequencySlot);
}

/** Radio config only - a beacon target or offer never reaches this, both always run a preset.
 *  The pin used to survive the clamp here, leaving the node on a slot the region does not hold. */
static void test_clampSlot_customModemSettingsStillGetRepaired()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = false; // so the preset display name, and this channel's name, is "Custom"
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.bandwidth = 250;
    cfg.spread_factor = 11;
    cfg.coding_rate = 5;
    cfg.channel_num = 999;

    meshtastic_Config_LoRaConfig derive = cfg;
    derive.channel_num = 0;
    const uint32_t expected = RadioInterface::resolveFrequencySlot(derive, "Custom");

    RadioInterface::clampConfigLora(cfg, "Custom", false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, cfg.channel_num, "an invalid pin must not survive the clamp");
    TEST_ASSERT_TRUE_MESSAGE(cfg.channel_num >= 1 && cfg.channel_num <= RadioInterface::frequencySlotCount(cfg),
                             "and what replaces it must be a slot the region holds");
}

/** Validation answers the question without repairing anything. */
static void test_validateSlot_outOfRangePinIsRejected()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    cfg.channel_num = 999;

    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::validateConfigLora(cfg, "NYMesh"), "999 is past the top of US");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(999, cfg.channel_num, "validation must not repair what it rejects");

    cfg.channel_num = 1;
    TEST_ASSERT_TRUE(RadioInterface::validateConfigLora(cfg, "NYMesh"));
}

/** A pin the region does hold is the operator's choice, and is left alone. */
static void test_clampSlot_inRangePinIsKept()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    meshtastic_Config_LoRaConfig derive = cfg;
    const uint32_t derived = RadioInterface::resolveFrequencySlot(derive, "NYMesh");
    cfg.channel_num = (derived == 1) ? 2 : 1; // anything but the slot the name would have picked

    const uint32_t pinned = cfg.channel_num;
    const RadioInterface::LoraSlotVerdict verdict = RadioInterface::clampConfigLora(cfg, "NYMesh", false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(pinned, cfg.channel_num, "a valid pin is not the clamp's business");
    TEST_ASSERT_FALSE_MESSAGE(verdict.usesDefaultFrequencySlot, "a deliberate pin is not the default slot");
}

// -----------------------------------------------------------------------
// applyModemConfig() coding rate tests (via reconfigure)
// -----------------------------------------------------------------------

static TestableRadioInterface *testRadio;

// ---------------------------------------------------------------------------
// Frequency slot boundaries. Width is spacing + 2*padding + bandwidth; getFreq() returns the slot
// CENTRE, so the upper edge is centre + bw/2 (getBw() is kHz, hence /2000 for MHz).
// ---------------------------------------------------------------------------

/** US: 26MHz of band at 250kHz tiles into exactly 104 slots, the last ending on 928.000. */
static void test_frequencySlot_usTopSlotEndsOnBandEdge()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(104, RadioInterface::frequencySlotCount(config.lora),
                                     "902-928MHz at 250kHz is exactly 104 slots");

    config.lora.channel_num = 104; // top slot, 1-based
    testRadio->reconfigure();

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0005f, 927.875f, testRadio->getFreq(), "top slot centre");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0005f, 928.0f, testRadio->getFreq() + testRadio->getBw() / 2000.0f,
                                     "the top slot must end on the band edge, never past it");
}

/** NZ_865 at 125kHz: 4MHz tiles into 32 slots, the last ending on 868.000. */
static void test_frequencySlot_nz865NarrowBandwidthTopSlot()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_NZ_865;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW; // 125kHz

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(32, RadioInterface::frequencySlotCount(config.lora),
                                     "864-868MHz at 125kHz is exactly 32 slots");

    config.lora.channel_num = 32;
    testRadio->reconfigure();

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0005f, 867.9375f, testRadio->getFreq(), "top slot centre");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0005f, 868.0f, testRadio->getFreq() + testRadio->getBw() / 2000.0f,
                                     "halving the bandwidth must not push the top slot past the edge");
}

/** EU_868: a single 250kHz slot filling the whole 869.4-869.65 allocation. */
static void test_frequencySlot_eu868IsExactlyOneSlot()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, RadioInterface::frequencySlotCount(config.lora),
                                     "869.4-869.65MHz at 250kHz holds one slot and no more");

    config.lora.channel_num = 1;
    testRadio->reconfigure();

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0005f, 869.525f, testRadio->getFreq(), "the only slot sits mid-band");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0005f, 869.65f, testRadio->getFreq() + testRadio->getBw() / 2000.0f,
                                     "the single slot fills the band exactly");
}

/** ITU1_2M: padding brackets each slot, coercing 15.6kHz onto the 20kHz ham raster. */
static void test_frequencySlot_itu1_2mPaddingBracketsTopSlot()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_ITU1_2M;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_TINY_FAST; // 15.6kHz + 2*2.2kHz = 20kHz

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(100, RadioInterface::frequencySlotCount(config.lora),
                                     "144-146MHz on a 20kHz raster is 100 slots");

    config.lora.channel_num = 100;
    testRadio->reconfigure();

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0005f, 145.99f, testRadio->getFreq(), "top slot centre");
    // Upper edge plus the trailing padding lands on 146.000: padding is a per-slot bracket, so the
    // last slot stops 2.2kHz short of the edge rather than on it.
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0005f, 145.9978f, testRadio->getFreq() + testRadio->getBw() / 2000.0f,
                                     "the padded raster must leave its trailing guard inside the band");
}

// After fresh flash: coding_rate=0, use_preset=true, modem_preset=LONG_FAST
// CR should come from the preset (5 for LONG_FAST), not from the zero default.
static void test_applyModemConfig_freshFlashCodingRateNotZero()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    // coding_rate is 0 (default after init_zero, same as fresh flash)

    testRadio->reconfigure();

    // LONG_FAST preset has cr=5; must never be 0
    TEST_ASSERT_EQUAL_UINT8(5, testRadio->getCr());
    TEST_ASSERT_EQUAL_UINT8(11, testRadio->getSf());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 250.0f, testRadio->getBw());
}

// When coding_rate matches the preset exactly, should still use the preset value
static void test_applyModemConfig_codingRateMatchesPreset()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    config.lora.coding_rate = 8; // LONG_SLOW default is cr=8

    testRadio->reconfigure();

    TEST_ASSERT_EQUAL_UINT8(8, testRadio->getCr());
}

// Custom CR higher than preset should be used
static void test_applyModemConfig_customCodingRateHigherThanPreset()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    config.lora.coding_rate = 7; // LONG_FAST preset has cr=5, 7 > 5

    testRadio->reconfigure();

    TEST_ASSERT_EQUAL_UINT8(7, testRadio->getCr());
}

// Custom CR lower than preset: preset wins (higher is more robust)
static void test_applyModemConfig_customCodingRateLowerThanPreset()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    config.lora.coding_rate = 5; // LONG_SLOW preset has cr=8, 5 < 8

    testRadio->reconfigure();

    TEST_ASSERT_EQUAL_UINT8(8, testRadio->getCr());
}

// MEDIUM_TURBO performs like MEDIUM_FAST (sf=9, cr=5) but at 500 kHz. Verify the params resolve.
static void test_applyModemConfig_mediumTurbo()
{
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO;

    testRadio->reconfigure();

    TEST_ASSERT_EQUAL_UINT8(5, testRadio->getCr());
    TEST_ASSERT_EQUAL_UINT8(9, testRadio->getSf());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 500.0f, testRadio->getBw());
}

// MEDIUM_TURBO is a 500 kHz preset, so it is invalid for EU_868 and must clamp to the region default.
/**
 * UNSET is "no region chosen yet", not a regulatory domain, so validation accepts every preset some
 * region offers - not just the LONG_FAST default. Rejecting would clamp away a preset the user
 * picked, on every boot and every set_config until they set a region.
 */
static void test_validateConfigLora_unsetRegionAcceptsEveryOfferedPreset()
{
    for (int p = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST; p <= meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO;
         p++) {
        // VERY_LONG_SLOW was deprecated in 2.5 and no region lists it, so it is not a preset a
        // user can be holding - it is rejected under UNSET like any other unknown value.
        if (p == meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW)
            continue;

        meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
        cfg.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
        cfg.use_preset = true;
        cfg.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)p;

        char msg[64];
        snprintf(msg, sizeof(msg), "preset %d must validate under UNSET", p);
        TEST_ASSERT_TRUE_MESSAGE(RadioInterface::validateConfigLora(cfg), msg);

        // And a clamp must leave it alone rather than rewriting it to the default.
        meshtastic_Config_LoRaConfig clamped = cfg;
        RadioInterface::clampConfigLora(clamped);
        TEST_ASSERT_EQUAL_MESSAGE(p, clamped.modem_preset, msg);
    }
}

/** The deprecated preset no region offers is rejected under UNSET, and clamped to the default. */
static void test_clampConfigLora_unsetRegionClampsTheDeprecatedPreset()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    cfg.use_preset = true;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW;

    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::validateConfigLora(cfg), "VERY_LONG_SLOW is offered by no region");
    RadioInterface::clampConfigLora(cfg);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
}

/** A fabricated preset value is still rejected under UNSET, and clamped to the default. */
static void test_clampConfigLora_unsetRegionStillClampsABogusPreset()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    cfg.use_preset = true;
    cfg.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)99;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset,
                              "a value no region offers is not a preset, and is clamped");
}

static void test_clampConfigLora_mediumTurboInvalidForEU868()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, cfg.modem_preset);
}

// MEDIUM_TURBO is valid for US (PROFILE_STD) and must be left unchanged.
static void test_clampConfigLora_mediumTurboValidForUS()
{
    meshtastic_Config_LoRaConfig cfg = meshtastic_Config_LoRaConfig_init_zero;
    cfg.use_preset = true;
    cfg.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    cfg.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO;

    RadioInterface::clampConfigLora(cfg);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO, cfg.modem_preset);
}

// -----------------------------------------------------------------------
// getRegionPresetMap() - region->valid-preset map sent to clients during want_config
// -----------------------------------------------------------------------

static size_t countKnownRegions()
{
    size_t n = 0;
    for (const RegionInfo *r = regions; r->code != meshtastic_Config_LoRaConfig_RegionCode_UNSET; r++)
        n++;
    return n;
}

// Every region in the firmware table (except the UNSET sentinel) must appear
// exactly once in the map, and all counts must stay within the mesh.options bounds
// (exceeding them would mean nanopb silently truncates the wire message).
static void test_regionPresetMap_coversAllRegionsWithinBounds()
{
    meshtastic_LoRaRegionPresetMap map;
    getRegionPresetMap(map);

#ifdef USERPREFS_LORACONFIG_MODEM_PRESET
    const size_t known = countKnownRegions() + 1; // + the UNSET intent entry
#else
    const size_t known = countKnownRegions();
#endif
    TEST_ASSERT_EQUAL_UINT((unsigned)known, (unsigned)map.region_groups_count);

    // Bounds derived from the generated nanopb arrays (mesh.options max_count), so
    // this stays correct if those bounds change.
    const size_t maxGroups = sizeof(map.groups) / sizeof(map.groups[0]);
    const size_t maxRegions = sizeof(map.region_groups) / sizeof(map.region_groups[0]);
    TEST_ASSERT_GREATER_THAN_UINT(0, map.groups_count);
    TEST_ASSERT_LESS_OR_EQUAL_UINT((unsigned)maxGroups, map.groups_count);
    TEST_ASSERT_LESS_OR_EQUAL_UINT((unsigned)maxRegions, map.region_groups_count);

    // Each known region appears exactly once.
    for (const RegionInfo *r = regions; r->code != meshtastic_Config_LoRaConfig_RegionCode_UNSET; r++) {
        int hits = 0;
        for (pb_size_t i = 0; i < map.region_groups_count; i++)
            if (map.region_groups[i].region == r->code)
                hits++;
        TEST_ASSERT_EQUAL_INT(1, hits);
    }
}

// The advertised presets must agree with the live region table: every preset is
// legal in its region, the default is among them, and the licensed flag matches.
static void test_regionPresetMap_matchesRegionTable()
{
    meshtastic_LoRaRegionPresetMap map;
    getRegionPresetMap(map);

    for (pb_size_t i = 0; i < map.region_groups_count; i++) {
        meshtastic_Config_LoRaConfig_RegionCode code = map.region_groups[i].region;
        uint8_t gi = map.region_groups[i].group_index;
        TEST_ASSERT_LESS_THAN_UINT(map.groups_count, gi);

        const meshtastic_LoRaPresetGroup &grp = map.groups[gi];
        const RegionInfo *r = getRegion(code);

#ifdef USERPREFS_LORACONFIG_MODEM_PRESET
        // UNSET states the pinned preset, not PROFILE_UNDEF's list, so the table checks below don't apply.
        if (code == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
            continue;
#endif

        // Group's list is non-empty and within the generated array bound.
        const size_t maxPresets = sizeof(grp.presets) / sizeof(grp.presets[0]);
        TEST_ASSERT_GREATER_THAN_UINT(0, grp.presets_count);
        TEST_ASSERT_LESS_OR_EQUAL_UINT((unsigned)maxPresets, grp.presets_count);

        // Every advertised preset must be selectable from this region: either legal here,
        // or legal in a sibling the firmware will auto-swap us to (the EU 86x trio, which
        // advertises the union of the trio's presets rather than just its own).
        for (pb_size_t p = 0; p < grp.presets_count; p++) {
            bool selectable =
                r->supportsPreset(grp.presets[p]) || RadioInterface::regionSwapForPreset(code, grp.presets[p]) != nullptr;
            TEST_ASSERT_TRUE(selectable);
        }

        // The region's own enforced presets must all be advertised (advertised is a
        // superset of the enforced list, never a subset).
        const meshtastic_Config_LoRaConfig_ModemPreset *enforced = r->getAvailablePresets();
        for (size_t e = 0; e < r->getNumPresets(); e++) {
            bool advertised = false;
            for (pb_size_t p = 0; p < grp.presets_count; p++)
                if (grp.presets[p] == enforced[e])
                    advertised = true;
            TEST_ASSERT_TRUE(advertised);
        }

        // Default preset matches the table, is legal, and is present in the list.
        TEST_ASSERT_EQUAL(r->getDefaultPreset(), grp.default_preset);
        TEST_ASSERT_TRUE(r->supportsPreset(grp.default_preset));
        bool defaultInList = false;
        for (pb_size_t p = 0; p < grp.presets_count; p++)
            if (grp.presets[p] == grp.default_preset)
                defaultInList = true;
        TEST_ASSERT_TRUE(defaultInList);

        // Licensed flag matches the region's profile.
        TEST_ASSERT_EQUAL(r->profile->licensedOnly, grp.licensed_only);
    }
}

// UNSET appears only when the build pins a preset, and then states exactly that preset.
// A stock build leaves it out entirely, which clients read as "unconstrained".
static void test_regionPresetMap_unsetCarriesUserprefsIntent()
{
    meshtastic_LoRaRegionPresetMap map;
    getRegionPresetMap(map);

    const meshtastic_LoRaPresetGroup *grp = nullptr;
    for (pb_size_t i = 0; i < map.region_groups_count; i++)
        if (map.region_groups[i].region == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
            grp = &map.groups[map.region_groups[i].group_index];

#ifdef USERPREFS_LORACONFIG_MODEM_PRESET
    const meshtastic_Config_LoRaConfig_ModemPreset pinned = USERPREFS_LORACONFIG_MODEM_PRESET;
    TEST_ASSERT_NOT_NULL_MESSAGE(grp, "a build that pins a preset must state it for UNSET");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, (unsigned)grp->presets_count, "the pinned preset is the sole entry");
    TEST_ASSERT_EQUAL(pinned, grp->presets[0]);
    TEST_ASSERT_EQUAL(pinned, grp->default_preset);
    TEST_ASSERT_FALSE_MESSAGE(grp->licensed_only, "UNSET is not a licensed-only region");

    // Stating intent must not narrow what the device accepts: the firmware still takes any
    // real preset while the region is unset (#11496), so the map cannot become enforcement.
    const RegionInfo *unset = getRegion(meshtastic_Config_LoRaConfig_RegionCode_UNSET);
    TEST_ASSERT_TRUE(unset->supportsPreset(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST));
    TEST_ASSERT_TRUE(unset->supportsPreset(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO));
#else
    TEST_ASSERT_NULL_MESSAGE(grp, "a stock build must leave UNSET out of the map entirely");
#endif
}

// In-flight packet bytes as packetPool reports them, 0 before the first alloc registers the tag.
static int32_t packetPoolLiveBytes()
{
    memaudit::Tag rows[memaudit::kMaxTags];
    size_t n = memaudit::snapshot(rows, memaudit::kMaxTags);
    for (size_t i = 0; i < n; i++)
        if (rows[i].tag && strcmp(rows[i].tag, "pktpool(live)") == 0)
            return rows[i].bytes;
    return 0;
}

// Oversize is refused at the radio queue in Router::send(). If one ever gets this far the memcpy is
// clamped instead of failing, and the packet stays the caller's to release.
static void test_beginSending_oversizedPayloadIsClamped()
{
    const int32_t liveBefore = packetPoolLiveBytes();

    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    TEST_ASSERT_NOT_NULL(p);
    // Without this the check below would also pass against a dead probe.
    TEST_ASSERT_GREATER_THAN_INT32(liveBefore, packetPoolLiveBytes());

    p->from = 0x12345678;
    p->to = 0x87654321;
    p->id = 0x10203040;
    p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p->encrypted.size = MAX_RADIO_PAYLOAD_LEN + 10;

    TEST_ASSERT_EQUAL_UINT_MESSAGE(MAX_LORA_PAYLOAD_LEN, testRadio->beginSendingPublic(p),
                                   "an oversized payload must be clamped to the PHY limit, not rejected");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(p, testRadio->getSendingPacket(), "beginSending must still take the packet");

    // beginSending has no failure path that releases, so the packet is ours to free.
    testRadio->clearSendingPacketForTest();
    packetPool.release(p);
    TEST_ASSERT_EQUAL_INT32(liveBefore, packetPoolLiveBytes());
}

// The clamp must not shorten ordinary traffic, and a maximum-size frame must still fit the PHY.
static void test_beginSending_fittingPayloadIsSentWhole()
{
    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    TEST_ASSERT_NOT_NULL(p);
    p->from = 0x12345678;
    p->to = 0x87654321;
    p->id = 0x10203041;
    p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p->encrypted.size = MAX_RADIO_PAYLOAD_LEN;

    TEST_ASSERT_EQUAL_UINT_MESSAGE(MAX_LORA_PAYLOAD_LEN, testRadio->beginSendingPublic(p),
                                   "the largest allowed payload must produce a frame at the PHY limit");
    testRadio->clearSendingPacketForTest();
    packetPool.release(p);
}
void setUp(void)
{
    mockMeshService = new MockMeshService();
    service = mockMeshService;

    // RadioInterface computes slotTimeMsec during construction and expects myRegion to be valid.
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    testRadio = new TestableRadioInterface();
}
void tearDown(void)
{
    delete testRadio;
    testRadio = nullptr;
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
    RUN_TEST(test_lr20x0BandClassification);
    RUN_TEST(test_lr20x0BandHopDetection);
    RUN_TEST(test_lr20x0ReconfigurePathSelection);
    RUN_TEST(test_frequencySlot_usTopSlotEndsOnBandEdge);
    RUN_TEST(test_frequencySlot_nz865NarrowBandwidthTopSlot);
    RUN_TEST(test_frequencySlot_eu868IsExactlyOneSlot);
    RUN_TEST(test_frequencySlot_itu1_2mPaddingBracketsTopSlot);
    RUN_TEST(test_bwCodeToKHz_specialMappings);
    RUN_TEST(test_bwCodeToKHz_passthrough);
    RUN_TEST(test_bwCodeToKHz_roundTrip);
    RUN_TEST(test_validateConfigLora_noopWhenUsePresetFalse);
    RUN_TEST(test_validateConfigLora_validPreset_nonWideRegion);
    RUN_TEST(test_validateConfigLora_validPreset_wideRegion);
    RUN_TEST(test_validateConfigLora_rejectsInvalidPresetForRegion);
    RUN_TEST(test_clampConfigLora_invalidPresetClampedToDefault);
    RUN_TEST(test_clampConfigLora_validPresetUnchanged);
    RUN_TEST(test_clampSlot_regionSlotOutranksACustomName);
    RUN_TEST(test_clampSlot_channelHashRegionUsesTheGivenName);
    RUN_TEST(test_clampSlot_defaultNamedChannelTakesTheSamePath);
    RUN_TEST(test_clampSlot_customModemSettingsStillGetRepaired);
    RUN_TEST(test_validateSlot_outOfRangePinIsRejected);
    RUN_TEST(test_clampSlot_inRangePinIsKept);
    RUN_TEST(test_applyModemConfig_freshFlashCodingRateNotZero);
    RUN_TEST(test_applyModemConfig_codingRateMatchesPreset);
    RUN_TEST(test_applyModemConfig_customCodingRateHigherThanPreset);
    RUN_TEST(test_applyModemConfig_customCodingRateLowerThanPreset);
    RUN_TEST(test_applyModemConfig_mediumTurbo);
    RUN_TEST(test_validateConfigLora_unsetRegionAcceptsEveryOfferedPreset);
    RUN_TEST(test_clampConfigLora_unsetRegionClampsTheDeprecatedPreset);
    RUN_TEST(test_clampConfigLora_unsetRegionStillClampsABogusPreset);
    RUN_TEST(test_clampConfigLora_mediumTurboInvalidForEU868);
    RUN_TEST(test_clampConfigLora_mediumTurboValidForUS);
    RUN_TEST(test_regionPresetMap_coversAllRegionsWithinBounds);
    RUN_TEST(test_regionPresetMap_matchesRegionTable);
    RUN_TEST(test_regionPresetMap_unsetCarriesUserprefsIntent);
    RUN_TEST(test_beginSending_oversizedPayloadIsClamped);
    RUN_TEST(test_beginSending_fittingPayloadIsSentWhole);
    exit(UNITY_END());
}

void loop() {}
