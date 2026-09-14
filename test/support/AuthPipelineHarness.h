#pragma once
// Ingress harness shared by the suites that push a packet through Router::perhapsHandleReceived()
// and observe what comes out the other side: a mock NodeDB with controllable keys and bits, a
// counting radio / routing module / module / MQTT, and builders for decoded, channel-encrypted and
// genuinely PKI-encrypted frames. One TU per suite includes this; the statics are per suite.
//
// Lifecycle: pipelineHarnessCreate() once from setup() before UNITY_BEGIN(); pipelineHarnessSetUp()
// at the top of setUp(); pipelineHarnessTearDown() at the top of tearDown(); pipelineHarnessDestroy()
// after UNITY_END(). Suites layer their own defaults (e.g. a signature policy) on top.
#include "MeshTypes.h" // include BEFORE TestUtil.h
#include "NodeStatus.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include "airtime.h"
#include "mesh/Channels.h"
#include "mesh/CryptoEngine.h"
#include "mesh/MeshRadio.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/ReliableRouter.h"
#include "mesh/Router.h"
#include "mesh/SinglePortModule.h"
#include "modules/RoutingModule.h"
#include "mqtt/MQTT.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <pb_decode.h>
#include <pb_encode.h>
#include <unity.h>

// Test fixture identifiers
// ---------------------------------------------------------------------------
static constexpr NodeNum LOCAL_NODE = 0x0A0A0A0A;
static constexpr NodeNum REMOTE_NODE = 0x0B0B0B0B;

// A "small" broadcast payload whose signed encoding easily fits a LoRa frame, and an "oversized"
// one whose signed encoding does not, yet still encodes within a LoRa frame unsigned.
static constexpr size_t SMALL_PAYLOAD = 16;
static constexpr size_t OVERSIZED_PAYLOAD = 180;

// ---------------------------------------------------------------------------
// MockNodeDB - inject nodes with controlled public keys / signer bits.
// Mirrors the pattern in test/test_hop_scaling. meshNodes/numMeshNodes are public on NodeDB.
// ---------------------------------------------------------------------------
class MockNodeDB : public NodeDB
{
  public:
    void installDefaultsPreservingIdentity() { installDefaultConfig(true); }

    void clearTestNodes()
    {
        testNodes.clear();
        meshNodes = &testNodes;
        numMeshNodes = 0;
    }

    // Add a bare node and return a stable handle (fetch via getMeshNode so the pointer stays valid
    // even if the vector reallocates after later adds).
    void addNode(NodeNum num)
    {
        meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
        node.num = num;
        testNodes.push_back(node);
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }

    void setPublicKey(NodeNum num, const uint8_t *pubKey)
    {
        meshtastic_NodeInfoLite *n = getMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        n->public_key.size = 32;
        memcpy(n->public_key.bytes, pubKey, 32);
    }

    void setSignerBit(NodeNum num, bool value)
    {
        meshtastic_NodeInfoLite *n = getMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_HAS_XEDDSA_SIGNED_MASK, value);
    }

    void markHasUser(NodeNum num)
    {
        meshtastic_NodeInfoLite *n = getMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_HAS_USER_MASK, true);
    }

    /// A node with a User whose licensed flag is `licensed`; getLicenseStatus() then says so.
    void markLicenseStatus(NodeNum num, bool licensed)
    {
        markHasUser(num);
        nodeInfoLiteSetBit(getMeshNode(num), NODEINFO_BITFIELD_IS_LICENSED_MASK, licensed);
    }

    void setLongName(NodeNum num, const char *name)
    {
        meshtastic_NodeInfoLite *n = getMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        strncpy(n->long_name, name, sizeof(n->long_name) - 1);
        n->long_name[sizeof(n->long_name) - 1] = '\0';
    }

    const char *longName(NodeNum num)
    {
        meshtastic_NodeInfoLite *n = getMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        return n->long_name;
    }

    std::vector<meshtastic_NodeInfoLite> testNodes;
};

static MockNodeDB *mockNodeDB = nullptr;

