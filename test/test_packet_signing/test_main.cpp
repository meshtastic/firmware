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
//
// What a relay carries per rebroadcast_mode lives in test_rebroadcast_mode; the ingress harness
// both suites share is test/support/AuthPipelineHarness.h.

#include "support/AuthPipelineHarness.h"

// The whole suite exercises XEdDSA sign/verify and checkXeddsaReceivePolicy, all of which are
// compiled out unless both PKI and XEdDSA are enabled (e.g. stm32 sets MESHTASTIC_EXCLUDE_XEDDSA).
#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)

#include "modules/NodeInfoModule.h"
#include "support/MockMeshService.h"
#include <ErriezCRC32.h>
#include <vector>

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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
    pipelineHarnessSetUp();
    // Exercise the downgrade-protection matrix by default. Production defaults to
    // COMPATIBLE so existing meshes remain interoperable; tests that cover that
    // mode opt in explicitly.
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_BALANCED);
}

// Set while C14's saturated AirTime is installed; see useDutyCycleSaturatedAirTime() below.
static AirTime *c14SavedAirTime = nullptr;

void tearDown(void)
{
    pipelineHarnessTearDown();
    // The AirTime swap is C14's duty-cycle setup; restore it here for the same reason the harness
    // restores the clock: an assertion aborts the body before any in-test restore.
    if (c14SavedAirTime) {
        airTime = c14SavedAirTime;
        c14SavedAirTime = nullptr;
    }
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
    // Mirror what NodeDB eviction actually stores for a signer: warmProtectedCategory() yields
    // XeddsaSigner *and* the dedicated warm signer bit is set from nodeInfoLiteHasXeddsaSigned().
    // isKnownXeddsaSigner() reads that signer bit, not the protected category.
    TEST_ASSERT_TRUE(mockNodeDB->warmStore.absorb(REMOTE_NODE, 2, pub, meshtastic_Config_DeviceConfig_Role_CLIENT,
                                                  static_cast<uint8_t>(WarmProtected::XeddsaSigner), /*signer=*/true));
    mockNodeDB->clearTestNodes();
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_BALANCED);
    meshtastic_MeshPacket unsignedPacket =
        makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_POSITION_APP, SMALL_PAYLOAD);
    TEST_ASSERT_FALSE_MESSAGE(checkXeddsaReceivePolicy(&unsignedPacket),
                              "Balanced downgrade memory must survive repeated hot-store eviction");
}
#endif

void test_A18_unsigned_broadcast_from_signer_with_unknown_fields_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_MeshPacket p = makeBroadcastWithUnknownFields();

    TEST_ASSERT_EQUAL_MESSAGE(DECODE_POLICY_REJECT, perhapsDecode(&p),
                              "unsigned broadcast from a signer must be dropped despite unknown fields");
}

void test_A19_unsigned_broadcast_from_nonsigner_with_unknown_fields_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);

    meshtastic_MeshPacket p = makeBroadcastWithUnknownFields();
    const size_t rawSize = p.encrypted.size;

    TEST_ASSERT_EQUAL_MESSAGE(DECODE_SUCCESS, perhapsDecode(&p), "frame from a non-signer must still decode");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_PortNum_POSITION_APP, p.decoded.portnum, "unknown fields must not disturb the portnum");
    TEST_ASSERT_EQUAL_MESSAGE(SMALL_PAYLOAD, p.decoded.payload.size, "payload must survive the unknown fields");
    TEST_ASSERT_FALSE(p.xeddsa_signed);
    TEST_ASSERT_LESS_THAN_MESSAGE(rawSize, encodedDataSize(&p.decoded),
                                  "unknown fields must drop at decode, leaving decoded size < raw");
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

void test_B8_licensed_broadcast_and_unicast_are_signed(void)
{
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, pub);
    owner.is_licensed = true;
    channels.ensureLicensedOperation();

    meshtastic_MeshPacket broadcast =
        makeDecoded(LOCAL_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&broadcast));
    TEST_ASSERT_EQUAL(XEDDSA_SIGNATURE_SIZE, broadcast.decoded.xeddsa_signature.size);
    TEST_ASSERT_TRUE(broadcast.xeddsa_signed);

    meshtastic_MeshPacket direct = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&direct));
    TEST_ASSERT_EQUAL(XEDDSA_SIGNATURE_SIZE, direct.decoded.xeddsa_signature.size);
    TEST_ASSERT_TRUE(direct.xeddsa_signed);
}

void test_B9_licensed_unicast_never_uses_pki_encryption(void)
{
    uint8_t localPub[32], localPriv[32], remotePub[32], remotePriv[32];
    crypto->generateKeyPair(localPub, localPriv);
    memcpy(config.security.private_key.bytes, localPriv, sizeof(localPriv));
    config.security.private_key.size = sizeof(localPriv);
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, localPub);
    mockNodeDB->addNode(REMOTE_NODE);
    crypto->generateKeyPair(remotePub, remotePriv);
    mockNodeDB->setPublicKey(REMOTE_NODE, remotePub);
    crypto->setDHPrivateKey(localPriv);

    owner.is_licensed = true;
    channels.ensureLicensedOperation();
    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);

    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, perhapsEncode(&p));
    TEST_ASSERT_FALSE(p.pki_encrypted);
    meshtastic_Data plaintext = meshtastic_Data_init_zero;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(p.encrypted.bytes, p.encrypted.size, &meshtastic_Data_msg, &plaintext));
    TEST_ASSERT_EQUAL(XEDDSA_SIGNATURE_SIZE, plaintext.xeddsa_signature.size);
}

void test_B10_licensed_oversized_unicast_remains_unsigned(void)
{
    owner.is_licensed = true;
    channels.ensureLicensedOperation();
    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_TEXT_MESSAGE_APP, OVERSIZED_PAYLOAD);

    TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&p));
    TEST_ASSERT_EQUAL(0, p.decoded.xeddsa_signature.size);
}

void test_B11_normal_unicast_still_uses_pki(void)
{
    uint8_t localPub[32], localPriv[32], remotePub[32], remotePriv[32];
    crypto->generateKeyPair(localPub, localPriv);
    crypto->generateKeyPair(remotePub, remotePriv);
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, localPub);
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setPublicKey(REMOTE_NODE, remotePub);
    memcpy(config.security.private_key.bytes, localPriv, sizeof(localPriv));
    config.security.private_key.size = sizeof(localPriv);
    crypto->setDHPrivateKey(localPriv);

    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, perhapsEncode(&p));
    TEST_ASSERT_TRUE(p.pki_encrypted);

    myNodeInfo.my_node_num = REMOTE_NODE;
    crypto->setDHPrivateKey(remotePriv);
    TEST_ASSERT_EQUAL(DECODE_SUCCESS, perhapsDecode(&p));
    TEST_ASSERT_TRUE(p.pki_encrypted);
    TEST_ASSERT_EQUAL(0, p.decoded.xeddsa_signature.size);
}

void test_B12_licensed_receiver_does_not_decrypt_pki(void)
{
    uint8_t localPub[32], localPriv[32], remotePub[32], remotePriv[32];
    crypto->generateKeyPair(localPub, localPriv);
    crypto->generateKeyPair(remotePub, remotePriv);
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, localPub);
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setPublicKey(REMOTE_NODE, remotePub);
    memcpy(config.security.private_key.bytes, localPriv, sizeof(localPriv));
    config.security.private_key.size = sizeof(localPriv);
    crypto->setDHPrivateKey(localPriv);

    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, perhapsEncode(&p));
    TEST_ASSERT_TRUE(p.pki_encrypted);

    owner.is_licensed = true;
    channels.ensureLicensedOperation();
    myNodeInfo.my_node_num = REMOTE_NODE;
    crypto->setDHPrivateKey(remotePriv);
    TEST_ASSERT_EQUAL(DECODE_FAILURE, perhapsDecode(&p));
}

void test_B13_licensed_port_and_destination_signing_matrix(void)
{
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->setPublicKey(LOCAL_NODE, pub);
    owner.is_licensed = true;
    channels.ensureLicensedOperation();

    const meshtastic_PortNum ports[] = {
        meshtastic_PortNum_TEXT_MESSAGE_APP, meshtastic_PortNum_POSITION_APP, meshtastic_PortNum_TELEMETRY_APP,
        meshtastic_PortNum_ROUTING_APP,      meshtastic_PortNum_NODEINFO_APP,
    };
    const NodeNum destinations[] = {NODENUM_BROADCAST, REMOTE_NODE};
    for (const auto port : ports) {
        for (const auto destination : destinations) {
            meshtastic_MeshPacket packet = makeDecoded(LOCAL_NODE, destination, port, SMALL_PAYLOAD);
            TEST_ASSERT_EQUAL(DECODE_SUCCESS, roundTrip(&packet));
            TEST_ASSERT_EQUAL(XEDDSA_SIGNATURE_SIZE, packet.decoded.xeddsa_signature.size);
            TEST_ASSERT_TRUE(packet.xeddsa_signed);
            TEST_ASSERT_FALSE(packet.pki_encrypted);
        }
    }
}

// ===========================================================================
// Group C - routing pipeline and NodeInfo authentication ordering
// ===========================================================================

class NodeInfoTestShim : public NodeInfoModule
{
  public:
    using MeshModule::currentRequest; // allocReply() only suppresses while a request is in flight
    using NodeInfoModule::allocReply;
    using NodeInfoModule::handleReceivedProtobuf;
};

