// trunk-ignore-all(trufflehog/Lob): matches test_* function names, not credentials
/**
 * Unit tests for MeshBeaconModule:
 *  - AdminModule::handleSetModuleConfig validation (invalid/valid inputs)
 *  - MeshBeaconBroadcastModule payload cache lifecycle
 *  - MeshBeaconBroadcastModule::sendBeacon sends a correctly formed packet
 *  - MeshBeaconListenerModule offer caching and empty-message guard
 */

#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define BEACON_TEST_ENTRY extern "C"
#else
#define BEACON_TEST_ENTRY
#endif

#if !MESHTASTIC_EXCLUDE_BEACON

#include "Default.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "airtime.h"
#include "modules/AdminModule.h"
#include "modules/MeshBeaconModule.h"
#include "support/AdminModuleTestShim.h"
#include "support/MockMeshService.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <pb_decode.h>
#include <vector>

// ---------------------------------------------------------------------------
// Formatted diagnostic helper. TEST_MESSAGE emits a line into Unity's per-test
// output (shown inline alongside the :PASS/:FAIL result); use this when you need
// a printf-style formatted note tied to a specific assertion. Plain printf() also
// works for free-standing log lines (e.g. the group headers in setup() below).
// ---------------------------------------------------------------------------
#define MSG_BUF_LEN 256
#define TEST_MSG_FMT(fmt, ...)                                                                                                   \
    do {                                                                                                                         \
        char _buf[MSG_BUF_LEN];                                                                                                  \
        snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__);                                                                        \
        TEST_MESSAGE(_buf);                                                                                                      \
    } while (0)

namespace
{

constexpr NodeNum kLocalNode = 0xAAAA0001;
constexpr NodeNum kRemoteNode = 0xBBBB0002;

// MockMeshService (test/support) stubs the side-effecting virtuals; handleToRadio is non-virtual so
// it runs the real implementation - the router->sendLocal path is guarded by MockRouter below.

// ---------------------------------------------------------------------------
// MockRouter: captures every packet handed to send() instead of transmitting.
// ---------------------------------------------------------------------------
class MockRouter : public Router
{
  public:
    ~MockRouter()
    {
        delete cryptLock;
        cryptLock = nullptr;
    }

    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        // Capture the primary channel as seen AT send() time, to prove the beacon never swaps it.
        if (channelFile.channels_count > 0)
            primaryAtSend.push_back(channels.getByIndex(channels.getPrimaryIndex()).settings);
        sentPackets.push_back(*p);
        // Mirror Router::send: every return but ERRNO_SHOULD_RELEASE has already freed the packet,
        // so a caller that reads p afterwards is reading freed memory.
        if (nextSendResult != ERRNO_SHOULD_RELEASE)
            packetPool.release(p);
        return nextSendResult;
    }

    ErrorCode nextSendResult = ERRNO_OK;

    // Locally-addressed packets land here instead of send().  Release immediately
    // rather than queuing into fromRadioQueue (which is never drained in tests).
    void enqueueReceivedMessage(meshtastic_MeshPacket *p) override { packetPool.release(p); }

    std::vector<meshtastic_MeshPacket> sentPackets;
    std::vector<meshtastic_ChannelSettings> primaryAtSend;
};

// AdminModuleTestShim (test/support) exposes protected handleSetModuleConfig.

// Build a sidecar entry the way sendBeacon() does, for tests that arm a switch directly.
static MeshBeaconModule_TargetRadioSettings targetSettings(meshtastic_Config_LoRaConfig_ModemPreset preset, bool usePreset,
                                                           uint16_t slot, bool legacyHopOverride,
                                                           meshtastic_Config_LoRaConfig_RegionCode region,
                                                           const char *channelName = nullptr)
{
    MeshBeaconModule_TargetRadioSettings s = {};
    s.lora = config.lora;
    s.lora.modem_preset = preset;
    s.lora.use_preset = usePreset;
    s.lora.channel_num = slot;
    // UNSET means "inherit the running region", which sendBeacon() resolves before it stores.
    s.lora.region = (region != meshtastic_Config_LoRaConfig_RegionCode_UNSET) ? region : config.lora.region;
    s.legacyHopOverride = legacyHopOverride;
    if (channelName)
        strncpy(s.channelName, channelName, sizeof(s.channelName) - 1);
    return s;
}

// ---------------------------------------------------------------------------
// MeshBeaconBroadcastModuleTestShim - exposes private internals for testing.
// ---------------------------------------------------------------------------
class MeshBeaconBroadcastModuleTestShim : public MeshBeaconBroadcastModule
{
  public:
    using MeshBeaconBroadcastModule::payloadCache;
    using MeshBeaconBroadcastModule::payloadCacheDirty;
    using MeshBeaconBroadcastModule::payloadCacheSize;
    using MeshBeaconBroadcastModule::rebuildCache;
    using MeshBeaconBroadcastModule::runOnce;
    using MeshBeaconBroadcastModule::sendBeacon;
};

// ---------------------------------------------------------------------------
// MeshBeaconListenerModuleTestShim - exposes handleReceivedProtobuf.
// ---------------------------------------------------------------------------
class MeshBeaconListenerModuleTestShim : public MeshBeaconListenerModule
{
  public:
    using MeshBeaconListenerModule::handleReceivedProtobuf;
    using MeshBeaconListenerModule::wantPacket;
};

// ---------------------------------------------------------------------------
// Globals managed by setUp / tearDown.
// ---------------------------------------------------------------------------
static MockMeshService *mockSvc = nullptr;
static MockRouter *mockRouter = nullptr;
static AdminModuleTestShim *testAdmin = nullptr;
static AirTime *testAirTime = nullptr;

// ---------------------------------------------------------------------------
// Helper: build a ModuleConfig wrapper for the beacon case (mirrors the wire
// format used by set_module_config admin messages).
// ---------------------------------------------------------------------------
static meshtastic_ModuleConfig makeBeaconModuleConfig(meshtastic_ModuleConfig_MeshBeaconConfig bcfg)
{
    meshtastic_ModuleConfig mc = meshtastic_ModuleConfig_init_zero;
    mc.which_payload_variant = meshtastic_ModuleConfig_mesh_beacon_tag;
    mc.payload_variant.mesh_beacon = bcfg;
    return mc;
}

// ---------------------------------------------------------------------------
// Helper: reset module/device config to a known baseline.
// ---------------------------------------------------------------------------
static void resetConfig()
{
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    config = meshtastic_LocalConfig_init_zero;

    // Device is an EU_868 node with LONG_FAST - the starting point.
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    // Allow TX unconditionally so airtime checks don't block sendBeacon().
    config.lora.override_duty_cycle = true;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;

    myNodeInfo.my_node_num = kLocalNode;

    // The sidecar is module state that outlives a case: the mock router never releases a packet,
    // so entries armed by an earlier test would otherwise still occupy the table.
    MeshBeaconModule::clearAllTargetRadioSettings();

    initRegion();
}

// Install a single PRIMARY channel with an explicit name + PSK so the beacon channel-swap path
// (sendBeaconPacket) has a real primary slot to save/restore and to read the active PSK from.
static void installTestPrimaryChannel(const char *name, const uint8_t *psk, size_t pskLen)
{
    channelFile.channels_count = 1;
    meshtastic_Channel &ch = channelFile.channels[0];
    ch = meshtastic_Channel_init_zero;
    ch.index = 0;
    ch.has_settings = true;
    ch.role = meshtastic_Channel_Role_PRIMARY;
    strncpy(ch.settings.name, name, sizeof(ch.settings.name) - 1);
    ch.settings.psk.size = (pb_size_t)pskLen;
    memcpy(ch.settings.psk.bytes, psk, pskLen);
    channels.onConfigChanged(); // set primaryIndex + recompute hashes
}

// Install a secondary channel at the given table index. Pass psk=nullptr/pskLen=0 for a blank slot
// (no name, no PSK) to exercise the "referenced slot is unconfigured" fallback.
static void installTestSecondaryChannel(uint8_t index, const char *name, const uint8_t *psk, size_t pskLen)
{
    if (channelFile.channels_count < (pb_size_t)(index + 1))
        channelFile.channels_count = index + 1;
    meshtastic_Channel &ch = channelFile.channels[index];
    ch = meshtastic_Channel_init_zero;
    ch.index = index;
    ch.has_settings = true;
    ch.role = meshtastic_Channel_Role_SECONDARY;
    if (name)
        strncpy(ch.settings.name, name, sizeof(ch.settings.name) - 1);
    if (psk && pskLen) {
        ch.settings.psk.size = (pb_size_t)pskLen;
        memcpy(ch.settings.psk.bytes, psk, pskLen);
    }
    channels.onConfigChanged();
}

// ===========================================================================
// Group 1: AdminModule config validation - bad inputs must be sanitised
// ===========================================================================

/**
 * SHORT_TURBO is clamped to the region default on EU_868, which does not allow the turbo presets.
 * Guards against admin storing radio settings that would breach regional regulations.
 */
static void test_adminValidation_turboPresetOnEU868_isClamped(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.flags |= MESH_BEACON_FLAG_BROADCAST_ENABLED;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].has_preset = true;
    bcfg.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.has_mesh_beacon);
    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.broadcast_targets[0].has_preset);
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
                              moduleConfig.mesh_beacon.broadcast_targets[0].preset,
                              "SHORT_TURBO must clamp to the EU_868 default, not be cleared");
}

/**
 * Verify LONG_TURBO is also clamped for EU_868, not just SHORT_TURBO.
 * Important to confirm rejection covers the entire turbo preset family rather than one variant.
 */
static void test_adminValidation_longTurboPresetOnEU868_isClamped(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].has_preset = true;
    bcfg.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.broadcast_targets[0].has_preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, moduleConfig.mesh_beacon.broadcast_targets[0].preset);
}

/**
 * Verify a turbo preset passes validation for US (PROFILE_STD allows all presets).
 * Important because the same preset that is illegal in EU_868 must be preserved in permissive regions.
 */
static void test_adminValidation_turboPresetOnUS_isAccepted(void)
{
    resetConfig();
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].has_preset = true;
    bcfg.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.broadcast_targets[0].has_preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, moduleConfig.mesh_beacon.broadcast_targets[0].preset);
}

/**
 * Verify MEDIUM_TURBO is also clamped for EU_868. Like SHORT_TURBO/LONG_TURBO it is a 500 kHz preset
 * that does not fit EU_868's 250 kHz band, so it must not survive admin validation there.
 */
static void test_adminValidation_mediumTurboPresetOnEU868_isClamped(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].has_preset = true;
    bcfg.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.has_mesh_beacon);
    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.broadcast_targets[0].has_preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, moduleConfig.mesh_beacon.broadcast_targets[0].preset);
}

/**
 * Verify MEDIUM_TURBO passes validation for US (PROFILE_STD allows the full turbo family).
 * The same 500 kHz preset that is illegal in EU_868 must be preserved in permissive regions.
 */
static void test_adminValidation_mediumTurboPresetOnUS_isAccepted(void)
{
    resetConfig();
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].has_preset = true;
    bcfg.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.broadcast_targets[0].has_preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO,
                      moduleConfig.mesh_beacon.broadcast_targets[0].preset);
}

/**
 * Verify an out-of-range region code (255) is sanitised to UNSET rather than stored verbatim.
 * Important to prevent invalid proto enum values from reaching the broadcaster and being broadcast
 * over the air.
 */
static void test_adminValidation_unknownOfferRegion_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_offer_region = (meshtastic_Config_LoRaConfig_RegionCode)255;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_UNSET, moduleConfig.mesh_beacon.broadcast_offer_region);
}

/**
 * Verify a known-good offer region (US) is written through unchanged after admin validation.
 * Important as a positive-path control alongside the rejection tests.
 */
static void test_adminValidation_validOfferRegion_isPreserved(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_offer_region = meshtastic_Config_LoRaConfig_RegionCode_US;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, moduleConfig.mesh_beacon.broadcast_offer_region);
}

/**
 * Verify an out-of-range region in a multi-target entry is sanitised to UNSET on write.
 * Important because an invalid enum must never reach the radio-switch path.
 */
static void test_adminValidation_targetUnknownRegion_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].region = (meshtastic_Config_LoRaConfig_RegionCode)255;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL(1, moduleConfig.mesh_beacon.broadcast_targets_count);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_UNSET, moduleConfig.mesh_beacon.broadcast_targets[0].region);
}

/**
 * Verify a preset that is illegal for a broadcast target's region clamps that entry's preset to the
 * region default and leaves its channel alone - the channel is a separate setting.
 */
static void test_adminValidation_targetInvalidPresetForRegion_clampsPresetKeepsChannel(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    bcfg.broadcast_targets[0].has_preset = true;
    bcfg.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
    bcfg.broadcast_targets[0].has_channel_index = true;
    bcfg.broadcast_targets[0].channel_index = 1;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.broadcast_targets[0].has_preset);
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
                              moduleConfig.mesh_beacon.broadcast_targets[0].preset,
                              "SHORT_TURBO must clamp to the EU_868 default for the target");
    TEST_ASSERT_TRUE_MESSAGE(moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index,
                             "a rejected preset must not take the target's channel with it");
    TEST_ASSERT_EQUAL(1, moduleConfig.mesh_beacon.broadcast_targets[0].channel_index);
}

/**
 * Verify a channel_index beyond the channel-table capacity is cleared on write.
 * Important so the broadcaster never indexes out of bounds when resolving a target channel.
 */
static void test_adminValidation_targetChannelIndexOutOfRange_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].has_channel_index = true;
    bcfg.broadcast_targets[0].channel_index = MAX_NUM_CHANNELS; // one past the last valid slot

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_FALSE(moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index);
}

/**
 * Verify an in-range channel_index survives admin validation unchanged.
 */
static void test_adminValidation_targetChannelIndexInRange_isPreserved(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].has_channel_index = true;
    bcfg.broadcast_targets[0].channel_index = 0;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index);
    TEST_ASSERT_EQUAL_UINT32(0, moduleConfig.mesh_beacon.broadcast_targets[0].channel_index);
}

/**
 * A pinned frequency_slot past the last slot the target's region holds must be cleared, not left
 * to fall back to the name hash silently at TX time.
 */
static void test_adminValidation_targetFrequencySlotOutOfRange_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].has_frequency_slot = true;
    bcfg.broadcast_targets[0].frequency_slot = RadioInterface::frequencySlotCount(config.lora) + 1;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_FALSE(moduleConfig.mesh_beacon.broadcast_targets[0].has_frequency_slot);
}