/// Counts sends, cancels and queue lookups instead of touching hardware; `failSend` simulates a full queue.
class AuthPipelineRadio : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        sendCalls++;
        packetPool.release(p);
        return failSend ? ERRNO_DISABLED : ERRNO_OK;
    }
    bool cancelSending(NodeNum, PacketId) override
    {
        cancelCalls++;
        return true;
    }
    bool findInTxQueue(NodeNum, PacketId) override
    {
        findCalls++;
        return false;
    }
    bool removePendingTXPacket(NodeNum, PacketId, uint32_t) override
    {
        removeCalls++;
        return true;
    }
    uint32_t getPacketTime(uint32_t, bool = false) override { return 7; }
    void reset()
    {
        sendCalls = cancelCalls = findCalls = removeCalls = 0;
        failSend = false;
    }

    bool failSend = false;
    uint32_t sendCalls = 0;
    uint32_t cancelCalls = 0;
    uint32_t findCalls = 0;
    uint32_t removeCalls = 0;
};

/// The production router with its protected state (history, pending retransmissions, upgrades) exposed.
class AuthPipelineRouter : public ReliableRouter
{
  public:
    bool filter(meshtastic_MeshPacket *p) { return ReliableRouter::shouldFilterReceived(p); }
    bool historyContains(const meshtastic_MeshPacket *p) { return wasSeenRecently(p, false); }
    void remember(const meshtastic_MeshPacket *p) { wasSeenRecently(p, true); }
    void forgetRelayer(uint8_t relay, PacketId id, NodeNum from) { removeRelayer(relay, id, from); }
    bool handleUpgrade(meshtastic_MeshPacket *p) { return perhapsHandleUpgradedPacket(p); }
    void addPending(const meshtastic_MeshPacket &p, uint32_t nextTx)
    {
        auto *copy = packetPool.allocCopy(p);
        TEST_ASSERT_NOT_NULL(copy);
        const GlobalPacketId key(copy);
        pending.emplace(key, PendingPacket(copy, NUM_INTERMEDIATE_RETX));
        pending.at(key).nextTxMsec = nextTx;
    }
    uint32_t pendingNextTx(NodeNum from, PacketId id)
    {
        PendingPacket *entry = findPendingPacket(from, id);
        return entry ? entry->nextTxMsec : 0;
    }
    uint8_t pendingTotalAttempts(NodeNum from, PacketId id)
    {
        PendingPacket *entry = findPendingPacket(from, id);
        return entry ? entry->initialNumRetransmissions + 1 : 0;
    }
    size_t pendingCount() const { return pending.size(); }
    void clearPending()
    {
        for (auto &entry : pending)
            packetPool.release(entry.second.packet);
        pending.clear();
    }
};

/// Records ACK/NAK sends and the last error reason instead of transmitting them.
class AuthPipelineRoutingModule : public RoutingModule
{
  public:
    void sendAckNak(meshtastic_Routing_Error err, NodeNum, PacketId, ChannelIndex, uint8_t = 0, bool = false,
                    const meshtastic_MeshPacket * = nullptr) override
    {
        ackCalls++;
        lastErr = err;
    }
    uint32_t ackCalls = 0;
    meshtastic_Routing_Error lastErr = meshtastic_Routing_Error_NONE;
};

/// A POSITION_APP module that counts how often ingress reaches module dispatch.
class AuthPipelineModule : public SinglePortModule
{
  public:
    AuthPipelineModule() : SinglePortModule("authPipeline", meshtastic_PortNum_POSITION_APP) {}
    ProcessMessage handleReceived(const meshtastic_MeshPacket &) override
    {
        calls++;
        return ProcessMessage::CONTINUE;
    }
    uint32_t calls = 0;
};

/// Exposes the uplink queue so a test can count what would have been published.
class AuthPipelineMqtt : public MQTT
{
  public:
    int queueSize() { return mqttQueue.numUsed(); }
    void clearQueue()
    {
        while (QueueEntry *entry = mqttQueue.dequeuePtr(0))
            delete entry;
    }
};

static AuthPipelineRouter *pipelineRouter = nullptr;
static AuthPipelineRadio *pipelineRadio = nullptr;
static AuthPipelineRoutingModule *pipelineRouting = nullptr;
static AuthPipelineModule *pipelineModule = nullptr;
static AuthPipelineMqtt *pipelineMqtt = nullptr;
static MeshService *pipelineService = nullptr;

