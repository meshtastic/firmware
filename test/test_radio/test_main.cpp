#include "MeshRadio.h"
#include "MeshService.h"
#include "RadioInterface.h"
#include "RadioLibInterface.h"
#include "TestUtil.h"
#include "airtime.h"
#include <unity.h>

#include "meshtastic/config.pb.h"
#include "support/MockMeshService.h"

static MockMeshService *mockMeshService;
static AirTime *testAirTime;

// Test shim to expose protected radio parameters set by applyModemConfig()
class TestableRadioInterface : public RadioInterface
{
  public:
    TestableRadioInterface() : RadioInterface() {}
    uint8_t getCr() const { return cr; }
    uint8_t getSf() const { return sf; }
    float getBw() const { return bw; }

    // Override reconfigure to call the base which invokes applyModemConfig().
    bool reconfigure() override
    {
        RadioInterface::reconfigure();
        return reconfigureResults[reconfigureCount++];
    }

    void scriptApply(bool applyResult, bool rollbackResult)
    {
        reconfigureResults[0] = applyResult;
        reconfigureResults[1] = rollbackResult;
        reconfigureCount = 0;
    }

    // Stubs for pure virtual methods required by RadioInterface
    uint32_t getPacketTime(uint32_t, bool) override { return 0; }
    ErrorCode send(meshtastic_MeshPacket *p) override { return ERRNO_OK; }

  private:
    bool reconfigureResults[2] = {true, true};
    size_t reconfigureCount = 0;
};

class TestableRadioLibInterface : public RadioLibInterface
{
  public:
    TestableRadioLibInterface() : RadioLibInterface(nullptr, 0, 0, 0, 0) {}

    void setSendingForTest(bool sending) { sendingPacket = sending ? &inFlightPacket : nullptr; }

    void finishTxForTest() { sendingPacket = nullptr; }
    bool sendingForTest() const { return sendingPacket != nullptr; }

    ErrorCode enqueuePacketForTest(meshtastic_MeshPacket *packet) { return send(packet); }

    uint32_t queuedPacketCount()
    {
        const auto status = static_cast<RadioInterface *>(this)->getQueueStatus();
        return status.maxlen - status.free;
    }

    void fireTransmitDelayForTest()
    {
        notify(TRANSMIT_DELAY_COMPLETED, true);
        checkNotification();
    }

    void clearPendingNotificationForTest() { checkNotification(); }

    uint32_t applyCount() const { return reconfigureCount; }
    uint32_t startSendCount() const { return startSendCalls; }

    bool reconfigure() override
    {
        ++reconfigureCount;
        return true;
    }

    void startReceive() override {}

    uint32_t getPacketTime(uint32_t, bool) override { return 0; }

  protected:
    void disableInterrupt() override {}
    void enableInterrupt(void (*)()) override {}
    int16_t getCurrentRSSI() override { return -120; }
    bool isChannelActive() override { return false; }
    bool isActivelyReceiving() override { return false; }
    void addReceiveMetadata(meshtastic_MeshPacket *) override {}

    bool startSend(meshtastic_MeshPacket *) override
    {
        ++startSendCalls;
        return true;
    }

