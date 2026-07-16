// Tests for XEdDSA packet-signing *policy* - the receive-path accept/reject behavior and the
// send-path signing policy - as opposed to the raw sign/verify primitive (covered in test_crypto).
//
// The decision logic under test lives in Router.cpp free functions. Groups A/B drive a real
// encode -> decode round-trip through the default channel (perhapsEncode/perhapsDecode, black-box,
// no production changes); Groups C-E exercise the policy helpers directly.
//
//   Group A  receive-side accept/reject matrix (verify, downgrade protection, signer-bit learning)
//   Group B  send-side signing policy (which outgoing packets perhapsEncode signs)
//   Group C  NodeInfoModule's broadcast-only "drop unsigned NodeInfo from a known signer" rule
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
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/NodeInfoModule.h"
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

static bool remoteSignerBit()
{
    return nodeInfoLiteHasXeddsaSigned(mockNodeDB->getMeshNode(REMOTE_NODE));
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
    nodeDB = mockNodeDB;

    // Clean global config/owner AFTER the ctor; zeroed config => rebroadcast ALL (no KNOWN_ONLY
    // drop) and security.private_key.size == 0 (PKI encrypt path skipped => simple channel crypto).
    config = meshtastic_LocalConfig_init_zero;
    owner = meshtastic_User_init_zero;
    myNodeInfo.my_node_num = LOCAL_NODE; // drives isFromUs()/getFrom()/isToUs()

    // Working primary channel with the default PSK so encrypt/decrypt round-trips.
    channels.initDefaults();
    channels.onConfigChanged();
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
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setPublicKey(REMOTE_NODE, pub);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    signWithCurrentKey(&p);
    p.decoded.xeddsa_signature.bytes[0] ^= 0xFF; // corrupt the signature

    TEST_ASSERT_EQUAL(DECODE_FAILURE, roundTrip(&p));
}

// A3: signed packet but we have no key for the sender -> accepted unverified, signer bit NOT set.
void test_A3_signed_no_pubkey_accepted_unverified(void)
{
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

    TEST_ASSERT_EQUAL(DECODE_FAILURE, roundTrip(&p));
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

    TEST_ASSERT_EQUAL(DECODE_FAILURE, roundTrip(&p));
}

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

// B2: our own unicast is NOT signed.
void test_B2_local_unicast_not_signed(void)
{
    mockNodeDB->addNode(REMOTE_NODE);

    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_PRIVATE_APP, SMALL_PAYLOAD);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_EQUAL_MESSAGE(0, p.decoded.xeddsa_signature.size, "unicast must not be signed");
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

// B5: a client-preset signature on a packet we originate is discarded, not transmitted.
// perhapsEncode owns signing for our packets; a stale/garbage signature from a phone app on a
// packet we don't sign (here: unicast) would otherwise fail verification at every receiver.
void test_B5_preset_signature_on_local_packet_cleared(void)
{
    mockNodeDB->addNode(REMOTE_NODE);

    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_PRIVATE_APP, SMALL_PAYLOAD);
    p.decoded.xeddsa_signature.size = XEDDSA_SIGNATURE_SIZE;
    memset(p.decoded.xeddsa_signature.bytes, 0xAB, XEDDSA_SIGNATURE_SIZE);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_EQUAL_MESSAGE(0, p.decoded.xeddsa_signature.size, "preset signature must be discarded on unicast");
}

// B6: the exact-fit gate tracks Data *shape*, not just payload size. A tapback-style broadcast
// (want_response + reply_id + emoji) carries extra wire bytes that shift the fit boundary; the
// sweep proves no dead band exists for that shape either, and - once the signer bit is learned -
// that the receiver's rawSize-driven downgrade predicate stays symmetric for it too. Window
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

// ===========================================================================
// Group C - NodeInfoModule downgrade drop (broadcast-only backstop for ingress paths that skip
// Router's check; unicast NodeInfo is never signed by senders, so it is exempt - see C4)
// ===========================================================================
class NodeInfoTestShim : public NodeInfoModule
{
  public:
    using NodeInfoModule::handleReceivedProtobuf; // protected virtual -> exposed for direct call
};

static meshtastic_MeshPacket makeNodeInfoPacket(bool signed_)
{
    // Broadcast so the module's phone-forward path (which needs `service`) is skipped.
    meshtastic_MeshPacket mp = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_NODEINFO_APP, SMALL_PAYLOAD);
    mp.xeddsa_signed = signed_;
    return mp;
}

// C1: unsigned NodeInfo from a node that previously signed -> dropped.
void test_C1_unsigned_nodeinfo_from_signer_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    NodeInfoTestShim shim;
    meshtastic_MeshPacket mp = makeNodeInfoPacket(/*signed_=*/false);
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;

    TEST_ASSERT_TRUE_MESSAGE(shim.handleReceivedProtobuf(mp, &user), "unsigned NodeInfo from signer must be dropped");
}

