#include "LR20x0Band.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "RadioInterface.h"
#include "RadioLibInterface.h"
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
    uint8_t lastCodingRate = 0;
    bool lastCrcEnabled = false;

    RadioLibTime_t getTimeOnAir(size_t) { return reportedTimeOnAir; }

    RadioLibTime_t calculateTimeOnAir(ModemType_t, DataRate_t dr, PacketConfig_t pc, size_t)
    {
        calculateCalls++;
        lastCodingRate = dr.lora.codingRate;
        lastCrcEnabled = pc.lora.crcEnabled;
        return calculatedTimeOnAir;
    }

    int16_t getLoRaRxHeaderInfo(uint8_t *cr, bool *crc)
    {
        if (headerInfoResult == RADIOLIB_ERR_NONE) {
            *cr = headerCodingRate;
            *crc = headerCrcEnabled;
        }
        return headerInfoResult;
    }
};

// Test shim: no chip, just the modem parameters computePacketTime() reads.
class TestableRadioLibInterface : public RadioLibInterface
{
  public:
    TestableRadioLibInterface() : RadioLibInterface(nullptr, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC) {}

    void setModem(uint8_t spreadFactor, float bandwidth, uint8_t codingRate)
    {
        sf = spreadFactor;
        bw = bandwidth;
        cr = codingRate;
    }

    uint32_t computePacketTimePublic(FakeLoraRadio &radio, uint32_t pl, bool received)
    {
        return computePacketTime(radio, pl, received);
    }

    static bool isRadioLibTimeErrorPublic(RadioLibTime_t usec) { return isRadioLibTimeError(usec); }

    // Chip-specific hooks this test never reaches
    uint32_t getPacketTime(uint32_t, bool) override { return 0; }
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

// shouldDeferPreambleVerdict(): whether a "that preamble was false" verdict has actually observed the
// window it is judging. Measured on a portduino host with a CH341 USB-SPI bridge at ShortTurbo, the
// deadline is 8 ms and the interval between reads of the IRQ flags ran 8-96 ms, median 60 - so 7 of 8
// false-preamble verdicts were reached by a read that arrived later than the entire deadline, and the
// node then transmitted over what may have been a real packet. The boundaries below are the whole
// decision, so they are pinned here rather than left to the call site.

static void test_preambleVerdict_trustedWhenLooksAreFasterThanTheDeadline()
{
    // The healthy case, and the one that must not change: a loop reading every 2 ms against an 8 ms
    // deadline has seen the window, so its verdict stands and the packet is declared false at once.
    TEST_ASSERT_FALSE(shouldDeferPreambleVerdict(2, 8, 9, 99));
    TEST_ASSERT_FALSE(shouldDeferPreambleVerdict(7, 8, 20, 99));
}

static void test_preambleVerdict_deferredWhenTheWindowWasNeverObserved()
{
    // The measured case: an 8 ms deadline judged by a read 60 ms after the previous one. The flag
    // would have read as absent either way, so wait for a read that can answer the question.
    TEST_ASSERT_TRUE(shouldDeferPreambleVerdict(60, 8, 60, 99));
    TEST_ASSERT_TRUE(shouldDeferPreambleVerdict(96, 8, 96, 99));
    // LongFast: a 262 ms deadline judged by a read 490 ms late.
    TEST_ASSERT_TRUE(shouldDeferPreambleVerdict(490, 262, 490, 2157));
}

static void test_preambleVerdict_boundariesAreExclusiveOnBothSides()
{
    // A read exactly as old as the deadline counts as having seen the window: the deadline test in
    // Throttle::isWithinTimespanMs() is an exclusive <, so at equality the window has just closed.
    TEST_ASSERT_FALSE(shouldDeferPreambleVerdict(8, 8, 20, 99));
    TEST_ASSERT_TRUE(shouldDeferPreambleVerdict(9, 8, 20, 99));
    // The wait ends exactly at the maximum packet time, matching the same exclusive <.
    TEST_ASSERT_TRUE(shouldDeferPreambleVerdict(60, 8, 98, 99));
    TEST_ASSERT_FALSE(shouldDeferPreambleVerdict(60, 8, 99, 99));
}

static void test_preambleVerdict_boundedSoAStalledLoopCannotHoldTheChannel()
{
    // Past a maximum-length packet nothing we could collide with is still in the air. A loop that has
    // stopped looking altogether must not be able to defer a transmission forever, however stale its
    // last read is.
    TEST_ASSERT_FALSE(shouldDeferPreambleVerdict(5000, 8, 150, 99));
    TEST_ASSERT_FALSE(shouldDeferPreambleVerdict(5000, 8, 5000, 99));
}

static void test_preambleVerdict_firstLookIsNotTreatedAsStale()
{
    // The caller passes 0 before anything has read the flags, which must not read as "infinitely long
    // ago" and defer every verdict on a freshly started radio.
    TEST_ASSERT_FALSE(shouldDeferPreambleVerdict(0, 8, 20, 99));
}

// staleRxFlagAction() in src/mesh/RadioInterface.h: the decision behind RadioLibInterface::checkStaleRxFlags(),
// the plain-RX twin of checkCadHandoffTimeout(). Plain RX runs with no chip timeout, and the DIO mask carries
// RX_DONE only, so a PREAMBLE_DETECTED, HEADER_VALID or HEADER_ERR that never completes stays latched until
// something re-arms RX - on an idle node, never. RX_DONE clears every flag in readData(), so a flag still
// latched one max packet after it was first seen has no frame behind it.
//
// Pinned: nothing happens inside that window (a frame may still be arriving); after it, a bare preamble is
// only cleared, because a clear never aborts a reception; a header re-arms, because SX1280 DS rev 3.3 16.2
// leaves RX wedged after a header error with no RX_DONE or timeout to follow, and a clear does not unwedge it.
// Regressions guarded: re-arming on a preamble (startReceive() goes through standby, which aborts a frame
// that is in fact arriving), acting before the window closes, or never acting at all.

static void test_staleRxFlagAction_keepsFlagsInsideTheWindow()
{
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::Keep), static_cast<int>(staleRxFlagAction(false, 0, 99)));
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::Keep), static_cast<int>(staleRxFlagAction(true, 98, 99)));
}