/**
 * Slots are 1-based, so a pinned 0 means the same as unset and must not survive as a pin.
 */
static void test_adminValidation_targetFrequencySlotZero_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].has_frequency_slot = true;
    bcfg.broadcast_targets[0].frequency_slot = 0;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_FALSE(moduleConfig.mesh_beacon.broadcast_targets[0].has_frequency_slot);
}

/**
 * Positive-path control: an in-range pin survives admin validation unchanged.
 */
static void test_adminValidation_targetFrequencySlotInRange_isPreserved(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].has_frequency_slot = true;
    bcfg.broadcast_targets[0].frequency_slot = 1;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.broadcast_targets[0].has_frequency_slot);
    TEST_ASSERT_EQUAL_UINT32(1, moduleConfig.mesh_beacon.broadcast_targets[0].frequency_slot);
}

/**
 * The offer's own pin gets the same range check as a target's.
 */
static void test_adminValidation_offerFrequencySlotOutOfRange_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.has_broadcast_offer_frequency_slot = true;
    bcfg.broadcast_offer_frequency_slot = RadioInterface::frequencySlotCount(config.lora) + 1;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_FALSE(moduleConfig.mesh_beacon.has_broadcast_offer_frequency_slot);
}

/**
 * An offer channel_index past the channel table must be cleared on write.
 */
static void test_adminValidation_offerChannelIndexOutOfRange_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.has_broadcast_offer_channel_index = true;
    bcfg.broadcast_offer_channel_index = MAX_NUM_CHANNELS;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_FALSE(moduleConfig.mesh_beacon.has_broadcast_offer_channel_index);
}

/**
 * Retiring a channel must clear every beacon reference to it - the offer as well as the targets.
 * A dangling offer index does not fail loudly; the offer just quietly stops naming a channel.
 */
static void test_adminValidation_retiredChannel_clearsOfferAndTarget(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xC3, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t sidePsk[16] = {0xC4, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    installTestSecondaryChannel(1, "Side", sidePsk, sizeof(sidePsk));

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_offer_channel_index = 1;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = 1;

    // Defer persistence: this asserts on the in-RAM edit, and a real SEGMENT_CHANNELS save needs
    // disk state the beacon fixture does not stand up.
    testAdmin->deferSaves();
    meshtastic_Channel retired = channels.getByIndex(1);
    retired.role = meshtastic_Channel_Role_DISABLED;
    testAdmin->handleSetChannel(retired);

    TEST_ASSERT_FALSE_MESSAGE(moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index,
                              "a target naming the retired channel must be cleared");
    TEST_ASSERT_FALSE_MESSAGE(moduleConfig.mesh_beacon.has_broadcast_offer_channel_index,
                              "the offer naming the retired channel must be cleared too");
    // The module config was edited, so it has to be in the save set or the clear is lost on reboot.
    TEST_ASSERT_TRUE_MESSAGE(testAdmin->savedSegments() & SEGMENT_MODULECONFIG,
                             "clearing a beacon reference must add SEGMENT_MODULECONFIG to the save");
}

/**
 * The offer gets the same partial-accept treatment as a target: a preset the region cannot run
 * must not take the region and channel the operator set with it.
 */
static void test_adminValidation_offerInvalidPreset_clampsAndKeepsRest(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_offer_region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    bcfg.has_broadcast_offer_preset = true;
    bcfg.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
    bcfg.has_broadcast_offer_channel_index = true;
    bcfg.broadcast_offer_channel_index = 1;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE_MESSAGE(moduleConfig.mesh_beacon.has_broadcast_offer_preset,
                             "an invalid offer preset must clamp, not clear");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, moduleConfig.mesh_beacon.broadcast_offer_preset,
                              "SHORT_TURBO must clamp to the EU_868 default");
    TEST_ASSERT_TRUE_MESSAGE(moduleConfig.mesh_beacon.has_broadcast_offer_channel_index,
                             "a rejected preset must not take the offer channel with it");
}

/**
 * Region is validated before preset, so an unknown region cannot drag down a preset that is
 * perfectly good once that region is discarded.
 */
static void test_adminValidation_offerUnknownRegion_keepsValidPreset(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_offer_region = (meshtastic_Config_LoRaConfig_RegionCode)255;
    bcfg.has_broadcast_offer_preset = true;
    bcfg.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_UNSET, moduleConfig.mesh_beacon.broadcast_offer_region);
    TEST_ASSERT_TRUE_MESSAGE(moduleConfig.mesh_beacon.has_broadcast_offer_preset,
                             "a discarded region must not take a valid preset with it");
}

/**
 * Verify a valid preset/region multi-target entry survives admin validation unchanged.
 * Positive-path control for the per-target validation.
 */
static void test_adminValidation_targetValidPresetForRegion_isPreserved(void)
{
    resetConfig();
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].region = meshtastic_Config_LoRaConfig_RegionCode_US;
    bcfg.broadcast_targets[0].has_preset = true;
    bcfg.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.broadcast_targets[0].has_preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, moduleConfig.mesh_beacon.broadcast_targets[0].preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, moduleConfig.mesh_beacon.broadcast_targets[0].region);
}

/**
 * Verify broadcast_message is hard-capped at 100 characters (NUL forced at index 100).
 * Important to prevent oversized beacon payloads from abusing airtime across the mesh.
 */
static void test_adminValidation_messageTooLong_isTruncatedAt100(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    // Fill with 'A' up to the full array size; admin must enforce ≤100 chars.
    memset(bcfg.broadcast_message, 'A', sizeof(bcfg.broadcast_message));
    bcfg.broadcast_message[sizeof(bcfg.broadcast_message) - 1] = '\0'; // pb_decode guarantee

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    // Byte at index 100 must be NUL (length capped at 100).
    TEST_ASSERT_EQUAL('\0', moduleConfig.mesh_beacon.broadcast_message[100]);
    // Bytes before it should still be 'A'.
    TEST_ASSERT_EQUAL('A', moduleConfig.mesh_beacon.broadcast_message[0]);
}

/**
 * Verify any non-zero interval below 3600 s is clamped up to the 1-hour minimum.
 * Important to prevent high-rate beacon floods from a misconfigured or malicious client.
 */
static void test_adminValidation_intervalTooLow_isClamped(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_interval_secs = 60; // way below minimum

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL_UINT32(3600, moduleConfig.mesh_beacon.broadcast_interval_secs);
}

/**
 * Verify an interval above the minimum is stored as-is without modification.
 * Important to confirm the clamp is one-sided (lower bound only, no upper bound enforced).
 */
static void test_adminValidation_intervalTooHigh_isPreserved(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_interval_secs = 999999;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL_UINT32(999999, moduleConfig.mesh_beacon.broadcast_interval_secs);
}

/**
 * Verify LONG_FAST (enum value 0) survives admin validation without being treated as 'absent'.
 * Important to guard the has_broadcast_offer_preset presence-flag fix against zero-value erasure.
 */
static void test_adminValidation_longFastOfferPreset_isPreserved(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.has_broadcast_offer_preset = true;
    bcfg.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.has_broadcast_offer_preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, moduleConfig.mesh_beacon.broadcast_offer_preset);
}

/**
 * Verify that interval 0 (the documented 'use default' sentinel) is not raised to 3600 by the clamp.
 * Important because 0 and 3600 have different runtime semantics in runOnce via
 * Default::getConfiguredOrDefault.
 */
static void test_adminValidation_intervalZero_isNotClamped(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_interval_secs = 0;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL_UINT32(0, moduleConfig.mesh_beacon.broadcast_interval_secs);
}

/**
 * Verify that saving a new beacon config marks the broadcaster's payload cache dirty.
 * Important so the next TX re-encodes from the latest config rather than a pre-save stale snapshot.
 */
static void test_adminValidation_validSave_invalidatesCache(void)
{
    resetConfig();

    // Prime the broadcaster with a clean state so the dirty flag is known.
    std::unique_ptr<MeshBeaconBroadcastModuleTestShim> bcast(new MeshBeaconBroadcastModuleTestShim());
    meshBeaconBroadcastModule = bcast.get();
    bcast->payloadCacheDirty = false; // pretend it was freshly built

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.flags |= MESH_BEACON_FLAG_BROADCAST_ENABLED;
    strncpy(bcfg.broadcast_message, "hello", sizeof(bcfg.broadcast_message) - 1);

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE_MESSAGE(bcast->payloadCacheDirty, "Config save must mark payload cache dirty");

    meshBeaconBroadcastModule = nullptr;
}

// ===========================================================================
// Group 2: Broadcaster payload cache
// ===========================================================================

/**
 * Verify rebuildCache produces at least one encoded byte when broadcast_message is set.
 * Important as the most basic liveness check for the protobuf encoding path.
 */
static void test_broadcaster_rebuildCache_producesNonEmptyPayload(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "Test beacon", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    TEST_ASSERT_TRUE(bcast.payloadCacheDirty);

    bcast.rebuildCache();

    TEST_ASSERT_FALSE_MESSAGE(bcast.payloadCacheDirty, "rebuildCache must clear dirty flag");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, (int)bcast.payloadCacheSize, "rebuildCache must produce a non-empty payload");
}

/**
 * Verify the cached bytes round-trip through pb_decode back to the original message string.
 * Important to catch any protobuf field-tag or wire-type regression in the encoding path.
 */
static void test_broadcaster_rebuildCache_payloadDecodesCorrectly(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    const char *msg = "Hello, Meshtastic!";
    strncpy(moduleConfig.mesh_beacon.broadcast_message, msg, sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.rebuildCache();

    // Decode the cached bytes back into a MeshBeacon struct.
    meshtastic_MeshBeacon decoded = meshtastic_MeshBeacon_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(bcast.payloadCache, bcast.payloadCacheSize);
    bool ok = pb_decode(&stream, &meshtastic_MeshBeacon_msg, &decoded);

    TEST_ASSERT_TRUE_MESSAGE(ok, "Cached payload must decode without error");
    TEST_ASSERT_EQUAL_STRING(msg, decoded.message);
}

/**
 * Verify offer_region and offer_preset are present in the encoded cache payload.
 * Important to confirm the offer-carrying path correctly uses the has_broadcast_offer_preset flag.
 */
static void test_broadcaster_rebuildCache_offerFieldsEncoded(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.broadcast_offer_region = meshtastic_Config_LoRaConfig_RegionCode_US;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "offer-test", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.rebuildCache();

    meshtastic_MeshBeacon decoded = meshtastic_MeshBeacon_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(bcast.payloadCache, bcast.payloadCacheSize);
    pb_decode(&stream, &meshtastic_MeshBeacon_msg, &decoded);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, decoded.offer_region);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, decoded.offer_preset);
}

/**
 * Verify invalidateCache flips payloadCacheDirty back to true after a successful rebuild.
 * Important to confirm the cache-invalidation contract relied on by admin saves and config observers.
 */
static void test_broadcaster_invalidateCache_setsDirtyFlag(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.rebuildCache();
    TEST_ASSERT_FALSE(bcast.payloadCacheDirty);

    bcast.invalidateCache();
    TEST_ASSERT_TRUE(bcast.payloadCacheDirty);
}

/**
 * Verify calling rebuildCache a second time without an intervening invalidation is a no-op.
 * Important to prevent spurious re-encodes when config-observer callbacks fire multiple times.
 */
static void test_broadcaster_rebuildCache_idempotent(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "idem", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.rebuildCache();
    pb_size_t firstSize = bcast.payloadCacheSize;
    bcast.rebuildCache(); // second call - should be identical
    pb_size_t secondSize = bcast.payloadCacheSize;

    TEST_ASSERT_FALSE(bcast.payloadCacheDirty);
    TEST_ASSERT_EQUAL(firstSize, secondSize);
}

// ===========================================================================
// Group 3: Broadcaster sendBeacon - packet structure
// ===========================================================================

/**
 * Verify the 'from' field is the local node number.
 * Important for correct source attribution in peer node tables that receive the beacon.
 */
static void test_broadcaster_sendBeacon_fromIsLocalNodeWhenUnset(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_BROADCAST_ENABLED;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "from-local", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(kLocalNode, mockRouter->sentPackets[0].from);
}

/**
 * Verify the 'to' field is always NODENUM_BROADCAST regardless of other settings.
 * Important because beacons are mesh-wide announcements and must never be addressed to a single peer.
 */
static void test_broadcaster_sendBeacon_addressedToBroadcast(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "bcast-addr", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(NODENUM_BROADCAST, mockRouter->sentPackets[0].to);
}

/**
 * Verify MESH_BEACON_APP portnum is used when the packet carries a radio offer payload.
 * Important so receivers use the structured protobuf decoder rather than treating it as raw text.
 */
static void test_broadcaster_sendBeacon_usesBeaconPortnum(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "portnum-check", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL(meshtastic_PortNum_MESH_BEACON_APP, mockRouter->sentPackets[0].decoded.portnum);
}

/**
 * Verify TEXT_MESSAGE_APP portnum is used when no offer content is present, even if a
 * broadcast target preset is set (that governs which radio config to use for TX, not portnum).
 * Important so standard clients display plain-text beacons without needing a MESH_BEACON_APP decoder.
 */
