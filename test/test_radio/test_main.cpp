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
// window it is judging. At SF7/BW500 the deadline is 8 ms, and measured on a two-node bench the
// interval between reads of the IRQ flags ran 8-96 ms, median 60 - so 7 of 8 false-preamble verdicts
// were reached by a read that arrived later than the entire deadline, and the node then transmitted
// over what may have been a real packet. That interval is a CSMA backoff rather than a poll, so it is
// not specific to any one bus or scheduler. The boundaries below are the whole decision, so they are
// pinned here rather than left to the call site.

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
    exit(UNITY_END());
}

void loop() {}