// ---------------------------------------------------------------------------
// Packet builders
// ---------------------------------------------------------------------------

// Build a decoded packet with a deterministic payload of the requested size.
static meshtastic_MeshPacket makeDecoded(NodeNum from, NodeNum to, meshtastic_PortNum port, size_t payloadLen)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = to;
    p.id = 0x12345678;
    p.channel = 0; // primary channel index (perhapsEncode rewrites this to the channel hash)
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = port;
    p.decoded.payload.size = payloadLen;
    for (size_t i = 0; i < payloadLen; i++)
        p.decoded.payload.bytes[i] = (uint8_t)(i & 0xff);
    return p;
}

// Sign a decoded packet with the CryptoEngine's current key - used to simulate a *remote* signer,
// because perhapsEncode only auto-signs packets that originate from us.
static meshtastic_MeshPacket channelEncode(meshtastic_MeshPacket p)
{
    uint8_t encoded[MAX_LORA_PAYLOAD_LEN + 1] = {};
    const size_t encodedSize = pb_encode_to_bytes(encoded, sizeof(encoded), &meshtastic_Data_msg, &p.decoded);
    TEST_ASSERT_GREATER_THAN(0, encodedSize);
    const int16_t hash = channels.setActiveByIndex(p.channel);
    TEST_ASSERT_GREATER_OR_EQUAL(0, hash);
    crypto->encryptPacket(p.from, p.id, encodedSize, encoded);
    memcpy(p.encrypted.bytes, encoded, encodedSize);
    p.encrypted.size = encodedSize;
    p.channel = hash;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    return p;
}

/// Set the receive-side signature policy for the next ingress.
static void setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy policy)
{
    config.security.packet_signature_policy = policy;
}

/// Push a copy of `p` through Router::perhapsHandleReceived() as radio ingress.
static void runPipelineIngress(const meshtastic_MeshPacket &p)
{
    meshtastic_MeshPacket *copy = packetPool.allocCopy(p);
    TEST_ASSERT_NOT_NULL(copy);
    pipelineRouter->enqueueReceivedMessage(copy);
    pipelineRouter->runOnce();
}

// ---------------------------------------------------------------------------
// PKI fixtures: two remote identities and frames encrypted between them
// ---------------------------------------------------------------------------

/// A remote node the tests can encrypt from and to.
struct RelayIdentity {
    NodeNum num;
    uint8_t pub[32];
    uint8_t priv[32];
};

// CryptoEngine::setDHPrivateKey takes a mutable pointer; the identities above are const.
static void useDHKey(const uint8_t *priv)
{
    uint8_t k[32];
    memcpy(k, priv, sizeof(k));
    crypto->setDHPrivateKey(k);
}

/// A remote node with a fresh Curve25519 keypair.
static RelayIdentity makeIdentity(NodeNum num)
{
    RelayIdentity id;
    id.num = num;
    crypto->generateKeyPair(id.pub, id.priv);
    return id;
}

static constexpr NodeNum ADMIN_NODE = 0x0C0C0C0C;  // the operator's node, sending remote admin
static constexpr NodeNum TARGET_NODE = 0x0D0D0D0D; // the node being administered

/// A genuine PKI-encrypted packet on `port` from one remote identity to another, as it would be
/// heard off the air by a third node (us). Leaves the engine holding a fresh key of its own.
static meshtastic_MeshPacket makePkiUnicastBetween(const RelayIdentity &from, const RelayIdentity &to, meshtastic_PortNum port,
                                                   PacketId id, bool wantAck = false)
{
    meshtastic_Data d = meshtastic_Data_init_zero;
    d.portnum = port;
    d.payload.size = SMALL_PAYLOAD;
    for (size_t i = 0; i < SMALL_PAYLOAD; i++)
        d.payload.bytes[i] = (uint8_t)(0xA0 + i);
    uint8_t plain[MAX_LORA_PAYLOAD_LEN + 1];
    const size_t plainSize = pb_encode_to_bytes(plain, sizeof(plain), &meshtastic_Data_msg, &d);
    TEST_ASSERT_GREATER_THAN(0, plainSize);

    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from.num;
    p.to = to.num;
    p.id = id;
    p.channel = 0; // PKI packets carry channel hash 0 on the wire
    p.hop_limit = 2;
    p.hop_start = 3;
    p.want_ack = wantAck;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;

    meshtastic_NodeInfoLite_public_key_t toKey = {32, {0}};
    memcpy(toKey.bytes, to.pub, 32);
    uint8_t ourPub[32], ourPriv[32];
    crypto->generateKeyPair(ourPub, ourPriv);
    useDHKey(from.priv);
    TEST_ASSERT_TRUE(crypto->encryptCurve25519(p.to, p.from, toKey, p.id, plainSize, plain, p.encrypted.bytes));
    p.encrypted.size = plainSize + MESHTASTIC_PKC_OVERHEAD;
    crypto->setDHPrivateKey(ourPriv);
    return p;
}

