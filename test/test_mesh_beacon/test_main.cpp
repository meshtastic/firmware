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
#include <cstring>
#include <memory>
#include <pb_decode.h>
#include <vector>

namespace
{

constexpr NodeNum kLocalNode = 0xAAAA0001;
constexpr NodeNum kRemoteNode = 0xBBBB0002;

// ---------------------------------------------------------------------------
// Minimal MockMeshService — stubs out side-effecting virtuals.
// handleToRadio is non-virtual so it runs the real implementation; we guard
// against the router->sendLocal path by setting a MockRouter below.
// ---------------------------------------------------------------------------
class MockMeshService : public MeshService
{
  public:
    void sendClientNotification(meshtastic_ClientNotification *n) override { releaseClientNotificationToPool(n); }
};

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
        sentPackets.push_back(*p);
        packetPool.release(p);
        return ERRNO_OK;
    }

    std::vector<meshtastic_MeshPacket> sentPackets;
};

// ---------------------------------------------------------------------------
// AdminModuleTestShim — exposes protected handleSetModuleConfig.
// ---------------------------------------------------------------------------
class AdminModuleTestShim : public AdminModule
{
  public:
    using AdminModule::handleSetModuleConfig;
};

// ---------------------------------------------------------------------------
// MeshBeaconBroadcastModuleTestShim — exposes private internals for testing.
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
// MeshBeaconListenerModuleTestShim — exposes handleReceivedProtobuf.
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

    // Device is an EU_868 node with LONG_FAST — the starting point.
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    // Allow TX unconditionally so airtime checks don't block sendBeacon().
    config.lora.override_duty_cycle = true;
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;

    myNodeInfo.my_node_num = kLocalNode;

    initRegion();
}

// ===========================================================================
// Group 1: AdminModule config validation — bad inputs must be sanitised
// ===========================================================================

// broadcast_on_preset=SHORT_TURBO is not in EU_868's preset list → zeroed out.
static void test_adminValidation_turboPresetOnEU868_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_enabled = true;
    bcfg.broadcast_on_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE(moduleConfig.has_mesh_beacon);
    TEST_ASSERT_EQUAL_MESSAGE(_meshtastic_Config_LoRaConfig_ModemPreset_MIN, moduleConfig.mesh_beacon.broadcast_on_preset,
                              "SHORT_TURBO must be cleared for EU_868");
}

// Same check for LONG_TURBO.
static void test_adminValidation_longTurboPresetOnEU868_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_on_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL(_meshtastic_Config_LoRaConfig_ModemPreset_MIN, moduleConfig.mesh_beacon.broadcast_on_preset);
}

// A turbo preset is valid on US (which uses PROFILE_STD) → must be kept.
static void test_adminValidation_turboPresetOnUS_isAccepted(void)
{
    resetConfig();
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_on_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO, moduleConfig.mesh_beacon.broadcast_on_preset);
}

// broadcast_offer_region with an unknown code (255) → cleared to UNSET.
static void test_adminValidation_unknownOfferRegion_isCleared(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_offer_region = (meshtastic_Config_LoRaConfig_RegionCode)255;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_UNSET, moduleConfig.mesh_beacon.broadcast_offer_region);
}

// A valid broadcast_offer_region (US) → preserved as-is.
static void test_adminValidation_validOfferRegion_isPreserved(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_offer_region = meshtastic_Config_LoRaConfig_RegionCode_US;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, moduleConfig.mesh_beacon.broadcast_offer_region);
}

// broadcast_message exactly 100 chars (i.e. length 100 including terminator
// means the last byte is at index 99) → last char must be NUL after truncation.
static void test_adminValidation_messageTooLong_isTruncatedAt99(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    // Fill with 'A' up to the full array size; admin must enforce ≤99 chars.
    memset(bcfg.broadcast_message, 'A', sizeof(bcfg.broadcast_message));
    bcfg.broadcast_message[sizeof(bcfg.broadcast_message) - 1] = '\0'; // pb_decode guarantee

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    // Byte at index 99 must be NUL (length capped at 99).
    TEST_ASSERT_EQUAL('\0', moduleConfig.mesh_beacon.broadcast_message[99]);
    // Bytes before it should still be 'A'.
    TEST_ASSERT_EQUAL('A', moduleConfig.mesh_beacon.broadcast_message[0]);
}