static void test_staleRxFlagAction_windowEndsAtExactlyOneMaxPacket()
{
    // Inclusive at the boundary, matching Throttle::hasElapsed().
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::ClearPreamble), static_cast<int>(staleRxFlagAction(false, 99, 99)));
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::Rearm), static_cast<int>(staleRxFlagAction(true, 99, 99)));
}

static void test_staleRxFlagAction_barePreambleIsOnlyCleared()
{
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::ClearPreamble), static_cast<int>(staleRxFlagAction(false, 2200, 2115)));
}

static void test_staleRxFlagAction_staleHeaderIsRearmed()
{
    TEST_ASSERT_EQUAL(static_cast<int>(StaleRxFlagAction::Rearm), static_cast<int>(staleRxFlagAction(true, 60000, 2115)));
}

// RxSighting::observe() in src/mesh/RadioLibInterface.h - the "may a frame be on air right
// now?" answer that RadioLibInterface::receiveDetected() gives the TX path for the SX126x, SX128x,
// LR11x0 and LR20x0 drivers.
//
// The radio's PREAMBLE_DETECTED and HEADER_VALID flags are latched status bits, never routed to DIO,
// and nothing reads them on a schedule: a look comes once per CSMA backoff, and from the noise-floor
// and AGC paths. receiveDetected() clears PREAMBLE_DETECTED at every look that finds it, so a set bit
// always means a detection since the previous look. Once cleared, the chip stays locked on that frame
// and never raises the bit for it again, so the stamp is the only record that a frame started. Neither
// flag says when the frame ends - a foreign sync word never produces HEADER_VALID at all - so each
// sighting holds TX for one max-length packet from the look that found it. Only RX_DONE, CRC_ERR or
// HEADER_ERR end a hold early, through the standby that reset() mirrors.
//
// Regressions guarded (#11933):
//  - The old code released a bare preamble 2 * preambleTimeMsec after the first look that saw it
//    (8 ms at SF7/BW500, 0-2 ms on 2.4 GHz presets), and TX went out over a frame still on air.
//  - A noise preamble left latched hid a real one that started after it. Here the noise sighting is
//    cleared, the real preamble latches again, and the hold restarts from the look that finds it.
//  - A HEADER_VALID left latched by a missed RX interrupt must not hold TX for more than one
//    max-length packet, and must not re-arm a fresh hold at every later look.

