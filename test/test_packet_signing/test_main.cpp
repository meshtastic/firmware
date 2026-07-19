// Tests for XEdDSA packet-signing *policy* - the receive-path accept/reject behavior and the
// send-path signing policy - as opposed to the raw sign/verify primitive (covered in test_crypto).
//
// The decision logic under test lives in Router.cpp free functions. Groups A/B drive a real
// encode -> decode round-trip through the default channel (perhapsEncode/perhapsDecode, black-box,
// no production changes); later groups exercise routing order and policy helpers directly.
//
//   Group A  receive-side accept/reject matrix (verify, downgrade protection, signer-bit learning)
//   Group B  send-side signing policy (which outgoing packets perhapsEncode signs)
//   Group C  routing pipeline ordering (authenticate before duplicate/retry/relay state)
//   Group D  encoding invariants the routing gates depend on
//   Group E  decoded-ingress policy (checkXeddsaReceivePolicy, the plaintext-MQTT trust boundary)

#include "MeshTypes.h" // include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

// The whole suite exercises XEdDSA sign/verify and checkXeddsaReceivePolicy, all of which are
// compiled out unless both PKI and XEdDSA are enabled (e.g. stm32 sets MESHTASTIC_EXCLUDE_XEDDSA).
#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)

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
#include <ErriezCRC32.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <pb_encode.h>
#include <vector>

// ---------------------------------------------------------------------------
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

    std::vector<meshtastic_NodeInfoLite> testNodes;
};

static MockNodeDB *mockNodeDB = nullptr;

class AuthPipelineRadio : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        sendCalls++;
        packetPool.release(p);
        return ERRNO_OK;
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
    void reset() { sendCalls = cancelCalls = findCalls = removeCalls = 0; }

    uint32_t sendCalls = 0;
    uint32_t cancelCalls = 0;
    uint32_t findCalls = 0;
    uint32_t removeCalls = 0;
};

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
    size_t pendingCount() const { return pending.size(); }
    void clearPending()
    {
        for (auto &entry : pending)
            packetPool.release(entry.second.packet);
        pending.clear();
    }
};

class AuthPipelineRoutingModule : public RoutingModule
{
  public:
    void sendAckNak(meshtastic_Routing_Error, NodeNum, PacketId, ChannelIndex, uint8_t = 0, bool = false) override { ackCalls++; }
    uint32_t ackCalls = 0;
};

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
// Helpers
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
static void signWithCurrentKey(meshtastic_MeshPacket *p)
{
    bool ok = crypto->xeddsa_sign(p->from, p->id, p->decoded.portnum, p->decoded.payload.bytes, p->decoded.payload.size,
                                  p->decoded.xeddsa_signature.bytes);
    TEST_ASSERT_TRUE_MESSAGE(ok, "xeddsa_sign failed in test setup");
    p->decoded.xeddsa_signature.size = XEDDSA_SIGNATURE_SIZE;
}

// Encrypt (perhapsEncode) then decrypt+evaluate (perhapsDecode) the same packet in place.
static DecodeState roundTrip(meshtastic_MeshPacket *p)
{
    meshtastic_Routing_Error enc = perhapsEncode(p);
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Routing_Error_NONE, enc, "perhapsEncode did not succeed");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_MeshPacket_encrypted_tag, p->which_payload_variant,
                              "perhapsEncode left packet unencrypted");
    return perhapsDecode(p);
}

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

static meshtastic_MeshPacket makeSignedWirePacket(NodeNum from, NodeNum to, PacketId id, uint8_t hopLimit = 1,
                                                  uint8_t hopStart = 2, uint8_t nextHop = NO_NEXT_HOP_PREFERENCE,
                                                  uint8_t relayNode = 0x33, bool valid = true)
{
    meshtastic_MeshPacket p = makeDecoded(from, to, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    p.id = id;
    p.hop_limit = hopLimit;
    p.hop_start = hopStart;
    p.next_hop = nextHop;
    p.relay_node = relayNode;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    signWithCurrentKey(&p);
    if (!valid)
        p.decoded.xeddsa_signature.bytes[0] ^= 0x80;
    return channelEncode(p);
}

static bool remoteSignerBit()
{
    return nodeInfoLiteHasXeddsaSigned(mockNodeDB->getMeshNode(REMOTE_NODE));
}

static void setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy policy)
{
    config.security.packet_signature_policy = policy;
}

// Size a Data message exactly as the wire encoder would.
static size_t encodedDataSize(const meshtastic_Data *d)
{
    size_t s = 0;
    TEST_ASSERT_TRUE_MESSAGE(pb_get_encoded_size(&s, &meshtastic_Data_msg, d), "pb_get_encoded_size failed");
    return s;
}

// Would this Data still fit a LoRa frame with a 64-byte signature attached? Mirror of the
// production gate in Router.cpp (signedDataFits / the perhapsDecode downgrade predicate).
static bool signedEncodingFits(const meshtastic_Data *d)
{
    meshtastic_Data copy = *d;
    copy.xeddsa_signature.size = XEDDSA_SIGNATURE_SIZE;
    return encodedDataSize(&copy) + MESHTASTIC_HEADER_LENGTH <= MAX_LORA_PAYLOAD_LEN;
}

// Append a length-delimited field whose tag this build's Data schema does not define, as a sender
// on a newer schema would emit. nanopb skips unknown fields at decode, so these bytes count toward
// the raw wire size but not the decoded struct. Returns the number of bytes appended.
static size_t appendUnknownField(uint8_t *dst, size_t dstLen, size_t contentLen)
{
    constexpr uint32_t UNKNOWN_FIELD_NUMBER = 100; // not a field of meshtastic_Data
    std::vector<uint8_t> content(contentLen, 0x77);
    pb_ostream_t stream = pb_ostream_from_buffer(dst, dstLen);
    TEST_ASSERT_TRUE(pb_encode_tag(&stream, PB_WT_STRING, UNKNOWN_FIELD_NUMBER));
    TEST_ASSERT_TRUE(pb_encode_string(&stream, content.data(), content.size()));
    return stream.bytes_written;
}

// Channel-encrypt raw Data bytes into a packet, exactly as perhapsEncode's non-PKI path does.
// Used to inject wire bytes perhapsEncode would never produce (it only encodes p->decoded).
static void encryptAsChannelPacket(meshtastic_MeshPacket *p, uint8_t *wire, size_t size)
{
    const int16_t hash = channels.setActiveByIndex(0);
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(0, hash, "no usable primary channel");
    crypto->encryptPacket(getFrom(p), p->id, size, wire);
    memcpy(p->encrypted.bytes, wire, size);
    p->encrypted.size = size;
    p->channel = hash; // on the wire the channel field carries the hash, not the index
    p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
}