/// Every rebroadcast_mode, for cases that table the full matrix.
static const meshtastic_Config_DeviceConfig_RebroadcastMode ALL_MODES[] = {
    meshtastic_Config_DeviceConfig_RebroadcastMode_ALL,
    meshtastic_Config_DeviceConfig_RebroadcastMode_ALL_SKIP_DECODING,
    meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY,
    meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY,
    meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY,
    meshtastic_Config_DeviceConfig_RebroadcastMode_NONE,
};

/// Mode name for assertion messages.
static const char *modeName(meshtastic_Config_DeviceConfig_RebroadcastMode m)
{
    switch (m) {
    case meshtastic_Config_DeviceConfig_RebroadcastMode_ALL:
        return "ALL";
    case meshtastic_Config_DeviceConfig_RebroadcastMode_ALL_SKIP_DECODING:
        return "ALL_SKIP_DECODING";
    case meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY:
        return "CORE_PORTNUMS_ONLY";
    case meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY:
        return "KNOWN_ONLY";
    case meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY:
        return "LOCAL_ONLY";
    default:
        return "NONE";
    }
}

/// Feed `p` through ingress under `mode` and assert it was, or was not, handed to the radio - and
/// that nothing else happened to it either way.
static void assertOpaqueRelay(const meshtastic_MeshPacket &p, meshtastic_Config_DeviceConfig_RebroadcastMode mode,
                              bool expectRelay, const char *why)
{
    pipelineRadio->reset();
    pipelineRouting->ackCalls = 0;
    pipelineModule->calls = 0;
    config.device.rebroadcast_mode = mode;
    meshtastic_MeshPacket copy = p;
    copy.id += (uint32_t)mode; // a fresh id per mode so the opaque dedup does not decide the outcome
    runPipelineIngress(copy);
    char msg[200];
    snprintf(msg, sizeof(msg), "%s: %s", modeName(mode), why);
    TEST_ASSERT_EQUAL_MESSAGE(expectRelay ? 1 : 0, pipelineRadio->sendCalls, msg);
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRouting->ackCalls, "an opaque packet not for us must never be ACKed");
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineModule->calls, "an opaque packet must not reach modules");
    TEST_ASSERT_NULL_MESSAGE(pipelineService->getForPhone(), "an opaque unicast for someone else is not the phone's business");
    TEST_ASSERT_FALSE_MESSAGE(pipelineRouter->historyContains(&copy), "an opaque packet must not enter PacketHistory");
}

/// A broadcast on a channel hash no local channel produces: opaque to us.
static meshtastic_MeshPacket makeUnknownChannelBroadcast(PacketId id)
{
    meshtastic_MeshPacket foreign = meshtastic_MeshPacket_init_zero;
    foreign.from = ADMIN_NODE;
    foreign.to = NODENUM_BROADCAST;
    foreign.id = id;
    foreign.channel = 0xFE;
    foreign.hop_limit = 1;
    foreign.hop_start = 2;
    foreign.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    foreign.encrypted.size = 16;
    memset(foreign.encrypted.bytes, 0x5A, foreign.encrypted.size);
    return foreign;
}

/// Give the local node a keypair in NodeDB so PKI frames addressed to it are decrypt candidates.
static RelayIdentity installOurIdentity()
{
    RelayIdentity us = makeIdentity(LOCAL_NODE);
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, us.pub);
    return us;
}

