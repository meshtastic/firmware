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
        // Capture the primary channel as seen AT send() time. sendBeaconPacket() temporarily swaps
        // the beacon channel into the primary slot around this call so perhapsEncode keys off it,
        // so this snapshot reflects which channel the packet would actually be encrypted on.
        if (channelFile.channels_count > 0)
            primaryAtSend.push_back(channels.getByIndex(channels.getPrimaryIndex()).settings);
        sentPackets.push_back(*p);
        packetPool.release(p);
        return ERRNO_OK;
    }

    // Locally-addressed packets land here instead of send().  Release immediately
    // rather than queuing into fromRadioQueue (which is never drained in tests).
    void enqueueReceivedMessage(meshtastic_MeshPacket *p) override { packetPool.release(p); }

    std::vector<meshtastic_MeshPacket> sentPackets;
    std::vector<meshtastic_ChannelSettings> primaryAtSend;
};

// AdminModuleTestShim (test/support) exposes protected handleSetModuleConfig.

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
 * Verify SHORT_TURBO is rejected when the region is EU_868 (turbo presets are not in that
 * region's allowed preset set). Important to catch regressions where admin stores unlawful
 * radio settings that would violate regional radio regulations.
 */
static void test_adminValidation_turboPresetOnEU868_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.flags |= MESH_BEACON_FLAG_BROADCAST_ENABLED;
    bcfg.has_broadcast_on_preset = true;
    bcfg.broadcast_on_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.has_mesh_beacon);
    TEST_ASSERT_FALSE_MESSAGE(moduleConfig.mesh_beacon.has_broadcast_on_preset, "SHORT_TURBO must be cleared for EU_868");
}

/**
 * Verify LONG_TURBO is also cleared for EU_868, not just SHORT_TURBO.
 * Important to confirm rejection covers the entire turbo preset family rather than one variant.
 */
static void test_adminValidation_longTurboPresetOnEU868_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.has_broadcast_on_preset = true;
    bcfg.broadcast_on_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_FALSE(moduleConfig.mesh_beacon.has_broadcast_on_preset);
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
    bcfg.has_broadcast_on_preset = true;
    bcfg.broadcast_on_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.has_broadcast_on_preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, moduleConfig.mesh_beacon.broadcast_on_preset);
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
 * Important because broadcast_targets entries are validated independently of the single-target
 * broadcast_on_* fields, and an invalid enum must never reach the radio-switch path.
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
 * Verify a preset that is illegal for a multi-target entry's region clears that entry's preset
 * (and its channel), matching the single-target broadcast_on_preset rule.
 */
static void test_adminValidation_targetInvalidPresetForRegion_isCleared(void)
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

    TEST_ASSERT_FALSE_MESSAGE(moduleConfig.mesh_beacon.broadcast_targets[0].has_preset,
                              "SHORT_TURBO must be cleared for EU_868 target");
    TEST_ASSERT_FALSE(moduleConfig.mesh_beacon.broadcast_targets[0].has_channel_index);
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
 * Verify the 'from' field defaults to the local node number when broadcast_send_as_node is 0.
 * Important for correct source attribution in peer node tables that receive the beacon.
 */
static void test_broadcaster_sendBeacon_fromIsLocalNodeWhenUnset(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_BROADCAST_ENABLED;
    moduleConfig.mesh_beacon.broadcast_send_as_node = 0;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "from-local", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(kLocalNode, mockRouter->sentPackets[0].from);
}

/**
 * Verify broadcast_send_as_node is currently disabled: 'from' is always the local node
 * even when broadcast_send_as_node is set to a remote node number.
 * (broadcast_send_as_node is commented out as "not suitable right now".)
 */