// Build A10's frame: an unsigned broadcast carrying a POSITION payload plus unknown fields, sized
// so the raw wire length exceeds the signature-fit threshold while the decoded fields stay under
// it. Channel-encrypted like a normal sender. The asserts pin that split, which is what makes A10
// and A11 meaningful.
static meshtastic_MeshPacket makeBroadcastWithUnknownFields()
{
    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);

    uint8_t wire[MAX_LORA_PAYLOAD_LEN + 1];
    const size_t base = pb_encode_to_bytes(wire, sizeof(wire), &meshtastic_Data_msg, &p.decoded);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, base, "failed to encode the base Data");
    const size_t raw = base + appendUnknownField(wire + base, sizeof(wire) - base, 160);

    // The decoded fields fit a signature, so a sender that signs would have signed this Data.
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(MAX_LORA_PAYLOAD_LEN, base + XEDDSA_SIGNATURE_FIELD_BYTES + MESHTASTIC_HEADER_LENGTH,
                                      "decoded fields must fit a signature, else the test is vacuous");
    // The unknown fields put the raw size over that threshold, so the two sizings disagree here.
    TEST_ASSERT_GREATER_THAN_MESSAGE(MAX_LORA_PAYLOAD_LEN, raw + XEDDSA_SIGNATURE_FIELD_BYTES + MESHTASTIC_HEADER_LENGTH,
                                     "unknown fields must push the raw size past the fit threshold");
    // The frame is still one a radio could actually send.
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(MAX_LORA_PAYLOAD_LEN, raw + MESHTASTIC_HEADER_LENGTH, "frame must still fit a LoRa frame");

    encryptAsChannelPacket(&p, wire, raw);
    return p;
}

// ---------------------------------------------------------------------------
// Unity lifecycle
// ---------------------------------------------------------------------------
void setUp(void)
{
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
    // Exercise the downgrade-protection matrix by default. Production defaults to
    // COMPATIBLE so existing meshes remain interoperable; tests that cover that
    // mode opt in explicitly.
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_BALANCED);
    myNodeInfo.my_node_num = LOCAL_NODE; // drives isFromUs()/getFrom()/isToUs()

    // Working primary channel with the default PSK so encrypt/decrypt round-trips.
    channels.initDefaults();
    channels.onConfigChanged();

    pipelineRouter->clearPending();
    pipelineRouter->rxDupe = 0;
    pipelineRouter->txRelayCanceled = 0;
    pipelineRadio->reset();
    pipelineRouting->ackCalls = 0;
    pipelineModule->calls = 0;
    pipelineMqtt->clearQueue();
    while (meshtastic_MeshPacket *queued = pipelineService->getForPhone())
        packetPool.release(queued);
    resetRoutingAuthEvaluationCount();
}

void tearDown(void)
{
    delete mockNodeDB;
    mockNodeDB = nullptr;
    nodeDB = nullptr;
}

// ===========================================================================
// Group A - receive-side accept/reject matrix
// ===========================================================================

// A1: valid signature from a node whose key we know -> accepted, marked signed, signer bit learned.
void test_A1_valid_signature_accepted_and_learns_signer(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv); // engine now holds REMOTE's key
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setPublicKey(REMOTE_NODE, pub);

    TEST_ASSERT_FALSE(remoteSignerBit()); // not known as a signer yet

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    signWithCurrentKey(&p);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_TRUE(p.xeddsa_signed);
    TEST_ASSERT_TRUE_MESSAGE(remoteSignerBit(), "verified signature must set the signer bit");
}

// A2: a tampered signature from a known key -> dropped.
void test_A2_bad_signature_dropped(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_COMPATIBLE);
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setPublicKey(REMOTE_NODE, pub);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    signWithCurrentKey(&p);
    p.decoded.xeddsa_signature.bytes[0] ^= 0xFF; // corrupt the signature

    TEST_ASSERT_EQUAL(DECODE_POLICY_REJECT, roundTrip(&p));
}

// A3: signed packet but we have no key for the sender -> accepted unverified, signer bit NOT set.
void test_A3_signed_no_pubkey_accepted_unverified(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_BALANCED);
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(REMOTE_NODE); // node exists, but no public key stored

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    signWithCurrentKey(&p);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_FALSE_MESSAGE(p.xeddsa_signed, "cannot be marked verified without a key");
    TEST_ASSERT_FALSE_MESSAGE(remoteSignerBit(), "must not learn signer without verifying");
}

// A4: downgrade protection - unsigned small broadcast from a known signer -> dropped.
void test_A4_downgrade_unsigned_broadcast_from_signer_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true); // we've seen this node sign before

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    // from != us, so perhapsEncode leaves it unsigned.

    TEST_ASSERT_EQUAL(DECODE_POLICY_REJECT, roundTrip(&p));
}

// A5: no prior knowledge - unsigned small broadcast from a non-signer -> accepted.
void test_A5_unsigned_broadcast_from_nonsigner_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE); // signer bit clear

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_FALSE(p.xeddsa_signed);
}

// A6: unsigned UNICAST from a known signer -> accepted (unicasts are never signed).
void test_A6_unsigned_unicast_from_signer_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    // Unicast to us; PRIVATE_APP avoids the unrelated legacy-DM rejection for TEXT_MESSAGE_APP.
    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_PRIVATE_APP, SMALL_PAYLOAD);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
}

// A7: unsigned OVERSIZED broadcast from a known signer -> accepted (couldn't have carried a sig).
void test_A7_unsigned_oversized_broadcast_from_signer_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, OVERSIZED_PAYLOAD);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
}

// A8: F2 regression - unsigned broadcast from a signer in the old "dead band": its *encoded* Data
// can't take a 64-byte signature and still fit a LoRa frame, but the old payload-size heuristic
// (payload + 64 < DATA_PAYLOAD_LEN) judged it signable and dropped it as a downgrade. Must be
// accepted: an honest signer physically cannot sign this packet.
void test_A8_unsigned_deadband_broadcast_from_signer_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    // Shape it like a real sender's Data: perhapsEncode adds the bitfield to packets a node
    // originates, so remote broadcast traffic carries it too.
    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, 167);
    p.decoded.has_bitfield = true;
    p.decoded.bitfield = 0;

    // Pin the payload inside the dead band; if Data's encoding ever shifts, retune the payload
    // size above instead of letting this test pass vacuously.
    TEST_ASSERT_TRUE_MESSAGE(p.decoded.payload.size + XEDDSA_SIGNATURE_SIZE < meshtastic_Constants_DATA_PAYLOAD_LEN,
                             "payload must sit in the old heuristic's drop range");
    TEST_ASSERT_FALSE_MESSAGE(signedEncodingFits(&p.decoded), "signed encoding must NOT fit a LoRa frame");

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_FALSE(p.xeddsa_signed);
}