/// A decodable, unsigned channel broadcast from `from` to `to`, as a plain-text relay would see it.
static meshtastic_MeshPacket makeChannelBroadcastFrom(NodeNum from, NodeNum to, PacketId id)
{
    meshtastic_MeshPacket p = makeDecoded(from, to, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    p.id = id;
    p.hop_limit = 1;
    p.hop_start = 2;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    return channelEncode(p);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static AirTime *harnessSavedAirTime = nullptr;
static meshtastic::NodeStatus *harnessSavedNodeStatus = nullptr;

/// Build the router / radio / module / service / MQTT stack once per process.
static void pipelineHarnessCreate()
{
    initializeTestEnvironment();
    harnessSavedAirTime = airTime;
    harnessSavedNodeStatus = nodeStatus;
    airTime = new AirTime();
    nodeStatus = new meshtastic::NodeStatus();

    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();
    pipelineRouter = new AuthPipelineRouter();
    auto pipelineRadioOwner = std::make_unique<AuthPipelineRadio>();
    pipelineRadio = pipelineRadioOwner.get();
    pipelineRouter->addInterface(std::move(pipelineRadioOwner));
    router = pipelineRouter;
    routingModule = pipelineRouting = new AuthPipelineRoutingModule();
    pipelineModule = new AuthPipelineModule();
    service = pipelineService = new MeshService();
    mqtt = pipelineMqtt = new AuthPipelineMqtt();
}

/// Free the AirTime and NodeStatus the harness installed, then put back the originals.
static void pipelineHarnessDestroy()
{
    delete airTime;
    delete nodeStatus;
    airTime = harnessSavedAirTime;
    nodeStatus = harnessSavedNodeStatus;
}

/// Fresh NodeDB, zeroed config / owner (=> rebroadcast ALL, no private key), default channels,
/// every counter reset. The signature policy is left at zero (COMPATIBLE); suites set their own.
static void pipelineHarnessSetUp()
{
    service = pipelineService;

    // Construct the mock FIRST: the NodeDB constructor can reload persisted state from the
    // host filesystem (portduino VFS) and repopulate the globals - a saved private key
    // re-enables the PKI encrypt path and fails the unicast tests on hosts with leftover prefs.
    mockNodeDB = new MockNodeDB();
    mockNodeDB->clearTestNodes();
#if WARM_NODE_COUNT > 0
    mockNodeDB->warmStore.clear();
#endif
    nodeDB = mockNodeDB;

    // Clean global config/owner AFTER the ctor; zeroed config => rebroadcast ALL (no KNOWN_ONLY
    // drop) and security.private_key.size == 0 (PKI encrypt path skipped => simple channel crypto).
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    owner = meshtastic_User_init_zero;
    myNodeInfo.my_node_num = LOCAL_NODE; // drives isFromUs()/getFrom()/isToUs()

    // Working primary channel with the default PSK so encrypt/decrypt round-trips.
    channels.initDefaults();
    channels.onConfigChanged();

    pipelineRouter->clearPending();
    pipelineRouter->rxDupe = 0;
    pipelineRouter->txRelayCanceled = 0;
    pipelineRadio->reset();
    pipelineRouting->ackCalls = 0;
    pipelineRouting->lastErr = meshtastic_Routing_Error_NONE;
    pipelineModule->calls = 0;
    pipelineMqtt->clearQueue();
    while (meshtastic_MeshPacket *queued = pipelineService->getForPhone())
        packetPool.release(queued);
    while (meshtastic_QueueStatus *queued = pipelineService->getQueueStatusForPhone())
        pipelineService->releaseQueueStatusToPool(queued);
    resetRoutingAuthEvaluationCount();
}

/// Drop the NodeDB and put the clock and region back. Runs here, not at the end of a test body:
/// an assertion aborts the body, and these would otherwise leak into every later case.
static void pipelineHarnessTearDown()
{
    delete mockNodeDB;
    mockNodeDB = nullptr;
    nodeDB = nullptr;
    Time::useRealClock();
    Time::resetMonotonicForTests();
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();
}