static void test_broadcaster_sendBeacon_fromIsCustomNodeWhenSet(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.flags |= MESH_BEACON_FLAG_BROADCAST_ENABLED;
    moduleConfig.mesh_beacon.broadcast_send_as_node = kRemoteNode;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "from-remote", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    // broadcast_send_as_node is disabled; from is always the local node
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
 * Verify TEXT_MESSAGE_APP portnum is used when no offer content is present, even if
 * broadcast_on_preset is set (that field governs which radio config to use for TX, not portnum).
 * Important so standard clients display plain-text beacons without needing a MESH_BEACON_APP decoder.
 */
static void test_broadcaster_sendBeacon_fallsBackToTextMessagePortnum(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    const char *msg = "plain-text-beacon";
    strncpy(moduleConfig.mesh_beacon.broadcast_message, msg, sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    // broadcast_on_preset set, but no offer - should still be TEXT_MESSAGE_APP
    moduleConfig.mesh_beacon.has_broadcast_on_preset = true;
    moduleConfig.mesh_beacon.broadcast_on_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

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
// Group 7: Beacon-channel PSK swap (broadcast_on_channel override)
// ===========================================================================

/**
 * When broadcast_on_channel overrides the primary channel's name/PSK, the packet must be encrypted
 * on the BEACON channel, not the primary. perhapsEncode keys off the primary slot, so sendBeaconPacket
 * temporarily installs the beacon channel there for the send and restores it after. Verify both: the
 * primary slot IS the beacon channel during send(), and it is restored afterwards (no leak).
 */
static void test_broadcaster_channelPskOverride_swapsBeaconChannelAndRestores(void)
{
    resetConfig();
    static const uint8_t homePsk[16] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    installTestPrimaryChannel("Home", homePsk, sizeof(homePsk));

    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.has_broadcast_offer_preset = true; // gives the beacon radio content to send
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;
    moduleConfig.mesh_beacon.has_broadcast_on_channel = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_on_channel.name, "BeaconCh",
            sizeof(moduleConfig.mesh_beacon.broadcast_on_channel.name) - 1);
    static const uint8_t beaconPsk[16] = {0xBB, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                          0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    moduleConfig.mesh_beacon.broadcast_on_channel.psk.size = sizeof(beaconPsk);
    memcpy(moduleConfig.mesh_beacon.broadcast_on_channel.psk.bytes, beaconPsk, sizeof(beaconPsk));

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    // During send(), the primary slot must hold the BEACON channel (so encryption uses its PSK).
    TEST_ASSERT_TRUE_MESSAGE(mockRouter->primaryAtSend.size() >= 1, "expected at least one send");
    const meshtastic_ChannelSettings &atSend = mockRouter->primaryAtSend[0];
    TEST_ASSERT_EQUAL_STRING_MESSAGE("BeaconCh", atSend.name, "primary must be the beacon channel during send");
    TEST_ASSERT_EQUAL_UINT(sizeof(beaconPsk), atSend.psk.size);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xBB, atSend.psk.bytes[0], "encryption must use the beacon channel PSK");

    // After send(), the primary channel must be restored to the original (no leak into normal traffic).
    const meshtastic_ChannelSettings &after = channels.getByIndex(channels.getPrimaryIndex()).settings;
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Home", after.name, "primary channel must be restored after send");
    TEST_ASSERT_EQUAL_UINT(sizeof(homePsk), after.psk.size);
    TEST_ASSERT_EQUAL_UINT8(0xAA, after.psk.bytes[0]);
}

/**
 * Without a broadcast_on_channel override, the beacon must transmit on the primary channel unchanged
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
    // No broadcast_on_channel override.

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_TRUE_MESSAGE(mockRouter->primaryAtSend.size() >= 1, "expected at least one send");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Home", mockRouter->primaryAtSend[0].name,
                                     "primary must stay unchanged when no channel override is set");
}

/**
 * A broadcast_target whose channel_index points at a configured table slot must transmit encrypted
 * on THAT slot's channel (name + PSK), not the primary. Verifies the slot-index → channel-table
 * resolution introduced when BroadcastTarget dropped its embedded ChannelSettings.
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

    TEST_ASSERT_TRUE_MESSAGE(mockRouter->primaryAtSend.size() >= 1, "expected at least one send");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("BeaconNet", mockRouter->primaryAtSend[0].name,
                                     "beacon must be encrypted on the referenced slot's channel");
    // Primary slot restored to home after send (no leak).
    TEST_ASSERT_EQUAL_STRING("Home", channels.getByIndex(channels.getPrimaryIndex()).settings.name);
}

/**
 * A broadcast_target whose channel_index points at a BLANK table slot (no name, no PSK) must fall
 * back to the default channel for the preset rather than borrowing the primary's name/PSK with a
 * clobbered channel_num. Guards the blank-slot handling for multi-target configs.
 */
static void test_broadcaster_targetChannelIndex_blankSlotFallsBackToPreset(void)
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

    // Exactly one beacon goes out, and a blank slot does NOT trigger a crypto swap - the primary
    // stays "Home" (no garbage / no clobber), matching the no-channel-override default-for-preset path.
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_TRUE_MESSAGE(mockRouter->primaryAtSend.size() >= 1, "expected at least one send");
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

    RUN_TEST(test_adminValidation_turboPresetOnEU868_isCleared);
    RUN_TEST(test_adminValidation_longTurboPresetOnEU868_isCleared);
    RUN_TEST(test_adminValidation_turboPresetOnUS_isAccepted);
    RUN_TEST(test_adminValidation_unknownOfferRegion_isCleared);
    RUN_TEST(test_adminValidation_validOfferRegion_isPreserved);
    RUN_TEST(test_adminValidation_targetUnknownRegion_isCleared);
    RUN_TEST(test_adminValidation_targetInvalidPresetForRegion_isCleared);
    RUN_TEST(test_adminValidation_targetValidPresetForRegion_isPreserved);
    RUN_TEST(test_adminValidation_targetChannelIndexOutOfRange_isCleared);
    RUN_TEST(test_adminValidation_targetChannelIndexInRange_isPreserved);
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
    RUN_TEST(test_broadcaster_sendBeacon_fromIsCustomNodeWhenSet);
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

    RUN_TEST(test_broadcaster_channelPskOverride_swapsBeaconChannelAndRestores);
    RUN_TEST(test_broadcaster_noChannelOverride_doesNotSwapPrimary);
    RUN_TEST(test_broadcaster_targetChannelIndex_usesTableSlot);
    RUN_TEST(test_broadcaster_targetChannelIndex_blankSlotFallsBackToPreset);
    RUN_TEST(test_broadcaster_duplicateTargets_dedupedToOnePacket);
    RUN_TEST(test_broadcaster_distinctTargets_bothSent);

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