// broadcast_interval_secs below minimum → clamped up to 3600.
static void test_adminValidation_intervalTooLow_isClamped(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_interval_secs = 60; // way below minimum

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL_UINT32(3600, moduleConfig.mesh_beacon.broadcast_interval_secs);
}

// broadcast_interval_secs above maximum → clamped down to 259200.
static void test_adminValidation_intervalTooHigh_isClamped(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_interval_secs = 999999;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL_UINT32(259200, moduleConfig.mesh_beacon.broadcast_interval_secs);
}

// Zero interval (unset) is special-cased and must not be clamped.
static void test_adminValidation_intervalZero_isNotClamped(void)
{
    resetConfig();

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_interval_secs = 0;

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_EQUAL_UINT32(0, moduleConfig.mesh_beacon.broadcast_interval_secs);
}

// After a successful save the broadcaster's cache must be invalidated.
static void test_adminValidation_validSave_invalidatesCache(void)
{
    resetConfig();

    // Prime the broadcaster with a clean state so the dirty flag is known.
    std::unique_ptr<MeshBeaconBroadcastModuleTestShim> bcast(new MeshBeaconBroadcastModuleTestShim());
    meshBeaconBroadcastModule = bcast.get();
    bcast->payloadCacheDirty = false; // pretend it was freshly built

    meshtastic_ModuleConfig_MeshBeaconConfig bcfg = meshtastic_ModuleConfig_MeshBeaconConfig_init_zero;
    bcfg.broadcast_enabled = true;
    strncpy(bcfg.broadcast_message, "hello", sizeof(bcfg.broadcast_message) - 1);

    testAdmin->handleSetModuleConfig(makeBeaconModuleConfig(bcfg));

    TEST_ASSERT_TRUE_MESSAGE(bcast->payloadCacheDirty, "Config save must mark payload cache dirty");

    meshBeaconBroadcastModule = nullptr;
}

// ===========================================================================
// Group 2: Broadcaster payload cache
// ===========================================================================

// rebuildCache encodes a non-empty payload when message is set.
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

// The encoded bytes decode back to the same message field.
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