static meshtastic_MeshPacket makeNodeInfoPacket(bool signed_)
{
    meshtastic_MeshPacket mp = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_NODEINFO_APP, SMALL_PAYLOAD);
    mp.xeddsa_signed = signed_;
    return mp;
}

void test_N1_unsigned_nodeinfo_from_signer_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    NodeInfoTestShim shim;
    meshtastic_MeshPacket mp = makeNodeInfoPacket(false);
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;

    TEST_ASSERT_TRUE_MESSAGE(shim.handleReceivedProtobuf(mp, &user), "unsigned NodeInfo from signer must be dropped");
}

void test_N2_signed_nodeinfo_from_signer_not_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    NodeInfoTestShim shim;
    meshtastic_MeshPacket mp = makeNodeInfoPacket(true);
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;

    TEST_ASSERT_FALSE(shim.handleReceivedProtobuf(mp, &user));
}

void test_N3_unsigned_nodeinfo_from_nonsigner_not_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);

    NodeInfoTestShim shim;
    meshtastic_MeshPacket mp = makeNodeInfoPacket(false);
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;

    TEST_ASSERT_FALSE(shim.handleReceivedProtobuf(mp, &user));
}

void test_N4_unsigned_unicast_nodeinfo_from_signer_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    NodeInfoTestShim shim;
    meshtastic_MeshPacket mp = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_NODEINFO_APP, SMALL_PAYLOAD);
    mp.xeddsa_signed = false;
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;

    TEST_ASSERT_FALSE_MESSAGE(shim.handleReceivedProtobuf(mp, &user),
                              "unsigned unicast NodeInfo from signer must not be dropped");
}

static void preparePipelineSigner(NodeNum sender)
{
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(sender);
    mockNodeDB->setPublicKey(sender, pub);
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
    // "Far future, so no retransmission is due." Must be a representable future time, not
    // UINT32_MAX: doRetransmissions() compares with an unsigned half-range test, under which
    // UINT32_MAX is ~1ms in the *past* and would fire a retransmit and rewrite nextTxMsec.
    const uint32_t notDueTxMsec = Time::getMillis() + 3600000UL;
    pipelineRouter->addPending(prior, notDueTxMsec);
    const uint32_t lastHeard = mockNodeDB->getMeshNode(LOCAL_NODE)->last_heard;

    meshtastic_MeshPacket invalid = makeSignedWirePacket(LOCAL_NODE, NODENUM_BROADCAST, id, 2, 2, 0, 0x34, false);
    runPipelineIngress(invalid);
    assertNoRejectedPipelineEffects(LOCAL_NODE, lastHeard);
    TEST_ASSERT_EQUAL(1, pipelineRouter->pendingCount());
    TEST_ASSERT_EQUAL_UINT32(notDueTxMsec, pipelineRouter->pendingNextTx(LOCAL_NODE, id));
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
    expectEncryptedPhoneDeliveries(1); // unreadable to us or broadcast: the phone still sees the frame
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
    expectEncryptedPhoneDeliveries(1); // unreadable to us or broadcast: the phone still sees the frame
    TEST_ASSERT_FALSE(pipelineRouter->historyContains(&addressed));

    // An unknown-channel broadcast is not PKI-shaped, so KNOWN/LOCAL decline it; CORE relays it
    // (test_rebroadcast_mode pins the full mode table).
    const meshtastic_Config_DeviceConfig_RebroadcastMode blockedModes[] = {
        meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY,
        meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY,
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
        // LOCAL_ONLY / KNOWN_ONLY ignore what they cannot read, phone included; NONE only declines to relay.
        expectEncryptedPhoneDeliveries(mode == meshtastic_Config_DeviceConfig_RebroadcastMode_NONE ? 1 : 0);
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

/// Outcome for a frame we matched but could not use (junk plaintext, a rejected legacy DM): exactly
/// one NO_CHANNEL NAK if it asked for one, never the phone, relayed only if `relayed`, nothing
/// learned.
static void assertMatchedFailureOutcome(const meshtastic_MeshPacket &p, bool wantAck, bool relayed, NodeNum sender,
                                        uint32_t lastHeardBefore)
{
    TEST_ASSERT_EQUAL_MESSAGE(wantAck ? 1 : 0, pipelineRouting->ackCalls, "a want_ack frame to us we cannot use is NAKed once");
    if (wantAck)
        TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Routing_Error_NO_CHANNEL, pipelineRouting->lastErr,
                                  "we held what we needed and it still failed: NO_CHANNEL, not PKI_UNKNOWN_PUBKEY");
    TEST_ASSERT_EQUAL_MESSAGE(relayed ? 1 : 0, pipelineRadio->sendCalls, "relay decision for a matched-but-unusable frame");
    TEST_ASSERT_NULL_MESSAGE(pipelineService->getForPhone(), "junk we could not parse is not delivered to the phone");
    TEST_ASSERT_EQUAL(0, pipelineModule->calls);
    TEST_ASSERT_EQUAL(0, pipelineMqtt->queueSize());
    TEST_ASSERT_FALSE(pipelineRouter->historyContains(&p));
    const meshtastic_NodeInfoLite *node = mockNodeDB->getMeshNode(sender);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(lastHeardBefore, node->last_heard, "an unusable frame must not update last_heard");
}

// C9: junk on a channel we hold, broadcast, with hops left. A colliding foreign channel is
// indistinguishable from tampering and must be carried, so it relays in every mode that carries
// opaque broadcasts; the phone never sees it and nothing is learned about the sender.
void test_C9_known_channel_junk_broadcast_is_relayed_not_delivered(void)
{
    meshtastic_MeshPacket junk = meshtastic_MeshPacket_init_zero;
    junk.from = REMOTE_NODE;
    junk.to = NODENUM_BROADCAST;
    junk.id = 0xC9000009;
    junk.hop_limit = 1;
    junk.hop_start = 2;
    junk.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    junk.encrypted.size = 3;
    memset(junk.encrypted.bytes, 0xFF, junk.encrypted.size);
    junk.channel = channels.setActiveByIndex(0);
    crypto->encryptPacket(junk.from, junk.id, junk.encrypted.size, junk.encrypted.bytes);

    meshtastic_MeshPacket verdictCopy = junk;
    TEST_ASSERT_EQUAL(static_cast<int>(RoutingAuthVerdict::OPAQUE_RELAY_ONLY),
                      static_cast<int>(passesRoutingAuthGate(&verdictCopy)));

    mockNodeDB->addNode(REMOTE_NODE);
    const uint32_t lastHeard = mockNodeDB->getMeshNode(REMOTE_NODE)->last_heard;
    const meshtastic_Config_DeviceConfig_RebroadcastMode modes[] = {
        meshtastic_Config_DeviceConfig_RebroadcastMode_ALL,
        meshtastic_Config_DeviceConfig_RebroadcastMode_ALL_SKIP_DECODING,
        meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY,
        meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY,
        meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY,
        meshtastic_Config_DeviceConfig_RebroadcastMode_NONE,
    };
    for (const auto mode : modes) {
        const bool expect = IS_ONE_OF(mode, meshtastic_Config_DeviceConfig_RebroadcastMode_ALL,
                                      meshtastic_Config_DeviceConfig_RebroadcastMode_ALL_SKIP_DECODING,
                                      meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY);
        pipelineRadio->reset();
        pipelineRouting->reset();
        config.device.rebroadcast_mode = mode;
        meshtastic_MeshPacket copy = junk;
        copy.id += (uint32_t)mode;
        runPipelineIngress(copy);
        assertMatchedFailureOutcome(copy, /*wantAck=*/false, expect, REMOTE_NODE, lastHeard);
    }
}

// C10: a legacy channel-PSK DM to us (rejected on purpose since PKI) that asked for an ACK. The
// sender must learn the DM did not land: NO_CHANNEL. Nothing is relayed, delivered or learned.
void test_C10_legacy_channel_dm_is_naked_no_channel_and_nothing_else(void)
{
    meshtastic_MeshPacket legacyDm = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_TEXT_MESSAGE_APP, SMALL_PAYLOAD);
    legacyDm.want_ack = true;
    legacyDm.hop_limit = 1;
    legacyDm.hop_start = 2;
    legacyDm = channelEncode(legacyDm);
    mockNodeDB->addNode(REMOTE_NODE);
    const uint32_t lastHeard = mockNodeDB->getMeshNode(REMOTE_NODE)->last_heard;
    moduleConfig.mqtt.enabled = true;
    runPipelineIngress(legacyDm);
    assertMatchedFailureOutcome(legacyDm, /*wantAck=*/true, /*relayed=*/false, REMOTE_NODE, lastHeard);
}

