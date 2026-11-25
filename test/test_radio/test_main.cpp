#include "LR20x0Band.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "RadioInterface.h"
#include "RadioLibInterface.h"
#include "TestUtil.h"
#include "UptimeClock.h"
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

static void test_lr20x0CustomLfPaBandCoversOnly500To1000MHz()
{
    // The board LF PA table is the datasheet's 915 MHz design; 433 MHz and anything above 1 GHz keep RadioLib's.
    TEST_ASSERT_FALSE(isLr20x0CustomLfPaBand(433.175f));
    TEST_ASSERT_FALSE(isLr20x0CustomLfPaBand(499.9f));
    TEST_ASSERT_TRUE(isLr20x0CustomLfPaBand(500.0f));
    TEST_ASSERT_TRUE(isLr20x0CustomLfPaBand(869.525f));
    TEST_ASSERT_TRUE(isLr20x0CustomLfPaBand(906.875f));
    TEST_ASSERT_TRUE(isLr20x0CustomLfPaBand(1000.0f));
    TEST_ASSERT_FALSE(isLr20x0CustomLfPaBand(1000.1f));
    TEST_ASSERT_FALSE(isLr20x0CustomLfPaBand(2420.71875f));
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

// -----------------------------------------------------------------------
// applyModemConfig() coding rate tests (via reconfigure)
// -----------------------------------------------------------------------

static TestableRadioInterface *testRadio;

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

// -----------------------------------------------------------------------
// computePacketTime(): RadioLib error codes must never be read as durations (#11935)
// -----------------------------------------------------------------------

// computePacketTime() is a template over the driver, so only the calls it makes have to exist.
class FakeLoraRadio
{
  public:
    RadioLibTime_t reportedTimeOnAir = 0;   // what getTimeOnAir() answers
    RadioLibTime_t calculatedTimeOnAir = 0; // what calculateTimeOnAir() answers
    int16_t headerInfoResult = RADIOLIB_ERR_UNSUPPORTED;
    uint8_t headerCodingRate = 0;
    bool headerCrcEnabled = true;

    uint32_t calculateCalls = 0;
    uint32_t timeOnAirCalls = 0;
    uint32_t headerInfoCalls = 0;
    uint8_t lastCodingRate = 0;
    bool lastCrcEnabled = false;

    RadioLibTime_t getTimeOnAir(size_t)
    {
        timeOnAirCalls++;
        return reportedTimeOnAir;
    }

    RadioLibTime_t calculateTimeOnAir(ModemType_t, DataRate_t dr, PacketConfig_t pc, size_t)
    {
        calculateCalls++;
        lastCodingRate = dr.lora.codingRate;
        lastCrcEnabled = pc.lora.crcEnabled;
        return calculatedTimeOnAir;
    }

    int16_t getLoRaRxHeaderInfo(uint8_t *cr, bool *crc)
    {
        headerInfoCalls++;
        if (headerInfoResult == RADIOLIB_ERR_NONE) {
            *cr = headerCodingRate;
            *crc = headerCrcEnabled;
        }
        return headerInfoResult;
    }
};

// A PhysicalLayer that answers its IRQ flags, each generic RadioLib IRQ mapped to its own bit, and the
// airtime it is asked for. A clear takes the bit down, as a chip's does.
class FakeIrqRadio : public PhysicalLayer
{
  public:
    uint32_t irqFlags = 0;
    RadioLibTime_t timeOnAirUsec = 0; // what calculateTimeOnAir() answers; 0 reads as an error
    uint8_t lastCodingRate = 0;       // the CR it was asked to time, so a caller's worst case is visible
    bool lastCrcEnabled = false;

    FakeIrqRadio()
    {
        for (uint8_t i = 0; i < sizeof(irqMap) / sizeof(irqMap[0]); i++)
            irqMap[i] = 1UL << i;
    }

    // What clearIrq() retired, and the register it retired it from: PhysicalLayer::clearIrq() maps the
    // generic flags and hands them to clearIrqFlags(), which is the chip-specific half the fake stands in for.
    uint32_t clearedIrqFlags = 0;

    uint32_t getIrqFlags() override { return irqFlags; }
    int16_t clearIrqFlags(uint32_t irq) override
    {
        clearedIrqFlags |= irq;
        irqFlags &= ~irq;
        return RADIOLIB_ERR_NONE;
    }
    RadioLibTime_t calculateTimeOnAir(ModemType_t, DataRate_t dr, PacketConfig_t pc, size_t) override
    {
        lastCodingRate = dr.lora.codingRate;
        lastCrcEnabled = pc.lora.crcEnabled;
        return timeOnAirUsec;
    }
    Module *getMod() override { return nullptr; }
};

// Test shim: no chip, just the modem parameters computePacketTime() reads.
class TestableRadioLibInterface : public RadioLibInterface
{
  public:
    explicit TestableRadioLibInterface(PhysicalLayer *phy = nullptr)
        : RadioLibInterface(nullptr, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, phy)
    {
    }

    // What getPacketTime() answers, so checkStaleRxFlags() has a max-packet window to time against
    uint32_t packetTimeMs = 0;

    void setModem(uint8_t spreadFactor, float bandwidth, uint8_t codingRate)
    {
        sf = spreadFactor;
        bw = bandwidth;
        cr = codingRate;
    }

    uint32_t computePacketTimePublic(FakeLoraRadio &radio, uint32_t pl, bool received, bool useRxHeaderInfo = true)
    {
        return computePacketTime(radio, pl, received, useRxHeaderInfo);
    }

    static bool isRadioLibTimeErrorPublic(RadioLibTime_t usec) { return isRadioLibTimeError(usec); }

    uint32_t maxRxFrameMsecPublic() { return maxRxFrameMsec(); }
    bool preambleHoldActivePublic() { return preambleHoldActive(); }
    bool preambleHoldPeekablePublic() { return preambleHoldPeekable(); }
    void setPreambleLength(uint16_t len) { preambleLength = len; }
    bool receiveDetectedPublic(uint16_t irq) { return receiveDetected(irq, kFakeHeaderValid, kFakePreambleDetected); }

    // Chip IRQ masks, as each driver passes its own to receiveDetected()
    static constexpr uint16_t kFakeHeaderValid = 1 << 1;
    static constexpr uint16_t kFakePreambleDetected = 1 << 2;

    // What a chip with a receive FIFO is asked to drop when a frame is given up on unread
    uint32_t discardedRxFrames = 0;
    void discardUnreadRxFrame() override { discardedRxFrames++; }
    void clearReadIrqsPublic() { clearReadIrqs(); }

    // Chip-specific hooks this test never reaches
    uint32_t getPacketTime(uint32_t, bool) override { return packetTimeMs; }
    int16_t getCurrentRSSI() override { return 0; }
    bool isChannelActive() override { return false; }
    bool isActivelyReceiving() override { return false; }
    void addReceiveMetadata(meshtastic_MeshPacket *) override {}
    void setRadioIsr(void (*)()) override {}
    void clearRadioIsr() override {}
};

static TestableRadioLibInterface *makeTestableRadioLibInterface()
{
    auto *radioIf = new TestableRadioLibInterface();
    radioIf->setModem(11, 250.0f, 5); // LONG_FAST
    return radioIf;
}

// A healthy chip answers getTimeOnAir(), and that answer is what we report.
static void test_computePacketTime_txUsesTheRadiosOwnAnswer()
{
    auto *radioIf = makeTestableRadioLibInterface();
    FakeLoraRadio radio;
    radio.reportedTimeOnAir = 123456; // usec
    radio.calculatedTimeOnAir = 999000;

    TEST_ASSERT_EQUAL_UINT32(123, radioIf->computePacketTimePublic(radio, 32, false));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, radio.calculateCalls, "a working radio must not need the fallback");

    delete radioIf;
}