// A9: the boundary holds - the largest broadcast whose signed encoding still fits is still
// subject to the downgrade drop when it arrives unsigned from a known signer.
// (Deliberately non-discriminating: the old heuristic dropped this packet too. A9 pins the
// boundary against over-correction; A8 and B4 are the F2 regression discriminators.)
void test_A9_unsigned_boundary_broadcast_from_signer_still_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, 166);
    p.decoded.has_bitfield = true;
    p.decoded.bitfield = 0;

    // Exactly at the limit: signed encoding fills the frame to the last byte. Pinned so the
    // boundary can't silently drift.
    meshtastic_Data signedCopy = p.decoded;
    signedCopy.xeddsa_signature.size = XEDDSA_SIGNATURE_SIZE;
    TEST_ASSERT_EQUAL_MESSAGE(MAX_LORA_PAYLOAD_LEN, encodedDataSize(&signedCopy) + MESHTASTIC_HEADER_LENGTH,
                              "payload no longer sits exactly on the fit boundary - retune it");

    TEST_ASSERT_EQUAL(DECODE_POLICY_REJECT, roundTrip(&p));
}

void test_A10_compatible_accepts_unsigned_broadcast_from_signer(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_COMPATIBLE);
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
}

void test_A11_strict_rejects_unsigned_all_portnums_destinations_and_sizes(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    const meshtastic_PortNum ports[] = {
        meshtastic_PortNum_TEXT_MESSAGE_APP, meshtastic_PortNum_POSITION_APP, meshtastic_PortNum_TELEMETRY_APP,
        meshtastic_PortNum_NODEINFO_APP,     meshtastic_PortNum_WAYPOINT_APP,
    };
    for (const auto port : ports) {
        meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, port, SMALL_PAYLOAD);
        TEST_ASSERT_EQUAL(DECODE_POLICY_REJECT, roundTrip(&p));
    }

    meshtastic_MeshPacket unicast = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    TEST_ASSERT_EQUAL(DECODE_POLICY_REJECT, roundTrip(&unicast));

    meshtastic_MeshPacket oversized =
        makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, OVERSIZED_PAYLOAD);
    TEST_ASSERT_EQUAL(DECODE_POLICY_REJECT, roundTrip(&oversized));
}

void test_A12_strict_rejects_signed_packet_without_key(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    signWithCurrentKey(&p);
    TEST_ASSERT_EQUAL(DECODE_POLICY_REJECT, roundTrip(&p));
}

void test_A13_strict_accepts_locally_authenticated_pki_packet(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    uint8_t localPub[32], localPriv[32], remotePub[32], remotePriv[32];
    crypto->generateKeyPair(localPub, localPriv);
    crypto->generateKeyPair(remotePub, remotePriv);
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, localPub);
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setPublicKey(REMOTE_NODE, remotePub);

    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_PRIVATE_APP;
    data.payload.size = SMALL_PAYLOAD;
    memset(data.payload.bytes, 0x5A, data.payload.size);
    uint8_t plaintext[MAX_LORA_PAYLOAD_LEN + 1] = {};
    const size_t plaintextSize = pb_encode_to_bytes(plaintext, sizeof(plaintext), &meshtastic_Data_msg, &data);
    TEST_ASSERT_GREATER_THAN(0, plaintextSize);

    meshtastic_NodeInfoLite_public_key_t localKey = {32, {0}};
    memcpy(localKey.bytes, localPub, sizeof(localPub));
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = REMOTE_NODE;
    p.to = LOCAL_NODE;
    p.id = 0x0CC01234;
    p.channel = 0;
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    crypto->setDHPrivateKey(remotePriv);
    TEST_ASSERT_TRUE(crypto->encryptCurve25519(p.to, p.from, localKey, p.id, plaintextSize, plaintext, p.encrypted.bytes));
    p.encrypted.size = plaintextSize + MESHTASTIC_PKC_OVERHEAD;

    // Only the receiver's private key can establish the local pki_encrypted authentication marker.
    crypto->setDHPrivateKey(localPriv);
    TEST_ASSERT_EQUAL(DECODE_SUCCESS, perhapsDecode(&p));
    TEST_ASSERT_TRUE(p.pki_encrypted);
    TEST_ASSERT_EQUAL(meshtastic_PortNum_PRIVATE_APP, p.decoded.portnum);
}

void test_A13b_strict_rejects_spoofed_pki_flag_on_encrypted_ingress(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, perhapsEncode(&p));
    p.pki_encrypted = true;
    p.public_key.size = 32;
    memset(p.public_key.bytes, 0xAB, p.public_key.size);

    TEST_ASSERT_EQUAL(DECODE_POLICY_REJECT, perhapsDecode(&p));
    TEST_ASSERT_FALSE(p.pki_encrypted);
    TEST_ASSERT_EQUAL(0, p.public_key.size);
}

void test_A14_strict_bootstraps_identity_bound_signed_nodeinfo(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    const NodeNum signer = crc32Buffer(pub, sizeof(pub));

    meshtastic_User user = meshtastic_User_init_zero;
    user.public_key.size = sizeof(pub);
    memcpy(user.public_key.bytes, pub, sizeof(pub));
    meshtastic_MeshPacket p = makeDecoded(signer, NODENUM_BROADCAST, meshtastic_PortNum_NODEINFO_APP, 0);
    p.decoded.payload.size =
        pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_User_msg, &user);
    signWithCurrentKey(&p);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    const meshtastic_NodeInfoLite *node = mockNodeDB->getMeshNode(signer);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pub, node->public_key.bytes, sizeof(pub));
    TEST_ASSERT_TRUE(p.xeddsa_signed);
}

void test_A15_strict_rejects_nodeinfo_key_without_identity_binding(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);

    meshtastic_User user = meshtastic_User_init_zero;
    user.public_key.size = sizeof(pub);
    memcpy(user.public_key.bytes, pub, sizeof(pub));
    meshtastic_MeshPacket p =
        makeDecoded(crc32Buffer(pub, sizeof(pub)) ^ 1, NODENUM_BROADCAST, meshtastic_PortNum_NODEINFO_APP, 0);
    p.decoded.payload.size =
        pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_User_msg, &user);
    signWithCurrentKey(&p);

    TEST_ASSERT_EQUAL(DECODE_POLICY_REJECT, roundTrip(&p));
    TEST_ASSERT_NULL(mockNodeDB->getMeshNode(p.from));
}

void test_A16_compatible_rejects_invalid_first_contact_nodeinfo(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_COMPATIBLE);
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);

    meshtastic_User user = meshtastic_User_init_zero;
    user.public_key.size = sizeof(pub);
    memcpy(user.public_key.bytes, pub, sizeof(pub));
    meshtastic_MeshPacket p =
        makeDecoded(crc32Buffer(pub, sizeof(pub)) ^ 1, NODENUM_BROADCAST, meshtastic_PortNum_NODEINFO_APP, 0);
    p.decoded.payload.size =
        pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_User_msg, &user);
    signWithCurrentKey(&p);

    TEST_ASSERT_EQUAL(DECODE_POLICY_REJECT, roundTrip(&p));
}