// C11: PKI to us with the right key, AEAD passes, plaintext is not a Data message. The ciphertext
// authenticated the sender, so its want_ack gets a NO_CHANNEL NAK; the junk goes nowhere.
void test_C11_malformed_pki_plaintext_is_naked_no_channel_and_nothing_else(void)
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
    malformed.want_ack = true;
    malformed.hop_limit = 1;
    malformed.hop_start = 2;
    malformed.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    crypto->setDHPrivateKey(remotePriv);
    TEST_ASSERT_TRUE(crypto->encryptCurve25519(malformed.to, malformed.from, localKey, malformed.id, sizeof(malformedPlaintext),
                                               malformedPlaintext, malformed.encrypted.bytes));
    malformed.encrypted.size = sizeof(malformedPlaintext) + MESHTASTIC_PKC_OVERHEAD;
    crypto->setDHPrivateKey(localPriv);

    const uint32_t lastHeard = mockNodeDB->getMeshNode(REMOTE_NODE)->last_heard;
    moduleConfig.mqtt.enabled = true;
    runPipelineIngress(malformed);
    assertMatchedFailureOutcome(malformed, /*wantAck=*/true, /*relayed=*/false, REMOTE_NODE, lastHeard);
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

    // Broadcast, so isToUs() is false like any colliding-hash foreign broadcast (see test_C17);
    // this still guards that the cache is reevaluated per exact bytes, not reused for a same-ID replay.
    meshtastic_MeshPacket collision = valid;
    collision.encrypted.bytes[0] ^= 0x80;
    TEST_ASSERT_EQUAL(static_cast<int>(RoutingAuthVerdict::OPAQUE_RELAY_ONLY),
                      static_cast<int>(passesRoutingAuthGate(&collision)));
    TEST_ASSERT_EQUAL_MESSAGE(3, routingAuthEvaluationCount(), "same packet ID with different bytes must be reevaluated");
}

// A local reliable send that fails before reaching the radio must not outlive the error as a scheduled retransmission.
void test_C13_failed_initial_reliable_send_does_not_retry(void)
{
    meshtastic_MeshPacket initial = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_ROUTING_APP, SMALL_PAYLOAD);
    initial.id = 0xC13C13C1;
    initial.want_ack = true;
    initial.channel = MAX_NUM_CHANNELS; // Out of range, so encoding returns NO_CHANNEL.

    auto *packet = packetPool.allocCopy(initial);
    TEST_ASSERT_NOT_NULL(packet);

    pipelineRouter->send(packet);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, pipelineRouting->ackCalls,
                                     "initial encoding failure must be reported to the originating client");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pipelineRouter->pendingCount(),
                                     "failed initial send must not leave a retransmission pending");

    pipelineRadio->failSend = true;
    meshtastic_MeshPacket interfaceFailure = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_ROUTING_APP, SMALL_PAYLOAD);
    interfaceFailure.id = 0xC13C13C2;
    interfaceFailure.want_ack = true;
    packet = packetPool.allocCopy(interfaceFailure);
    TEST_ASSERT_NOT_NULL(packet);

    pipelineService->sendToMesh(packet, RX_SRC_USER);
    meshtastic_QueueStatus *status = pipelineService->getQueueStatusForPhone();
    TEST_ASSERT_NOT_NULL(status);
    TEST_ASSERT_EQUAL(ERRNO_DISABLED, status->res);
    TEST_ASSERT_EQUAL_UINT32(interfaceFailure.id, status->mesh_packet_id);
    pipelineService->releaseQueueStatusToPool(status);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, pipelineRouting->ackCalls,
                                     "interface errors are reported to the client through QueueStatus, not a routing NAK");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pipelineRouter->pendingCount(),
                                     "failed interface enqueue must not leave a retransmission pending");
}

// C14 needs a node that has used its whole hourly duty-cycle allowance. Swaps in a separate AirTime
// rather than poking the global's buckets, which are private now.
//
// Deliberately NOT a scoped guard: Unity's TEST_ABORT() is longjmp, which does not run destructors
// of automatic objects, so a guard would leave `airTime` dangling into an abandoned stack frame on
// any assertion failure - and later cases dereference it (NodeInfoModule::allocReply). tearDown()
// restores the global unconditionally instead. The instance is a function-local static so it
// outlives the longjmp.
//
// Note it also parks channel utilisation at ~6000%, because logAirtime() credits that for every
// report type. C14 gates on utilizationTXPercent() alone; do not reuse this for an
// isTxAllowedChannelUtil() path, which would then pass for the wrong reason.
static void useDutyCycleSaturatedAirTime()
{
    static AirTime saturated;
    c14SavedAirTime = airTime;
    airTime = &saturated;
    saturated.logAirtime(TX_LOG, MS_IN_HOUR); // utilizationTXPercent() sums every bucket -> 100%
}

void test_C14_duty_cycle_limited_reliable_send_remains_pending(void)
{
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    config.lora.override_duty_cycle = false;
    initRegion();
    useDutyCycleSaturatedAirTime();

    meshtastic_MeshPacket initial = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_ROUTING_APP, SMALL_PAYLOAD);
    initial.id = 0xC14C14C1;
    initial.want_ack = true;
    auto *packet = packetPool.allocCopy(initial);
    TEST_ASSERT_NOT_NULL(packet);

    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_DUTY_CYCLE_LIMIT, pipelineRouter->send(packet));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, pipelineRouting->ackCalls,
                                     "duty-cycle rejection must still notify the originating client");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, pipelineRouter->pendingCount(),
                                     "duty-cycle rejection must retain the retry for when airtime is available");

    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();
}

void test_C15_reliable_unicast_tracks_five_total_attempts(void)
{
    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, REMOTE_NODE, meshtastic_PortNum_ROUTING_APP, SMALL_PAYLOAD);
    p.id = 0x51530001;
    p.want_ack = true;

    TEST_ASSERT_EQUAL(ERRNO_OK, pipelineRouter->send(packetPool.allocCopy(p)));
    TEST_ASSERT_EQUAL_UINT8(5, pipelineRouter->pendingTotalAttempts(LOCAL_NODE, p.id));
}

void test_C16_reliable_broadcast_keeps_three_total_attempts(void)
{
    meshtastic_MeshPacket p = makeDecoded(LOCAL_NODE, NODENUM_BROADCAST, meshtastic_PortNum_ROUTING_APP, SMALL_PAYLOAD);
    p.id = 0x51530002;
    p.want_ack = true;

    TEST_ASSERT_EQUAL(ERRNO_OK, pipelineRouter->send(packetPool.allocCopy(p)));
    TEST_ASSERT_EQUAL_UINT8(3, pipelineRouter->pendingTotalAttempts(LOCAL_NODE, p.id));
}

void test_C17_colliding_channel_hash_foreign_broadcast_is_relay_only(void)
{
    // Foreign channel whose PSK collides with our channel 0's one-byte hash (see test_C9/test_C12
    // for the paired tradeoff): indistinguishable from tampering, so it must relay opaquely.
    setPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    meshtastic_MeshPacket foreign = meshtastic_MeshPacket_init_zero;
    foreign.from = REMOTE_NODE;
    foreign.to = NODENUM_BROADCAST;
    foreign.id = 0xC1700017;
    foreign.hop_limit = 1;
    foreign.hop_start = 2;
    foreign.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    const int16_t hash = channels.setActiveByIndex(0);
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(0, hash, "no usable primary channel");
    foreign.channel = (uint8_t)hash; // collides with our channel 0, but the ciphertext below is not ours
    foreign.encrypted.size = 16;
    memset(foreign.encrypted.bytes, 0xA5, foreign.encrypted.size);

    TEST_ASSERT_EQUAL(static_cast<int>(RoutingAuthVerdict::OPAQUE_RELAY_ONLY), static_cast<int>(passesRoutingAuthGate(&foreign)));

    // Same undecodable frame claiming to be from us must still be dropped: OPAQUE_RELAY_ONLY would
    // reach perhapsGenerateImplicitAckForOwnOverheard, which acts on header bytes alone.
    meshtastic_MeshPacket spoofed = foreign;
    spoofed.from = LOCAL_NODE;
    TEST_ASSERT_EQUAL(static_cast<int>(RoutingAuthVerdict::REJECT), static_cast<int>(passesRoutingAuthGate(&spoofed)));
}

// C5: the packet survives (C4) but the identity claim inside it must not land - the pubkey guard
// can't tell a signer from an impersonator replaying its (public) key. Only the write is refused.
void test_N5_unsigned_unicast_nodeinfo_from_signer_does_not_change_name(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);
    mockNodeDB->setLongName(REMOTE_NODE, "Genuine");

    NodeInfoTestShim shim;
    meshtastic_MeshPacket mp = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_NODEINFO_APP, SMALL_PAYLOAD);
    mp.xeddsa_signed = false;
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;
    strcpy(user.long_name, "Spoofed");
    strcpy(user.short_name, "SPF");

    TEST_ASSERT_FALSE_MESSAGE(shim.handleReceivedProtobuf(mp, &user), "the packet itself must still be accepted");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Genuine", mockNodeDB->longName(REMOTE_NODE),
                                     "unsigned unicast NodeInfo from a signer must not rewrite its stored name");
}

// C6: the same exchange signed - the update is authenticated and must land, pinning C5 as a
// targeted refusal rather than a blanket block on unicast NodeInfo from signers.
void test_N6_signed_unicast_nodeinfo_from_signer_changes_name(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);
    mockNodeDB->setLongName(REMOTE_NODE, "Genuine");

    NodeInfoTestShim shim;
    meshtastic_MeshPacket mp = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_NODEINFO_APP, SMALL_PAYLOAD);
    mp.xeddsa_signed = true;
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;
    strcpy(user.long_name, "Renamed");
    strcpy(user.short_name, "RNM");

    TEST_ASSERT_FALSE(shim.handleReceivedProtobuf(mp, &user));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Renamed", mockNodeDB->longName(REMOTE_NODE),
                                     "a signed update from a signer must still be learned");
}