// WRONG_MODEM read as a duration is 4294967ms of airtime for one packet, which stops the node
// transmitting until it is rebooted.
static void test_computePacketTime_txFallsBackWhenTheRadioReportsAnError()
{
    auto *radioIf = makeTestableRadioLibInterface();
    FakeLoraRadio radio;
    radio.reportedTimeOnAir = (RadioLibTime_t)RADIOLIB_ERR_WRONG_MODEM;
    radio.calculatedTimeOnAir = 500000; // usec, from the modem config we asked for

    uint32_t msec = radioIf->computePacketTimePublic(radio, 32, false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(500, msec, "an error must fall back to the configured modem, not be divided by 1000");
    TEST_ASSERT_EQUAL_UINT32(1, radio.calculateCalls);

    delete radioIf;
}

// If the fallback fails too, report no airtime rather than let a code reach the airtime windows.
static void test_computePacketTime_reportsNoAirtimeWhenNothingCanBeComputed()
{
    auto *radioIf = makeTestableRadioLibInterface();
    FakeLoraRadio radio;
    radio.reportedTimeOnAir = (RadioLibTime_t)RADIOLIB_ERR_WRONG_MODEM;
    radio.calculatedTimeOnAir = (RadioLibTime_t)RADIOLIB_ERR_INVALID_CODING_RATE;

    TEST_ASSERT_EQUAL_UINT32(0, radioIf->computePacketTimePublic(radio, 32, false));

    delete radioIf;
}

// The RX path still takes coding rate and CRC from the header, and is guarded the same way.
static void test_computePacketTime_rxUsesHeaderInfoAndIsGuarded()
{
    auto *radioIf = makeTestableRadioLibInterface();
    FakeLoraRadio radio;
    radio.headerInfoResult = RADIOLIB_ERR_NONE;
    radio.headerCodingRate = 2; // raw header value for 4/6
    radio.headerCrcEnabled = false;
    radio.calculatedTimeOnAir = 78000;

    TEST_ASSERT_EQUAL_UINT32(78, radioIf->computePacketTimePublic(radio, 32, true));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(6, radio.lastCodingRate, "raw header coding rate must become a denominator");
    TEST_ASSERT_FALSE(radio.lastCrcEnabled);

    radio.calculatedTimeOnAir = (RadioLibTime_t)RADIOLIB_ERR_WRONG_MODEM;
    TEST_ASSERT_EQUAL_UINT32(0, radioIf->computePacketTimePublic(radio, 32, true));

    delete radioIf;
}

// The LR2021 times received frames without the header info: the chip ORs every received header's coding rate into
// one field, so a receiver configured 4/5 (raw 1) reports raw 7 once frames at 4/6 and 4/8 have been on air, and
// keeps it for the session. That reads as 4/8 below, timing a 200-byte frame about 55% long. So the configured rate
// and CRC are used, and neither the header nor the chip's own airtime answer is asked for.
static void test_computePacketTime_rxWithoutHeaderInfoUsesConfiguredModem()
{
    auto *radioIf = makeTestableRadioLibInterface(); // configured 4/5
    FakeLoraRadio radio;
    radio.headerInfoResult = RADIOLIB_ERR_NONE;
    radio.headerCodingRate = 7; // 1|2|4: the configured 4/5 ORed with frames at 4/6 and 4/8, read below as 4/8
    radio.headerCrcEnabled = false;
    radio.calculatedTimeOnAir = 79000;
    radio.reportedTimeOnAir = 999000;

    TEST_ASSERT_EQUAL_UINT32(79, radioIf->computePacketTimePublic(radio, 32, true, false));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5, radio.lastCodingRate, "the configured coding rate must be timed, not the header's");
    TEST_ASSERT_TRUE_MESSAGE(radio.lastCrcEnabled, "the configured CRC setting must be timed, not the header's");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, radio.headerInfoCalls, "the header info must not be read at all");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, radio.timeOnAirCalls, "a received frame must not be timed as our own TX");

    // TX is unchanged: the chip's own answer still wins
    radio.reportedTimeOnAir = 123456;
    TEST_ASSERT_EQUAL_UINT32(123, radioIf->computePacketTimePublic(radio, 32, false, false));

    delete radioIf;
}