#if WARM_NODE_COUNT > 0
void test_A17_strict_verifies_signer_from_warm_key_store(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    TEST_ASSERT_TRUE(mockNodeDB->warmStore.absorb(REMOTE_NODE, 1, pub));
    TEST_ASSERT_NULL(mockNodeDB->getMeshNode(REMOTE_NODE));

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    signWithCurrentKey(&p);
    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_TRUE(p.xeddsa_signed);
    const meshtastic_NodeInfoLite *rehydrated = mockNodeDB->getMeshNode(REMOTE_NODE);
    TEST_ASSERT_NOT_NULL_MESSAGE(rehydrated, "verified warm signer must be re-admitted to the hot store");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pub, rehydrated->public_key.bytes, sizeof(pub));
    TEST_ASSERT_TRUE_MESSAGE(nodeInfoLiteHasXeddsaSigned(rehydrated), "re-admitted signer must retain Balanced downgrade memory");

    // Model its next hot-store eviction and prove Balanced still remembers the signer without
    // allocating a hot node merely to evaluate an unsigned packet.
    TEST_ASSERT_TRUE(mockNodeDB->warmStore.absorb(REMOTE_NODE, 2, pub, meshtastic_Config_DeviceConfig_Role_CLIENT,
                                                  static_cast<uint8_t>(WarmProtected::XeddsaSigner)));
    mockNodeDB->clearTestNodes();
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_BALANCED);
    meshtastic_MeshPacket unsignedPacket =
        makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    TEST_ASSERT_FALSE_MESSAGE(checkXeddsaReceivePolicy(&unsignedPacket),
                              "Balanced downgrade memory must survive repeated hot-store eviction");
}
#endif
// ===========================================================================
// Group B - send-side signing policy (perhapsEncode)
// ===========================================================================

// B1: our own small broadcast is auto-signed (and verifies on the way back in).
void test_B1_local_broadcast_is_signed(void)
{
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv); // engine signs with this; store the matching pubkey for us
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, pub);

    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_EQUAL_MESSAGE(XEDDSA_SIGNATURE_SIZE, p.decoded.xeddsa_signature.size, "broadcast should be auto-signed");
    TEST_ASSERT_TRUE(p.xeddsa_signed);
}

// B2: preserve the existing wire behavior: non-PKI unicast is not signed.
void test_B2_local_unicast_not_signed(void)
{
    mockNodeDB->addNode(REMOTE_NODE);

    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_EQUAL_MESSAGE(0, p.decoded.xeddsa_signature.size, "unicast must remain unsigned");
}

// B3: our own oversized broadcast is NOT signed (signature wouldn't fit).
void test_B3_local_oversized_broadcast_not_signed(void)
{
    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, OVERSIZED_PAYLOAD);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_EQUAL_MESSAGE(0, p.decoded.xeddsa_signature.size, "oversized broadcast must not be signed");
}

// B4: F2 regression sweep - every broadcast payload size that fits a LoRa frame unsigned must
// still be deliverable: signing steps aside exactly when the signed encoding stops fitting,
// never producing TOO_LARGE (the old heuristic dead-banded payloads 167-168). Because the first
// verified packet sets our signer bit in the mock DB, the later unsigned sizes also prove the
// receiver's downgrade predicate stays exactly symmetric with the sender's sign gate.
void test_B4_all_broadcast_sizes_deliverable_no_deadband(void)
{
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, pub);

    bool sawSigned = false, sawUnsigned = false;
    for (size_t n = 1; n <= 232; n++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "payload size %u", (unsigned)n);

        meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, n);
        TEST_ASSERT_EQUAL_MESSAGE(DECODE_SUCCESS, roundTrip(&p), msg);

        // Exact oracle: signed iff the signed encoding fits the frame. signedEncodingFits() forces
        // the signature size itself, so it reads the same whether or not p.decoded came back signed.
        const bool isSigned = p.decoded.xeddsa_signature.size == XEDDSA_SIGNATURE_SIZE;
        TEST_ASSERT_EQUAL_MESSAGE(signedEncodingFits(&p.decoded), isSigned, msg);

        if (isSigned) {
            TEST_ASSERT_FALSE_MESSAGE(sawUnsigned, msg);    // monotonic: once too big, never signed again
            TEST_ASSERT_TRUE_MESSAGE(p.xeddsa_signed, msg); // and it verified on the way back in
            sawSigned = true;
        } else {
            sawUnsigned = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(sawSigned, "sweep never produced a signed packet");
    TEST_ASSERT_TRUE_MESSAGE(sawUnsigned, "sweep never crossed the fit boundary");
}

// B5: a client-preset signature on a packet outside the existing broadcast sign class is discarded.
void test_B5_preset_signature_on_local_packet_cleared(void)
{
    mockNodeDB->addNode(REMOTE_NODE);

    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    p.decoded.xeddsa_signature.size = XEDDSA_SIGNATURE_SIZE;
    memset(p.decoded.xeddsa_signature.bytes, 0xAB, XEDDSA_SIGNATURE_SIZE);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_EQUAL_MESSAGE(0, p.decoded.xeddsa_signature.size, "preset signature must be discarded on unicast");
}

// B6: the exact-fit gate tracks Data *shape*, not just payload size. A tapback-style broadcast
// (want_response + reply_id + emoji) carries extra wire bytes that shift the fit boundary; the
// sweep proves no dead band exists for that shape either, and - once the signer bit is learned -
// that the receiver's downgrade predicate stays symmetric for it too. Window
// straddles this shape's boundary; capped at 200 so even the unsigned rich encoding stays well
// inside the frame (at n=221 it first hits the pre-existing, signing-unrelated TOO_LARGE).
void test_B6_rich_shape_sweep_no_deadband(void)
{
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, pub);

    bool sawSigned = false, sawUnsigned = false;
    for (size_t n = 100; n <= 200; n++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "payload size %u", (unsigned)n);

        meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, n);
        p.decoded.want_response = true;
        p.decoded.reply_id = 0x11223344;
        p.decoded.emoji = 1;
        TEST_ASSERT_EQUAL_MESSAGE(DECODE_SUCCESS, roundTrip(&p), msg);

        const bool isSigned = p.decoded.xeddsa_signature.size == XEDDSA_SIGNATURE_SIZE;
        TEST_ASSERT_EQUAL_MESSAGE(signedEncodingFits(&p.decoded), isSigned, msg);

        if (isSigned) {
            TEST_ASSERT_FALSE_MESSAGE(sawUnsigned, msg);
            TEST_ASSERT_TRUE_MESSAGE(p.xeddsa_signed, msg);
            sawSigned = true;
        } else {
            sawUnsigned = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(sawSigned, "rich sweep never produced a signed packet");
    TEST_ASSERT_TRUE_MESSAGE(sawUnsigned, "rich sweep never crossed the fit boundary");
}