// C7: a node that has never signed is unaffected - the ordinary case for most of the mesh.
void test_N7_unsigned_unicast_nodeinfo_from_nonsigner_changes_name(void)
{
    mockNodeDB->addNode(REMOTE_NODE); // signer bit clear
    mockNodeDB->setLongName(REMOTE_NODE, "Genuine");

    NodeInfoTestShim shim;
    meshtastic_MeshPacket mp = makeDecoded(REMOTE_NODE, LOCAL_NODE, meshtastic_PortNum_NODEINFO_APP, SMALL_PAYLOAD);
    mp.xeddsa_signed = false;
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;
    strcpy(user.long_name, "Renamed");
    strcpy(user.short_name, "RNM");

    TEST_ASSERT_FALSE(shim.handleReceivedProtobuf(mp, &user));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Renamed", mockNodeDB->longName(REMOTE_NODE),
                                     "non-signer identity learning must be unaffected");
}

// ---------------------------------------------------------------------------
// N8-N11: the 12h reply-suppression window.
//
// The stamp is uptime SECONDS, not milliseconds: entries live for as long as the node stays in the
// DB, so a 32-bit millisecond stamp aliased back into the window once uptime passed 49.7 days and
// suppressed a legitimate reply for up to 12h. Driven through Time::setTestMillis() rather than by
// waiting.
// ---------------------------------------------------------------------------

static constexpr uint32_t kSuppressSecs = 12 * 60 * 60;

// Deliver a NodeInfo request from `sender` and report whether we would reply to it.
static bool wouldReplyToNodeInfoRequest(NodeInfoTestShim &shim, NodeNum sender)
{
    meshtastic_MeshPacket mp = makeDecoded(sender, NODENUM_BROADCAST, meshtastic_PortNum_NODEINFO_APP, SMALL_PAYLOAD);
    mp.decoded.want_response = true;
    meshtastic_User user = meshtastic_User_init_zero;
    user.is_licensed = owner.is_licensed;

    shim.handleReceivedProtobuf(mp, &user);

    NodeInfoTestShim::currentRequest = &mp;
    meshtastic_MeshPacket *reply = shim.allocReply();
    NodeInfoTestShim::currentRequest = nullptr;

    if (reply) {
        packetPool.release(reply);
        return true;
    }
    return false;
}

// Step the injected clock the way the main loop does - advance, then publish the wrap carry.
static void advanceUptime(uint32_t deltaMs)
{
    Time::advanceTestMillis(deltaMs);
    Time::serviceMonotonic();
}

void test_N8_second_request_inside_the_window_is_suppressed(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    Time::setTestMillis(60 * 1000);
    Time::serviceMonotonic();

    NodeInfoTestShim shim;
    TEST_ASSERT_TRUE_MESSAGE(wouldReplyToNodeInfoRequest(shim, REMOTE_NODE), "first request must be answered");

    advanceUptime(60 * 60 * 1000); // 1h later, well inside the 12h window
    TEST_ASSERT_FALSE_MESSAGE(wouldReplyToNodeInfoRequest(shim, REMOTE_NODE), "repeat request inside 12h must be suppressed");
}

void test_N9_request_after_the_window_is_answered(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    Time::setTestMillis(60 * 1000);
    Time::serviceMonotonic();

    NodeInfoTestShim shim;
    TEST_ASSERT_TRUE(wouldReplyToNodeInfoRequest(shim, REMOTE_NODE));

    advanceUptime((kSuppressSecs + 60) * 1000); // 12h + a minute
    TEST_ASSERT_TRUE_MESSAGE(wouldReplyToNodeInfoRequest(shim, REMOTE_NODE), "request after 12h must be answered");
}

// The regression. A stamp is only aliased by a counter that wraps underneath it, so the failure
// needs a *full* 2^32 ms of uptime to elapse, not merely a crossing of the boundary: with 32-bit
// millisecond stamps `now - stamp` then computes as 0 and the sender looks like it was answered
// this instant. Uptime seconds do not wrap for 136 years, so the entry reads as ~49.7 days old.
void test_N10_stale_stamp_does_not_alias_after_a_full_wrap(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    Time::setTestMillis(0x80000000u); // ~24.8 days of uptime
    Time::serviceMonotonic();

    NodeInfoTestShim shim;
    TEST_ASSERT_TRUE(wouldReplyToNodeInfoRequest(shim, REMOTE_NODE));

    // A whole millis() cycle, in two serviced halves - one publish per window is the contract.
    advanceUptime(0x80000000u);
    advanceUptime(0x80000000u);

    TEST_ASSERT_TRUE_MESSAGE(wouldReplyToNodeInfoRequest(shim, REMOTE_NODE),
                             "a stamp one full wrap old must read as ~49.7 days, not as this instant");
}

// Suppression must still behave normally either side of the boundary: still suppressing inside the
// window, and answering again once 12h have passed, with the stamp and the reading on opposite
// sides of the wrap.
void test_N11_window_still_applies_across_the_wrap(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    Time::setTestMillis(0xFFFF0000u); // just short of the wrap
    Time::serviceMonotonic();

    NodeInfoTestShim shim;
    TEST_ASSERT_TRUE(wouldReplyToNodeInfoRequest(shim, REMOTE_NODE));

    advanceUptime(0x20000u); // ~131s later, and now past the wrap
    TEST_ASSERT_FALSE_MESSAGE(wouldReplyToNodeInfoRequest(shim, REMOTE_NODE),
                              "the window must still bite when the stamp sits the other side of the wrap");

    advanceUptime((kSuppressSecs + 60) * 1000);
    TEST_ASSERT_TRUE_MESSAGE(wouldReplyToNodeInfoRequest(shim, REMOTE_NODE),
                             "and must still release once 12h have passed across the wrap");
}

void test_L1_licensed_nodeinfo_publishes_public_key(void)
{
    owner.is_licensed = true;
    owner.public_key.size = 32;
    memset(owner.public_key.bytes, 0x5A, owner.public_key.size);

    NodeInfoTestShim shim;
    meshtastic_MeshPacket *reply = shim.allocReply();
    TEST_ASSERT_NOT_NULL(reply);
    meshtastic_User published = meshtastic_User_init_zero;
    TEST_ASSERT_TRUE(
        pb_decode_from_bytes(reply->decoded.payload.bytes, reply->decoded.payload.size, &meshtastic_User_msg, &published));
    TEST_ASSERT_TRUE(published.is_licensed);
    TEST_ASSERT_EQUAL(32, published.public_key.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(owner.public_key.bytes, published.public_key.bytes, 32);
    packetPool.release(reply);
}

void test_L2_licensed_identity_key_is_generated_and_preserved(void)
{
    owner.is_licensed = true;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.security = meshtastic_Config_SecurityConfig_init_zero;

    TEST_ASSERT_TRUE(mockNodeDB->generateCryptoKeyPair());
    uint8_t privateKey[32], publicKey[32];
    memcpy(privateKey, config.security.private_key.bytes, sizeof(privateKey));
    memcpy(publicKey, config.security.public_key.bytes, sizeof(publicKey));
    const NodeNum migratedNodeNum = myNodeInfo.my_node_num;
    TEST_ASSERT_NOT_EQUAL(LOCAL_NODE, migratedNodeNum);
    TEST_ASSERT_TRUE(mockNodeDB->licensedIdentityMigrationPending);

    MockMeshService mockService;
    service = &mockService;
    TEST_ASSERT_TRUE(mockNodeDB->notifyPendingLicensedIdentityMigration());
    TEST_ASSERT_EQUAL(1, mockService.notificationCount);
    TEST_ASSERT_FALSE(mockNodeDB->licensedIdentityMigrationPending);
    TEST_ASSERT_FALSE(mockNodeDB->notifyPendingLicensedIdentityMigration());
    TEST_ASSERT_EQUAL(1, mockService.notificationCount);
    service = pipelineService;

    config.security.public_key.size = 0;
    owner.public_key.size = 0;
    TEST_ASSERT_TRUE(mockNodeDB->generateCryptoKeyPair());
    TEST_ASSERT_EQUAL(migratedNodeNum, myNodeInfo.my_node_num);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(privateKey, config.security.private_key.bytes, sizeof(privateKey));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(publicKey, config.security.public_key.bytes, sizeof(publicKey));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(publicKey, owner.public_key.bytes, sizeof(publicKey));
}

void test_L3_factory_config_reset_preserves_valid_identity_private_key(void)
{
    uint8_t publicKey[32], privateKey[32];
    crypto->generateKeyPair(publicKey, privateKey);
    config.has_security = true;
    config.security.private_key.size = 32;
    memcpy(config.security.private_key.bytes, privateKey, sizeof(privateKey));

    mockNodeDB->installDefaultsPreservingIdentity();
    TEST_ASSERT_EQUAL(32, config.security.private_key.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(privateKey, config.security.private_key.bytes, sizeof(privateKey));
    TEST_ASSERT_EQUAL(0, config.security.public_key.size);

    owner.is_licensed = true;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    TEST_ASSERT_TRUE(mockNodeDB->generateCryptoKeyPair());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(privateKey, config.security.private_key.bytes, sizeof(privateKey));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(publicKey, config.security.public_key.bytes, sizeof(publicKey));
}

void test_L4_licensed_low_entropy_identity_is_regenerated(void)
{
    static const uint8_t compromisedPublicKey[32] = {
        0xac, 0xaf, 0x8c, 0x1c, 0x3c, 0x1c, 0x37, 0xac, 0x4f, 0x03, 0xa1, 0xe9, 0xfc, 0x37, 0x23, 0x29,
        0xc8, 0xa3, 0x5d, 0x7f, 0x05, 0x26, 0xeb, 0x00, 0xbd, 0x26, 0xb8, 0x2e, 0xb1, 0x94, 0x7d, 0x24,
    };
    owner.is_licensed = true;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0xA5, 32);
    config.security.public_key.size = 32;
    memcpy(config.security.public_key.bytes, compromisedPublicKey, sizeof(compromisedPublicKey));
    TEST_ASSERT_TRUE(mockNodeDB->checkLowEntropyPublicKey(config.security.public_key));

    uint8_t oldPrivateKey[32];
    memcpy(oldPrivateKey, config.security.private_key.bytes, sizeof(oldPrivateKey));
    TEST_ASSERT_TRUE(mockNodeDB->generateCryptoKeyPair());
    TEST_ASSERT_TRUE(mockNodeDB->keyIsLowEntropy);
    TEST_ASSERT_FALSE(mockNodeDB->checkLowEntropyPublicKey(config.security.public_key));
    TEST_ASSERT_FALSE(memcmp(oldPrivateKey, config.security.private_key.bytes, sizeof(oldPrivateKey)) == 0);
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

// Build an unsigned broadcast whose inner message is padded with an unknown field, and pin that the
// padding pushes the RAW size past the fit threshold - the exemption the attacker is buying - while
// the frame stays sendable. Without canonical inner sizing these packets are wrongly accepted.
static meshtastic_MeshPacket makePayloadPaddedBroadcast(meshtastic_PortNum port, const pb_msgdesc_t *fields, const void *inner,
                                                        size_t padLen)
{
    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, port, 0);
    const size_t innerLen = pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), fields, inner);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, innerLen, "failed to encode the spoofed inner message");
    p.decoded.payload.size =
        innerLen + appendUnknownField(p.decoded.payload.bytes + innerLen, sizeof(p.decoded.payload.bytes) - innerLen, padLen);

    TEST_ASSERT_FALSE_MESSAGE(signedEncodingFits(&p.decoded), "padding must push the raw size past the fit threshold");
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(MAX_LORA_PAYLOAD_LEN, encodedDataSize(&p.decoded) + MESHTASTIC_HEADER_LENGTH,
                                      "padded frame must still be one a radio could send");
    return p;
}