static void test_broadcaster_sendBeacon_fallsBackToTextMessagePortnum(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    const char *msg = "plain-text-beacon";
    strncpy(moduleConfig.mesh_beacon.broadcast_message, msg, sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    // Target preset set, but no offer - should still be TEXT_MESSAGE_APP
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_preset = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    const meshtastic_MeshPacket &p = mockRouter->sentPackets[0];
    TEST_ASSERT_EQUAL(meshtastic_PortNum_TEXT_MESSAGE_APP, p.decoded.portnum);
    TEST_ASSERT_EQUAL_UINT32(strlen(msg), p.decoded.payload.size);
    TEST_ASSERT_EQUAL_STRING_LEN(msg, (const char *)p.decoded.payload.bytes, p.decoded.payload.size);
}

/**
 * Verify the MESH_BEACON_APP payload decodes back to the original message string.
 * Important to catch encode/decode regressions in the full sendBeacon → wire → pb_decode round-trip.
 */
static void test_broadcaster_sendBeacon_payloadDecodesCorrectly(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    const char *msg = "Greetings from the beacon";
    strncpy(moduleConfig.mesh_beacon.broadcast_message, msg, sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    const meshtastic_MeshPacket &p = mockRouter->sentPackets[0];
    meshtastic_MeshBeacon decoded = meshtastic_MeshBeacon_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(p.decoded.payload.bytes, p.decoded.payload.size);
    bool ok = pb_decode(&stream, &meshtastic_MeshBeacon_msg, &decoded);

    TEST_ASSERT_TRUE_MESSAGE(ok, "Sent payload must decode without error");
    TEST_ASSERT_EQUAL_STRING(msg, decoded.message);
}

/**
 * Verify a beacon with offer fields but no message text is still emitted on MESH_BEACON_APP.
 * Important because offer-only beacons are a valid use case that the early-return guard must not
 * suppress.
 */
static void test_broadcaster_sendBeacon_offerOnly_isSent(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL(meshtastic_PortNum_MESH_BEACON_APP, mockRouter->sentPackets[0].decoded.portnum);
}

/**
 * Verify runOnce sends exactly one packet when broadcast_enabled is true.
 * Important to confirm the OSThread timer callback drives the full send path end-to-end.
 */
static void test_broadcaster_runOnce_sendsWhenEnabled(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_BROADCAST_ENABLED;
    moduleConfig.mesh_beacon.broadcast_interval_secs = 3600;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "runOnce-enabled",
            sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.runOnce();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
}

/**
 * Verify runOnce transmits nothing when broadcast_enabled is false.
 * Important to confirm the feature can be cleanly disabled via remote admin without rebooting.
 */
static void test_broadcaster_runOnce_silentWhenDisabled(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags &= ~MESH_BEACON_FLAG_BROADCAST_ENABLED;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "runOnce-disabled",
            sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.runOnce();

    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size());
}

// ===========================================================================
// Group 4: Listener - offer caching and guards
// ===========================================================================

// Helper: build a decoded MESH_BEACON_APP packet carrying the given MeshBeacon.
static meshtastic_MeshPacket makeBeaconPacket(const meshtastic_MeshBeacon &b, NodeNum from = kRemoteNode)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = NODENUM_BROADCAST;
    p.id = 0xDEAD0001;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;
    p.decoded.payload.size =
        (pb_size_t)pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_MeshBeacon_msg, &b);
    return p;
}

/**
 * Verify a beacon carrying preset and region offer fields is stored in lastReceivedOffer.
 * Important to confirm the client app's offer cache is populated correctly for join-offer UI flows.
 */
static void test_listener_receiveWithOffer_cachesOffer(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LISTEN_ENABLED;

    MeshBeaconListenerModuleTestShim listener;
    MeshBeaconListenerModule::lastReceivedOffer = {};

    meshtastic_MeshBeacon b = meshtastic_MeshBeacon_init_zero;
    strncpy(b.message, "Join us on US/MEDIUM_FAST", sizeof(b.message) - 1);
    b.has_offer_preset = true;
    b.offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    b.offer_region = meshtastic_Config_LoRaConfig_RegionCode_US;

    meshtastic_MeshPacket mp = makeBeaconPacket(b);
    listener.handleReceivedProtobuf(mp, &b);

    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconListenerModule::lastReceivedOffer.valid, "Offer with preset must be cached");
    TEST_ASSERT_EQUAL(kRemoteNode, MeshBeaconListenerModule::lastReceivedOffer.sender);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, MeshBeaconListenerModule::lastReceivedOffer.preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, MeshBeaconListenerModule::lastReceivedOffer.region);
}

/**
 * Verify a beacon with a full ChannelSettings offer sets has_channel and copies the channel struct.
 * Important because the client app checks has_channel before rendering a channel join offer.
 */
static void test_listener_receiveWithChannelOffer_setsHasChannel(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LISTEN_ENABLED;

    MeshBeaconListenerModuleTestShim listener;
    MeshBeaconListenerModule::lastReceivedOffer = {};

    meshtastic_MeshBeacon b = meshtastic_MeshBeacon_init_zero;
    strncpy(b.message, "Channel offer test", sizeof(b.message) - 1);
    b.has_offer_channel = true;
    b.offer_channel.channel_num = 5;
    strncpy(b.offer_channel.name, "TestNet", sizeof(b.offer_channel.name) - 1);

    meshtastic_MeshPacket mp = makeBeaconPacket(b);
    listener.handleReceivedProtobuf(mp, &b);

    TEST_ASSERT_TRUE(MeshBeaconListenerModule::lastReceivedOffer.valid);
    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconListenerModule::lastReceivedOffer.has_channel,
                             "has_channel must be set when offer_channel is present");
    TEST_ASSERT_EQUAL_UINT32(5, MeshBeaconListenerModule::lastReceivedOffer.channel.channel_num);
}

/**
 * Verify a beacon with neither message text nor offer fields is silently discarded.
 * Important to avoid spurious cache updates and wasted inbox copies from empty-payload packets.
 */
static void test_listener_emptyMessageWithoutOffer_isDropped(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LISTEN_ENABLED;

    MeshBeaconListenerModuleTestShim listener;
    MeshBeaconListenerModule::lastReceivedOffer = {};

    meshtastic_MeshBeacon b = meshtastic_MeshBeacon_init_zero;
    // message field intentionally left blank

    meshtastic_MeshPacket mp = makeBeaconPacket(b);
    listener.handleReceivedProtobuf(mp, &b);

    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconListenerModule::lastReceivedOffer.valid, "Empty message must not update offer cache");
}

/**
 * Verify a LONG_FAST offer (preset enum value 0) with no message still populates the offer cache.
 * Important to guard the has_offer_preset fix - LONG_FAST must not be treated as 'no offer present'.
 */
static void test_listener_offerOnly_isCached(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LISTEN_ENABLED;

    MeshBeaconListenerModuleTestShim listener;
    MeshBeaconListenerModule::lastReceivedOffer = {};

    meshtastic_MeshBeacon b = meshtastic_MeshBeacon_init_zero;
    b.has_offer_preset = true;
    b.offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    meshtastic_MeshPacket mp = makeBeaconPacket(b);
    listener.handleReceivedProtobuf(mp, &b);

    TEST_ASSERT_TRUE(MeshBeaconListenerModule::lastReceivedOffer.valid);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, MeshBeaconListenerModule::lastReceivedOffer.preset);
}

/**
 * Verify a null MeshBeacon pointer is handled gracefully and returns false without a crash.
 * Important to guard against the ProtobufModule base class passing nullptr on a decode failure.
 */
static void test_listener_nullBeacon_isDropped(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LISTEN_ENABLED;

    MeshBeaconListenerModuleTestShim listener;
    MeshBeaconListenerModule::lastReceivedOffer = {};

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    bool result = listener.handleReceivedProtobuf(mp, nullptr);

    TEST_ASSERT_FALSE_MESSAGE(result, "Null beacon must return false");
    TEST_ASSERT_FALSE(MeshBeaconListenerModule::lastReceivedOffer.valid);
}

/**
 * Verify a text-only beacon (no offer fields set) does not mark the offer cache valid.
 * Important to prevent the client from showing a join dialog in response to plain-text beacons.
 */
static void test_listener_receiveWithNoOffer_cacheStaysInvalid(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LISTEN_ENABLED;

    MeshBeaconListenerModuleTestShim listener;
    MeshBeaconListenerModule::lastReceivedOffer = {};

    meshtastic_MeshBeacon b = meshtastic_MeshBeacon_init_zero;
    strncpy(b.message, "No offer here", sizeof(b.message) - 1);
    // has_offer_preset == false, has_offer_channel == false

    meshtastic_MeshPacket mp = makeBeaconPacket(b);
    listener.handleReceivedProtobuf(mp, &b);

    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconListenerModule::lastReceivedOffer.valid, "No offer fields → cache must stay invalid");
}

/**
 * Verify the listener does NOT unwrap a combined beacon's text into a synthesized TEXT_MESSAGE_APP.
 * The original MESH_BEACON_APP packet already reaches the client (the handler returns CONTINUE), so
 * a beacon-aware client reads `message` directly from it - injecting a copy would only duplicate it,
 * and re-injecting onto the mesh would amplify/re-attribute. So: nothing onto the mesh, nothing
 * synthesized to the phone, and the handler must not consume the packet.
 */
static void test_listener_textMessage_notUnwrapped(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LISTEN_ENABLED;

    MeshBeaconListenerModuleTestShim listener;
    MeshBeaconListenerModule::lastReceivedOffer = {};

    meshtastic_MeshBeacon b = meshtastic_MeshBeacon_init_zero;
    strncpy(b.message, "hello mesh", sizeof(b.message) - 1);

    meshtastic_MeshPacket mp = makeBeaconPacket(b);
    bool consumed = listener.handleReceivedProtobuf(mp, &b);

    // CONTINUE (not STOP): the original MESH_BEACON_APP keeps flowing to the client, which reads
    // `message` from it - the simple path for a beacon-aware client.
    TEST_ASSERT_FALSE_MESSAGE(consumed, "Listener must not consume the beacon; the original must reach the client");
    // Nothing re-injected onto the mesh.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, mockRouter->sentPackets.size(),
                                     "Received beacon text must not be re-injected into the mesh");
    // No synthesized TEXT_MESSAGE_APP delivered to the phone (no duplicate of the beacon's text).
    meshtastic_MeshPacket *toPhone = service->getForPhone();
    TEST_ASSERT_NULL_MESSAGE(toPhone, "Listener must not inject a duplicate text packet to the phone");
}

/**
 * Verify wantPacket returns false for MESH_BEACON_APP when listen_enabled is false.
 * Important to confirm the module opts out of processing when its config flag is cleared.
 */
static void test_listener_wantPacket_falseWhenDisabled(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags &= ~MESH_BEACON_FLAG_LISTEN_ENABLED;

    MeshBeaconListenerModuleTestShim listener;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;

    TEST_ASSERT_FALSE(listener.wantPacket(&mp));
}

/**
 * Verify wantPacket returns true for MESH_BEACON_APP packets when listen_enabled is true.
 * Important as a basic routing sanity check confirming the module is registered for its portnum.
 */
static void test_listener_wantPacket_trueWhenEnabled(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LISTEN_ENABLED;

    MeshBeaconListenerModuleTestShim listener;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;

    TEST_ASSERT_TRUE(listener.wantPacket(&mp));
}

// ===========================================================================
// Group 6: Legacy split messages
// ===========================================================================

/**
 * Verify broadcast_legacy_split causes sendBeacon to emit exactly two packets when both
 * text and offer content are present.
 * Important to confirm the split path is wired end-to-end rather than short-circuiting.
 */
static void test_broadcaster_legacySplit_sendsTwoPackets(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LEGACY_SPLIT;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "split-text", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, mockRouter->sentPackets.size(), "Legacy split must emit exactly 2 packets");
}

/**
 * Verify the first packet in a legacy-split send is MESH_BEACON_APP (the offer packet).
 * Important so receivers without a MESH_BEACON_APP decoder still get the text from packet B.
 */
static void test_broadcaster_legacySplit_firstPacketIsBeaconApp(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LEGACY_SPLIT;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "split-offer-only",
            sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(2, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL(meshtastic_PortNum_MESH_BEACON_APP, mockRouter->sentPackets[0].decoded.portnum);
}

/**
 * Verify the MESH_BEACON_APP packet in a legacy-split send carries no message text.
 * Important so peers' MESH_BEACON_APP handlers do not duplicate the text already in packet B.
 */
static void test_broadcaster_legacySplit_firstPacketHasNoMessageText(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LEGACY_SPLIT;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "hidden-in-split",
            sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(2, mockRouter->sentPackets.size());
    const meshtastic_MeshPacket &pA = mockRouter->sentPackets[0];
    meshtastic_MeshBeacon decodedA = meshtastic_MeshBeacon_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(pA.decoded.payload.bytes, pA.decoded.payload.size);
    pb_decode(&stream, &meshtastic_MeshBeacon_msg, &decodedA);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", decodedA.message, "Offer-only MESH_BEACON_APP must have empty message");
}

/**
 * Verify the second packet in a legacy-split send is TEXT_MESSAGE_APP containing the message text.
 * Important so legacy clients that only handle TEXT_MESSAGE_APP receive the human-readable text.
 */
static void test_broadcaster_legacySplit_secondPacketIsTextMessage(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_LEGACY_SPLIT;
    const char *msg = "split-B-text";
    strncpy(moduleConfig.mesh_beacon.broadcast_message, msg, sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(2, mockRouter->sentPackets.size());
    const meshtastic_MeshPacket &pB = mockRouter->sentPackets[1];
    TEST_ASSERT_EQUAL(meshtastic_PortNum_TEXT_MESSAGE_APP, pB.decoded.portnum);
    TEST_ASSERT_EQUAL_STRING_LEN(msg, (const char *)pB.decoded.payload.bytes, pB.decoded.payload.size);
}

// ===========================================================================
// Group 7: Beacon-channel PSK swap (target channel-table slot)
// ===========================================================================

/**
 * With no target channel_index, the beacon must transmit on the primary channel unchanged
 * (no swap). Guards against the swap firing - and churning the channel table - when it isn't needed.
 */
static void test_broadcaster_noChannelOverride_doesNotSwapPrimary(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    // No target channel_index.

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_TRUE_MESSAGE(mockRouter->primaryAtSend.size() >= 1, "expected at least one send");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Home", mockRouter->primaryAtSend[0].name,
                                     "primary must stay unchanged when no channel override is set");
}

/**
 * A target naming a configured table slot transmits on THAT channel, by addressing the packet at the
 * slot rather than installing it as primary - so no other traffic picks up the beacon's channel.
 */
static void test_broadcaster_targetChannelIndex_usesTableSlot(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t beaconPsk[16] = {0xBB, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                          0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    installTestSecondaryChannel(1, "BeaconNet", beaconPsk, sizeof(beaconPsk));

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true; // content to send
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = 1;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_TRUE_MESSAGE(mockRouter->sentPackets.size() >= 1, "expected at least one send");
    // The packet names the slot; perhapsEncode() keys off that index and stamps its hash.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, mockRouter->sentPackets[0].channel,
                                   "beacon must be addressed at the referenced channel-table slot");
    // Stronger than "restored": the primary is never touched, at send time or after.
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Home", mockRouter->primaryAtSend[0].name,
                                     "primary channel must not be swapped during send");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xAA, mockRouter->primaryAtSend[0].psk.bytes[0],
                                    "primary PSK must not be swapped during send");
    const meshtastic_ChannelSettings &after = channels.getByIndex(channels.getPrimaryIndex()).settings;
    TEST_ASSERT_EQUAL_STRING("Home", after.name);
    TEST_ASSERT_EQUAL_UINT(sizeof(homePsk), after.psk.size);
    TEST_ASSERT_EQUAL_UINT8(0xAA, after.psk.bytes[0]);
}