// Every RADIOLIB_ERR_* is rejected, every duration a LoRa packet can actually take is kept.
static void test_isRadioLibTimeError_separatesCodesFromDurations()
{
    TEST_ASSERT_TRUE(TestableRadioLibInterface::isRadioLibTimeErrorPublic((RadioLibTime_t)RADIOLIB_ERR_WRONG_MODEM));
    TEST_ASSERT_TRUE(TestableRadioLibInterface::isRadioLibTimeErrorPublic((RadioLibTime_t)RADIOLIB_ERR_UNKNOWN));
    TEST_ASSERT_TRUE(TestableRadioLibInterface::isRadioLibTimeErrorPublic((RadioLibTime_t)RADIOLIB_ERR_SPI_CMD_FAILED));
    TEST_ASSERT_TRUE_MESSAGE(TestableRadioLibInterface::isRadioLibTimeErrorPublic(0), "the PhysicalLayer stub answers 0");

    TEST_ASSERT_FALSE(TestableRadioLibInterface::isRadioLibTimeErrorPublic(1));
    TEST_ASSERT_FALSE(TestableRadioLibInterface::isRadioLibTimeErrorPublic(123456));
    // ~229s: SF12 at 7.8kHz with a full 255-byte frame, the slowest packet that can be configured.
    TEST_ASSERT_FALSE(TestableRadioLibInterface::isRadioLibTimeErrorPublic(229ul * 1000ul * 1000ul));
}

// clearReadIrqs() is how every early out gives up on a received frame unread: the readout task's bad length and full
// ring, and the radio thread's bad length, unset region and failed readData(). The LR2021 receives into a FIFO that only
// RadioLib's readData() empties, so r113 found each such drop left the frame's bytes at the front of the FIFO, and the
// next frame was read out on top of them and failed to decode - 37 of 37 drops on an X1.
//
// Pinned: dropping a frame's flags also asks the chip to drop the frame. Regression guarded: a drop path that clears
// only the IRQs, which corrupts the following frame on an LR2021 and is invisible on every other chip.

static void test_clearReadIrqs_alsoDiscardsTheUnreadFrame()
{
    FakeIrqRadio phy;
    TestableRadioLibInterface radioIf(&phy);
    phy.irqFlags = (1UL << RADIOLIB_IRQ_RX_DONE);

    radioIf.clearReadIrqsPublic();

    TEST_ASSERT_EQUAL_UINT32(1, radioIf.discardedRxFrames);
}

// A dropped frame's HEADER_VALID is the same hazard one step earlier: left latched, it starts the stale-flag window,
// and the next frame's own HEADER_VALID lands on a bit already set, so that window is never restarted and its expiry
// re-arms RX over a live frame. Regression guarded: clearing only RX_DONE and CRC_ERR, which leaves the dead frame's
// header and preamble to time the next frame's window.
static void test_clearReadIrqs_retiresTheDeadFramesHeaderAndPreamble()
{
    FakeIrqRadio phy;
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->packetTimeMs = 100;
    const uint32_t rxDone = phy.getIrqMapped(1UL << RADIOLIB_IRQ_RX_DONE);
    const uint32_t valid = phy.getIrqMapped(1UL << RADIOLIB_IRQ_HEADER_VALID);
    const uint32_t preamble = phy.getIrqMapped(1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);

    // A frame arrived and nobody will read it out, so its flags are all still up
    Time::setTestMillis(1000);
    phy.irqFlags = rxDone | valid | preamble;
    radioIf->clearReadIrqsPublic();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, phy.irqFlags & (valid | preamble), "the dead frame's header must go with its RX_DONE");

    // Nothing is latched, so the poll has no window to start
    radioIf->checkStaleRxFlags();
    TEST_ASSERT_EQUAL_UINT32(0, radioIf->rxFlagsSeenMs);

    // The next frame's header starts its own window, timed from here and not from the dead frame
    Time::setTestMillis(1100);
    phy.irqFlags = valid | preamble;
    radioIf->checkStaleRxFlags();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1100, radioIf->rxFlagsSeenMs, "the live frame's header must time its own window");
    TEST_ASSERT_TRUE(radioIf->rxFlagsSeenValidHeader);

    // Still inside that window one max packet after the drop: the live frame is not re-armed over
    Time::setTestMillis(1150);
    radioIf->checkStaleRxFlags();
    TEST_ASSERT_EQUAL_UINT32(1100, radioIf->rxFlagsSeenMs);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(valid, phy.irqFlags & valid, "the live frame's header must still be latched");

    Time::useRealClock();
    delete radioIf;
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