// E10: unknown fields buried inside Data.payload are discarded by the module's own pb_decode, so
// they must not sway the downgrade decision the way A10 already pins for Data-level unknown fields.
void test_E10_decoded_unsigned_position_padded_inside_payload_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.has_latitude_i = pos.has_longitude_i = true;
    pos.latitude_i = 371234567;
    pos.longitude_i = -1221234567;

    meshtastic_MeshPacket p = makePayloadPaddedBroadcast(meshtastic_PortNum_POSITION_APP, &meshtastic_Position_msg, &pos, 163);

    TEST_ASSERT_FALSE_MESSAGE(checkXeddsaReceivePolicy(&p), "payload-padded unsigned Position from a signer must be dropped");
    TEST_ASSERT_FALSE(p.xeddsa_signed);
}

// E11: over-correction guard. Telemetry (272 bytes max) and Waypoint (199) can legitimately exceed
// the signable budget, so canonical sizing must not shrink an honest one into the drop range.
void test_E11_decoded_unsigned_oversized_telemetry_from_signer_accepted(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_Telemetry t = meshtastic_Telemetry_init_zero;
    t.which_variant = meshtastic_Telemetry_host_metrics_tag;
    t.variant.host_metrics.uptime_seconds = 123456;
    t.variant.host_metrics.has_user_string = true;
    memset(t.variant.host_metrics.user_string, 'x', sizeof(t.variant.host_metrics.user_string) - 1);

    meshtastic_MeshPacket p = makeDecoded(REMOTE_NODE, NODENUM_BROADCAST, meshtastic_PortNum_TELEMETRY_APP, 0);
    p.decoded.payload.size =
        pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_Telemetry_msg, &t);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, p.decoded.payload.size, "failed to encode the oversized Telemetry");

    // Every byte here is a field this build understands, so canonical sizing must leave it alone.
    TEST_ASSERT_FALSE_MESSAGE(signedEncodingFits(&p.decoded), "telemetry must be too big to sign, else the test is vacuous");

    TEST_ASSERT_TRUE_MESSAGE(checkXeddsaReceivePolicy(&p), "honest oversized telemetry from a signer must not be dropped");
}

// E12: E10 for the Waypoint branch of the canonical-sizing switch.
void test_E12_decoded_unsigned_waypoint_padded_inside_payload_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_Waypoint w = meshtastic_Waypoint_init_zero;
    w.id = 42;
    w.has_latitude_i = w.has_longitude_i = true;
    w.latitude_i = 371234567;
    w.longitude_i = -1221234567;
    strcpy(w.name, "spoofed");

    meshtastic_MeshPacket p = makePayloadPaddedBroadcast(meshtastic_PortNum_WAYPOINT_APP, &meshtastic_Waypoint_msg, &w, 150);

    TEST_ASSERT_FALSE_MESSAGE(checkXeddsaReceivePolicy(&p), "payload-padded unsigned Waypoint from a signer must be dropped");
    TEST_ASSERT_FALSE(p.xeddsa_signed);
}

// E13: E10 for the NodeInfo/User branch. Router drops it before rebroadcast; NodeInfoModule's own
// check (Group C) is receiver-local and would not stop the packet propagating.
void test_E13_decoded_unsigned_nodeinfo_padded_inside_payload_dropped(void)
{
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    meshtastic_User u = meshtastic_User_init_zero;
    strcpy(u.id, "!0b0b0b0b");
    strcpy(u.long_name, "spoofed node");
    strcpy(u.short_name, "SPF");

    meshtastic_MeshPacket p = makePayloadPaddedBroadcast(meshtastic_PortNum_NODEINFO_APP, &meshtastic_User_msg, &u, 150);

    TEST_ASSERT_FALSE_MESSAGE(checkXeddsaReceivePolicy(&p), "payload-padded unsigned NodeInfo from a signer must be dropped");
    TEST_ASSERT_FALSE(p.xeddsa_signed);
}

// C18: a PKI DM to us from a node whose key we do not hold. We cannot read it, but the sender must
// find out why: a PKI_UNKNOWN_PUBKEY NAK makes it send us its NodeInfo, after which its retry
// decrypts. The phone also gets the encrypted frame. Nothing is relayed and NodeDB is untouched.
void test_C18_undecryptable_pki_to_us_naks_unknown_pubkey_and_reaches_phone(void)
{
    const RelayIdentity us = installOurIdentity();
    const RelayIdentity stranger = makeIdentity(ADMIN_NODE); // not added to NodeDB
    const meshtastic_MeshPacket dm =
        makePkiUnicastBetween(stranger, us, meshtastic_PortNum_TEXT_MESSAGE_APP, 0xADA50005, /*wantAck=*/true);
    useDHKey(us.priv); // we hold our own key; the sender's is what we lack
    // An unrelated admin key is tried as a fallback and fails; that must not turn "no key" into "key failed".
    const RelayIdentity admin = makeIdentity(TARGET_NODE);
    config.security.admin_key[0].size = 32;
    memcpy(config.security.admin_key[0].bytes, admin.pub, 32);

    for (const auto mode : ALL_MODES) {
        pipelineRadio->reset();
        pipelineRouting->reset();
        config.device.rebroadcast_mode = mode;
        meshtastic_MeshPacket copy = dm;
        copy.id += (uint32_t)mode;
        runPipelineIngress(copy);
        char msg[120];
        snprintf(msg, sizeof(msg), "%s: an undecryptable DM to us must be NAKed exactly once", modeName(mode));
        TEST_ASSERT_EQUAL_MESSAGE(1, pipelineRouting->ackCalls, msg);
        TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY, pipelineRouting->lastErr,
                                  "the NAK must say PKI_UNKNOWN_PUBKEY so the sender answers with its NodeInfo");
        TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRadio->sendCalls, "a packet addressed to us is never relayed");
        meshtastic_MeshPacket *toPhone = pipelineService->getForPhone();
        TEST_ASSERT_NOT_NULL_MESSAGE(toPhone, "the phone must see a frame to us that we had no way to read");
        TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, toPhone->which_payload_variant);
        packetPool.release(toPhone);
        TEST_ASSERT_NULL(pipelineService->getForPhone());
        TEST_ASSERT_FALSE(pipelineRouter->historyContains(&copy));
        TEST_ASSERT_NULL_MESSAGE(mockNodeDB->getMeshNode(ADMIN_NODE), "an unverified sender must not be added to NodeDB");
    }

    // Once per frame, not once per copy: three neighbours rebroadcasting the same DM owe the sender
    // one NAK and the phone one frame.
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
    pipelineRadio->reset();
    pipelineRouting->reset();
    drainPhoneQueue();
    meshtastic_MeshPacket first = dm;
    first.id = 0xADA5F005;
    runPipelineIngress(first);
    TEST_ASSERT_EQUAL(1, pipelineRouting->ackCalls);
    expectEncryptedPhoneDeliveries(1);

    runPipelineIngress(makeRelayedCopy(first));
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineRouting->ackCalls, "a relayed copy of a frame we answered owes nothing");
    TEST_ASSERT_NULL_MESSAGE(pipelineService->getForPhone(), "and the phone has it already");

    // The sender's own retransmission says it heard no answer, so answer again - at hop 0, since only
    // a direct neighbour ever sees hop_start == hop_limit.
    meshtastic_MeshPacket retx = first;
    retx.hop_limit = retx.hop_start;
    runPipelineIngress(retx);
    TEST_ASSERT_EQUAL_MESSAGE(2, pipelineRouting->ackCalls, "the sender's own retransmission is answered again");
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRouting->lastHopLimit, "the second answer goes back one hop only");
    TEST_ASSERT_NULL_MESSAGE(pipelineService->getForPhone(), "but the phone is not shown the frame twice");
}