void test_B7_infrastructure_port_signing_matrix(void)
{
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, pub);

    const meshtastic_PortNum ports[] = {
        meshtastic_PortNum_NODEINFO_APP,
        meshtastic_PortNum_ROUTING_APP,
        meshtastic_PortNum_TRACEROUTE_APP,
        meshtastic_PortNum_POSITION_APP,
    };
    for (const auto port : ports) {
        meshtastic_MeshPacket broadcast = makeDecoded(LOCAL_NODE, NODENUM_BROADCAST, port, SMALL_PAYLOAD);
        TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&broadcast));
        TEST_ASSERT_EQUAL_MESSAGE(XEDDSA_SIGNATURE_SIZE, broadcast.decoded.xeddsa_signature.size,
                                  "signable infrastructure broadcast must be signed");

        meshtastic_MeshPacket unicast = makeDecoded(LOCAL_NODE, REMOTE_NODE, port, SMALL_PAYLOAD);
        TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&unicast));
        TEST_ASSERT_EQUAL_MESSAGE(0, unicast.decoded.xeddsa_signature.size,
                                  "infrastructure unicast must preserve existing unsigned behavior");
    }
}

// ===========================================================================
// Group C - routing pipeline authentication ordering
// ===========================================================================

static void preparePipelineSigner(NodeNum sender)
{
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(sender);
    mockNodeDB->setPublicKey(sender, pub);
}

static void runPipelineIngress(const meshtastic_MeshPacket &p)
{
    meshtastic_MeshPacket *copy = packetPool.allocCopy(p);
    TEST_ASSERT_NOT_NULL(copy);
    pipelineRouter->enqueueReceivedMessage(copy);
    pipelineRouter->runOnce();
}

static void assertNoRejectedPipelineEffects(NodeNum sender, uint32_t lastHeardBefore)
{
    TEST_ASSERT_EQUAL(0, pipelineRadio->sendCalls);
    TEST_ASSERT_EQUAL(0, pipelineRadio->cancelCalls);
    TEST_ASSERT_EQUAL(0, pipelineRadio->findCalls);
    TEST_ASSERT_EQUAL(0, pipelineRadio->removeCalls);
    TEST_ASSERT_EQUAL(0, pipelineRouting->ackCalls);
    TEST_ASSERT_EQUAL(0, pipelineRouter->rxDupe);
    TEST_ASSERT_EQUAL(0, pipelineRouter->txRelayCanceled);
    TEST_ASSERT_EQUAL(0, pipelineModule->calls);
    TEST_ASSERT_EQUAL(0, pipelineMqtt->queueSize());
    TEST_ASSERT_NULL(pipelineService->getForPhone());
    const meshtastic_NodeInfoLite *node = mockNodeDB->getMeshNode(sender);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_UINT32(lastHeardBefore, node->last_heard);
}

void test_C1_invalid_first_copy_does_not_poison_valid_same_id(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    preparePipelineSigner(REMOTE_NODE);
    const PacketId id = 0xC1000001;
    const uint32_t lastHeard = mockNodeDB->getMeshNode(REMOTE_NODE)->last_heard;

    meshtastic_MeshPacket invalid = makeSignedWirePacket(REMOTE_NODE, NODENUM_BROADCAST, id, 1, 2, 0, 0x31, false);
    moduleConfig.mqtt.enabled = true;
    runPipelineIngress(invalid);
    assertNoRejectedPipelineEffects(REMOTE_NODE, lastHeard);
    TEST_ASSERT_FALSE(pipelineRouter->historyContains(&invalid));

    meshtastic_MeshPacket valid = makeSignedWirePacket(REMOTE_NODE, NODENUM_BROADCAST, id);
    TEST_ASSERT_EQUAL(static_cast<int>(RoutingAuthVerdict::ACCEPT), static_cast<int>(passesRoutingAuthGate(&valid)));
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_MeshPacket_encrypted_tag, valid.which_payload_variant,
                              "routing auth gate must preserve encrypted relay/MQTT bytes");
    TEST_ASSERT_FALSE_MESSAGE(pipelineRouter->filter(&valid), "valid same-ID packet was poisoned by rejected first copy");
    TEST_ASSERT_TRUE(pipelineRouter->historyContains(&valid));
}

void test_C2_invalid_ordinary_duplicate_has_no_cancel_or_delivery_effects(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    preparePipelineSigner(REMOTE_NODE);
    const PacketId id = 0xC2000002;
    meshtastic_MeshPacket prior = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    prior.id = id;
    prior.hop_limit = 1;
    prior.hop_start = 2;
    prior.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    pipelineRouter->remember(&prior);
    const uint32_t lastHeard = mockNodeDB->getMeshNode(REMOTE_NODE)->last_heard;

    meshtastic_MeshPacket invalid = makeSignedWirePacket(REMOTE_NODE, NODENUM_BROADCAST, id, 1, 2, 0, 0x32, false);
    runPipelineIngress(invalid);
    assertNoRejectedPipelineEffects(REMOTE_NODE, lastHeard);
    TEST_ASSERT_TRUE(pipelineRouter->historyContains(&prior));
}

void test_C3_invalid_repeated_packet_cannot_ack_or_change_retry_state(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    preparePipelineSigner(LOCAL_NODE);
    const PacketId id = 0xC3000003;
    meshtastic_MeshPacket prior = makeDecoded(LOCAL_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    prior.id = id;
    prior.hop_limit = 2;
    prior.hop_start = 2;
    prior.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    pipelineRouter->remember(&prior);
    pipelineRouter->addPending(prior, UINT32_MAX);
    const uint32_t lastHeard = mockNodeDB->getMeshNode(LOCAL_NODE)->last_heard;

    meshtastic_MeshPacket invalid = makeSignedWirePacket(LOCAL_NODE, NODENUM_BROADCAST, id, 2, 2, 0, 0x34, false);
    runPipelineIngress(invalid);
    assertNoRejectedPipelineEffects(LOCAL_NODE, lastHeard);
    TEST_ASSERT_EQUAL(1, pipelineRouter->pendingCount());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, pipelineRouter->pendingNextTx(LOCAL_NODE, id));
}

void test_C4_invalid_fallback_packet_cannot_relay(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    preparePipelineSigner(REMOTE_NODE);
    const PacketId id = 0xC4000004;
    const uint8_t ourRelay = mockNodeDB->getLastByteOfNodeNum(LOCAL_NODE);
    meshtastic_MeshPacket prior = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    prior.id = id;
    prior.next_hop = 0x22;
    prior.relay_node = ourRelay;
    prior.hop_limit = 1;
    prior.hop_start = 2;
    pipelineRouter->remember(&prior);
    meshtastic_MeshPacket relayed = prior;
    relayed.relay_node = 0x35;
    pipelineRouter->remember(&relayed);
    pipelineRouter->forgetRelayer(ourRelay, id, REMOTE_NODE);
    const uint32_t lastHeard = mockNodeDB->getMeshNode(REMOTE_NODE)->last_heard;

    meshtastic_MeshPacket invalid = makeSignedWirePacket(REMOTE_NODE, LOCAL_NODE, id, 1, 2, 0, 0x35, false);
    runPipelineIngress(invalid);
    assertNoRejectedPipelineEffects(REMOTE_NODE, lastHeard);
}