// A max-length packet at SHORT_TURBO sub-GHz (SF7/BW500) is about 102 ms.
static constexpr uint32_t kRxMaxPacketMs = 102;

static void test_rxSighting_quietChannelNeverHolds()
{
    RxSighting s;
    TEST_ASSERT_FALSE(s.observe(1000, false, false, kRxMaxPacketMs));
    TEST_ASSERT_FALSE(s.observe(1001, false, false, kRxMaxPacketMs));
    TEST_ASSERT_EQUAL_UINT32(1001, s.lastPeek());
    TEST_ASSERT_EQUAL_UINT32(0, s.preambleSeen());
}

static void test_rxSighting_barePreamble_holdsOneMaxPacket()
{
    // No header ever arrives - the foreign-sync-word case - so the hold runs its full length.
    RxSighting s;
    TEST_ASSERT_TRUE(s.observe(1000, true, false, kRxMaxPacketMs));
    TEST_ASSERT_TRUE(s.observe(1060, false, false, kRxMaxPacketMs));
    TEST_ASSERT_TRUE(s.observe(1000 + kRxMaxPacketMs - 1, false, false, kRxMaxPacketMs));
    TEST_ASSERT_FALSE(s.observe(1000 + kRxMaxPacketMs, false, false, kRxMaxPacketMs));
    TEST_ASSERT_EQUAL_UINT32(0, s.preambleSeen());
}

static void test_rxSighting_retrigger_restartsTheHold()
{
    // Noise at 1000, then a real preamble at 1050 that is only visible because the noise latch was
    // cleared. Its hold runs from the look at 1055, not from the noise.
    RxSighting s;
    TEST_ASSERT_TRUE(s.observe(1000, true, false, kRxMaxPacketMs));
    TEST_ASSERT_TRUE(s.observe(1055, true, false, kRxMaxPacketMs));
    TEST_ASSERT_TRUE(s.observe(1000 + kRxMaxPacketMs, false, false, kRxMaxPacketMs));
    TEST_ASSERT_TRUE(s.observe(1055 + kRxMaxPacketMs - 1, false, false, kRxMaxPacketMs));
    TEST_ASSERT_FALSE(s.observe(1055 + kRxMaxPacketMs, false, false, kRxMaxPacketMs));
}

static void test_rxSighting_headerAfterPreamble_extendsTheHold()
{
    RxSighting s;
    TEST_ASSERT_TRUE(s.observe(1000, true, false, kRxMaxPacketMs));
    TEST_ASSERT_TRUE(s.observe(1040, false, true, kRxMaxPacketMs));
    TEST_ASSERT_TRUE(s.observe(1040 + kRxMaxPacketMs - 1, false, true, kRxMaxPacketMs));
    TEST_ASSERT_FALSE(s.observe(1040 + kRxMaxPacketMs, false, true, kRxMaxPacketMs));
}

static void test_rxSighting_headerClearedByPoll_stillHolds()
{
    // LORA_DIO1_SOFTWARE_POLL clears HEADER_VALID after its look, so later looks see no flags while
    // the frame is still on air. The first sighting must carry the hold.
    RxSighting s;
    TEST_ASSERT_TRUE(s.observe(1000, false, true, kRxMaxPacketMs));
    TEST_ASSERT_TRUE(s.observe(1050, false, false, kRxMaxPacketMs));
    TEST_ASSERT_FALSE(s.observe(1000 + kRxMaxPacketMs, false, false, kRxMaxPacketMs));
}

static void test_rxSighting_stuckHeader_expiresAndDoesNotRearm()
{
    RxSighting s;
    TEST_ASSERT_TRUE(s.observe(1000, false, true, kRxMaxPacketMs));
    TEST_ASSERT_FALSE(s.observe(1000 + kRxMaxPacketMs, false, true, kRxMaxPacketMs));
    // The old code restarted its timer here and held TX for another whole packet.
    TEST_ASSERT_FALSE(s.observe(1000 + 3 * kRxMaxPacketMs, false, true, kRxMaxPacketMs));
}