// Offer fields round-trip through the cache.
static void test_broadcaster_rebuildCache_offerFieldsEncoded(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.broadcast_offer_region = meshtastic_Config_LoRaConfig_RegionCode_US;
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

// invalidateCache sets payloadCacheDirty.
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

// Calling rebuildCache twice without invalidation must leave dirty=false.
static void test_broadcaster_rebuildCache_idempotent(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "idem", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.rebuildCache();
    pb_size_t firstSize = bcast.payloadCacheSize;
    bcast.rebuildCache(); // second call — should be identical
    pb_size_t secondSize = bcast.payloadCacheSize;

    TEST_ASSERT_FALSE(bcast.payloadCacheDirty);
    TEST_ASSERT_EQUAL(firstSize, secondSize);
}

// ===========================================================================
// Group 3: Broadcaster sendBeacon — packet structure
// ===========================================================================

// sendBeacon with broadcast_send_as_node=0 uses the local node number.
static void test_broadcaster_sendBeacon_fromIsLocalNodeWhenUnset(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.broadcast_enabled = true;
    moduleConfig.mesh_beacon.broadcast_send_as_node = 0;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "from-local", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(kLocalNode, mockRouter->sentPackets[0].from);
}

// sendBeacon respects a custom broadcast_send_as_node.
static void test_broadcaster_sendBeacon_fromIsCustomNodeWhenSet(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.broadcast_enabled = true;
    moduleConfig.mesh_beacon.broadcast_send_as_node = kRemoteNode;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "from-remote", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(kRemoteNode, mockRouter->sentPackets[0].from);
}

// sendBeacon must address the packet to NODENUM_BROADCAST.
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

// sendBeacon uses MESH_BEACON_APP portnum when a modem preset is offered.
static void test_broadcaster_sendBeacon_usesBeaconPortnum(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "portnum-check", sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    moduleConfig.mesh_beacon.broadcast_offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL(meshtastic_PortNum_MESH_BEACON_APP, mockRouter->sentPackets[0].decoded.portnum);
}

// sendBeacon falls back to TEXT_MESSAGE_APP when no radio settings are offered,
// even when broadcast_on_preset is configured (that controls TX radio, not portnum).
static void test_broadcaster_sendBeacon_fallsBackToTextMessagePortnum(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    const char *msg = "plain-text-beacon";
    strncpy(moduleConfig.mesh_beacon.broadcast_message, msg, sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
    // broadcast_on_preset set, but no offer — should still be TEXT_MESSAGE_APP
    moduleConfig.mesh_beacon.broadcast_on_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW;

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.sendBeacon();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    const meshtastic_MeshPacket &p = mockRouter->sentPackets[0];
    TEST_ASSERT_EQUAL(meshtastic_PortNum_TEXT_MESSAGE_APP, p.decoded.portnum);
    TEST_ASSERT_EQUAL_UINT32(strlen(msg), p.decoded.payload.size);
    TEST_ASSERT_EQUAL_STRING_LEN(msg, (const char *)p.decoded.payload.bytes, p.decoded.payload.size);
}

// sendBeacon payload decodes back to the correct message string (MESH_BEACON_APP path).
static void test_broadcaster_sendBeacon_payloadDecodesCorrectly(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    const char *msg = "Greetings from the beacon";
    strncpy(moduleConfig.mesh_beacon.broadcast_message, msg, sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);
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

// runOnce with broadcast_enabled=true must send exactly one packet.
static void test_broadcaster_runOnce_sendsWhenEnabled(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.broadcast_enabled = true;
    moduleConfig.mesh_beacon.broadcast_interval_secs = 3600;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "runOnce-enabled",
            sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.runOnce();

    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
}

// runOnce with broadcast_enabled=false must not send anything.
static void test_broadcaster_runOnce_silentWhenDisabled(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.broadcast_enabled = false;
    strncpy(moduleConfig.mesh_beacon.broadcast_message, "runOnce-disabled",
            sizeof(moduleConfig.mesh_beacon.broadcast_message) - 1);

    MeshBeaconBroadcastModuleTestShim bcast;
    bcast.runOnce();

    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size());
}

// ===========================================================================
// Group 4: Listener — offer caching and guards
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

// Receiving a beacon with an offer stores it in lastReceivedOffer.
static void test_listener_receiveWithOffer_cachesOffer(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.listen_enabled = true;

    MeshBeaconListenerModuleTestShim listener;
    MeshBeaconListenerModule::lastReceivedOffer = {};

    meshtastic_MeshBeacon b = meshtastic_MeshBeacon_init_zero;
    strncpy(b.message, "Join us on US/MEDIUM_FAST", sizeof(b.message) - 1);
    b.offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST;
    b.offer_region = meshtastic_Config_LoRaConfig_RegionCode_US;

    meshtastic_MeshPacket mp = makeBeaconPacket(b);
    listener.handleReceivedProtobuf(mp, &b);

    TEST_ASSERT_TRUE_MESSAGE(MeshBeaconListenerModule::lastReceivedOffer.valid, "Offer with preset must be cached");
    TEST_ASSERT_EQUAL(kRemoteNode, MeshBeaconListenerModule::lastReceivedOffer.sender);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, MeshBeaconListenerModule::lastReceivedOffer.preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, MeshBeaconListenerModule::lastReceivedOffer.region);
}

// Receiving a beacon with a channel offer stores the has_channel flag.
static void test_listener_receiveWithChannelOffer_setsHasChannel(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.listen_enabled = true;

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

// An empty message must be silently dropped; cache must stay invalid.
static void test_listener_emptyMessage_isDropped(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.listen_enabled = true;

    MeshBeaconListenerModuleTestShim listener;
    MeshBeaconListenerModule::lastReceivedOffer = {};

    meshtastic_MeshBeacon b = meshtastic_MeshBeacon_init_zero;
    b.offer_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    // message field intentionally left blank

    meshtastic_MeshPacket mp = makeBeaconPacket(b);
    listener.handleReceivedProtobuf(mp, &b);

    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconListenerModule::lastReceivedOffer.valid, "Empty message must not update offer cache");
}

// A null MeshBeacon pointer must be handled gracefully.
static void test_listener_nullBeacon_isDropped(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.listen_enabled = true;

    MeshBeaconListenerModuleTestShim listener;
    MeshBeaconListenerModule::lastReceivedOffer = {};

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    bool result = listener.handleReceivedProtobuf(mp, nullptr);

    TEST_ASSERT_FALSE_MESSAGE(result, "Null beacon must return false");
    TEST_ASSERT_FALSE(MeshBeaconListenerModule::lastReceivedOffer.valid);
}

// Receiving a beacon with no offer fields must not mark the cache valid.
static void test_listener_receiveWithNoOffer_cacheStaysInvalid(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.listen_enabled = true;

    MeshBeaconListenerModuleTestShim listener;
    MeshBeaconListenerModule::lastReceivedOffer = {};

    meshtastic_MeshBeacon b = meshtastic_MeshBeacon_init_zero;
    strncpy(b.message, "No offer here", sizeof(b.message) - 1);
    // offer_preset == 0, has_offer_channel == false

    meshtastic_MeshPacket mp = makeBeaconPacket(b);
    listener.handleReceivedProtobuf(mp, &b);

    TEST_ASSERT_FALSE_MESSAGE(MeshBeaconListenerModule::lastReceivedOffer.valid, "No offer fields → cache must stay invalid");
}

// wantPacket returns false when listen_enabled is false.
static void test_listener_wantPacket_falseWhenDisabled(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.listen_enabled = false;

    MeshBeaconListenerModuleTestShim listener;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;

    TEST_ASSERT_FALSE(listener.wantPacket(&mp));
}

// wantPacket returns true when listen_enabled is true and portnum matches.
static void test_listener_wantPacket_trueWhenEnabled(void)
{
    resetConfig();
    moduleConfig.has_mesh_beacon = true;
    moduleConfig.mesh_beacon.listen_enabled = true;

    MeshBeaconListenerModuleTestShim listener;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;

    TEST_ASSERT_TRUE(listener.wantPacket(&mp));
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

    // Admin validation
    RUN_TEST(test_adminValidation_turboPresetOnEU868_isCleared);
    RUN_TEST(test_adminValidation_longTurboPresetOnEU868_isCleared);
    RUN_TEST(test_adminValidation_turboPresetOnUS_isAccepted);
    RUN_TEST(test_adminValidation_unknownOfferRegion_isCleared);
    RUN_TEST(test_adminValidation_validOfferRegion_isPreserved);
    RUN_TEST(test_adminValidation_messageTooLong_isTruncatedAt99);
    RUN_TEST(test_adminValidation_intervalTooLow_isClamped);
    RUN_TEST(test_adminValidation_intervalTooHigh_isClamped);
    RUN_TEST(test_adminValidation_intervalZero_isNotClamped);
    RUN_TEST(test_adminValidation_validSave_invalidatesCache);

    // Broadcaster cache
    RUN_TEST(test_broadcaster_rebuildCache_producesNonEmptyPayload);
    RUN_TEST(test_broadcaster_rebuildCache_payloadDecodesCorrectly);
    RUN_TEST(test_broadcaster_rebuildCache_offerFieldsEncoded);
    RUN_TEST(test_broadcaster_invalidateCache_setsDirtyFlag);
    RUN_TEST(test_broadcaster_rebuildCache_idempotent);

    // Broadcaster send
    RUN_TEST(test_broadcaster_sendBeacon_fromIsLocalNodeWhenUnset);
    RUN_TEST(test_broadcaster_sendBeacon_fromIsCustomNodeWhenSet);
    RUN_TEST(test_broadcaster_sendBeacon_addressedToBroadcast);
    RUN_TEST(test_broadcaster_sendBeacon_usesBeaconPortnum);
    RUN_TEST(test_broadcaster_sendBeacon_fallsBackToTextMessagePortnum);
    RUN_TEST(test_broadcaster_sendBeacon_payloadDecodesCorrectly);
    RUN_TEST(test_broadcaster_runOnce_sendsWhenEnabled);
    RUN_TEST(test_broadcaster_runOnce_silentWhenDisabled);

    // Listener
    RUN_TEST(test_listener_receiveWithOffer_cachesOffer);
    RUN_TEST(test_listener_receiveWithChannelOffer_setsHasChannel);
    RUN_TEST(test_listener_emptyMessage_isDropped);
    RUN_TEST(test_listener_nullBeacon_isDropped);
    RUN_TEST(test_listener_receiveWithNoOffer_cacheStaysInvalid);
    RUN_TEST(test_listener_wantPacket_falseWhenDisabled);
    RUN_TEST(test_listener_wantPacket_trueWhenEnabled);

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