// staleRxFlagAction() in src/mesh/RadioInterface.h: the decision behind RadioLibInterface::checkStaleRxFlags(),
// the plain-RX twin of checkCadHandoffTimeout(). Plain RX runs with no chip timeout, and the DIO mask carries
// RX_DONE only, so a PREAMBLE_DETECTED, HEADER_VALID or HEADER_ERR that never completes stays latched until
// something re-arms RX - on an idle node, never. RX_DONE clears every flag in readData(), so a flag still
// latched one max packet after it was first seen has no frame behind it.
//
// Pinned: nothing happens inside that window (a frame may still be arriving); after it, a bare preamble is
// only cleared, because a clear never aborts a reception; a valid header re-arms, because nothing short of a
// restart retires HEADER_VALID. A header error re-arms only where it wedges RX - SX1280 DS rev 3.3 16.2 leaves
// RX stuck after one with no RX_DONE or timeout to follow, and a clear does not unwedge it - and is otherwise
// cleared, because on every other chip the demodulator is already back hunting for a preamble.
//
// One header raises HEADER_VALID or HEADER_ERR and never both (SX1262 DS rev 2.1 table 13-29), so the two
// together are two frames: the error is the older one and must not decide anything about the valid header.
// Regressions guarded: re-arming on a preamble (startReceive() goes through standby, which aborts a frame
// that is in fact arriving), re-arming on a stale error while a good frame is on air, acting before the
// window closes, or never acting at all.

// errWedgesRx false unless a case is about the SX1280 wedge: every other chip is the common path.
static StaleRxFlagAction policy(bool validSeen, bool errSeen, uint32_t sinceMsec, uint32_t maxPacketMsec)
{
    return staleRxFlagAction(validSeen, errSeen, false, sinceMsec, maxPacketMsec);
}

static void test_staleRxFlagAction_keepsFlagsInsideTheWindow()
{
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::Keep), static_cast<int>(policy(false, false, 0, 99)));
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::Keep), static_cast<int>(policy(true, false, 98, 99)));
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::Keep), static_cast<int>(policy(false, true, 98, 99)));
}

static void test_staleRxFlagAction_windowEndsAtExactlyOneMaxPacket()
{
    // Inclusive at the boundary, matching Throttle::hasElapsed().
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::ClearPreamble), static_cast<int>(policy(false, false, 99, 99)));
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::Rearm), static_cast<int>(policy(true, false, 99, 99)));
}

static void test_staleRxFlagAction_barePreambleIsOnlyCleared()
{
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::ClearPreamble), static_cast<int>(policy(false, false, 2200, 2115)));
}

static void test_staleRxFlagAction_staleValidHeaderIsRearmed()
{
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::Rearm), static_cast<int>(policy(true, false, 60000, 2115)));
}

// The regression. A header error latches, no RX_DONE follows, and a good frame's HEADER_VALID lands beside it.
// Deciding on "a header is up" re-arms one max packet after the ERROR was first seen - a standby in the middle
// of the good frame. The error is the stale one, so it is retired and the valid header's own window decides.
static void test_staleRxFlagAction_staleHeaderErrBesideValidHeaderIsRetired()
{
    // Inside the valid header's window and past it: either way the error goes first and nothing re-arms on it.
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::ClearHeaderErr), static_cast<int>(policy(true, true, 10, 2115)));
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::ClearHeaderErr), static_cast<int>(policy(true, true, 60000, 2115)));
    // On a chip a header error wedges, the same pair means the opposite: RX could not have reached a later
    // header while wedged, so the error came second, the valid header's frame is dead, and only a re-arm
    // listens again. Guarded here: clearing the error there leaves the chip deaf until that window expires.
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(StaleRxFlagAction::Rearm),
                              static_cast<int>(staleRxFlagAction(true, true, true, 10, 2115)),
                              "a wedging chip must re-arm, not wait out the valid header's window");
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::Rearm),
                      static_cast<int>(staleRxFlagAction(true, true, true, 60000, 2115)));
}

// A lone stale header error: only the chips it wedges pay a re-arm's standby for it.
static void test_staleRxFlagAction_loneStaleHeaderErrRearmsOnlyWhereItWedgesRx()
{
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::ClearHeaderErr), static_cast<int>(policy(false, true, 60000, 2115)));
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::Rearm),
                      static_cast<int>(staleRxFlagAction(false, true, true, 60000, 2115)));
}

// checkStaleRxFlags() itself, which the cases above cannot see: a header that shows after a bare preamble gets its own
// max-packet window, timed from when the header showed. Regression guarded: timing the header from the earlier
// preamble's stamp, which re-arms RX - a standby that aborts the frame whose header has only just arrived.
// maxRxFrameMsec() and the preamble hold in src/mesh/RadioLibInterface.cpp: how long a sighted preamble keeps TX off
// the air. Neither PREAMBLE_DETECTED nor HEADER_VALID says when its frame ends - a foreign sync word never produces a
// header at all - so the hold runs for the longest frame that could be arriving.
//
// Pinned: that length is the 255-byte PHY frame at CR 4/8 with CRC, not our own configured CR, because the sender's
// header carries its CR and is unreadable when the hold starts. Regressions guarded: sizing the hold from
// getPacketTime(DATA_PAYLOAD_LEN + header), which is 249 bytes at our own CR and expires with a legal maximum frame
// still on air; and reading a RadioLib error code as a duration (#11935), which getPacketTime() then has to cover.