/**
 * A broadcast_target whose channel_index points at a BLANK table slot (no name, no PSK) has no key
 * to encrypt with, so it transmits on the primary - without swapping anything into the primary slot.
 */
static void test_broadcaster_targetChannelIndex_blankSlotFallsBackToPrimary(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    installTestSecondaryChannel(1, nullptr, nullptr, 0); // blank slot: no name, no PSK

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = 1;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    // Exactly one beacon goes out, addressed at the primary, and the primary itself is untouched.
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_TRUE_MESSAGE(mockRouter->primaryAtSend.size() >= 1, "expected at least one send");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(channels.getPrimaryIndex(), mockRouter->sentPackets[0].channel,
                                   "a blank slot has no key, so the beacon must fall back to the primary");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Home", mockRouter->primaryAtSend[0].name,
                                     "blank slot must not swap the beacon onto a borrowed channel");
}

/**
 * Two broadcast_targets that resolve to the same effective radio config (same preset/region/channel)
 * must produce only ONE beacon - the payload is identical, so re-broadcasting wastes airtime.
 */
static void test_broadcaster_duplicateTargets_dedupedToOnePacket(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 2;
    for (int i = 0; i < 2; i++) {
        moduleConfig.mesh_beacon.broadcast_targets[i].has_preset = true;
        moduleConfig.mesh_beacon.broadcast_targets[i].preset = meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW;
        moduleConfig.mesh_beacon.broadcast_targets[i].region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    }

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, mockRouter->sentPackets.size(), "duplicate targets must collapse to one beacon");
}

/**
 * Two distinct broadcast_targets (different presets) must BOTH be sent - dedup must not over-collapse.
 */
static void test_broadcaster_distinctTargets_bothSent(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 2;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_preset = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets[1].has_preset = true;
    moduleConfig.mesh_beacon.broadcast_targets[1].preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, mockRouter->sentPackets.size(), "distinct targets must each be sent");
}

// ---------------------------------------------------------------------------
// Radio switch/restore re-entrancy
// ---------------------------------------------------------------------------

/**
 * Stands in for a real driver on the restore path. RadioLibInterface::reconfigure() standbys the
 * chip, setStandby() calls completeSending(), and completeSending() calls back into
 * reconfigureForBeaconTX(iface, nullptr) - so reconfigure() re-entering is the normal case, not an
 * exotic one. Bounded, so a regression fails an assertion instead of overflowing the stack.
 */
class ReentrantRadioInterface : public RadioInterface
{
  public:
    static constexpr int kReentryLimit = 16;
    int reconfigureCalls = 0;
    bool reenterOnReconfigure = false;

    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }

    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override
    {
        (void)totalPacketLen;
        (void)received;
        return 0;
    }

    bool reconfigure() override
    {
        reconfigureCalls++;
        if (reenterOnReconfigure && reconfigureCalls < kReentryLimit)
            MeshBeaconModule::reconfigureForBeaconTX(this, nullptr);
        return true;
    }
};

// Keeps the base reconfigure(), so applyModemConfig() actually runs.
class ApplyingRadioInterface : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }

    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override
    {
        (void)totalPacketLen;
        (void)received;
        return 0;
    }
};

/**
 * A target on the config the node already runs must arm no switch. A resolved 1-based slot compared
 * against a channel_num still 0 ("derive it") reads as a difference and switches to the same freq.
 */
static void test_broadcaster_targetMatchingRunningConfig_armsNoSwitch(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    config.lora.channel_num = 0; // derive the slot, as an unconfigured node does

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true; // content to send
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = channels.getPrimaryIndex();

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_TRUE_MESSAGE(mockRouter->sentPackets.size() >= 1, "expected at least one send");
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&mockRouter->sentPackets[0]),
                              "a target on the running preset, region and slot must arm no radio switch");
}

/**
 * Clamping a beacon target is a question about a config the radio will never run, so it must not
 * republish uses_default_frequency_slot - NeighborInfoModule reads that as the running radio's state.
 */
static void test_adminValidation_targetClamp_leavesRunningSlotState(void)
{
    resetConfig();
    RadioInterface::uses_default_frequency_slot = true;
    RadioInterface::uses_custom_channel_name = false;

    // A turbo preset on EU_868 forces the clamp path inside admin validation.
    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_targets_count = 1;
    bcfg.broadcast_targets[0].has_preset = true;
    bcfg.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
    bcfg.broadcast_targets[0].region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE_MESSAGE(RadioInterface::uses_default_frequency_slot,
                             "a candidate clamp must not rewrite the running radio's slot state");
    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::uses_custom_channel_name,
                              "a candidate clamp must not rewrite the running channel-name state");
}

/**
 * The other half of the above, and the reason it is not vacuous: no clamp publishes the flags any
 * more, so something still has to. A config the node runs must move them when it is applied.
 */
static void test_applyModemConfig_publishesTheSlotVerdict(void)
{
    resetConfig();
    static const uint8_t psk[16] = {0xA1, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    // A name that is not the preset's display name is what uses_custom_channel_name reports on.
    installTestPrimaryChannel("NotAPreset", psk, sizeof(psk));
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    // Pin a slot the name would not hash to, so "uses the default slot" must come out false.
    const uint32_t derived = RadioInterface::resolveFrequencySlot(config.lora, "NotAPreset");
    const uint32_t slots = RadioInterface::frequencySlotCount(config.lora);
    config.lora.channel_num = (derived % slots) + 1; // any valid slot that is not the derived one

    RadioInterface::uses_default_frequency_slot = true;
    RadioInterface::uses_custom_channel_name = false;

    ApplyingRadioInterface radio;
    radio.reconfigure();

    TEST_ASSERT_FALSE_MESSAGE(RadioInterface::uses_default_frequency_slot,
                              "applying a config on a pinned slot must publish uses_default_frequency_slot=false");
    TEST_ASSERT_TRUE_MESSAGE(RadioInterface::uses_custom_channel_name,
                             "applying a config on a non-preset channel name must publish uses_custom_channel_name=true");
}

/**
 * The sidecar carries a whole LoRaConfig so a target can vary more than a preset. Nothing in the
 * beacon config can ask for that yet, so pin it at the sidecar: custom modem params must survive
 * the round trip rather than being flattened onto a preset.
 */
static void test_sidecar_carriesCustomModemParams(void)
{
    resetConfig();
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x5EED0100;

    MeshBeaconModule_TargetRadioSettings s = {};
    s.lora = config.lora;
    s.lora.use_preset = false; // custom params: bandwidth/SF/CR are the config, not the preset
    s.lora.bandwidth = 125;
    s.lora.spread_factor = 11;
    s.lora.coding_rate = 8;
    s.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    strncpy(s.channelName, "Custom", sizeof(s.channelName) - 1);
    MeshBeaconModule::setTargetRadioSettings(&pkt, s);

    const MeshBeaconModule_TargetRadioSettings *got = MeshBeaconModule::getTargetRadioSettings(&pkt);
    TEST_ASSERT_NOT_NULL_MESSAGE(got, "the entry must be retrievable by packet id");
    TEST_ASSERT_FALSE_MESSAGE(got->lora.use_preset, "use_preset must survive the sidecar");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(125, got->lora.bandwidth, "bandwidth must survive the sidecar");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(11, got->lora.spread_factor, "spread_factor must survive the sidecar");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(8, got->lora.coding_rate, "coding_rate must survive the sidecar");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Custom", got->channelName, "the hashed channel name must survive the sidecar");

    MeshBeaconModule::clearTargetRadioSettings(&pkt);
}

/**
 * Legacy split sends the offer and the text on identical settings. They share one entry, so
 * releasing the first must not pull the settings out from under the second still queued.
 */
static void test_sidecar_legacySplitPair_sharesOneEntryUntilBothRelease(void)
{
    resetConfig();
    meshtastic_MeshPacket offerHalf = meshtastic_MeshPacket_init_zero;
    meshtastic_MeshPacket textHalf = meshtastic_MeshPacket_init_zero;
    offerHalf.id = 0x5EED0200;
    textHalf.id = 0x5EED0201;

    const MeshBeaconModule_TargetRadioSettings s = targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, true, 3,
                                                                  true, meshtastic_Config_LoRaConfig_RegionCode_US, "Split");
    MeshBeaconModule::setTargetRadioSettings(&offerHalf, s);
    MeshBeaconModule::setTargetRadioSettings(&textHalf, s);

    const MeshBeaconModule_TargetRadioSettings *a = MeshBeaconModule::getTargetRadioSettings(&offerHalf);
    const MeshBeaconModule_TargetRadioSettings *b = MeshBeaconModule::getTargetRadioSettings(&textHalf);
    TEST_ASSERT_NOT_NULL_MESSAGE(a, "the offer half must resolve");
    TEST_ASSERT_NOT_NULL_MESSAGE(b, "the text half must resolve");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(a, b, "identical settings must share one entry, not occupy two");

    MeshBeaconModule::clearTargetRadioSettings(&offerHalf);
    TEST_ASSERT_NULL_MESSAGE(MeshBeaconModule::getTargetRadioSettings(&offerHalf), "the released half must be gone");
    TEST_ASSERT_NOT_NULL_MESSAGE(MeshBeaconModule::getTargetRadioSettings(&textHalf),
                                 "the still-queued half must keep its settings, or it keys up on the home config");

    MeshBeaconModule::clearTargetRadioSettings(&textHalf);
    TEST_ASSERT_NULL_MESSAGE(MeshBeaconModule::getTargetRadioSettings(&textHalf), "the last release must free the entry");
    TEST_ASSERT_NULL_MESSAGE(MeshBeaconModule::getTargetRadioSettings(&offerHalf),
                             "and must not resurrect the half released before it");
}

/**
 * The shape legacy mode actually produces: four targets, each split into two packets. All eight
 * must hold their own settings at once - the table is four entries because the pairs share.
 */
static void test_sidecar_fourLegacySplitTargets_allFit(void)
{
    resetConfig();
    meshtastic_MeshPacket pkts[8];
    for (int t = 0; t < 4; t++) {
        // One distinct config per target; both of its packets carry that config.
        const MeshBeaconModule_TargetRadioSettings s =
            targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, true, (uint16_t)(t + 1), true,
                           meshtastic_Config_LoRaConfig_RegionCode_US, "Split");
        for (int half = 0; half < 2; half++) {
            meshtastic_MeshPacket &p = pkts[t * 2 + half];
            p = meshtastic_MeshPacket_init_zero;
            p.id = 0x5EED0300 + t * 2 + half;
            MeshBeaconModule::setTargetRadioSettings(&p, s);
        }
    }

    for (int i = 0; i < 8; i++) {
        const MeshBeaconModule_TargetRadioSettings *got = MeshBeaconModule::getTargetRadioSettings(&pkts[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(got, "every packet of a full legacy cycle must keep its settings");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)(i / 2 + 1), got->lora.channel_num,
                                         "each packet must resolve to its own target's slot");
    }

    for (int i = 0; i < 8; i++)
        MeshBeaconModule::clearTargetRadioSettings(&pkts[i]);
}

// Backdate an armed entry so it reads as older than a broadcast interval. The table entry is not
// itself const - only the accessor's return - so writing through it is well defined.
static void backdateArmedAt(const meshtastic_MeshPacket &p, uint32_t byMs)
{
    const MeshBeaconModule_TargetRadioSettings *got = MeshBeaconModule::getTargetRadioSettings(&p);
    TEST_ASSERT_NOT_NULL_MESSAGE(got, "backdating requires an armed entry");
    const_cast<MeshBeaconModule_TargetRadioSettings *>(got)->armedAtMs = millis() - byMs;
}

static const uint32_t kBeaconIntervalMs = (uint32_t)default_mesh_beacon_min_broadcast_interval_secs * 1000UL;

/**
 * A beacon still queued a broadcast interval later describes a mesh that has moved on, and the next
 * beacon is already due. It must be dropped, not transmitted on an hour-old description.
 */
static void test_sidecar_entryQueuedPastItsInterval_dropsThePacket(void)
{
    resetConfig();
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x5EED0400;
    MeshBeaconModule::setTargetRadioSettings(&pkt,
                                             targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, true, 1, false,
                                                            meshtastic_Config_LoRaConfig_RegionCode_EU_868, "Stale"));

    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::beaconTxConfigInvalid(&pkt),
                              "a freshly armed beacon on a valid config must not be dropped");

    backdateArmedAt(pkt, kBeaconIntervalMs + 1000);
    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconModule::beaconTxConfigInvalid(&pkt),
                             "a beacon queued past its broadcast interval must be dropped");

    MeshBeaconModule::clearTargetRadioSettings(&pkt);
}

/**
 * The table is one cycle deep, so an entry a previous cycle never sent must not hold its slot
 * against the next one - otherwise the new target evicts a live entry and keys up on the home config.
 */
static void test_sidecar_staleEntry_freesItsSlotForTheNextCycle(void)
{
    resetConfig();
    meshtastic_MeshPacket old[4];
    for (int t = 0; t < 4; t++) {
        old[t] = meshtastic_MeshPacket_init_zero;
        old[t].id = 0x5EED0500 + t;
        MeshBeaconModule::setTargetRadioSettings(&old[t], targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, true,
                                                                         (uint16_t)(t + 1), false,
                                                                         meshtastic_Config_LoRaConfig_RegionCode_EU_868, "Old"));
    }
    // The table is now full. Age one entry out, as a cycle that never drained would.
    backdateArmedAt(old[1], kBeaconIntervalMs + 1000);

    meshtastic_MeshPacket fresh = meshtastic_MeshPacket_init_zero;
    fresh.id = 0x5EED0600;
    MeshBeaconModule::setTargetRadioSettings(&fresh,
                                             targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, true, 2, false,
                                                            meshtastic_Config_LoRaConfig_RegionCode_EU_868, "New"));

    TEST_ASSERT_NOT_NULL_MESSAGE(MeshBeaconModule::getTargetRadioSettings(&fresh), "the new cycle must take the expired slot");
    TEST_ASSERT_NULL_MESSAGE(MeshBeaconModule::getTargetRadioSettings(&old[1]), "the expired entry must be gone");
    for (int t = 0; t < 4; t++) {
        if (t == 1)
            continue;
        TEST_ASSERT_NOT_NULL_MESSAGE(MeshBeaconModule::getTargetRadioSettings(&old[t]),
                                     "a live entry must not be evicted while an expired one was reapable");
    }

    MeshBeaconModule::clearAllTargetRadioSettings();
}