  private:
    meshtastic_MeshPacket inFlightPacket = meshtastic_MeshPacket_init_zero;
    uint32_t reconfigureCount = 0;
    uint32_t startSendCalls = 0;
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
static TestableRadioLibInterface *testRadioLib;

static meshtastic_Config_LoRaConfig makeLoraConfig(meshtastic_Config_LoRaConfig_RegionCode region)
{
    meshtastic_Config_LoRaConfig loraConfig = meshtastic_Config_LoRaConfig_init_zero;
    loraConfig.region = region;
    return loraConfig;
}

static meshtastic_Config_LoRaConfig usConfig()
{
    return makeLoraConfig(meshtastic_Config_LoRaConfig_RegionCode_US);
}

static meshtastic_Config_LoRaConfig lora24Config()
{
    return makeLoraConfig(meshtastic_Config_LoRaConfig_RegionCode_LORA_24);
}

static void test_configApply_idle_success_commitsCandidate()
{
    auto oldConfig = usConfig();
    auto candidate = lora24Config();
    RadioConfigApplyRequest request{oldConfig, candidate, 1000, 5000};
    config.lora = oldConfig;

    testRadio->scriptApply(true, true);
    TEST_ASSERT_TRUE(testRadio->requestConfigApply(&request));
    testRadio->serviceConfigApply(1000);

    TEST_ASSERT_EQUAL(RadioConfigApplyResult::APPLIED, request.result.load());
    TEST_ASSERT_EQUAL(candidate.region, config.lora.region);
}

static void test_configApply_applyFailure_rollsBack()
{
    auto oldConfig = usConfig();
    auto candidate = lora24Config();
    RadioConfigApplyRequest request{oldConfig, candidate, 1000, 5000};
    config.lora = oldConfig;

    testRadio->scriptApply(false, true);
    TEST_ASSERT_TRUE(testRadio->requestConfigApply(&request));
    testRadio->serviceConfigApply(1000);

    TEST_ASSERT_EQUAL(RadioConfigApplyResult::APPLY_FAILED_ROLLED_BACK, request.result.load());
    TEST_ASSERT_EQUAL(oldConfig.region, config.lora.region);
}

static void test_configApply_rollbackFailure_inhibitsTx()
{
    RadioConfigApplyRequest request{usConfig(), lora24Config(), 1000, 5000};
    config.lora = request.previous;
    testRadio->scriptApply(false, false);

    TEST_ASSERT_TRUE(testRadio->requestConfigApply(&request));
    testRadio->serviceConfigApply(1000);

    TEST_ASSERT_EQUAL(RadioConfigApplyResult::ROLLBACK_FAILED, request.result.load());
    TEST_ASSERT_TRUE(testRadio->configApplyTxInhibited());
}

static void test_configApply_secondRequest_rejectedBusy()
{
    RadioConfigApplyRequest first{usConfig(), lora24Config(), 1000, 5000};
    RadioConfigApplyRequest second{usConfig(), lora24Config(), 1000, 5000};

    TEST_ASSERT_TRUE(testRadio->requestConfigApply(&first));
    TEST_ASSERT_FALSE(testRadio->requestConfigApply(&second));
    TEST_ASSERT_EQUAL(RadioConfigApplyResult::BUSY, second.result.load());
}

static void test_configApply_activeTx_waits_withoutCompletingPacket()
{
    RadioConfigApplyRequest request{usConfig(), lora24Config(), 1000, 5000};
    config.lora = request.previous;
    testRadioLib->setSendingForTest(true);

    TEST_ASSERT_TRUE(testRadioLib->requestConfigApply(&request));
    testRadioLib->serviceConfigApply(1000);

    const auto resultWhileSending = request.result.load();
    const bool stillSending = testRadioLib->sendingForTest();
    const uint32_t appliesWhileSending = testRadioLib->applyCount();

    testRadioLib->finishTxForTest();
    testRadioLib->serviceConfigApply(1001);
    testRadioLib->clearPendingNotificationForTest();

    TEST_ASSERT_EQUAL(RadioConfigApplyResult::PENDING, resultWhileSending);
    TEST_ASSERT_TRUE(stillSending);
    TEST_ASSERT_EQUAL_UINT32(0, appliesWhileSending);
    TEST_ASSERT_EQUAL(RadioConfigApplyResult::APPLIED, request.result.load());
}

static void test_configApply_barrier_keepsQueuedPacketQueued()
{
    RadioConfigApplyRequest request{usConfig(), lora24Config(), 1000, 5000};
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    config.lora = request.previous;
    config.lora.tx_enabled = true;

    TEST_ASSERT_EQUAL(ERRNO_OK, testRadioLib->enqueuePacketForTest(&packet));
    TEST_ASSERT_TRUE(testRadioLib->requestConfigApply(&request));

    testRadioLib->fireTransmitDelayForTest();
    const uint32_t queuedPackets = testRadioLib->queuedPacketCount();
    const uint32_t startedPackets = testRadioLib->startSendCount();

    testRadioLib->serviceConfigApply(1000);
    testRadioLib->fireTransmitDelayForTest();

    TEST_ASSERT_EQUAL_UINT32(1, queuedPackets);
    TEST_ASSERT_EQUAL_UINT32(0, startedPackets);
}

static void test_configApply_completion_resumesQueuedTraffic()
{
    RadioConfigApplyRequest request{usConfig(), lora24Config(), 1000, 5000};
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    config.lora = request.previous;
    config.lora.tx_enabled = true;

    TEST_ASSERT_EQUAL(ERRNO_OK, testRadioLib->enqueuePacketForTest(&packet));
    TEST_ASSERT_TRUE(testRadioLib->requestConfigApply(&request));
    testRadioLib->serviceConfigApply(1000);
    testRadioLib->fireTransmitDelayForTest();

    TEST_ASSERT_EQUAL_UINT32(1, testRadioLib->startSendCount());
}

static void test_configApply_timeout_doesNotAbortTx()
{
    const uint32_t requestedAtMsec = millis();
    RadioConfigApplyRequest request{usConfig(), lora24Config(), requestedAtMsec, 5000};
    config.lora = request.previous;
    testRadioLib->setSendingForTest(true);

    TEST_ASSERT_TRUE(testRadioLib->requestConfigApply(&request));
    testRadioLib->serviceConfigApply(request.requestedAtMsec + request.timeoutMsec + 1);

    const auto result = request.result.load();
    const bool stillSending = testRadioLib->sendingForTest();
    testRadioLib->finishTxForTest();
    testRadioLib->clearPendingNotificationForTest();

    TEST_ASSERT_EQUAL(RadioConfigApplyResult::TIMED_OUT, result);
    TEST_ASSERT_TRUE(stillSending);
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

    const size_t known = countKnownRegions();
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

void setUp(void)
{
    mockMeshService = new MockMeshService();
    service = mockMeshService;

    // RadioInterface computes slotTimeMsec during construction and expects myRegion to be valid.
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    testRadio = new TestableRadioInterface();
    testAirTime = new AirTime();
    airTime = testAirTime;

    testRadioLib = new TestableRadioLibInterface();
}
void tearDown(void)
{
    delete testRadioLib;
    testRadioLib = nullptr;

    airTime = nullptr;
    delete testAirTime;
    testAirTime = nullptr;

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
    RUN_TEST(test_configApply_idle_success_commitsCandidate);
    RUN_TEST(test_configApply_applyFailure_rollsBack);
    RUN_TEST(test_configApply_rollbackFailure_inhibitsTx);
    RUN_TEST(test_configApply_secondRequest_rejectedBusy);
    RUN_TEST(test_configApply_activeTx_waits_withoutCompletingPacket);
    RUN_TEST(test_configApply_barrier_keepsQueuedPacketQueued);
    RUN_TEST(test_configApply_completion_resumesQueuedTraffic);
    RUN_TEST(test_configApply_timeout_doesNotAbortTx);
    RUN_TEST(test_regionPresetMap_coversAllRegionsWithinBounds);
    RUN_TEST(test_regionPresetMap_matchesRegionTable);
    exit(UNITY_END());
}

void loop() {}