// C2: signed NodeInfo from a known signer -> not dropped by this rule.
void test_C2_signed_nodeinfo_from_signer_not_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    NodeInfoTestShim shim;
    meshtastic_MeshPacket mp = makeNodeInfoPacket(/*signed_=*/true);
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;

    TEST_ASSERT_FALSE(shim.handleReceivedProtobuf(mp, &user));
}

// C3: unsigned NodeInfo from a node we've never seen sign -> not dropped.
void test_C3_unsigned_nodeinfo_from_nonsigner_not_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE); // signer bit clear

    NodeInfoTestShim shim;
    meshtastic_MeshPacket mp = makeNodeInfoPacket(/*signed_=*/false);
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;

    TEST_ASSERT_FALSE(shim.handleReceivedProtobuf(mp, &user));
}

// C4: F1 regression - unsigned UNICAST NodeInfo from a known signer -> NOT dropped. Unicast
// NodeInfo (want_response replies, phone-initiated exchanges) is never signed by the sender,
// so treating it as a downgrade broke NodeInfo exchange with signer nodes.
void test_C4_unsigned_unicast_nodeinfo_from_signer_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    NodeInfoTestShim shim;
    meshtastic_MeshPacket mp = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_NODEINFO_APP, SMALL_PAYLOAD);
    mp.xeddsa_signed = false;
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;

    TEST_ASSERT_FALSE_MESSAGE(shim.handleReceivedProtobuf(mp, &user),
                              "unsigned unicast NodeInfo from a signer must not be dropped");
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
// tests drive it the same way: decoded packets, encodedDataSize = 0 (canonical sizing).
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

// E5: unsigned oversized broadcast from a signer -> accepted (canonical sizing exempts packets
// whose signed encoding wouldn't fit, mirroring the RF-path rawSize rule).
void test_E5_decoded_unsigned_oversized_broadcast_from_signer_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, OVERSIZED_PAYLOAD);

    TEST_ASSERT_TRUE(checkXeddsaReceivePolicy(&p));
}

// E6: unsigned unicast from a signer -> accepted (unicast is never signed).
void test_E6_decoded_unsigned_unicast_from_signer_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_PRIVATE_APP, SMALL_PAYLOAD);

    TEST_ASSERT_TRUE(checkXeddsaReceivePolicy(&p));
}

// E7: unsigned PKI-flagged packet from a signer -> accepted. Senders never sign PKI traffic,
// so the predicate's !pki_encrypted guard must exempt it (pins the assumption that the
// downgrade drop can never fire on PKI packets, whatever their addressing).
void test_E7_decoded_unsigned_pki_from_signer_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    p.pki_encrypted = true;

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

    printf("\n=== Group B: send-side signing policy ===\n");
    RUN_TEST(test_B1_local_broadcast_is_signed);
    RUN_TEST(test_B2_local_unicast_not_signed);
    RUN_TEST(test_B3_local_oversized_broadcast_not_signed);
    RUN_TEST(test_B4_all_broadcast_sizes_deliverable_no_deadband);
    RUN_TEST(test_B5_preset_signature_on_local_packet_cleared);
    RUN_TEST(test_B6_rich_shape_sweep_no_deadband);

    printf("\n=== Group C: NodeInfoModule downgrade drop ===\n");
    RUN_TEST(test_C1_unsigned_nodeinfo_from_signer_dropped);
    RUN_TEST(test_C2_signed_nodeinfo_from_signer_not_dropped);
    RUN_TEST(test_C3_unsigned_nodeinfo_from_nonsigner_not_dropped);
    RUN_TEST(test_C4_unsigned_unicast_nodeinfo_from_signer_accepted);

    printf("\n=== Group D: encoding invariants ===\n");
    RUN_TEST(test_D1_signature_field_overhead_exact);

    printf("\n=== Group E: decoded-ingress policy ===\n");
    RUN_TEST(test_E1_decoded_unsigned_broadcast_from_signer_dropped);
    RUN_TEST(test_E2_decoded_unsigned_broadcast_from_nonsigner_accepted);
    RUN_TEST(test_E3_decoded_valid_signature_verified_and_learns_signer);
    RUN_TEST(test_E4_decoded_bad_signature_dropped);
    RUN_TEST(test_E5_decoded_unsigned_oversized_broadcast_from_signer_accepted);
    RUN_TEST(test_E6_decoded_unsigned_unicast_from_signer_accepted);
    RUN_TEST(test_E7_decoded_unsigned_pki_from_signer_accepted);
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