static void test_rxSighting_freshPreambleDuringStuckHeader_holds()
{
    // A stale header must not mask a new preamble: its own hold still applies.
    RxSighting s;
    s.observe(1000, false, true, kRxMaxPacketMs);
    TEST_ASSERT_FALSE(s.observe(1200, false, true, kRxMaxPacketMs));
    TEST_ASSERT_TRUE(s.observe(1300, true, true, kRxMaxPacketMs));
    TEST_ASSERT_FALSE(s.observe(1300 + kRxMaxPacketMs, false, true, kRxMaxPacketMs));
}

static void test_rxSighting_resetEndsEveryHold()
{
    // RX_DONE, CRC_ERR and HEADER_ERR restart RX through standby: the frame is over, TX may go.
    RxSighting s;
    s.observe(1000, true, true, kRxMaxPacketMs);
    s.reset();
    TEST_ASSERT_FALSE(s.observe(1001, false, false, kRxMaxPacketMs));
    TEST_ASSERT_EQUAL_UINT32(0, s.headerSeen());
    TEST_ASSERT_TRUE(s.observe(1002, false, true, kRxMaxPacketMs));
}

static void test_rxSighting_millisWrap_andZeroAreHandled()
{
    // A sighting at millis() 0 must still count (skipZero), and elapsed time is wrap-safe.
    RxSighting s;
    TEST_ASSERT_TRUE(s.observe(0, true, false, kRxMaxPacketMs));
    TEST_ASSERT_NOT_EQUAL(0, s.preambleSeen());
    s.reset();
    TEST_ASSERT_TRUE(s.observe(UINT32_MAX - 2, true, false, kRxMaxPacketMs));
    TEST_ASSERT_TRUE(s.observe(50, false, false, kRxMaxPacketMs));
    TEST_ASSERT_FALSE(s.observe(kRxMaxPacketMs - 3, false, false, kRxMaxPacketMs));
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
    RUN_TEST(test_preambleVerdict_trustedWhenLooksAreFasterThanTheDeadline);
    RUN_TEST(test_preambleVerdict_deferredWhenTheWindowWasNeverObserved);
    RUN_TEST(test_preambleVerdict_boundariesAreExclusiveOnBothSides);
    RUN_TEST(test_preambleVerdict_boundedSoAStalledLoopCannotHoldTheChannel);
    RUN_TEST(test_preambleVerdict_firstLookIsNotTreatedAsStale);
    RUN_TEST(test_rxSighting_quietChannelNeverHolds);
    RUN_TEST(test_rxSighting_barePreamble_holdsOneMaxPacket);
    RUN_TEST(test_rxSighting_retrigger_restartsTheHold);
    RUN_TEST(test_rxSighting_headerAfterPreamble_extendsTheHold);
    RUN_TEST(test_rxSighting_headerClearedByPoll_stillHolds);
    RUN_TEST(test_rxSighting_stuckHeader_expiresAndDoesNotRearm);
    RUN_TEST(test_rxSighting_freshPreambleDuringStuckHeader_holds);
    RUN_TEST(test_rxSighting_resetEndsEveryHold);
    RUN_TEST(test_rxSighting_millisWrap_andZeroAreHandled);
    RUN_TEST(test_computePacketTime_txUsesTheRadiosOwnAnswer);
    RUN_TEST(test_computePacketTime_txFallsBackWhenTheRadioReportsAnError);
    RUN_TEST(test_computePacketTime_reportsNoAirtimeWhenNothingCanBeComputed);
    RUN_TEST(test_computePacketTime_rxUsesHeaderInfoAndIsGuarded);
    RUN_TEST(test_isRadioLibTimeError_separatesCodesFromDurations);
    RUN_TEST(test_staleRxFlagAction_keepsFlagsInsideTheWindow);
    RUN_TEST(test_staleRxFlagAction_windowEndsAtExactlyOneMaxPacket);
    RUN_TEST(test_staleRxFlagAction_barePreambleIsOnlyCleared);
    RUN_TEST(test_staleRxFlagAction_staleHeaderIsRearmed);
    exit(UNITY_END());
}

void loop() {}