/**
 * A slot pinned outside the region must not be advertised verbatim - a userPrefs build installs the
 * offer without passing through admin validation, so fillOffer() has to resolve it itself.
 */
static void test_offer_pinnedOutOfRangeSlot_isNotAdvertisedVerbatim(void)
{
    resetConfig();
    static const uint8_t psk[16] = {0xEE, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", psk, sizeof(psk));
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_offer_channel_index = channels.getPrimaryIndex();
    moduleConfig.mesh_beacon.has_broadcast_offer_frequency_slot = true;
    moduleConfig.mesh_beacon.broadcast_offer_frequency_slot = RadioInterface::frequencySlotCount(config.lora) + 99;

    meshtastic_MeshBeacon beacon = meshtastic_MeshBeacon_init_zero;
    MeshBeaconModule::fillOffer(beacon, moduleConfig.mesh_beacon);

    // Unset is fine (the derived slot stands); what must never happen is the pin going out verbatim.
    TEST_ASSERT_TRUE_MESSAGE(beacon.offer_frequency_slot <= RadioInterface::frequencySlotCount(config.lora),
                             "an advertised slot must exist in the advertised region");
}

/**
 * An offer whose channel_index names a disabled slot advertises no channel, so it is an
 * announcement - the redundancy gate must not swallow it just because the index was set.
 */
static void test_broadcaster_offerOnDisabledSlot_isStillSent(void)
{
    resetConfig();
    static const uint8_t psk[16] = {0xEF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", psk, sizeof(psk));
    installTestSecondaryChannel(1, "Retired", psk, sizeof(psk));
    channelFile.channels[1].role = meshtastic_Channel_Role_DISABLED;
    channels.onConfigChanged();

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_offer_channel_index = 1;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = config.lora.modem_preset;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = 1; // falls back to the primary

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_MESSAGE(1, mockRouter->sentPackets.size(), "an offer with no channel must not be suppressed");
}

/**
 * A slot a receiver can work out for itself from the advertised region, preset and channel name
 * must not be spent on the air.
 */
static void test_offer_derivableSlot_isNotAdvertised(void)
{
    resetConfig();
    static const uint8_t offerPsk[16] = {0xBB, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                         0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Offer", offerPsk, sizeof(offerPsk));

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.has_broadcast_offer_channel_index = true;
    bcfg.broadcast_offer_channel_index = channels.getPrimaryIndex();

    meshtastic_MeshBeacon beacon = meshtastic_MeshBeacon_init_zero;
    MeshBeaconModule::fillOffer(beacon, bcfg);

    TEST_ASSERT_TRUE_MESSAGE(beacon.has_offer_channel, "the offer must carry the channel it names");
    TEST_ASSERT_FALSE_MESSAGE(beacon.has_offer_frequency_slot, "a derivable slot must not be advertised");
}

/**
 * Pinning the very slot derivation would produce is still derivable, so it stays off the air.
 */
static void test_offer_pinnedButDerivableSlot_isNotAdvertised(void)
{
    resetConfig();
    static const uint8_t offerPsk[16] = {0xBB, 0x11, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                         0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Offer", offerPsk, sizeof(offerPsk));

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.has_broadcast_offer_channel_index = true;
    bcfg.broadcast_offer_channel_index = channels.getPrimaryIndex();
    bcfg.has_broadcast_offer_frequency_slot = true;
    bcfg.broadcast_offer_frequency_slot =
        RadioInterface::resolveFrequencySlot(config.lora, channels.getName(channels.getPrimaryIndex()));

    meshtastic_MeshBeacon beacon = meshtastic_MeshBeacon_init_zero;
    MeshBeaconModule::fillOffer(beacon, bcfg);

    TEST_ASSERT_FALSE_MESSAGE(beacon.has_offer_frequency_slot, "a pin equal to the derived slot adds nothing");
}

/**
 * A pin that deviates from derivation is the whole point of the field, so it must be advertised.
 */
static void test_offer_pinnedSlot_isAdvertised(void)
{
    resetConfig();
    static const uint8_t offerPsk[16] = {0xBB, 0x22, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                         0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Offer", offerPsk, sizeof(offerPsk));
    // EU_868 holds a single 250kHz slot, so there is no second slot to deviate to.
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;

    const uint32_t derived = RadioInterface::resolveFrequencySlot(config.lora, channels.getName(channels.getPrimaryIndex()));
    const uint32_t pinned = (derived == 1) ? 2 : 1;

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.has_broadcast_offer_channel_index = true;
    bcfg.broadcast_offer_channel_index = channels.getPrimaryIndex();
    bcfg.has_broadcast_offer_frequency_slot = true;
    bcfg.broadcast_offer_frequency_slot = pinned;

    meshtastic_MeshBeacon beacon = meshtastic_MeshBeacon_init_zero;
    MeshBeaconModule::fillOffer(beacon, bcfg);

    TEST_ASSERT_TRUE_MESSAGE(beacon.has_offer_frequency_slot, "a deviating slot must be advertised");
    TEST_ASSERT_EQUAL_UINT32(pinned, beacon.offer_frequency_slot);
}

/**
 * A disabled slot keeps the settings of whatever channel was deleted from it, so the offer must
 * advertise nothing rather than hand out a retired name and PSK.
 */
static void test_offer_disabledChannelSlot_advertisesNothing(void)
{
    resetConfig();
    static const uint8_t offerPsk[16] = {0xBB, 0x33, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                         0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Offer", offerPsk, sizeof(offerPsk));
    const ChannelIndex idx = channels.getPrimaryIndex();
    meshtastic_Channel retired = channels.getByIndex(idx);
    retired.role = meshtastic_Channel_Role_DISABLED;
    channels.setChannel(retired);

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.has_broadcast_offer_channel_index = true;
    bcfg.broadcast_offer_channel_index = idx;

    meshtastic_MeshBeacon beacon = meshtastic_MeshBeacon_init_zero;
    MeshBeaconModule::fillOffer(beacon, bcfg);

    TEST_ASSERT_FALSE_MESSAGE(beacon.has_offer_channel, "a disabled slot must not be advertised");
}

/**
 * A pinned frequency_slot applies with no target channel at all, and moves the radio to exactly
 * that slot.
 */
static void test_broadcaster_targetPinnedSlot_armsThatSlot(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xCC, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    config.lora.channel_num = 0;
    // EU_868 holds a single 250kHz slot, so there is no second slot to pin.
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;

    const uint32_t home = RadioInterface::resolveFrequencySlot(config.lora, channels.getName(channels.getPrimaryIndex()));
    const uint32_t pinned = (home == 1) ? 2 : 1;

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true; // content to send
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_frequency_slot = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].frequency_slot = pinned;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_TRUE_MESSAGE(mockRouter->sentPackets.size() >= 1, "expected at least one send");
    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&mockRouter->sentPackets[0]),
                             "a pin away from the home slot must arm a switch");

    ReentrantRadioInterface iface;
    MeshBeaconModule::reconfigureForBeaconTX(&iface, &mockRouter->sentPackets[0]);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(pinned, config.lora.channel_num, "the radio must move to the pinned slot");
    MeshBeaconModule::reconfigureForBeaconTX(&iface, nullptr);
}

/**
 * A preset-only target derives its slot for THAT preset's bandwidth. Carrying the home slot number
 * over is a silent drop: a wider preset halves the count, so the number can fall outside the band.
 */
static void test_broadcaster_presetOnlyTargetOnNarrowerBand_isNotDropped(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xDD, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    // US/LONG_FAST is 250kHz and 104 slots; SHORT_TURBO is 500kHz and 52. Sit home above 52.
    meshtastic_Config_LoRaConfig turbo = config.lora;
    turbo.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;
    const uint32_t turboSlots = RadioInterface::frequencySlotCount(turbo);
    TEST_ASSERT_TRUE_MESSAGE(RadioInterface::frequencySlotCount(config.lora) > turboSlots, "need a narrower target band");
    config.lora.channel_num = turboSlots + 1;

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true; // content to send
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_preset = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_TRUE_MESSAGE(mockRouter->sentPackets.size() >= 1, "expected at least one send");
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::beaconTxConfigInvalid(&mockRouter->sentPackets[0]),
                              "a preset-only target must resolve a slot its own band can hold");

    ReentrantRadioInterface iface;
    MeshBeaconModule::reconfigureForBeaconTX(&iface, &mockRouter->sentPackets[0]);
    TEST_ASSERT_TRUE_MESSAGE(config.lora.channel_num >= 1 && config.lora.channel_num <= turboSlots,
                             "the armed slot must exist in the target preset's band");
    MeshBeaconModule::reconfigureForBeaconTX(&iface, nullptr);
}

/**
 * A node running custom modem params must still be switched onto a target's preset. applyModemConfig
 * only reads modem_preset when use_preset is set, so leaving it off makes a preset target a no-op.
 */
static void test_broadcaster_presetTargetOnCustomModemNode_switchesUsePreset(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xDE, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.use_preset = false; // custom bandwidth/SF/CR
    config.lora.bandwidth = 250;
    config.lora.spread_factor = 11;
    config.lora.coding_rate = 5;

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true; // content to send
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_preset = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_TRUE_MESSAGE(mockRouter->sentPackets.size() >= 1, "expected at least one send");
    ReentrantRadioInterface iface;
    MeshBeaconModule::reconfigureForBeaconTX(&iface, &mockRouter->sentPackets[0]);
    TEST_ASSERT_TRUE_MESSAGE(config.lora.use_preset, "a preset target must turn use_preset on to take effect");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, config.lora.modem_preset,
                              "the radio must move to the target preset");

    // The restore is gated on the switching packet no longer being live, as packetReleased() does.
    MeshBeaconModule::clearTargetRadioSettings(&mockRouter->sentPackets[0]);
    MeshBeaconModule::reconfigureForBeaconTX(&iface, nullptr);
    TEST_ASSERT_FALSE_MESSAGE(config.lora.use_preset, "the restore must give the custom modem params back");
}

/**
 * Pinning the slot the node already runs on must arm nothing - the pinned form of the 0-vs-resolved
 * trap, where a pin of the derived value has to compare equal against a channel_num still 0.
 */
static void test_broadcaster_targetPinnedHomeSlot_armsNoSwitch(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xCC, 0x11, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    config.lora.channel_num = 0; // derive the slot, as an unconfigured node does

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_frequency_slot = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].frequency_slot =
        RadioInterface::resolveFrequencySlot(config.lora, channels.getName(channels.getPrimaryIndex()));

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_TRUE_MESSAGE(mockRouter->sentPackets.size() >= 1, "expected at least one send");
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&mockRouter->sentPackets[0]),
                              "a pin equal to the home slot must arm no radio switch");
}

/**
 * Two channel-less targets pinning different slots are two destinations, so both go out. The dedup
 * key has to carry the slot, or a channel-less target compares equal to any other and is dropped.
 */
static void test_broadcaster_twoPinnedSlotsNoChannel_bothSent(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xC0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    // EU_868 holds a single slot, so there is no second slot to pin.
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.channel_num = 0;

    const uint32_t home = RadioInterface::resolveFrequencySlot(config.lora, channels.getName(channels.getPrimaryIndex()));
    const uint32_t pinned = (home == 1) ? 2 : 1;

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 2;
    // [0] takes the home slot by omission, [1] pins a different one. Same preset and region.
    moduleConfig.mesh_beacon.broadcast_targets[1].has_frequency_slot = true;
    moduleConfig.mesh_beacon.broadcast_targets[1].frequency_slot = pinned;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, mockRouter->sentPackets.size(),
                                     "a pinned slot is a distinct destination, not a duplicate");
}

/**
 * The mirror of the above: naming the primary index explicitly and leaving it unset are the same
 * destination when the target does not override the preset, so they must collapse to one packet.
 */
static void test_broadcaster_bareTargetAndPrimaryIndexTarget_dedupToOne(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xC1, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    config.lora.channel_num = 0; // derive, so the bare target and the named one land on one slot

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 2;
    moduleConfig.mesh_beacon.broadcast_targets[1].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[1].channel_index = channels.getPrimaryIndex();

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, mockRouter->sentPackets.size(),
                                     "naming the primary index is the same destination as omitting it");
}

/**
 * Router::send() frees the packet on every failure path, so the sidecar must be cleared by id -
 * reading it back off the packet is a use-after-free. Needs the coverage env's ASan to catch that.
 */
static void test_broadcaster_sendFailure_releasesTargetEntry(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xC2, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_preset = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW;

    // NO_CHANNEL is what perhapsEncode() returns for an unusable channel; it reaches us through
    // abortSendAndNak(), which has already released the packet.
    mockRouter->nextSendResult = (ErrorCode)meshtastic_Routing_Error_NO_CHANNEL;
    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();
    mockRouter->nextSendResult = ERRNO_OK;

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, mockRouter->sentPackets.size(), "expected one send attempt");
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&mockRouter->sentPackets[0]),
                              "a failed send must free its sidecar entry or the table leaks");
}

/**
 * A target already transmitting on the offered mesh must not carry the offer - everyone hearing it
 * is already there. Valid config at write time, redundant only at TX.
 */
static void test_broadcaster_offerMatchesTarget_offerIsOmitted(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xDD, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    moduleConfig.has_mesh_beacon = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "hello", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    moduleConfig.mesh_beacon.has_broadcast_offer_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_offer_channel_index = channels.getPrimaryIndex();
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = channels.getPrimaryIndex();

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_MESSAGE(1, mockRouter->sentPackets.size(), "the text must still go out");
    const meshtastic_MeshPacket &p = mockRouter->sentPackets[0];
    meshtastic_MeshBeacon decoded = meshtastic_MeshBeacon_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(p.decoded.payload.bytes, p.decoded.payload.size);
    TEST_ASSERT_TRUE(pb_decode(&stream, &meshtastic_MeshBeacon_msg, &decoded));
    TEST_ASSERT_EQUAL_STRING("hello", decoded.message);
    TEST_ASSERT_FALSE_MESSAGE(decoded.has_offer_channel, "a target already on the offered mesh must not carry the offer");
}

/**
 * The suppression is per target: an offer redundant for one target is still worth sending on
 * another, so it must not be dropped wholesale.
 */