static void test_preambleHold_isSizedForTheWorstCaseFrame()
{
    FakeIrqRadio phy;
    phy.timeOnAirUsec = 158001; // 255 B at CR 4/8 with CRC, about SHORT_TURBO
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->packetTimeMs = 100; // what 249 B at our own CR would have given

    Time::setTestMillis(1000);
    TEST_ASSERT_TRUE(radioIf->receiveDetectedPublic(TestableRadioLibInterface::kFakePreambleDetected));
    // The first sighting short-circuits on the unset stamp, so the bound has to be asked for before the chip has been
    // asked anything at all.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(159, radioIf->maxRxFrameMsecPublic(), "part milliseconds round up");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(8, phy.lastCodingRate, "the hold must be timed at the worst case CR, not ours");
    TEST_ASSERT_TRUE_MESSAGE(phy.lastCrcEnabled, "a received frame may carry a CRC, which lengthens it");

    Time::setTestMillis(1100); // the old sizing released here, with the frame still arriving
    TEST_ASSERT_TRUE(radioIf->preambleHoldActivePublic());
    Time::setTestMillis(1158);
    TEST_ASSERT_TRUE(radioIf->preambleHoldActivePublic());
    Time::setTestMillis(1159);
    TEST_ASSERT_FALSE(radioIf->preambleHoldActivePublic());

    Time::useRealClock();
    delete radioIf;
}

static void test_preambleHold_fallsBackWhenTheRadioCannotTimeTheFrame()
{
    FakeIrqRadio phy; // answers 0 usec, which isRadioLibTimeError() reads as an error, not as no airtime
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->packetTimeMs = 120;
    radioIf->setModem(11, 250.0f, 8); // already the worst case, so nothing to scale

    TEST_ASSERT_EQUAL_UINT32(120, radioIf->maxRxFrameMsecPublic());

    delete radioIf;
}

static void test_preambleHold_fallbackIsScaledToTheWorstCaseCodingRate()
{
    // getPacketTime() times the frame at our own CR, so a configured rate below 4/8 would otherwise hold for less than
    // the worst case a sender may be transmitting. Regression guarded: returning the configured-CR figure unscaled.
    FakeIrqRadio phy; // 0 usec again, so the bound comes from the fallback
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->packetTimeMs = 120;
    radioIf->setModem(11, 250.0f, 5); // CR 4/5, the shortest we configure

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(192, radioIf->maxRxFrameMsecPublic(), "120 ms at CR 4/5 is 192 ms at CR 4/8");

    delete radioIf;
}

static void test_preambleHold_clearsTheFlagAndSurvivesIt()
{
    // The hold is a timestamp, not a flag: the look that starts it clears PREAMBLE_DETECTED so the next look can see a
    // fresh one, and the hold it started has to outlive that clear.
    FakeIrqRadio phy;
    phy.timeOnAirUsec = 50000;
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->packetTimeMs = 50;

    Time::setTestMillis(1000);
    phy.irqFlags = phy.getIrqMapped(1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);
    TEST_ASSERT_TRUE(radioIf->receiveDetectedPublic(TestableRadioLibInterface::kFakePreambleDetected));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, phy.irqFlags, "the look must clear the preamble it found");

    Time::setTestMillis(1020);
    TEST_ASSERT_TRUE_MESSAGE(radioIf->receiveDetectedPublic(0), "a look with no flags set must still see the hold");

    Time::useRealClock();
    delete radioIf;
}

// preambleHeaderGraceMsec() in src/mesh/RadioInterface.h and preambleHoldPeekable(): when the TX path may stop waiting
// out a bare-preamble hold and let its pre-TX CAD judge the channel instead. The grace is the rest of the preamble plus
// the ~12.25 symbols of sync word, SFD and explicit header, so once it has passed with no HEADER_VALID the sighting was
// noise or LoRa we cannot decode - and a hold is 159 ms at SHORT_TURBO but 14 s at LONG_SLOW, long enough to overflow
// the Tx queue.
//
// Pinned: nothing peeks inside the grace (our own header may still be coming), a header ends the peek for the whole
// hold, and the grace is never zero. Regressions guarded: deriving the grace from RadioInterface::preambleTimeMsec,
// whole milliseconds that truncate to 0 above 2 GHz where a 12-symbol preamble is 0.95 ms - every sighting then
// peekable the moment it was seen, which is the one case the hold exists for.

static void test_preambleHeaderGrace_wideLoraIsNeverZero()
{
    // SHORT_TURBO at 2.4 GHz: SF7, 1625 kHz, 12-symbol preamble. A symbol is 79 us and the whole preamble 0.95 ms.
    TEST_ASSERT_EQUAL_UINT32(3, preambleHeaderGraceMsec(7, 1625.0f, 12));
    // The shortest preamble we can be configured for stays above zero too.
    TEST_ASSERT_EQUAL_UINT32(1, preambleHeaderGraceMsec(5, 1625.0f, 12));
}

static void test_preambleHeaderGrace_roundsUp()
{
    // SHORT_FAST at 2.4 GHz: 4.14 ms of symbols. Truncating the preamble to 1 ms first gave 3.
    TEST_ASSERT_EQUAL_UINT32(5, preambleHeaderGraceMsec(7, 812.5f, 12));
}

static void test_preambleHeaderGrace_subGhzPresets()
{
    TEST_ASSERT_EQUAL_UINT32(8, preambleHeaderGraceMsec(7, 500.0f, 16));    // SHORT_TURBO
    TEST_ASSERT_EQUAL_UINT32(248, preambleHeaderGraceMsec(11, 250.0f, 16)); // LONG_FAST
    TEST_ASSERT_EQUAL_UINT32(992, preambleHeaderGraceMsec(12, 125.0f, 16)); // LONG_SLOW
}