// C19: the same frame claiming to be from us is a forgery and gets nothing - no NAK, no phone,
// no relay. We hold our own identity, so the decrypt is attempted with our key and fails: the gate
// must REJECT (from-us decode failure), not hand it to the opaque path.
void test_C19_undecryptable_frame_claiming_to_be_from_us_gets_no_reaction(void)
{
    const RelayIdentity target = makeIdentity(TARGET_NODE);
    const RelayIdentity forger = makeIdentity(LOCAL_NODE);
    meshtastic_MeshPacket p = makePkiUnicastBetween(forger, target, meshtastic_PortNum_TEXT_MESSAGE_APP, 0xADA60006, true);
    p.to = LOCAL_NODE; // to us, "from" us

    // Control: with no identity of ours in NodeDB the frame is not a PKI candidate, so nothing is
    // attempted and it is merely unreadable. The REJECT below is therefore the from-us decode-failure
    // arm and not the key mismatch, which is present in both cases.
    meshtastic_MeshPacket controlCopy = p;
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(RoutingAuthVerdict::OPAQUE_RELAY_ONLY),
                              static_cast<int>(passesRoutingAuthGate(&controlCopy)),
                              "without our key the same frame is opaque, not rejected");

    const RelayIdentity us = installOurIdentity();
    useDHKey(us.priv);
    meshtastic_MeshPacket verdictCopy = p;
    TEST_ASSERT_EQUAL(static_cast<int>(RoutingAuthVerdict::REJECT), static_cast<int>(passesRoutingAuthGate(&verdictCopy)));
    runPipelineIngress(p);
    TEST_ASSERT_EQUAL(0, pipelineRouting->ackCalls);
    TEST_ASSERT_EQUAL(0, pipelineRadio->sendCalls);
    TEST_ASSERT_NULL(pipelineService->getForPhone());
}

// C20: sender key known, decrypt fails anyway (tampered or rotated key): NAK NO_CHANNEL - we had a
// key and it did not work, which is not the same as not having one.
void test_C20_pki_to_us_with_known_key_that_fails_naks_no_channel(void)
{
    const RelayIdentity us = installOurIdentity();
    const RelayIdentity sender = makeIdentity(ADMIN_NODE);
    const RelayIdentity stale = makeIdentity(ADMIN_NODE);
    mockNodeDB->addNode(ADMIN_NODE);
    mockNodeDB->setPublicKey(ADMIN_NODE, stale.pub); // we hold a key for the sender, just not the one it used
    const meshtastic_MeshPacket dm = makePkiUnicastBetween(sender, us, meshtastic_PortNum_TEXT_MESSAGE_APP, 0xADA70007, true);
    useDHKey(us.priv);

    runPipelineIngress(dm);
    TEST_ASSERT_EQUAL(1, pipelineRouting->ackCalls);
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NO_CHANNEL, pipelineRouting->lastErr);
    TEST_ASSERT_EQUAL(0, pipelineRadio->sendCalls);
    // We had a key and it failed: junk or tampering, not something the phone can use.
    TEST_ASSERT_NULL(pipelineService->getForPhone());
}

// C21: an MQTT gateway carries PKI DMs between other nodes as ciphertext when encrypted uplink is
// on, and never uplinks them in plaintext mode. Relay and uplink are independent of each other.
void test_C21_opaque_pki_unicast_is_uplinked_only_with_encrypted_mqtt(void)
{
    const RelayIdentity admin = makeIdentity(ADMIN_NODE);
    const RelayIdentity target = makeIdentity(TARGET_NODE);
    const meshtastic_MeshPacket p = makePkiUnicastBetween(admin, target, meshtastic_PortNum_ADMIN_APP, 0xADA80008);
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_NONE; // uplink is not relay
    channels.getByIndex(0).settings.uplink_enabled = true;

    moduleConfig.mqtt.enabled = true;
    moduleConfig.mqtt.encryption_enabled = true;
    runPipelineIngress(p);
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineMqtt->queueSize(), "encrypted uplink must carry a PKI DM it cannot read");
    TEST_ASSERT_EQUAL(0, pipelineRadio->sendCalls);

    pipelineMqtt->clearQueue();
    moduleConfig.mqtt.encryption_enabled = false;
    meshtastic_MeshPacket again = p;
    again.id++;
    runPipelineIngress(again);
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineMqtt->queueSize(), "plaintext uplink has nothing to publish for an opaque packet");

    moduleConfig.mqtt.enabled = false;
    moduleConfig.mqtt.encryption_enabled = true;
    again.id++;
    runPipelineIngress(again);
    TEST_ASSERT_EQUAL(0, pipelineMqtt->queueSize());

    // Once per frame: a gateway that hears the same DM from three neighbours publishes it once.
    moduleConfig.mqtt.enabled = true;
    meshtastic_MeshPacket carried = p;
    carried.id = 0xADA8F008;
    runPipelineIngress(carried);
    TEST_ASSERT_EQUAL(1, pipelineMqtt->queueSize());
    runPipelineIngress(makeRelayedCopy(carried));
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineMqtt->queueSize(), "a second copy of the same frame is not a second publish");

    // And only for a PKI unicast between other nodes: never a broadcast, never one addressed to us,
    // never one that arrived over MQTT in the first place.
    pipelineMqtt->clearQueue();
    meshtastic_MeshPacket broadcast = p;
    broadcast.id = 0xADA8F108;
    broadcast.to = NODENUM_BROADCAST;
    runPipelineIngress(broadcast);
    meshtastic_MeshPacket toUs = p;
    toUs.id = 0xADA8F208;
    toUs.to = LOCAL_NODE;
    runPipelineIngress(toUs);
    meshtastic_MeshPacket viaMqtt = p;
    viaMqtt.id = 0xADA8F308;
    viaMqtt.via_mqtt = true;
    runPipelineIngress(viaMqtt);
    drainPhoneQueue();
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineMqtt->queueSize(), "only a PKI DM between other nodes is carried for the mesh");
}

// C22: an unreadable broadcast on a channel we do not hold reaches the phone without touching
// NodeDB, in every mode - relay and phone delivery are separate decisions.
void test_C22_unknown_channel_broadcast_reaches_phone_without_nodedb(void)
{
    const meshtastic_MeshPacket foreign = makeUnknownChannelBroadcast(0xADA90009);
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
    runPipelineIngress(foreign);
    meshtastic_MeshPacket *toPhone = pipelineService->getForPhone();
    TEST_ASSERT_NOT_NULL(toPhone);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, toPhone->which_payload_variant);
    packetPool.release(toPhone);
    TEST_ASSERT_EQUAL(0, pipelineRouting->ackCalls);
    TEST_ASSERT_NULL(mockNodeDB->getMeshNode(ADMIN_NODE));
}

// C23: a broadcast on wire hash 0 from a sender whose key we hold, on a channel we do not hold. PKI
// never applies to a broadcast, so the sender's key is irrelevant: the frame is unreadable to us and
// reaches the phone. Guards the header-based guess this replaced, which called it readable.
void test_C23_hash0_broadcast_from_keyed_sender_is_unreadable_and_reaches_phone(void)
{
    TEST_ASSERT_FALSE_MESSAGE(channels.hasHash(0), "fixture assumes no configured channel hashes to 0");
    const RelayIdentity sender = makeIdentity(ADMIN_NODE);
    mockNodeDB->addNode(ADMIN_NODE);
    mockNodeDB->setPublicKey(ADMIN_NODE, sender.pub);
    meshtastic_MeshPacket foreign = makeUnknownChannelBroadcast(0xADAC000C);
    foreign.channel = 0;
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
    runPipelineIngress(foreign);
    meshtastic_MeshPacket *toPhone = pipelineService->getForPhone();
    TEST_ASSERT_NOT_NULL_MESSAGE(toPhone, "a hash-0 broadcast we cannot read must reach the phone whoever sent it");
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_encrypted_tag, toPhone->which_payload_variant);
    packetPool.release(toPhone);
    TEST_ASSERT_EQUAL(0, pipelineRouting->ackCalls);
}