static void test_broadcaster_offerMatchesOneTarget_stillSentOnTheOther(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xDD, 0x11, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    static const uint8_t otherPsk[16] = {0xEE, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                         0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestSecondaryChannel(1, "Other", otherPsk, sizeof(otherPsk));

    moduleConfig.has_mesh_beacon = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "hello", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    moduleConfig.mesh_beacon.has_broadcast_offer_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_offer_channel_index = channels.getPrimaryIndex();
    moduleConfig.mesh_beacon.broadcast_targets_count = 2;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = channels.getPrimaryIndex();
    moduleConfig.mesh_beacon.broadcast_targets[1].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[1].channel_index = 1;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_MESSAGE(2, mockRouter->sentPackets.size(), "both targets must be sent");
    meshtastic_MeshBeacon first = meshtastic_MeshBeacon_init_zero;
    meshtastic_MeshBeacon second = meshtastic_MeshBeacon_init_zero;
    pb_istream_t s0 =
        pb_istream_from_buffer(mockRouter->sentPackets[0].decoded.payload.bytes, mockRouter->sentPackets[0].decoded.payload.size);
    pb_istream_t s1 =
        pb_istream_from_buffer(mockRouter->sentPackets[1].decoded.payload.bytes, mockRouter->sentPackets[1].decoded.payload.size);
    TEST_ASSERT_TRUE(pb_decode(&s0, &meshtastic_MeshBeacon_msg, &first));
    TEST_ASSERT_TRUE(pb_decode(&s1, &meshtastic_MeshBeacon_msg, &second));
    TEST_ASSERT_FALSE_MESSAGE(first.has_offer_channel, "the matching target must not carry the offer");
    TEST_ASSERT_TRUE_MESSAGE(second.has_offer_channel, "a different channel is a different mesh, still worth offering");
}

/**
 * With no text there is nothing left once the offer is dropped, so that target sends nothing at
 * all rather than an empty beacon.
 */
static void test_broadcaster_offerMatchesTargetNoText_sendsNothing(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xDD, 0x22, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.broadcast_message[0] = '\0';
    moduleConfig.mesh_beacon.has_broadcast_offer_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_offer_channel_index = channels.getPrimaryIndex();
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = channels.getPrimaryIndex();

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_MESSAGE(0, mockRouter->sentPackets.size(), "nothing left to say, so nothing is sent");
}

// ---------------------------------------------------------------------------
// The four target shapes in one config, and the four offer-vs-setting cases.
// ---------------------------------------------------------------------------

// Decode a sent beacon packet's payload. Returns false if it is not a MeshBeacon.
static bool decodeBeaconPacket(const meshtastic_MeshPacket &p, meshtastic_MeshBeacon &out)
{
    out = meshtastic_MeshBeacon_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(p.decoded.payload.bytes, p.decoded.payload.size);
    return pb_decode(&stream, &meshtastic_MeshBeacon_msg, &out);
}

/**
 * Case 1: all four target shapes in a single config - the home channel, a second channel on the
 * same frequency slot, a different slot, and a different region/preset/slot together. Each must
 * reach the air as its own packet on its own settings.
 */
static void test_broadcaster_homeSameSlotOtherSlotAndOtherRegion_allSent(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xB0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t altPsk[16] = {0xB1, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                       0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    installTestSecondaryChannel(1, "Alt", altPsk, sizeof(altPsk));
    channels.onConfigChanged();
    // US holds many slots, so there is a second slot to pin. EU_868 holds one.
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.channel_num = 0;

    const uint32_t homeSlot = RadioInterface::resolveFrequencySlot(config.lora, channels.getName(channels.getPrimaryIndex()));
    const uint32_t otherSlot = (homeSlot == 1) ? 2 : 1;

    moduleConfig.has_mesh_beacon = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "hi", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    moduleConfig.mesh_beacon.broadcast_targets_count = 4;

    // [0] the home channel, inheriting everything.
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = channels.getPrimaryIndex();
    moduleConfig.mesh_beacon.broadcast_targets[0].has_frequency_slot = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].frequency_slot = homeSlot;
    // [1] the other channel, pinned to the SAME slot as home - the channel differs, the RF does not.
    moduleConfig.mesh_beacon.broadcast_targets[1].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[1].channel_index = 1;
    moduleConfig.mesh_beacon.broadcast_targets[1].has_frequency_slot = true;
    moduleConfig.mesh_beacon.broadcast_targets[1].frequency_slot = homeSlot;
    // [2] the other channel again, on a different slot.
    moduleConfig.mesh_beacon.broadcast_targets[2].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[2].channel_index = 1;
    moduleConfig.mesh_beacon.broadcast_targets[2].has_frequency_slot = true;
    moduleConfig.mesh_beacon.broadcast_targets[2].frequency_slot = otherSlot;
    // [3] a different region, preset and slot together.
    moduleConfig.mesh_beacon.broadcast_targets[3].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[3].channel_index = channels.getPrimaryIndex();
    moduleConfig.mesh_beacon.broadcast_targets[3].region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    moduleConfig.mesh_beacon.broadcast_targets[3].has_preset = true;
    moduleConfig.mesh_beacon.broadcast_targets[3].preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.broadcast_targets[3].has_frequency_slot = true;
    moduleConfig.mesh_beacon.broadcast_targets[3].frequency_slot = 1; // EU_868 holds exactly one

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_MESSAGE(4, mockRouter->sentPackets.size(),
                              "four targets differing in channel, slot or region are four transmissions");

    // [0] is the running config, so it arms no switch; the rest each differ in RF or channel.
    TEST_ASSERT_EQUAL_MESSAGE(channels.getPrimaryIndex(), mockRouter->sentPackets[0].channel, "target 0 rides the primary");
    TEST_ASSERT_EQUAL_MESSAGE(1, mockRouter->sentPackets[1].channel, "target 1 rides the second channel");
    TEST_ASSERT_EQUAL_MESSAGE(1, mockRouter->sentPackets[2].channel, "target 2 rides the second channel too");
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&mockRouter->sentPackets[0]),
                              "the home target runs the live config, so it must arm no radio switch");
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&mockRouter->sentPackets[1]),
                              "a channel change on the home slot is not a radio change");

    const MeshBeaconModule_TargetRadioSettings *other = MeshBeaconModule::getTargetRadioSettings(&mockRouter->sentPackets[2]);
    TEST_ASSERT_NOT_NULL_MESSAGE(other, "a different frequency slot must arm a switch");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(otherSlot, other->lora.channel_num, "and it must be the slot the target named");

    const MeshBeaconModule_TargetRadioSettings *far = MeshBeaconModule::getTargetRadioSettings(&mockRouter->sentPackets[3]);
    TEST_ASSERT_NOT_NULL_MESSAGE(far, "a different region must arm a switch");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_RegionCode_EU_868, far->lora.region, "on the target's region");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, far->lora.modem_preset,
                              "on the target's preset");
}

/**
 * Case 2: the offer describes a mesh other than the one the node runs. Region, preset and slot all
 * differ, and all three have to reach the air - that is the whole point of an offer.
 */
static void test_offer_differentFromHome_advertisesItsOwnSettings(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xB2, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t altPsk[16] = {0xB3, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                       0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    installTestSecondaryChannel(1, "Offered", altPsk, sizeof(altPsk));
    channels.onConfigChanged();
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    meshtastic_Config_LoRaConfig offered = config.lora;
    offered.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    const uint32_t derived = RadioInterface::resolveFrequencySlot(offered, "Offered");
    const uint32_t pinned = (derived == 1) ? 2 : 1; // deliberately not the derivable one

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_offer_channel_index = 1;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    moduleConfig.mesh_beacon.has_broadcast_offer_frequency_slot = true;
    moduleConfig.mesh_beacon.broadcast_offer_frequency_slot = pinned;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_MESSAGE(1, mockRouter->sentPackets.size(), "an offer alone is still a beacon");
    meshtastic_MeshBeacon decoded;
    TEST_ASSERT_TRUE(decodeBeaconPacket(mockRouter->sentPackets[0], decoded));
    TEST_ASSERT_TRUE_MESSAGE(decoded.has_offer_channel, "the offered channel must be advertised");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Offered", decoded.offer_channel.name, "and it must be the channel named, not the primary");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, decoded.offer_preset,
                              "a preset unlike home must be advertised");
    TEST_ASSERT_TRUE_MESSAGE(decoded.has_offer_frequency_slot, "a slot the name does not hash to must be advertised");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(pinned, decoded.offer_frequency_slot, "and it must be the slot pinned");
}

/**
 * Case 3: the offer describes the mesh the node already runs, advertised onto a target that is
 * somewhere else - "I am over here, come and join me". The offer is only suppressed on a target
 * that already runs it (case 5), so here it must go out in full.
 */
static void test_offer_sameAsHome_isAdvertisedOntoAnotherMesh(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xB4, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t awayPsk[16] = {0xB8, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    installTestSecondaryChannel(1, "Away", awayPsk, sizeof(awayPsk));
    channels.onConfigChanged();
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    moduleConfig.has_mesh_beacon = true;
    // The offer is everything the node already is: its own channel, preset and region.
    moduleConfig.mesh_beacon.has_broadcast_offer_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_offer_channel_index = channels.getPrimaryIndex();
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    moduleConfig.mesh_beacon.broadcast_offer_region = meshtastic_Config_LoRaConfig_RegionCode_US;
    // The target is the other mesh, which is the audience for that offer.
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = 1;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_MESSAGE(1, mockRouter->sentPackets.size(), "an offer matching home is still worth broadcasting");
    TEST_ASSERT_EQUAL_MESSAGE(1, mockRouter->sentPackets[0].channel, "onto the mesh that does not already have it");
    meshtastic_MeshBeacon decoded;
    TEST_ASSERT_TRUE(decodeBeaconPacket(mockRouter->sentPackets[0], decoded));
    TEST_ASSERT_TRUE_MESSAGE(decoded.has_offer_channel, "the offered channel must be advertised");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Home", decoded.offer_channel.name, "which is the node's own");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, decoded.offer_preset, "on the node's preset");
    TEST_ASSERT_FALSE_MESSAGE(decoded.has_offer_frequency_slot,
                              "a receiver derives this slot from the name, so spending bytes on it is waste");
}

/**
 * Case 4: the offer names the same mesh as one of the broadcast targets. A legal configuration -
 * the beacon still goes out on that target, carrying its text.
 */
static void test_offer_sameAsABeaconTarget_targetIsStillSent(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xB5, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    static const uint8_t altPsk[16] = {0xB6, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                       0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    installTestSecondaryChannel(1, "Shared", altPsk, sizeof(altPsk));
    channels.onConfigChanged();

    moduleConfig.has_mesh_beacon = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "join us", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    // The offer and the single target name the same channel.
    moduleConfig.mesh_beacon.has_broadcast_offer_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_offer_channel_index = 1;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = 1;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_MESSAGE(1, mockRouter->sentPackets.size(), "naming the same mesh twice is legal, and still transmits");
    TEST_ASSERT_EQUAL_MESSAGE(1, mockRouter->sentPackets[0].channel, "on the channel both name");
    meshtastic_MeshBeacon decoded;
    TEST_ASSERT_TRUE(decodeBeaconPacket(mockRouter->sentPackets[0], decoded));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("join us", decoded.message, "the text survives");
}

/**
 * Case 5: the offer is exactly the mesh the packet is going out on, so it tells the listener
 * nothing it does not already have. The offer is dropped; the text it was riding with is not.
 */
static void test_offer_identicalToItsOwnTarget_offerDroppedTextKept(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xB7, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));
    channels.onConfigChanged();
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;

    moduleConfig.has_mesh_beacon = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "still here", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    // Offer and target are the same channel, preset, region and slot - identical in every field.
    moduleConfig.mesh_beacon.has_broadcast_offer_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_offer_channel_index = channels.getPrimaryIndex();
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    moduleConfig.mesh_beacon.broadcast_offer_region = meshtastic_Config_LoRaConfig_RegionCode_US;
    moduleConfig.mesh_beacon.broadcast_targets_count = 1;
    moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].channel_index = channels.getPrimaryIndex();
    moduleConfig.mesh_beacon.broadcast_targets[0].has_preset = true;
    moduleConfig.mesh_beacon.broadcast_targets[0].preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    moduleConfig.mesh_beacon.broadcast_targets[0].region = meshtastic_Config_LoRaConfig_RegionCode_US;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_MESSAGE(1, mockRouter->sentPackets.size(), "the text still goes out");
    meshtastic_MeshBeacon decoded;
    TEST_ASSERT_TRUE(decodeBeaconPacket(mockRouter->sentPackets[0], decoded));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("still here", decoded.message, "the text must not be dropped with the offer");
    TEST_ASSERT_FALSE_MESSAGE(decoded.has_offer_channel,
                              "a listener on this packet already has the offer, so it must not be spent on the air");
}

/**
 * An offer naming no channel is an announcement, not an invitation elsewhere, so it is sent even on
 * a target already running its preset and region. Guards the redundancy gate against eating it.
 */
static void test_broadcaster_offerWithoutChannelMatchingTarget_isStillSent(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true;
    moduleConfig.mesh_beacon.broadcast_offer_preset = config.lora.modem_preset;
    moduleConfig.mesh_beacon.broadcast_offer_region = config.lora.region;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_MESSAGE(1, mockRouter->sentPackets.size(), "a channel-less offer must not be suppressed");
}

/**
 * A pre-consolidation client still sends broadcast_send_as_node on MeshBeaconConfig tag 3. nanopb
 * must skip a retired tag as unknown, or that client cannot write a beacon config at all.
 */
static void test_proto_retiredSendAsNodeTag_isSkipped(void)
{
    const uint8_t wire[] = {
        0x08, 0x03,       // field 1 (flags) varint 3
        0x18, 0x01,       // field 3 (retired broadcast_send_as_node) varint 1
        0x58, 0x90, 0x1c, // field 11 (broadcast_interval_secs) varint 3600
    };
    meshtastic_ModuleConfig_MeshBeaconConfig decoded = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(wire, sizeof(wire));

    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&stream, &meshtastic_ModuleConfig_MeshBeaconConfig_msg, &decoded),
                             "a retired tag must be skipped, not fail the whole decode");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, decoded.flags, "fields before the retired tag must survive");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3600, decoded.broadcast_interval_secs, "fields after it must survive too");
}

/**
 * BroadcastTarget tag 3 was an embedded ChannelSettings before channel_index replaced it, so the
 * skipped field is length-delimited rather than a varint. Different skip path in nanopb.
 */