// SHORT_TURBO sub-GHz: a 16-symbol preamble, so a grace of 8 ms inside a hold of 159 ms.
static TestableRadioLibInterface *makeHoldingRadio(FakeIrqRadio &phy)
{
    phy.timeOnAirUsec = 159000;
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->setModem(7, 500.0f, 5);
    radioIf->setPreambleLength(16);
    radioIf->packetTimeMs = 159;
    return radioIf;
}

static void test_preambleHoldPeekable_onlyOnceTheHeaderWasDue()
{
    FakeIrqRadio phy;
    auto *radioIf = makeHoldingRadio(phy);

    Time::setTestMillis(1000);
    TEST_ASSERT_FALSE_MESSAGE(radioIf->preambleHoldPeekablePublic(), "nothing has been sighted yet");
    TEST_ASSERT_TRUE(radioIf->receiveDetectedPublic(TestableRadioLibInterface::kFakePreambleDetected));

    Time::setTestMillis(1007);
    TEST_ASSERT_FALSE_MESSAGE(radioIf->preambleHoldPeekablePublic(), "a frame we can decode may still show its header");
    Time::setTestMillis(1008);
    TEST_ASSERT_TRUE_MESSAGE(radioIf->preambleHoldPeekablePublic(), "past the header time, the CAD should settle it");

    Time::setTestMillis(1159);
    TEST_ASSERT_FALSE_MESSAGE(radioIf->preambleHoldPeekablePublic(),
                              "the hold itself has ended, so there is nothing to peek past");

    Time::useRealClock();
    delete radioIf;
}

static void test_preambleHoldPeekable_aHeaderEndsThePeekForTheWholeHold()
{
    // HEADER_VALID means a real frame on a modem we share: wait it out rather than keying up over it.
    FakeIrqRadio phy;
    auto *radioIf = makeHoldingRadio(phy);

    Time::setTestMillis(1000);
    TEST_ASSERT_TRUE(radioIf->receiveDetectedPublic(TestableRadioLibInterface::kFakePreambleDetected));
    Time::setTestMillis(1020);
    TEST_ASSERT_TRUE(radioIf->receiveDetectedPublic(TestableRadioLibInterface::kFakeHeaderValid));
    TEST_ASSERT_FALSE(radioIf->preambleHoldPeekablePublic());

    Time::setTestMillis(1100);
    TEST_ASSERT_TRUE_MESSAGE(radioIf->preambleHoldActivePublic(), "the hold still runs");
    TEST_ASSERT_FALSE_MESSAGE(radioIf->preambleHoldPeekablePublic(), "and stays unpeekable for the rest of it");

    Time::useRealClock();
    delete radioIf;
}

static void test_preambleHoldPeekable_aNewHoldForgetsTheOldHeader()
{
    // The next sighting is its own frame: a header from the hold before it must not keep the CAD out.
    FakeIrqRadio phy;
    auto *radioIf = makeHoldingRadio(phy);

    Time::setTestMillis(1000);
    TEST_ASSERT_TRUE(radioIf->receiveDetectedPublic(TestableRadioLibInterface::kFakePreambleDetected));
    Time::setTestMillis(1020);
    TEST_ASSERT_TRUE(radioIf->receiveDetectedPublic(TestableRadioLibInterface::kFakeHeaderValid));
    TEST_ASSERT_FALSE(radioIf->preambleHoldPeekablePublic());

    Time::setTestMillis(1159); // that hold, and the frame it stood for, are over
    TEST_ASSERT_FALSE(radioIf->preambleHoldActivePublic());
    Time::setTestMillis(1160);
    TEST_ASSERT_TRUE(radioIf->receiveDetectedPublic(TestableRadioLibInterface::kFakePreambleDetected));

    Time::setTestMillis(1168);
    TEST_ASSERT_TRUE_MESSAGE(radioIf->preambleHoldPeekablePublic(), "the new sighting is judged on its own");

    Time::useRealClock();
    delete radioIf;
}

static void test_checkStaleRxFlags_headerAfterPreambleRestartsTheWindow()
{
    FakeIrqRadio phy;
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->packetTimeMs = 100;

    Time::setTestMillis(1000);
    phy.irqFlags = phy.getIrqMapped(1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);
    radioIf->checkStaleRxFlags();
    TEST_ASSERT_EQUAL_UINT32(1000, radioIf->rxFlagsSeenMs);
    TEST_ASSERT_FALSE(radioIf->rxFlagsSeenHeader);

    // The header shows inside the preamble's window: its own window starts here
    Time::setTestMillis(1090);
    phy.irqFlags |= phy.getIrqMapped(1UL << RADIOLIB_IRQ_HEADER_VALID);
    radioIf->checkStaleRxFlags();
    TEST_ASSERT_EQUAL_UINT32(1090, radioIf->rxFlagsSeenMs);
    TEST_ASSERT_TRUE(radioIf->rxFlagsSeenHeader);

    // Past one max packet from the preamble, not from the header: nothing happens
    Time::setTestMillis(1150);
    radioIf->checkStaleRxFlags();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1090, radioIf->rxFlagsSeenMs, "the header must be timed from when it showed");
    TEST_ASSERT_TRUE(radioIf->rxFlagsSeenHeader);

    Time::useRealClock();
    delete radioIf;
}