// C24: KNOWN_ONLY declines a stranger before any decrypt attempt. On a channel we hold that must
// still count as "matched", so the phone never sees a stranger's ciphertext; only a frame we had no
// key or channel for is unreadable. Nothing is relayed, NAKed or learned.
void test_C24_known_only_stranger_on_held_channel_is_withheld_from_phone(void)
{
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY;
    meshtastic_MeshPacket junk = meshtastic_MeshPacket_init_zero;
    junk.from = REMOTE_NODE; // never added to NodeDB
    junk.to = NODENUM_BROADCAST;
    junk.id = 0xADAD000D;
    junk.hop_limit = 1;
    junk.hop_start = 2;
    junk.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    junk.encrypted.size = 3;
    memset(junk.encrypted.bytes, 0xFF, junk.encrypted.size);
    junk.channel = channels.setActiveByIndex(0);
    crypto->encryptPacket(junk.from, junk.id, junk.encrypted.size, junk.encrypted.bytes);

    meshtastic_MeshPacket verdictCopy = junk;
    TEST_ASSERT_EQUAL(static_cast<int>(RoutingAuthVerdict::OPAQUE_RELAY_ONLY),
                      static_cast<int>(passesRoutingAuthGate(&verdictCopy)));
    runPipelineIngress(junk);
    TEST_ASSERT_NULL_MESSAGE(pipelineService->getForPhone(), "a stranger's frame on a channel we hold is not for the phone");
    TEST_ASSERT_EQUAL(0, pipelineRouting->ackCalls);
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRadio->sendCalls, "KNOWN_ONLY does not carry an opaque broadcast");
    TEST_ASSERT_NULL(mockNodeDB->getMeshNode(REMOTE_NODE));

    // The same stranger on a channel we do not hold is unreadable - and KNOWN_ONLY ignores that too,
    // exactly as it declines to relay it. The mode is the whole rule, not just the relay half.
    meshtastic_MeshPacket foreign = makeUnknownChannelBroadcast(0xADAD001D);
    foreign.from = REMOTE_NODE;
    runPipelineIngress(foreign);
    TEST_ASSERT_NULL_MESSAGE(pipelineService->getForPhone(), "KNOWN_ONLY does not hand a stranger's ciphertext to the phone");

    // Under ALL the same frame reaches the phone, so it is the mode deciding and not the frame.
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
    meshtastic_MeshPacket carried = makeUnknownChannelBroadcast(0xADAD002D);
    carried.from = REMOTE_NODE;
    runPipelineIngress(carried);
    expectEncryptedPhoneDeliveries(1);
}

// C25: a frame heard three times is relayed once. The originator's own retransmission is the one
// exception - it means our first relay never reached it - and even that is skipped while our copy
// is still waiting in the TX queue. The uplink happens once regardless.
void test_C25_duplicate_opaque_frame_is_relayed_once_but_an_originator_retx_is_carried_again(void)
{
    const RelayIdentity admin = makeIdentity(ADMIN_NODE);
    const RelayIdentity target = makeIdentity(TARGET_NODE);
    const meshtastic_MeshPacket dm = makePkiUnicastBetween(admin, target, meshtastic_PortNum_ADMIN_APP, 0xADB10011);
    channels.getByIndex(0).settings.uplink_enabled = true;
    moduleConfig.mqtt.enabled = true;
    moduleConfig.mqtt.encryption_enabled = true;

    runPipelineIngress(dm);
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineRadio->sentCountFor(ADMIN_NODE, dm.id), "first copy is relayed");
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineMqtt->queueSize(), "and uplinked");

    runPipelineIngress(makeRelayedCopy(dm));
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineRadio->sentCountFor(ADMIN_NODE, dm.id), "a neighbour's copy is not relayed again");
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineMqtt->queueSize(), "nor uplinked again");

    meshtastic_MeshPacket retx = dm;
    retx.hop_limit = retx.hop_start; // only a direct neighbour of the sender sees this
    runPipelineIngress(retx);
    TEST_ASSERT_EQUAL_MESSAGE(2, pipelineRadio->sentCountFor(ADMIN_NODE, dm.id), "the sender's own retransmission is carried");
    TEST_ASSERT_EQUAL_MESSAGE(1, pipelineMqtt->queueSize(), "but the uplink stays once per frame");

    pipelineRadio->holdInTxQueue(ADMIN_NODE, dm.id); // our copy has not gone out yet
    runPipelineIngress(retx);
    TEST_ASSERT_EQUAL_MESSAGE(2, pipelineRadio->sentCountFor(ADMIN_NODE, dm.id),
                              "no second copy while the first is still queued");
}

// C26: channel 0 is where PKI lives, but a frame too short to hold the PKI overhead never had a key
// to miss. It gets NO_CHANNEL, and the phone still sees a frame we could not read.
void test_C26_short_channel0_frame_to_us_naks_no_channel(void)
{
    TEST_ASSERT_FALSE_MESSAGE(channels.hasHash(0), "fixture assumes no configured channel hashes to 0");
    meshtastic_MeshPacket runt = meshtastic_MeshPacket_init_zero;
    runt.from = REMOTE_NODE;
    runt.to = LOCAL_NODE;
    runt.id = 0xADB20012;
    runt.channel = 0;
    runt.hop_limit = 2;
    runt.hop_start = 3;
    runt.want_ack = true;
    runt.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    runt.encrypted.size = 8; // <= MESHTASTIC_PKC_OVERHEAD, so perhapsDecode never called it a PKI candidate
    memset(runt.encrypted.bytes, 0x5A, runt.encrypted.size);

    runPipelineIngress(runt);
    TEST_ASSERT_EQUAL(1, pipelineRouting->ackCalls);
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Routing_Error_NO_CHANNEL, pipelineRouting->lastErr,
                              "too short to be PKI, so the reason is the channel, not a missing key");
    expectEncryptedPhoneDeliveries(1);
}

// C27: a licensed station transmits in the clear, so it must not answer a node it knows to be
// unlicensed - not even to say it could not read them. A peer of unknown status still gets a NAK.
void test_C27_licensed_node_does_not_nak_a_known_unlicensed_sender(void)
{
    const RelayIdentity us = installOurIdentity();
    markOurselvesLicensed();
    const RelayIdentity unlicensed = makeIdentity(ADMIN_NODE);
    mockNodeDB->addNode(ADMIN_NODE);
    mockNodeDB->markLicenseStatus(ADMIN_NODE, false);
    useDHKey(us.priv);

    const meshtastic_MeshPacket fromUnlicensed =
        makePkiUnicastBetween(unlicensed, us, meshtastic_PortNum_TEXT_MESSAGE_APP, 0xADB30013, /*wantAck=*/true);
    runPipelineIngress(fromUnlicensed);
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineRouting->ackCalls, "a licensed station does not answer a known unlicensed node");
    TEST_ASSERT_NULL_MESSAGE(pipelineService->getForPhone(), "and has no lawful use for its ciphertext either");
    TEST_ASSERT_EQUAL(0, pipelineRadio->sendCalls);

    // Same frame shape from a node whose licence status we do not know: answered, as before. A
    // licensed node declines the PKI decrypt itself, so this is a failed attempt, not a missing key.
    const RelayIdentity unknown = makeIdentity(REMOTE_NODE);
    const meshtastic_MeshPacket fromUnknown =
        makePkiUnicastBetween(unknown, us, meshtastic_PortNum_TEXT_MESSAGE_APP, 0xADB30023, /*wantAck=*/true);
    runPipelineIngress(fromUnknown);
    TEST_ASSERT_EQUAL(1, pipelineRouting->ackCalls);
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NO_CHANNEL, pipelineRouting->lastErr);
    TEST_ASSERT_NULL_MESSAGE(pipelineService->getForPhone(), "a decrypt we declined is not something the phone can use");
}

// C28: the NAK goes back the way the request came - the response hop budget, on the primary channel
// index - and a frame that arrived with no hops left is still answered. The relay gate is not the
// NAK gate.
void test_C28_nak_uses_the_response_hop_limit_on_the_primary_channel(void)
{
    const RelayIdentity us = installOurIdentity();
    const RelayIdentity stranger = makeIdentity(ADMIN_NODE);
    useDHKey(us.priv);
    meshtastic_MeshPacket dm = makePkiUnicastBetween(stranger, us, meshtastic_PortNum_TEXT_MESSAGE_APP, 0xADB40014, true);
    dm.hop_start = 3;
    dm.hop_limit = 1;

    runPipelineIngress(dm);
    TEST_ASSERT_EQUAL(1, pipelineRouting->ackCalls);
    TEST_ASSERT_EQUAL_MESSAGE(routingModule->getHopLimitForResponse(dm), pipelineRouting->lastHopLimit,
                              "the NAK gets the same budget any response to this packet would");
    TEST_ASSERT_EQUAL_MESSAGE(channels.getPrimaryIndex(), pipelineRouting->lastChIndex,
                              "an unreadable frame is answered on the primary channel");

    meshtastic_MeshPacket spent = dm;
    spent.id++;
    spent.hop_limit = 0; // would stop a relay dead; the sender is still owed an answer
    runPipelineIngress(spent);
    TEST_ASSERT_EQUAL_MESSAGE(2, pipelineRouting->ackCalls, "a frame with no hops left is still NAKed");
    TEST_ASSERT_EQUAL(0, pipelineRadio->sendCalls);
}

/// Rename channel 0 until its wire hash is 0 - the collision a PKI frame's hash 0 can run into.
static void forceChannel0HashZero()
{
    meshtastic_Channel &ch = channels.getByIndex(0);
    for (int len = 1; len <= 2; len++) {
        for (int c = 1; c < 256; c++) {
            memset(ch.settings.name, 0, sizeof(ch.settings.name));
            for (int i = 0; i < len; i++)
                ch.settings.name[i] = (char)c;
            channels.onConfigChanged();
            if (channels.getHash(0) == 0)
                return;
        }
    }
    TEST_FAIL_MESSAGE("could not force channel 0 to hash 0");
}