void test_C5_invalid_upgrade_cannot_remove_pending_valid_send(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    preparePipelineSigner(REMOTE_NODE);
    const PacketId id = 0xC5000005;
    meshtastic_MeshPacket prior = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    prior.id = id;
    prior.hop_limit = 1;
    prior.hop_start = 2;
    pipelineRouter->remember(&prior);
    const uint32_t lastHeard = mockNodeDB->getMeshNode(REMOTE_NODE)->last_heard;

    meshtastic_MeshPacket directInvalid = makeSignedWirePacket(REMOTE_NODE, NODENUM_BROADCAST, id, 2, 2, 0, 0x36, false);
    TEST_ASSERT_TRUE(pipelineRouter->handleUpgrade(&directInvalid));
    TEST_ASSERT_EQUAL(0, pipelineRadio->removeCalls);

    meshtastic_MeshPacket invalid = makeSignedWirePacket(REMOTE_NODE, NODENUM_BROADCAST, id, 2, 2, 0, 0x36, false);
    runPipelineIngress(invalid);
    assertNoRejectedPipelineEffects(REMOTE_NODE, lastHeard);

    // The rejected upgrade did not raise the history watermark or remove the queued valid copy;
    // a later authenticated replacement still performs the intended upgrade.
    meshtastic_MeshPacket valid = makeSignedWirePacket(REMOTE_NODE, NODENUM_BROADCAST, id, 2, 2, 0, 0x36, true);
    TEST_ASSERT_EQUAL(static_cast<int>(RoutingAuthVerdict::ACCEPT), static_cast<int>(passesRoutingAuthGate(&valid)));
    TEST_ASSERT_TRUE(pipelineRouter->filter(&valid));
    TEST_ASSERT_EQUAL(1, pipelineRadio->removeCalls);
}

void test_C6_opaque_unknown_channel_is_relay_only(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    meshtastic_MeshPacket opaque = meshtastic_MeshPacket_init_zero;
    opaque.from = REMOTE_NODE;
    opaque.to = NODENUM_BROADCAST;
    opaque.id = 0xC6000006;
    opaque.channel = 0xFE;
    opaque.hop_limit = 1;
    opaque.hop_start = 2;
    opaque.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    opaque.encrypted.size = 16;
    memset(opaque.encrypted.bytes, 0xA5, opaque.encrypted.size);

    TEST_ASSERT_EQUAL(static_cast<int>(RoutingAuthVerdict::OPAQUE_RELAY_ONLY), static_cast<int>(passesRoutingAuthGate(&opaque)));
    moduleConfig.mqtt.enabled = true;
    runPipelineIngress(opaque);
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineRadio->sendCalls, "opaque broadcast should take only the safety-controlled relay path");
    TEST_ASSERT_EQUAL(0, pipelineRouting->ackCalls);
    TEST_ASSERT_EQUAL(0, pipelineModule->calls);
    TEST_ASSERT_EQUAL(0, pipelineMqtt->queueSize());
    TEST_ASSERT_NULL(pipelineService->getForPhone());
    TEST_ASSERT_FALSE(pipelineRouter->historyContains(&opaque));
    TEST_ASSERT_NULL(mockNodeDB->getMeshNode(REMOTE_NODE));

    pipelineRadio->reset();
    meshtastic_MeshPacket addressed = opaque;
    addressed.to = LOCAL_NODE;
    addressed.id++;
    runPipelineIngress(addressed);
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRadio->sendCalls, "opaque packet addressed to us must not be relayed");
    TEST_ASSERT_EQUAL(0, pipelineRouting->ackCalls);
    TEST_ASSERT_EQUAL(0, pipelineModule->calls);
    TEST_ASSERT_EQUAL(0, pipelineMqtt->queueSize());
    TEST_ASSERT_NULL(pipelineService->getForPhone());
    TEST_ASSERT_FALSE(pipelineRouter->historyContains(&addressed));

    const meshtastic_Config_DeviceConfig_RebroadcastMode blockedModes[] = {
        meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY,
        meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY,
        meshtastic_Config_DeviceConfig_RebroadcastMode_NONE,
    };
    for (const auto mode : blockedModes) {
        pipelineRadio->reset();
        config.device.rebroadcast_mode = mode;
        meshtastic_MeshPacket blocked = opaque;
        blocked.id++;
        blocked.id += static_cast<uint32_t>(mode);
        blocked.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MULTICAST_UDP;
        runPipelineIngress(blocked);
        TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRadio->sendCalls, "restricted rebroadcast mode must suppress opaque relay");
        TEST_ASSERT_EQUAL(0, pipelineRouting->ackCalls);
        TEST_ASSERT_EQUAL(0, pipelineModule->calls);
        TEST_ASSERT_EQUAL(0, pipelineMqtt->queueSize());
        TEST_ASSERT_NULL(pipelineService->getForPhone());
        TEST_ASSERT_FALSE(pipelineRouter->historyContains(&blocked));
    }
}

void test_C7_strict_rejects_unsigned_decoded_simradio_ingress(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    mockNodeDB->addNode(REMOTE_NODE);
    const uint32_t lastHeard = mockNodeDB->getMeshNode(REMOTE_NODE)->last_heard;
    meshtastic_MeshPacket injected = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    injected.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    runPipelineIngress(injected);
    assertNoRejectedPipelineEffects(REMOTE_NODE, lastHeard);
    TEST_ASSERT_FALSE(pipelineRouter->historyContains(&injected));
}

void test_C8_trusted_local_decoded_delivery_is_not_filtered(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    meshtastic_MeshPacket *local =
        packetPool.allocCopy(makeDecoded(0, LOCAL_NODE, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD));
    TEST_ASSERT_NOT_NULL(local);
    TEST_ASSERT_EQUAL(ERRNO_SHOULD_RELEASE, pipelineRouter->sendLocal(local, RX_SRC_USER));
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineModule->calls, "trusted phone-origin packet must reach local modules");
    packetPool.release(local);
}

void test_C9_known_channel_malformed_plaintext_is_not_relayed_as_opaque(void)
{
    meshtastic_MeshPacket malformed = meshtastic_MeshPacket_init_zero;
    malformed.from = REMOTE_NODE;
    malformed.to = NODENUM_BROADCAST;
    malformed.id = 0xC9000009;
    malformed.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    malformed.encrypted.size = 3;
    malformed.encrypted.bytes[0] = 0xFF;
    malformed.encrypted.bytes[1] = 0xFF;
    malformed.encrypted.bytes[2] = 0xFF;
    malformed.channel = channels.setActiveByIndex(0);
    crypto->encryptPacket(malformed.from, malformed.id, malformed.encrypted.size, malformed.encrypted.bytes);
    mockNodeDB->addNode(REMOTE_NODE);
    const uint32_t lastHeard = mockNodeDB->getMeshNode(REMOTE_NODE)->last_heard;
    runPipelineIngress(malformed);
    assertNoRejectedPipelineEffects(REMOTE_NODE, lastHeard);
    TEST_ASSERT_FALSE(pipelineRouter->historyContains(&malformed));
}