// The regression, end to end through the poll. A header fails on noise or a collision, HEADER_ERR latches and
// nothing clears it. A good frame then arrives and raises HEADER_VALID beside it. Timing a header from the
// error's stamp re-arms RX one max packet later, and startReceive()'s standby aborts the frame being received.
// Guarded here: the valid header gets its own window, and the error is retired instead of deciding anything.
static void test_checkStaleRxFlags_validHeaderAfterHeaderErrRestartsTheWindow()
{
    FakeIrqRadio phy;
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->packetTimeMs = 100;

    // A failed header, seen by the poll: this is where develop starts the clock that goes on to abort the frame
    Time::setTestMillis(1000);
    phy.irqFlags = phy.getIrqMapped(1UL << RADIOLIB_IRQ_HEADER_ERR);
    radioIf->checkStaleRxFlags();
    TEST_ASSERT_EQUAL_UINT32(1000, radioIf->rxFlagsSeenMs);
    TEST_ASSERT_TRUE(radioIf->rxFlagsSeenHeader);
    TEST_ASSERT_FALSE(radioIf->rxFlagsSeenValidHeader);

    // A good frame's header lands beside the stale error: its own window starts here
    Time::setTestMillis(1090);
    phy.irqFlags |= phy.getIrqMapped(1UL << RADIOLIB_IRQ_HEADER_VALID);
    radioIf->checkStaleRxFlags();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1090, radioIf->rxFlagsSeenMs, "a valid header must restart a failed header's window");
    TEST_ASSERT_TRUE(radioIf->rxFlagsSeenValidHeader);

    // Past one max packet from the error, inside the valid header's window: the error goes, the window stays
    Time::setTestMillis(1150);
    radioIf->checkStaleRxFlags();
    TEST_ASSERT_EQUAL_UINT32(phy.getIrqMapped(1UL << RADIOLIB_IRQ_HEADER_ERR), phy.clearedIrqFlags);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1090, radioIf->rxFlagsSeenMs, "retiring the error must not restart the window");
    TEST_ASSERT_TRUE(radioIf->rxFlagsSeenValidHeader);

    Time::useRealClock();
    delete radioIf;
}

// A failed header latches beside its own preamble, and both are the dead frame's. Retiring only the error
// leaves the preamble for the next send's receiveDetected(), which holds TX a max packet over nothing - a wait
// the re-arm this replaces never cost, since its standby cleared the whole flag register.
static void test_checkStaleRxFlags_loneStaleHeaderErrRetiresItsPreambleToo()
{
    FakeIrqRadio phy;
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->packetTimeMs = 100;

    const uint32_t preamble = phy.getIrqMapped(1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);
    const uint32_t headerErr = phy.getIrqMapped(1UL << RADIOLIB_IRQ_HEADER_ERR);

    Time::setTestMillis(1000);
    phy.irqFlags = preamble | headerErr;
    radioIf->checkStaleRxFlags();
    TEST_ASSERT_EQUAL_UINT32(1000, radioIf->rxFlagsSeenMs);
    TEST_ASSERT_TRUE(radioIf->rxFlagsSeenHeader);
    TEST_ASSERT_FALSE(radioIf->rxFlagsSeenValidHeader);

    // Past one max packet with no valid header: the whole failed frame is retired, without a standby
    Time::setTestMillis(1150);
    radioIf->checkStaleRxFlags();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(preamble | headerErr, phy.clearedIrqFlags, "the dead frame's preamble must go too");
    TEST_ASSERT_EQUAL_UINT32(0, phy.irqFlags);
    TEST_ASSERT_EQUAL_UINT32(0, radioIf->rxFlagsSeenMs);
    TEST_ASSERT_FALSE(radioIf->rxFlagsSeenHeader);

    Time::useRealClock();
    delete radioIf;
}

// cadHandoffRxEnded() in src/mesh/RadioLibInterface.cpp: whether a CAD->RX handoff's RX has ended without a frame, so
// the radio thread must end the handoff and re-arm. The readout task used to clear a damaged header's CRC_ERR or
// HEADER_ERR and return, which left the handoff open and the chip possibly in standby until checkCadHandoffTimeout()
// re-armed it up to a second later ("CAD>RX timeout" on an LR2021). Guards that every frameless end counts, and that
// nothing counts outside a handoff, where plain RX leaves HEADER_ERR to checkStaleRxFlags().
static void test_cadHandoffRxEnded_everyFramelessEndCounts()
{
    FakeIrqRadio phy;
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->cadHandoffRxStart = 1000;

    const uint32_t ends[] = {RADIOLIB_IRQ_TIMEOUT, RADIOLIB_IRQ_CRC_ERR, RADIOLIB_IRQ_HEADER_ERR};
    for (uint32_t irq : ends) {
        phy.irqFlags = phy.getIrqMapped(1UL << irq);
        TEST_ASSERT_TRUE_MESSAGE(radioIf->cadHandoffRxEnded(), "a frameless end must close the handoff");
    }

    delete radioIf;
}

static void test_cadHandoffRxEnded_anRxStillRunningDoesNotCount()
{
    FakeIrqRadio phy;
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->cadHandoffRxStart = 1000;

    phy.irqFlags = 0;
    TEST_ASSERT_FALSE(radioIf->cadHandoffRxEnded());
    phy.irqFlags = phy.getIrqMapped((1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED) | (1UL << RADIOLIB_IRQ_HEADER_VALID));
    TEST_ASSERT_FALSE_MESSAGE(radioIf->cadHandoffRxEnded(), "a frame still arriving must be left to finish");

    delete radioIf;
}