static void test_proto_retiredTargetChannelTag_isSkipped(void)
{
    const uint8_t wire[] = {
        0x08, 0x04,             // field 1 (preset) varint 4
        0x1a, 0x02, 0x0a, 0x00, // field 3 (retired ChannelSettings) length-delimited, 2 bytes
        0x20, 0x07,             // field 4 (channel_index) varint 7
    };
    meshtastic_ModuleConfig_MeshBeaconConfig_BroadcastTarget decoded =
        meshtastic_ModuleConfig_MeshBeaconConfig_BroadcastTarget_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(wire, sizeof(wire));

    TEST_ASSERT_TRUE_MESSAGE(pb_decode(&stream, &meshtastic_ModuleConfig_MeshBeaconConfig_BroadcastTarget_msg, &decoded),
                             "a retired length-delimited tag must be skipped, not fail the decode");
    TEST_ASSERT_TRUE(decoded.has_preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, decoded.preset);
    TEST_ASSERT_TRUE(decoded.has_channel_index);
    TEST_ASSERT_EQUAL_UINT32(7, decoded.channel_index);
}

/**
 * The restore must clear its guard before reconfiguring, or completeSending() re-enters the restore
 * branch and it reconfigures the radio once per level until the stack runs out. Seen in the field as
 * a run of "Beacon: restore radio config after TX" with a full applyModemConfig() between each.
 */
static void test_beaconRestore_isNotReenteredByCompleteSending(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    ReentrantRadioInterface radio;
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x5EED0001;
    MeshBeaconModule::setTargetRadioSettings(&pkt, targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, true, 0,
                                                                  false, meshtastic_Config_LoRaConfig_RegionCode_UNSET));

    // Switch to the beacon config. Not the case under test, so leave re-entry off.
    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconModule::reconfigureForBeaconTX(&radio, &pkt), "beacon switch should have applied");
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, config.lora.modem_preset,
                                  "switch must leave the radio on the beacon preset");

    // Now restore, with reconfigure() re-entering exactly as completeSending() does.
    MeshBeaconModule::clearTargetRadioSettings(&pkt);
    radio.reconfigureCalls = 0;
    radio.reenterOnReconfigure = true;
    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconModule::reconfigureForBeaconTX(&radio, nullptr), "restore should have applied");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, radio.reconfigureCalls, "restore must reconfigure the radio exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, config.lora.modem_preset,
                                  "restore must put the home preset back");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Home", channels.getByIndex(channels.getPrimaryIndex()).settings.name,
                                     "restore must put the home channel back");
}

/**
 * A second switch before the restore has run must survive the same re-entry. completeSending() calls
 * in with a null packet, which reads as "restore" - so without the guard it would undo the switch that
 * is still being applied, leaving the beacon to transmit on the home channel instead of its target.
 */
static void test_beaconSwitch_isNotUndoneByCompleteSending(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    ReentrantRadioInterface radio;
    meshtastic_MeshPacket first = meshtastic_MeshPacket_init_zero;
    first.id = 0x5EED0002;
    MeshBeaconModule::setTargetRadioSettings(&first, targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, true, 0,
                                                                    false, meshtastic_Config_LoRaConfig_RegionCode_UNSET));
    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconModule::reconfigureForBeaconTX(&radio, &first), "first switch should have applied");

    // Second switch with the restore still outstanding, and reconfigure() re-entering.
    meshtastic_MeshPacket second = meshtastic_MeshPacket_init_zero;
    second.id = 0x5EED0003;
    MeshBeaconModule::setTargetRadioSettings(&second, targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST, true, 0,
                                                                     false, meshtastic_Config_LoRaConfig_RegionCode_UNSET));
    radio.reconfigureCalls = 0;
    radio.reenterOnReconfigure = true;
    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconModule::reconfigureForBeaconTX(&radio, &second), "second switch should have applied");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, radio.reconfigureCalls, "second switch must reconfigure the radio exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST, config.lora.modem_preset,
                                  "second switch must not be undone mid-flight");

    // The home config must still be recoverable afterwards - the snapshot survives a second switch.
    radio.reenterOnReconfigure = false;
    MeshBeaconModule::clearTargetRadioSettings(&first);
    MeshBeaconModule::clearTargetRadioSettings(&second);
    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconModule::reconfigureForBeaconTX(&radio, nullptr), "restore should have applied");
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, config.lora.modem_preset,
                                  "restore must return to the home preset, not the first beacon target");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Home", channels.getByIndex(channels.getPrimaryIndex()).settings.name,
                                     "restore must return to the home channel");
}

/** A restore with nothing switched must do nothing at all - the guard is what makes re-entry safe. */
static void test_beaconRestore_withoutSwitch_isNoOp(void)
{
    resetConfig();
    ReentrantRadioInterface radio;

    // reconfigureForBeaconTX() keeps its switched/not-switched state in a function-local static, so an
    // earlier test that aborted mid-way can leave a switch outstanding. Drain it before asserting.
    MeshBeaconModule::reconfigureForBeaconTX(&radio, nullptr);

    radio.reconfigureCalls = 0;
    radio.reenterOnReconfigure = true;
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::reconfigureForBeaconTX(&radio, nullptr), "restore without a switch is a no-op");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, radio.reconfigureCalls, "no-op restore must not touch the radio");
}

/**
 * The restore is driven by "our beacon finished", not by "the radio changed state". completeSending()
 * clears a packet's target settings before restoring, so a caller that arrives without that - the
 * pre-TX channel scan standbys the radio, and setStandby() calls completeSending() - must be refused.
 * Otherwise the home config goes back under a beacon that has not keyed up yet, and it transmits on
 * the wrong preset with the beacon channel hash already stamped on it.
 */
static void test_beaconRestore_deferredUntilPacketCompletes(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    ReentrantRadioInterface radio;
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x5EED0004;
    MeshBeaconModule::setTargetRadioSettings(&pkt, targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, true, 0,
                                                                  false, meshtastic_Config_LoRaConfig_RegionCode_UNSET));
    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconModule::reconfigureForBeaconTX(&radio, &pkt), "beacon switch should have applied");

    // The packet has not been sent yet, so its target settings are still live.
    radio.reconfigureCalls = 0;
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::reconfigureForBeaconTX(&radio, nullptr),
                              "restore must be refused while the beacon is still outstanding");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, radio.reconfigureCalls, "a refused restore must not touch the radio");
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, config.lora.modem_preset,
                                  "the beacon preset must still be in place when the packet keys up");

    // completeSending() clears the target settings first; only then is the restore ours to make.
    MeshBeaconModule::clearTargetRadioSettings(&pkt);
    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconModule::reconfigureForBeaconTX(&radio, nullptr), "restore should have applied");
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, config.lora.modem_preset,
                                  "restore must put the home preset back");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Home", channels.getByIndex(channels.getPrimaryIndex()).settings.name,
                                     "restore must put the home channel back");
}

// ---------------------------------------------------------------------------
// MeshBeaconTxHook (the radio driver's view of the beacon)
// ---------------------------------------------------------------------------

/**
 * Normal traffic must pass straight through the hook: no radio switch, no drop, and nothing claimed
 * that would stop the driver listening on a busy channel.
 */
static void test_txHook_normalPacket_isSend(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    MeshBeaconTxHook hook;
    ReentrantRadioInterface radio;
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x7A000001;

    TEST_ASSERT_EQUAL_INT_MESSAGE(RadioTxHook::PRETX_SEND, RadioTxHooks::beforeTransmit(&radio, &pkt),
                                  "a packet with no beacon target must transmit as usual");
    TEST_ASSERT_FALSE_MESSAGE(RadioTxHooks::holdsRadio(&pkt), "normal traffic must not claim the radio");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, radio.reconfigureCalls, "normal traffic must not reconfigure the radio");
}

/**
 * A beacon asks the driver for a fresh transmit delay, because the switch leaves the radio on a
 * channel the last channel scan never covered.
 */
static void test_txHook_beaconPacket_isDefer(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    MeshBeaconTxHook hook;
    ReentrantRadioInterface radio;
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x7A000002;
    MeshBeaconModule::setTargetRadioSettings(&pkt, targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, true, 0,
                                                                  false, meshtastic_Config_LoRaConfig_RegionCode_UNSET));

    TEST_ASSERT_EQUAL_INT_MESSAGE(RadioTxHook::PRETX_DEFER, RadioTxHooks::beforeTransmit(&radio, &pkt),
                                  "a beacon switch must defer the transmit");
    TEST_ASSERT_TRUE_MESSAGE(RadioTxHooks::holdsRadio(&pkt), "a queued beacon must claim the radio");
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, config.lora.modem_preset,
                                  "the hook must leave the radio on the beacon preset");

    // packetReleased() is what the driver calls once the packet is sent, cancelled or dropped.
    RadioTxHooks::packetReleased(&radio, &pkt);
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, config.lora.modem_preset,
                                  "releasing the packet must put the home preset back");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Home", channels.getByIndex(channels.getPrimaryIndex()).settings.name,
                                     "releasing the packet must put the home channel back");
}

/**
 * SHORT_TURBO is not legal on EU_868, so the beacon has nowhere valid to transmit. The driver must be
 * told to drop it rather than let it fall through onto the home config.
 */
static void test_txHook_invalidTarget_isDrop(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    MeshBeaconTxHook hook;
    ReentrantRadioInterface radio;
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x7A000003;
    MeshBeaconModule::setTargetRadioSettings(&pkt, targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, true, 0,
                                                                  false, meshtastic_Config_LoRaConfig_RegionCode_UNSET));

    TEST_ASSERT_EQUAL_INT_MESSAGE(RadioTxHook::PRETX_DROP, RadioTxHooks::beforeTransmit(&radio, &pkt),
                                  "an invalid target config must be dropped, not transmitted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, radio.reconfigureCalls, "a dropped beacon must not reconfigure the radio");
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, config.lora.modem_preset,
                                  "a dropped beacon must leave the home preset alone");

    RadioTxHooks::packetReleased(&radio, &pkt); // the driver's drop path, so the sidecar entry is freed
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&pkt), "the dropped packet's target must be released");
}

/**
 * The driver drops one packet and carries on with the queue, so a drop must take only its own
 * entry with it - the targets still queued behind it have to reach the air on their own settings.
 */
static void test_txHook_dropTakesOnlyItsOwnTarget(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xC1, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    MeshBeaconTxHook hook; // registers itself, so RadioTxHooks routes through the beacon
    ReentrantRadioInterface radio;
    meshtastic_MeshPacket bad = meshtastic_MeshPacket_init_zero;
    meshtastic_MeshPacket good = meshtastic_MeshPacket_init_zero;
    bad.id = 0x7A000010;
    good.id = 0x7A000011;

    // SHORT_TURBO is invalid on EU_868, so this one is refused before transmit.
    MeshBeaconModule::setTargetRadioSettings(&bad, targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, true, 0,
                                                                  false, meshtastic_Config_LoRaConfig_RegionCode_UNSET));
    MeshBeaconModule::setTargetRadioSettings(&good, targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, true, 1,
                                                                   false, meshtastic_Config_LoRaConfig_RegionCode_EU_868));

    TEST_ASSERT_EQUAL_INT_MESSAGE(RadioTxHook::PRETX_DROP, RadioTxHooks::beforeTransmit(&radio, &bad),
                                  "the invalid target must be dropped");
    RadioTxHooks::packetReleased(&radio, &bad); // the driver's drop path

    const MeshBeaconModule_TargetRadioSettings *survivor = MeshBeaconModule::getTargetRadioSettings(&good);
    TEST_ASSERT_NOT_NULL_MESSAGE(survivor, "a drop must not release the entry of a packet still queued");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, survivor->lora.modem_preset,
                              "and must not disturb its settings");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, survivor->lora.channel_num, "including the slot it will switch to");

    MeshBeaconModule::clearAllTargetRadioSettings();
}

/**
 * The legacy split pair shares one entry, so a drop of the offer half runs the release path against
 * settings the text half is still queued on. The survivor must keep them, or it keys up on the home
 * config - the exact failure the sidecar exists to prevent.
 */
static void test_txHook_dropOfOneSplitHalf_leavesTheOtherArmed(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xC2, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    MeshBeaconTxHook hook; // registers itself, so RadioTxHooks routes through the beacon
    ReentrantRadioInterface radio;
    meshtastic_MeshPacket offerHalf = meshtastic_MeshPacket_init_zero;
    meshtastic_MeshPacket textHalf = meshtastic_MeshPacket_init_zero;
    offerHalf.id = 0x7A000020;
    textHalf.id = 0x7A000021;

    // Identical settings, so both halves land on one entry - what legacy split produces.
    const MeshBeaconModule_TargetRadioSettings s = targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, true, 1,
                                                                  true, meshtastic_Config_LoRaConfig_RegionCode_EU_868);
    MeshBeaconModule::setTargetRadioSettings(&offerHalf, s);
    MeshBeaconModule::setTargetRadioSettings(&textHalf, s);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(MeshBeaconModule::getTargetRadioSettings(&offerHalf),
                                  MeshBeaconModule::getTargetRadioSettings(&textHalf), "the pair must share one entry");

    RadioTxHooks::packetReleased(&radio, &offerHalf); // the driver drops or finishes the first half

    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&offerHalf), "the released half must be gone");
    const MeshBeaconModule_TargetRadioSettings *survivor = MeshBeaconModule::getTargetRadioSettings(&textHalf);
    TEST_ASSERT_NOT_NULL_MESSAGE(survivor, "the half still queued must keep the shared entry");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, survivor->lora.modem_preset,
                              "with its settings intact");
    TEST_ASSERT_TRUE_MESSAGE(survivor->legacyHopOverride, "including the hop override the split depends on");

    // Releasing the last id compacts a duplicate of it into the tail slot. Every reader bounds by
    // idCount, so neither half may answer afterwards - iterate ids[] by its size and both come back.
    RadioTxHooks::packetReleased(&radio, &textHalf);
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&textHalf), "the last half released must be gone");
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&offerHalf),
                              "and the half released first must not come back with it");

    // The entry is genuinely free, not merely unreadable: a fresh target must be able to take it.
    meshtastic_MeshPacket reuse = meshtastic_MeshPacket_init_zero;
    reuse.id = 0x7A000022;
    MeshBeaconModule::setTargetRadioSettings(&reuse, targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, true, 1,
                                                                    false, meshtastic_Config_LoRaConfig_RegionCode_EU_868));
    TEST_ASSERT_NOT_NULL_MESSAGE(MeshBeaconModule::getTargetRadioSettings(&reuse), "the freed entry must be reusable");
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&offerHalf),
                              "and reusing it must not revive either released id");
    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&textHalf),
                              "nor the second, whose value is still sitting in the tail slot");

    MeshBeaconModule::clearAllTargetRadioSettings();
}