void test_C10_legacy_channel_dm_failure_has_no_pipeline_effects(void)
{
    meshtastic_MeshPacket legacyDm = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    legacyDm = channelEncode(legacyDm);
    mockNodeDB->addNode(REMOTE_NODE);
    const uint32_t lastHeard = mockNodeDB->getMeshNode(REMOTE_NODE)->last_heard;
    moduleConfig.mqtt.enabled = true;
    runPipelineIngress(legacyDm);
    assertNoRejectedPipelineEffects(REMOTE_NODE, lastHeard);
    TEST_ASSERT_FALSE(pipelineRouter->historyContains(&legacyDm));
}

void test_C11_malformed_pki_plaintext_has_no_pipeline_effects(void)
{
    uint8_t localPub[32], localPriv[32], remotePub[32], remotePriv[32];
    crypto->generateKeyPair(localPub, localPriv);
    crypto->generateKeyPair(remotePub, remotePriv);
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, localPub);
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setPublicKey(REMOTE_NODE, remotePub);

    const uint8_t malformedPlaintext[] = {0xFF, 0xFF, 0xFF};
    meshtastic_NodeInfoLite_public_key_t localKey = {32, {0}};
    memcpy(localKey.bytes, localPub, sizeof(localPub));
    meshtastic_MeshPacket malformed = meshtastic_MeshPacket_init_zero;
    malformed.from = REMOTE_NODE;
    malformed.to = LOCAL_NODE;
    malformed.id = 0xCB00000B;
    malformed.channel = 0;
    malformed.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    crypto->setDHPrivateKey(remotePriv);
    TEST_ASSERT_TRUE(crypto->encryptCurve25519(malformed.to, malformed.from, localKey, malformed.id, sizeof(malformedPlaintext),
                                               malformedPlaintext, malformed.encrypted.bytes));
    malformed.encrypted.size = sizeof(malformedPlaintext) + MESHTASTIC_PKC_OVERHEAD;
    crypto->setDHPrivateKey(localPriv);

    const uint32_t lastHeard = mockNodeDB->getMeshNode(REMOTE_NODE)->last_heard;
    moduleConfig.mqtt.enabled = true;
    runPipelineIngress(malformed);
    assertNoRejectedPipelineEffects(REMOTE_NODE, lastHeard);
    TEST_ASSERT_FALSE(pipelineRouter->historyContains(&malformed));
}

void test_C12_exact_authenticated_replay_reuses_verdict_without_collision_bypass(void)
{
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    preparePipelineSigner(REMOTE_NODE);
    meshtastic_MeshPacket valid = makeSignedWirePacket(REMOTE_NODE, NODENUM_BROADCAST, 0xCC00000C);
    // Full ingress replaces this nonzero wire timestamp with the local arrival time. The exact
    // authentication handoff must be consumed before that mutation, avoiding a second evaluation.
    valid.rx_time = 0x12345678;
    runPipelineIngress(valid);
    TEST_ASSERT_EQUAL_MESSAGE(1, routingAuthEvaluationCount(), "full ingress must consume the primed verdict exactly once");
    runPipelineIngress(valid);
    TEST_ASSERT_EQUAL_MESSAGE(2, routingAuthEvaluationCount(), "consumed verdict must not authenticate a later replay");

    meshtastic_MeshPacket collision = valid;
    collision.encrypted.bytes[0] ^= 0x80;
    TEST_ASSERT_EQUAL(static_cast<int>(RoutingAuthVerdict::REJECT), static_cast<int>(passesRoutingAuthGate(&collision)));
    TEST_ASSERT_EQUAL_MESSAGE(3, routingAuthEvaluationCount(), "same packet ID with different bytes must be reevaluated");
}

// ===========================================================================
// Group D - encoding invariants the routing gates depend on
// ===========================================================================

// D1: the encoded overhead of the signature field must be exactly XEDDSA_SIGNATURE_FIELD_BYTES
// (1 tag byte + 1 length byte + 64 signature bytes). The receiver downgrade predicate adds this
// constant to the unsigned size; this test pins that it matches the real wire overhead the
// sender's encoder produces, keeping the two sides symmetric. It drifts if the field number ever
// moves to >= 16 or the signature grows past 127 bytes.
void test_D1_signature_field_overhead_exact(void)
{
    meshtastic_Data d = meshtastic_Data_init_zero;
    d.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    d.payload.size = 100;

    const size_t without = encodedDataSize(&d);
    d.xeddsa_signature.size = XEDDSA_SIGNATURE_SIZE;
    const size_t with = encodedDataSize(&d);

    TEST_ASSERT_EQUAL_MESSAGE(XEDDSA_SIGNATURE_FIELD_BYTES, with - without, "signature field wire overhead drifted");
}

// ===========================================================================
// Group E - decoded-ingress policy (checkXeddsaReceivePolicy)
// ===========================================================================
// Already-decoded packets never reach perhapsDecode's crypto path (it early-returns), so
// plaintext-MQTT downlink applies this policy function directly at ingress (MQTT.cpp). These
// tests drive it the same way: decoded packets, sized from p->decoded exactly as the RF path is.
// End-to-end MQTT wiring is covered in test_mqtt.

// E1: unsigned small broadcast from a known signer -> dropped (downgrade protection holds on
// the decoded-ingress path too - the F3 bypass).
void test_E1_decoded_unsigned_broadcast_from_signer_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);

    TEST_ASSERT_FALSE(checkXeddsaReceivePolicy(&p));
}

// E2: unsigned broadcast from a non-signer -> accepted.
void test_E2_decoded_unsigned_broadcast_from_nonsigner_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE); // signer bit clear

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);

    TEST_ASSERT_TRUE(checkXeddsaReceivePolicy(&p));
    TEST_ASSERT_FALSE(p.xeddsa_signed);
}

// E3: valid signature with a known key -> accepted, marked verified, signer bit learned.
void test_E3_decoded_valid_signature_verified_and_learns_signer(void)
{
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setPublicKey(REMOTE_NODE, pub);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    signWithCurrentKey(&p);

    TEST_ASSERT_TRUE(checkXeddsaReceivePolicy(&p));
    TEST_ASSERT_TRUE(p.xeddsa_signed);
    TEST_ASSERT_TRUE_MESSAGE(remoteSignerBit(), "verified signature must set the signer bit");
}

// E4: corrupted signature with a known key -> dropped.
void test_E4_decoded_bad_signature_dropped(void)
{
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setPublicKey(REMOTE_NODE, pub);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    signWithCurrentKey(&p);
    p.decoded.xeddsa_signature.bytes[0] ^= 0xFF;

    TEST_ASSERT_FALSE(checkXeddsaReceivePolicy(&p));
    TEST_ASSERT_FALSE(p.xeddsa_signed);
}