static void test_cadHandoffRxEnded_falseWithoutAHandoff()
{
    FakeIrqRadio phy;
    auto *radioIf = new TestableRadioLibInterface(&phy);
    radioIf->cadHandoffRxStart = 0;

    phy.irqFlags = phy.getIrqMapped((1UL << RADIOLIB_IRQ_HEADER_ERR) | (1UL << RADIOLIB_IRQ_TIMEOUT));
    TEST_ASSERT_FALSE_MESSAGE(radioIf->cadHandoffRxEnded(), "plain RX's flags are not a handoff's end");

    delete radioIf;
}

void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_lr20x0BandClassification);
    RUN_TEST(test_lr20x0CustomLfPaBandCoversOnly500To1000MHz);
    RUN_TEST(test_lr20x0BandHopDetection);
    RUN_TEST(test_lr20x0ReconfigurePathSelection);
    RUN_TEST(test_bwCodeToKHz_specialMappings);
    RUN_TEST(test_bwCodeToKHz_passthrough);
    RUN_TEST(test_bwCodeToKHz_roundTrip);
    RUN_TEST(test_validateConfigLora_noopWhenUsePresetFalse);
    RUN_TEST(test_validateConfigLora_validPreset_nonWideRegion);
    RUN_TEST(test_validateConfigLora_validPreset_wideRegion);
    RUN_TEST(test_validateConfigLora_rejectsInvalidPresetForRegion);
    RUN_TEST(test_clampConfigLora_invalidPresetClampedToDefault);
    RUN_TEST(test_clampConfigLora_validPresetUnchanged);
    RUN_TEST(test_applyModemConfig_freshFlashCodingRateNotZero);
    RUN_TEST(test_applyModemConfig_codingRateMatchesPreset);
    RUN_TEST(test_applyModemConfig_customCodingRateHigherThanPreset);
    RUN_TEST(test_applyModemConfig_customCodingRateLowerThanPreset);
    RUN_TEST(test_applyModemConfig_mediumTurbo);
    RUN_TEST(test_clampConfigLora_mediumTurboInvalidForEU868);
    RUN_TEST(test_clampConfigLora_mediumTurboValidForUS);
    RUN_TEST(test_regionPresetMap_coversAllRegionsWithinBounds);
    RUN_TEST(test_regionPresetMap_matchesRegionTable);
    RUN_TEST(test_regionPresetMap_unsetCarriesUserprefsIntent);
    RUN_TEST(test_beginSending_oversizedPayloadIsClamped);
    RUN_TEST(test_beginSending_fittingPayloadIsSentWhole);
    RUN_TEST(test_computePacketTime_txUsesTheRadiosOwnAnswer);
    RUN_TEST(test_computePacketTime_txFallsBackWhenTheRadioReportsAnError);
    RUN_TEST(test_computePacketTime_reportsNoAirtimeWhenNothingCanBeComputed);
    RUN_TEST(test_computePacketTime_rxUsesHeaderInfoAndIsGuarded);
    RUN_TEST(test_computePacketTime_rxWithoutHeaderInfoUsesConfiguredModem);
    RUN_TEST(test_isRadioLibTimeError_separatesCodesFromDurations);
    RUN_TEST(test_clearReadIrqs_alsoDiscardsTheUnreadFrame);
    RUN_TEST(test_clearReadIrqs_retiresTheDeadFramesHeaderAndPreamble);
    RUN_TEST(test_staleRxFlagAction_keepsFlagsInsideTheWindow);
    RUN_TEST(test_staleRxFlagAction_windowEndsAtExactlyOneMaxPacket);
    RUN_TEST(test_staleRxFlagAction_barePreambleIsOnlyCleared);
    RUN_TEST(test_staleRxFlagAction_staleValidHeaderIsRearmed);
    RUN_TEST(test_staleRxFlagAction_staleHeaderErrBesideValidHeaderIsRetired);
    RUN_TEST(test_staleRxFlagAction_loneStaleHeaderErrRearmsOnlyWhereItWedgesRx);
    RUN_TEST(test_checkStaleRxFlags_headerAfterPreambleRestartsTheWindow);
    RUN_TEST(test_checkStaleRxFlags_validHeaderAfterHeaderErrRestartsTheWindow);
    RUN_TEST(test_checkStaleRxFlags_loneStaleHeaderErrRetiresItsPreambleToo);
    RUN_TEST(test_cadHandoffRxEnded_everyFramelessEndCounts);
    RUN_TEST(test_cadHandoffRxEnded_anRxStillRunningDoesNotCount);
    RUN_TEST(test_cadHandoffRxEnded_falseWithoutAHandoff);
    RUN_TEST(test_preambleHold_isSizedForTheWorstCaseFrame);
    RUN_TEST(test_preambleHold_fallsBackWhenTheRadioCannotTimeTheFrame);
    RUN_TEST(test_preambleHold_fallbackIsScaledToTheWorstCaseCodingRate);
    RUN_TEST(test_preambleHold_clearsTheFlagAndSurvivesIt);
    RUN_TEST(test_preambleHeaderGrace_wideLoraIsNeverZero);
    RUN_TEST(test_preambleHeaderGrace_roundsUp);
    RUN_TEST(test_preambleHeaderGrace_subGhzPresets);
    RUN_TEST(test_preambleHoldPeekable_onlyOnceTheHeaderWasDue);
    RUN_TEST(test_preambleHoldPeekable_aHeaderEndsThePeekForTheWholeHold);
    RUN_TEST(test_preambleHoldPeekable_aNewHoldForgetsTheOldHeader);
    exit(UNITY_END());
}

void loop() {}