/**
 * The hook list is what keeps the driver free of module includes: with nothing registered every call
 * is a no-op, so a build without the beacon module behaves exactly as one with beacons idle.
 */
static void test_txHook_unregistered_isNoOp(void)
{
    resetConfig();
    ReentrantRadioInterface radio;
    meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_zero;
    pkt.id = 0x7A000004;
    MeshBeaconModule::setTargetRadioSettings(&pkt, targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, true, 0,
                                                                  false, meshtastic_Config_LoRaConfig_RegionCode_UNSET));

    TEST_ASSERT_EQUAL_INT_MESSAGE(RadioTxHook::PRETX_SEND, RadioTxHooks::beforeTransmit(&radio, &pkt),
                                  "with no hook registered even a beacon is ordinary traffic to the driver");
    TEST_ASSERT_FALSE_MESSAGE(RadioTxHooks::holdsRadio(&pkt), "with no hook registered nothing claims the radio");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, radio.reconfigureCalls, "with no hook registered the radio is never reconfigured");

    MeshBeaconModule::clearTargetRadioSettings(&pkt);
}

/**
 * A higher-priority packet can enqueue ahead of a still-queued beacon, so the driver asks the hook about
 * an untagged packet while the beacon that armed the switch is live. The restore gate is right to hold
 * off a release then, and wrong to hold off this: the untagged packet is about to key up, and without
 * the restore it transmits on the beacon's preset, slot and region.
 */
static void test_txHook_untaggedPacketAheadOfQueuedBeacon_restoresHome(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    MeshBeaconTxHook hook;
    ReentrantRadioInterface radio;

    // The beacon reaches the head of the queue and the hook switches the radio for it.
    meshtastic_MeshPacket beacon = meshtastic_MeshPacket_init_zero;
    beacon.id = 0x7A000005;
    MeshBeaconModule::setTargetRadioSettings(&beacon, targetSettings(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, true, 0,
                                                                     false, meshtastic_Config_LoRaConfig_RegionCode_UNSET));
    TEST_ASSERT_EQUAL_INT_MESSAGE(RadioTxHook::PRETX_DEFER, RadioTxHooks::beforeTransmit(&radio, &beacon),
                                  "the beacon switch should have applied");
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, config.lora.modem_preset,
                                  "the radio must be on the beacon preset before the queue jump");

    // An ordinary packet now jumps the queue. The beacon is still queued, so its target is still live.
    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconModule::hasTargetRadioSettings(&beacon),
                             "the queued beacon must still hold its target, or this is not the case under test");
    meshtastic_MeshPacket ordinary = meshtastic_MeshPacket_init_zero;
    ordinary.id = 0x7A000006;

    TEST_ASSERT_EQUAL_INT_MESSAGE(RadioTxHook::PRETX_DEFER, RadioTxHooks::beforeTransmit(&radio, &ordinary),
                                  "restoring the radio owes the driver a fresh delay and scan");
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, config.lora.modem_preset,
                                  "an untagged packet must never transmit on the beacon preset");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Home", channels.getByIndex(channels.getPrimaryIndex()).settings.name,
                                     "an untagged packet must never transmit on the beacon channel");

    // The beacon is not lost by the restore: it switches the radio back when it next reaches the head.
    TEST_ASSERT_EQUAL_INT_MESSAGE(RadioTxHook::PRETX_DEFER, RadioTxHooks::beforeTransmit(&radio, &beacon),
                                  "the beacon must switch back when it reaches the head again");
    TEST_ASSERT_EQUAL_INT_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW, config.lora.modem_preset,
                                  "the beacon must be back on its target preset");

    MeshBeaconModule::clearTargetRadioSettings(&beacon);
    RadioTxHooks::packetReleased(&radio, &beacon);
}

} // namespace

// ===========================================================================
// Unity lifecycle
// ===========================================================================

void setUp(void)
{
    testAirTime = new AirTime();
    airTime = testAirTime;

    mockSvc = new MockMeshService();
    service = mockSvc;

    mockRouter = new MockRouter();
    router = mockRouter;

    testAdmin = new AdminModuleTestShim();
}

void tearDown(void)
{
    meshBeaconBroadcastModule = nullptr;

    delete testAdmin;
    testAdmin = nullptr;

    // Drain any packets the listener delivered via sendToPhone() (toPhoneQueue takes ownership and
    // nothing else dequeues them in tests) so they are returned to packetPool - otherwise they leak
    // and LeakSanitizer aborts the process at exit.
    if (mockSvc) {
        meshtastic_MeshPacket *p;
        while ((p = mockSvc->getForPhone()) != nullptr)
            mockSvc->releaseToPool(p);
    }

    service = nullptr;
    delete mockSvc;
    mockSvc = nullptr;

    router = nullptr;
    delete mockRouter;
    mockRouter = nullptr;

    airTime = nullptr;
    delete testAirTime;
    testAirTime = nullptr;
}

BEACON_TEST_ENTRY void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();

    printf("\n=== AdminModule config validation ===\n");

    RUN_TEST(test_adminValidation_turboPresetOnEU868_isClamped);
    RUN_TEST(test_adminValidation_longTurboPresetOnEU868_isClamped);
    RUN_TEST(test_adminValidation_turboPresetOnUS_isAccepted);
    RUN_TEST(test_adminValidation_mediumTurboPresetOnEU868_isClamped);
    RUN_TEST(test_adminValidation_mediumTurboPresetOnUS_isAccepted);
    RUN_TEST(test_adminValidation_unknownOfferRegion_isCleared);
    RUN_TEST(test_adminValidation_validOfferRegion_isPreserved);
    RUN_TEST(test_adminValidation_targetUnknownRegion_isCleared);
    RUN_TEST(test_adminValidation_targetInvalidPresetForRegion_clampsPresetKeepsChannel);
    RUN_TEST(test_adminValidation_targetValidPresetForRegion_isPreserved);
    RUN_TEST(test_adminValidation_targetChannelIndexOutOfRange_isCleared);
    RUN_TEST(test_adminValidation_targetChannelIndexInRange_isPreserved);
    RUN_TEST(test_adminValidation_targetFrequencySlotOutOfRange_isCleared);
    RUN_TEST(test_adminValidation_targetFrequencySlotZero_isCleared);
    RUN_TEST(test_adminValidation_targetFrequencySlotInRange_isPreserved);
    RUN_TEST(test_adminValidation_offerFrequencySlotOutOfRange_isCleared);
    RUN_TEST(test_adminValidation_offerChannelIndexOutOfRange_isCleared);
    RUN_TEST(test_adminValidation_retiredChannel_clearsOfferAndTarget);
    RUN_TEST(test_adminValidation_offerInvalidPreset_clampsAndKeepsRest);
    RUN_TEST(test_adminValidation_offerUnknownRegion_keepsValidPreset);
    RUN_TEST(test_adminValidation_messageTooLong_isTruncatedAt100);
    RUN_TEST(test_adminValidation_intervalTooLow_isClamped);
    RUN_TEST(test_adminValidation_intervalTooHigh_isPreserved);
    RUN_TEST(test_adminValidation_intervalZero_isNotClamped);
    RUN_TEST(test_adminValidation_longFastOfferPreset_isPreserved);
    RUN_TEST(test_adminValidation_validSave_invalidatesCache);

    printf("\n=== Broadcaster payload cache ===\n");

    RUN_TEST(test_broadcaster_rebuildCache_producesNonEmptyPayload);
    RUN_TEST(test_broadcaster_rebuildCache_payloadDecodesCorrectly);
    RUN_TEST(test_broadcaster_rebuildCache_offerFieldsEncoded);
    RUN_TEST(test_broadcaster_invalidateCache_setsDirtyFlag);
    RUN_TEST(test_broadcaster_rebuildCache_idempotent);

    printf("\n=== Broadcaster sendBeacon ===\n");

    RUN_TEST(test_broadcaster_sendBeacon_fromIsLocalNodeWhenUnset);
    RUN_TEST(test_broadcaster_sendBeacon_addressedToBroadcast);
    RUN_TEST(test_broadcaster_sendBeacon_usesBeaconPortnum);
    RUN_TEST(test_broadcaster_sendBeacon_fallsBackToTextMessagePortnum);
    RUN_TEST(test_broadcaster_sendBeacon_payloadDecodesCorrectly);
    RUN_TEST(test_broadcaster_sendBeacon_offerOnly_isSent);
    RUN_TEST(test_broadcaster_runOnce_sendsWhenEnabled);
    RUN_TEST(test_broadcaster_runOnce_silentWhenDisabled);

    printf("\n=== Listener offer caching ===\n");

    RUN_TEST(test_listener_receiveWithOffer_cachesOffer);
    RUN_TEST(test_listener_receiveWithChannelOffer_setsHasChannel);
    RUN_TEST(test_listener_emptyMessageWithoutOffer_isDropped);
    RUN_TEST(test_listener_offerOnly_isCached);
    RUN_TEST(test_listener_nullBeacon_isDropped);
    RUN_TEST(test_listener_receiveWithNoOffer_cacheStaysInvalid);
    RUN_TEST(test_listener_textMessage_notUnwrapped);
    RUN_TEST(test_listener_wantPacket_falseWhenDisabled);
    RUN_TEST(test_listener_wantPacket_trueWhenEnabled);

    printf("\n=== Legacy split messages ===\n");

    RUN_TEST(test_broadcaster_legacySplit_sendsTwoPackets);
    RUN_TEST(test_broadcaster_legacySplit_firstPacketIsBeaconApp);
    RUN_TEST(test_broadcaster_legacySplit_firstPacketHasNoMessageText);
    RUN_TEST(test_broadcaster_legacySplit_secondPacketIsTextMessage);

    printf("\n=== Beacon-channel PSK swap ===\n");

    RUN_TEST(test_broadcaster_noChannelOverride_doesNotSwapPrimary);
    RUN_TEST(test_broadcaster_targetChannelIndex_usesTableSlot);
    RUN_TEST(test_broadcaster_targetChannelIndex_blankSlotFallsBackToPrimary);
    RUN_TEST(test_broadcaster_duplicateTargets_dedupedToOnePacket);
    RUN_TEST(test_broadcaster_distinctTargets_bothSent);

    printf("\n=== Offer advertisement ===\n");

    RUN_TEST(test_adminValidation_targetClamp_leavesRunningSlotState);
    RUN_TEST(test_applyModemConfig_publishesTheSlotVerdict);
    RUN_TEST(test_sidecar_carriesCustomModemParams);
    RUN_TEST(test_sidecar_legacySplitPair_sharesOneEntryUntilBothRelease);
    RUN_TEST(test_sidecar_fourLegacySplitTargets_allFit);
    RUN_TEST(test_sidecar_entryQueuedPastItsInterval_dropsThePacket);
    RUN_TEST(test_sidecar_staleEntry_freesItsSlotForTheNextCycle);
    RUN_TEST(test_offer_pinnedOutOfRangeSlot_isNotAdvertisedVerbatim);
    RUN_TEST(test_broadcaster_offerOnDisabledSlot_isStillSent);
    RUN_TEST(test_offer_derivableSlot_isNotAdvertised);
    RUN_TEST(test_offer_pinnedButDerivableSlot_isNotAdvertised);
    RUN_TEST(test_offer_pinnedSlot_isAdvertised);
    RUN_TEST(test_offer_disabledChannelSlot_advertisesNothing);

    printf("\n=== Radio switch/restore re-entrancy ===\n");

    RUN_TEST(test_broadcaster_targetMatchingRunningConfig_armsNoSwitch);
    RUN_TEST(test_broadcaster_targetPinnedSlot_armsThatSlot);
    RUN_TEST(test_broadcaster_presetOnlyTargetOnNarrowerBand_isNotDropped);
    RUN_TEST(test_broadcaster_presetTargetOnCustomModemNode_switchesUsePreset);
    RUN_TEST(test_broadcaster_targetPinnedHomeSlot_armsNoSwitch);
    RUN_TEST(test_broadcaster_twoPinnedSlotsNoChannel_bothSent);
    RUN_TEST(test_broadcaster_bareTargetAndPrimaryIndexTarget_dedupToOne);
    RUN_TEST(test_broadcaster_sendFailure_releasesTargetEntry);
    RUN_TEST(test_broadcaster_offerMatchesTarget_offerIsOmitted);
    RUN_TEST(test_broadcaster_offerMatchesOneTarget_stillSentOnTheOther);
    RUN_TEST(test_broadcaster_offerMatchesTargetNoText_sendsNothing);
    RUN_TEST(test_broadcaster_offerWithoutChannelMatchingTarget_isStillSent);

    printf("\n=== Target shapes and offer-vs-setting ===\n");

    RUN_TEST(test_broadcaster_homeSameSlotOtherSlotAndOtherRegion_allSent);
    RUN_TEST(test_offer_differentFromHome_advertisesItsOwnSettings);
    RUN_TEST(test_offer_sameAsHome_isAdvertisedOntoAnotherMesh);
    RUN_TEST(test_offer_sameAsABeaconTarget_targetIsStillSent);
    RUN_TEST(test_offer_identicalToItsOwnTarget_offerDroppedTextKept);

    printf("\n=== Retired proto tags ===\n");

    RUN_TEST(test_proto_retiredSendAsNodeTag_isSkipped);
    RUN_TEST(test_proto_retiredTargetChannelTag_isSkipped);
    RUN_TEST(test_beaconRestore_isNotReenteredByCompleteSending);
    RUN_TEST(test_beaconSwitch_isNotUndoneByCompleteSending);
    RUN_TEST(test_beaconRestore_withoutSwitch_isNoOp);
    RUN_TEST(test_beaconRestore_deferredUntilPacketCompletes);

    printf("\n=== MeshBeaconTxHook ===\n");

    RUN_TEST(test_txHook_normalPacket_isSend);
    RUN_TEST(test_txHook_beaconPacket_isDefer);
    RUN_TEST(test_txHook_invalidTarget_isDrop);
    RUN_TEST(test_txHook_dropTakesOnlyItsOwnTarget);
    RUN_TEST(test_txHook_dropOfOneSplitHalf_leavesTheOtherArmed);
    RUN_TEST(test_txHook_unregistered_isNoOp);
    RUN_TEST(test_txHook_untaggedPacketAheadOfQueuedBeacon_restoresHome);

    exit(UNITY_END());
}

BEACON_TEST_ENTRY void loop() {}

#else // MESHTASTIC_EXCLUDE_BEACON

void setUp(void) {}
void tearDown(void) {}

BEACON_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

BEACON_TEST_ENTRY void loop() {}

#endif