// E5: unsigned oversized broadcast from a signer -> accepted (packets whose signed encoding
// wouldn't fit are exempt, identically to the RF path: both size p->decoded).
void test_E5_decoded_unsigned_oversized_broadcast_from_signer_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, OVERSIZED_PAYLOAD);

    TEST_ASSERT_TRUE(checkXeddsaReceivePolicy(&p));
}

// E6: Balanced accepts unsigned unicast from a signer for legacy compatibility.
void test_E6_decoded_unsigned_unicast_from_signer_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_PRIVATE_APP, SMALL_PAYLOAD);

    TEST_ASSERT_TRUE(checkXeddsaReceivePolicy(&p));
}

// E8: a crafted partial (non-0, non-64) signature must not let a forged broadcast dodge the
// downgrade drop. A 63-byte junk signature inflates the encoded size past the fit threshold, so
// a size-only predicate would treat the packet as "too big to sign" and accept it as an
// impersonation of signer REMOTE. The malformed-size reject drops it before that math runs.
void test_E8_decoded_partial_signature_from_signer_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    // 146-byte payload sits in the band that WOULD fit a signature (so an honest unsigned one is a
    // downgrade), but the 63 bogus signature bytes push the raw size over the frame limit.
    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, 146);
    p.decoded.xeddsa_signature.size = XEDDSA_SIGNATURE_SIZE - 1;
    memset(p.decoded.xeddsa_signature.bytes, 0xCD, p.decoded.xeddsa_signature.size);

    TEST_ASSERT_FALSE_MESSAGE(checkXeddsaReceivePolicy(&p), "partial signature from a signer must be dropped");
    TEST_ASSERT_FALSE(p.xeddsa_signed);
}

// E9: the malformed-size reject is unconditional - a partial signature is dropped even from a
// node we've never seen sign (an honest sender never emits a 1..63-byte signature field).
void test_E9_decoded_partial_signature_from_nonsigner_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE); // signer bit clear

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    p.decoded.xeddsa_signature.size = 10;
    memset(p.decoded.xeddsa_signature.bytes, 0x5A, p.decoded.xeddsa_signature.size);

    TEST_ASSERT_FALSE_MESSAGE(checkXeddsaReceivePolicy(&p), "partial signature must be dropped as malformed");
}

void setup()
{
    initializeTestEnvironment();

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

    UNITY_BEGIN();

    printf("\n=== Group A: receive-side accept/reject ===\n");
    RUN_TEST(test_A1_valid_signature_accepted_and_learns_signer);
    RUN_TEST(test_A2_bad_signature_dropped);
    RUN_TEST(test_A3_signed_no_pubkey_accepted_unverified);
    RUN_TEST(test_A4_downgrade_unsigned_broadcast_from_signer_dropped);
    RUN_TEST(test_A5_unsigned_broadcast_from_nonsigner_accepted);
    RUN_TEST(test_A6_unsigned_unicast_from_signer_accepted);
    RUN_TEST(test_A7_unsigned_oversized_broadcast_from_signer_accepted);
    RUN_TEST(test_A8_unsigned_deadband_broadcast_from_signer_accepted);
    RUN_TEST(test_A9_unsigned_boundary_broadcast_from_signer_still_dropped);
    RUN_TEST(test_A10_compatible_accepts_unsigned_broadcast_from_signer);
    RUN_TEST(test_A11_strict_rejects_unsigned_all_portnums_destinations_and_sizes);
    RUN_TEST(test_A12_strict_rejects_signed_packet_without_key);
    RUN_TEST(test_A13_strict_accepts_locally_authenticated_pki_packet);
    RUN_TEST(test_A13b_strict_rejects_spoofed_pki_flag_on_encrypted_ingress);
    RUN_TEST(test_A14_strict_bootstraps_identity_bound_signed_nodeinfo);
    RUN_TEST(test_A15_strict_rejects_nodeinfo_key_without_identity_binding);
    RUN_TEST(test_A16_compatible_rejects_invalid_first_contact_nodeinfo);
#if WARM_NODE_COUNT > 0
    RUN_TEST(test_A17_strict_verifies_signer_from_warm_key_store);
#endif

    printf("\n=== Group B: send-side signing policy ===\n");
    RUN_TEST(test_B1_local_broadcast_is_signed);
    RUN_TEST(test_B2_local_unicast_not_signed);
    RUN_TEST(test_B3_local_oversized_broadcast_not_signed);
    RUN_TEST(test_B4_all_broadcast_sizes_deliverable_no_deadband);
    RUN_TEST(test_B5_preset_signature_on_local_packet_cleared);
    RUN_TEST(test_B6_rich_shape_sweep_no_deadband);
    RUN_TEST(test_B7_infrastructure_port_signing_matrix);

    printf("\n=== Group C: routing pipeline authentication ordering ===\n");
    RUN_TEST(test_C1_invalid_first_copy_does_not_poison_valid_same_id);
    RUN_TEST(test_C2_invalid_ordinary_duplicate_has_no_cancel_or_delivery_effects);
    RUN_TEST(test_C3_invalid_repeated_packet_cannot_ack_or_change_retry_state);
    RUN_TEST(test_C4_invalid_fallback_packet_cannot_relay);
    RUN_TEST(test_C5_invalid_upgrade_cannot_remove_pending_valid_send);
    RUN_TEST(test_C6_opaque_unknown_channel_is_relay_only);
    RUN_TEST(test_C7_strict_rejects_unsigned_decoded_simradio_ingress);
    RUN_TEST(test_C8_trusted_local_decoded_delivery_is_not_filtered);
    RUN_TEST(test_C9_known_channel_malformed_plaintext_is_not_relayed_as_opaque);
    RUN_TEST(test_C10_legacy_channel_dm_failure_has_no_pipeline_effects);
    RUN_TEST(test_C11_malformed_pki_plaintext_has_no_pipeline_effects);
    RUN_TEST(test_C12_exact_authenticated_replay_reuses_verdict_without_collision_bypass);

    printf("\n=== Group D: encoding invariants ===\n");
    RUN_TEST(test_D1_signature_field_overhead_exact);

    printf("\n=== Group E: decoded-ingress policy ===\n");
    RUN_TEST(test_E1_decoded_unsigned_broadcast_from_signer_dropped);
    RUN_TEST(test_E2_decoded_unsigned_broadcast_from_nonsigner_accepted);
    RUN_TEST(test_E3_decoded_valid_signature_verified_and_learns_signer);
    RUN_TEST(test_E4_decoded_bad_signature_dropped);
    RUN_TEST(test_E5_decoded_unsigned_oversized_broadcast_from_signer_accepted);
    RUN_TEST(test_E6_decoded_unsigned_unicast_from_signer_accepted);
    RUN_TEST(test_E8_decoded_partial_signature_from_signer_dropped);
    RUN_TEST(test_E9_decoded_partial_signature_from_nonsigner_dropped);

    exit(UNITY_END());
}

void loop() {}

#else // XEdDSA or PKI excluded

void setUp(void) {}
void tearDown(void) {}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
void loop() {}

#endif