// C29: holding a channel whose hash happens to be 0 must not make a stranger's PKI DM look like
// channel traffic. Under KNOWN_ONLY the sender is refused before any decrypt, and the frame is still
// classified from what was attempted, not from the hash it carries.
void test_C29_known_only_pki_dm_to_us_is_opaque_even_when_a_held_channel_hashes_to_0(void)
{
    const RelayIdentity us = installOurIdentity();
    const RelayIdentity stranger = makeIdentity(ADMIN_NODE); // never added to NodeDB
    useDHKey(us.priv);
    const meshtastic_MeshPacket dm =
        makePkiUnicastBetween(stranger, us, meshtastic_PortNum_TEXT_MESSAGE_APP, 0xADB50015, /*wantAck=*/true);
    forceChannel0HashZero();
    TEST_ASSERT_TRUE_MESSAGE(channels.hasHash(0), "fixture must hold a channel on hash 0");
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY;

    runPipelineIngress(dm);
    TEST_ASSERT_EQUAL(1, pipelineRouting->ackCalls);
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY, pipelineRouting->lastErr,
                              "a PKI DM to us is opaque whatever our channels hash to");
    expectEncryptedPhoneDeliveries(1); // a DM to us satisfies KNOWN_ONLY: we are the known party
    TEST_ASSERT_NULL(mockNodeDB->getMeshNode(ADMIN_NODE));
}

// C30: the mirror case. A channel-0 unicast between other nodes, on that same hash-0 channel, is one
// we did try and fail to decrypt - so it is not PKI ciphertext and must not be published as such.
void test_C30_failed_decrypt_on_a_held_hash0_channel_is_not_uplinked_as_pki(void)
{
    forceChannel0HashZero();
    channels.getByIndex(0).settings.uplink_enabled = true;
    moduleConfig.mqtt.enabled = true;
    moduleConfig.mqtt.encryption_enabled = true;
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_NONE; // uplink is not relay

    meshtastic_MeshPacket junk = meshtastic_MeshPacket_init_zero;
    junk.from = ADMIN_NODE;
    junk.to = TARGET_NODE;
    junk.id = 0xADB60016;
    junk.channel = 0; // matches the held channel, so the decrypt is attempted and fails
    junk.hop_limit = 2;
    junk.hop_start = 3;
    junk.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    junk.encrypted.size = 24;
    memset(junk.encrypted.bytes, 0xC3, junk.encrypted.size);

    runPipelineIngress(junk);
    TEST_ASSERT_EQUAL_MESSAGE(0, pipelineMqtt->queueSize(), "a failed decrypt is not a PKI frame to carry for others");
}

void setup()
{
    pipelineHarnessCreate();

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
    RUN_TEST(test_A18_unsigned_broadcast_from_signer_with_unknown_fields_dropped);
    RUN_TEST(test_A19_unsigned_broadcast_from_nonsigner_with_unknown_fields_accepted);

    printf("\n=== Group B: send-side signing policy ===\n");
    RUN_TEST(test_B1_local_broadcast_is_signed);
    RUN_TEST(test_B2_local_unicast_not_signed);
    RUN_TEST(test_B3_local_oversized_broadcast_not_signed);
    RUN_TEST(test_B4_all_broadcast_sizes_deliverable_no_deadband);
    RUN_TEST(test_B5_preset_signature_on_local_packet_cleared);
    RUN_TEST(test_B6_rich_shape_sweep_no_deadband);
    RUN_TEST(test_B7_infrastructure_port_signing_matrix);
    RUN_TEST(test_B8_licensed_broadcast_and_unicast_are_signed);
    RUN_TEST(test_B9_licensed_unicast_never_uses_pki_encryption);
    RUN_TEST(test_B10_licensed_oversized_unicast_remains_unsigned);
    RUN_TEST(test_B11_normal_unicast_still_uses_pki);
    RUN_TEST(test_B12_licensed_receiver_does_not_decrypt_pki);
    RUN_TEST(test_B13_licensed_port_and_destination_signing_matrix);

    printf("\n=== Group C: routing pipeline authentication ordering ===\n");
    RUN_TEST(test_C1_invalid_first_copy_does_not_poison_valid_same_id);
    RUN_TEST(test_C2_invalid_ordinary_duplicate_has_no_cancel_or_delivery_effects);
    RUN_TEST(test_C3_invalid_repeated_packet_cannot_ack_or_change_retry_state);
    RUN_TEST(test_C4_invalid_fallback_packet_cannot_relay);
    RUN_TEST(test_C5_invalid_upgrade_cannot_remove_pending_valid_send);
    RUN_TEST(test_C6_opaque_unknown_channel_is_relay_only);
    RUN_TEST(test_C7_strict_rejects_unsigned_decoded_simradio_ingress);
    RUN_TEST(test_C8_trusted_local_decoded_delivery_is_not_filtered);
    RUN_TEST(test_C9_known_channel_junk_broadcast_is_relayed_not_delivered);
    RUN_TEST(test_C10_legacy_channel_dm_is_naked_no_channel_and_nothing_else);
    RUN_TEST(test_C11_malformed_pki_plaintext_is_naked_no_channel_and_nothing_else);
    RUN_TEST(test_C12_exact_authenticated_replay_reuses_verdict_without_collision_bypass);
    RUN_TEST(test_C13_failed_initial_reliable_send_does_not_retry);
    RUN_TEST(test_C14_duty_cycle_limited_reliable_send_remains_pending);
    RUN_TEST(test_C15_reliable_unicast_tracks_five_total_attempts);
    RUN_TEST(test_C16_reliable_broadcast_keeps_three_total_attempts);
    RUN_TEST(test_C17_colliding_channel_hash_foreign_broadcast_is_relay_only);
    RUN_TEST(test_C18_undecryptable_pki_to_us_naks_unknown_pubkey_and_reaches_phone);
    RUN_TEST(test_C19_undecryptable_frame_claiming_to_be_from_us_gets_no_reaction);
    RUN_TEST(test_C20_pki_to_us_with_known_key_that_fails_naks_no_channel);
    RUN_TEST(test_C21_opaque_pki_unicast_is_uplinked_only_with_encrypted_mqtt);
    RUN_TEST(test_C22_unknown_channel_broadcast_reaches_phone_without_nodedb);
    RUN_TEST(test_C23_hash0_broadcast_from_keyed_sender_is_unreadable_and_reaches_phone);
    RUN_TEST(test_C24_known_only_stranger_on_held_channel_is_withheld_from_phone);
    RUN_TEST(test_C25_duplicate_opaque_frame_is_relayed_once_but_an_originator_retx_is_carried_again);
    RUN_TEST(test_C26_short_channel0_frame_to_us_naks_no_channel);
    RUN_TEST(test_C27_licensed_node_does_not_nak_a_known_unlicensed_sender);
    RUN_TEST(test_C28_nak_uses_the_response_hop_limit_on_the_primary_channel);
    RUN_TEST(test_C29_known_only_pki_dm_to_us_is_opaque_even_when_a_held_channel_hashes_to_0);
    RUN_TEST(test_C30_failed_decrypt_on_a_held_hash0_channel_is_not_uplinked_as_pki);
    printf("\n=== Group N: NodeInfoModule authentication ===\n");
    RUN_TEST(test_N1_unsigned_nodeinfo_from_signer_dropped);
    RUN_TEST(test_N2_signed_nodeinfo_from_signer_not_dropped);
    RUN_TEST(test_N3_unsigned_nodeinfo_from_nonsigner_not_dropped);
    RUN_TEST(test_N4_unsigned_unicast_nodeinfo_from_signer_accepted);
    RUN_TEST(test_N5_unsigned_unicast_nodeinfo_from_signer_does_not_change_name);
    RUN_TEST(test_N6_signed_unicast_nodeinfo_from_signer_changes_name);
    RUN_TEST(test_N7_unsigned_unicast_nodeinfo_from_nonsigner_changes_name);
    RUN_TEST(test_N8_second_request_inside_the_window_is_suppressed);
    RUN_TEST(test_N9_request_after_the_window_is_answered);
    RUN_TEST(test_N10_stale_stamp_does_not_alias_after_a_full_wrap);
    RUN_TEST(test_N11_window_still_applies_across_the_wrap);

    printf("\n=== Group L: licensed identity and plaintext signing ===\n");
    RUN_TEST(test_L1_licensed_nodeinfo_publishes_public_key);
    RUN_TEST(test_L2_licensed_identity_key_is_generated_and_preserved);
    RUN_TEST(test_L3_factory_config_reset_preserves_valid_identity_private_key);
    RUN_TEST(test_L4_licensed_low_entropy_identity_is_regenerated);

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
    RUN_TEST(test_E10_decoded_unsigned_position_padded_inside_payload_dropped);
    RUN_TEST(test_E11_decoded_unsigned_oversized_telemetry_from_signer_accepted);
    RUN_TEST(test_E12_decoded_unsigned_waypoint_padded_inside_payload_dropped);
    RUN_TEST(test_E13_decoded_unsigned_nodeinfo_padded_inside_payload_dropped);

    const int result = UNITY_END();
    pipelineHarnessDestroy();
    exit(result);
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
