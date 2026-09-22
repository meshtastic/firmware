// PROTOTYPE suite for the pairwise ACK proof (src/mesh/AckProof.h).
//
// Covers the mechanism end to end - key agreement, request_id binding, wire encoding, and the
// verify verdicts - without standing up a NodeDB. The NodeDB-backed policy wrappers
// (ackProofAttach/ackProofVerify) and the ReliableRouter hook are deliberately NOT covered here;
// they need the full router harness, and this is a spike.
#include "configuration.h"

#include "TestUtil.h"
#include <unity.h>

#if !(MESHTASTIC_EXCLUDE_PKI)

#include "mesh/AckProof.h"
#include "mesh/CryptoEngine.h"
#include "mesh/MeshTypes.h"
#include <pb_decode.h>
#include <pb_encode.h>

static constexpr NodeNum ALICE = 0x0A0A0A0A; // sends the DM, verifies the ack
static constexpr NodeNum BOB = 0x0B0B0B0B;   // receives the DM, produces the ack
static constexpr uint32_t REQUEST_ID = 0xABCD1234;

// Stand-in for the encoded Routing message the proof covers.
static const uint8_t ROUTING[] = {0x18, 0x00}; // error_reason = NONE
#define ROUTING_LEN sizeof(ROUTING)

struct Identity {
    uint8_t pub[32];
    uint8_t priv[32];
};

static Identity makeIdentity()
{
    Identity id;
    crypto->generateKeyPair(id.pub, id.priv);
    return id;
}

/** Act as this node for subsequent crypto calls. */
static void becomeNode(const Identity &id)
{
    uint8_t priv[32];
    memcpy(priv, id.priv, sizeof(priv));
    crypto->setDHPrivateKey(priv);
}

/** A minimal ROUTING ack, shaped the way MeshModule::allocAckNak builds one. */
static meshtastic_MeshPacket makeAck(NodeNum from, NodeNum to, uint32_t requestId)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = to;
    p.id = 0x5150;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_ROUTING_APP;
    p.decoded.request_id = requestId;

    meshtastic_Routing ack = meshtastic_Routing_init_default;
    ack.which_variant = meshtastic_Routing_error_reason_tag;
    ack.error_reason = meshtastic_Routing_Error_NONE;
    p.decoded.payload.size =
        pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_Routing_msg, &ack);
    TEST_ASSERT_GREATER_THAN(0, p.decoded.payload.size);
    return p;
}

void setUp(void) {}
void tearDown(void) {}

// The whole scheme rests on this: both endpoints derive the same value from opposite halves of the
// key pair, and nobody else can.
void test_proof_is_pairwise_and_symmetric(void)
{
    const Identity alice = makeIdentity();
    const Identity bob = makeIdentity();
    const Identity mallory = makeIdentity();

    uint8_t fromBob[ACK_PROOF_SIZE], fromAlice[ACK_PROOF_SIZE], fromMallory[ACK_PROOF_SIZE];

    // Both endpoints compute over the SAME direction tuple (Bob is the acker); only the key half
    // they hold differs.
    becomeNode(bob);
    TEST_ASSERT_TRUE(crypto->ackProofCompute(alice.pub, BOB, ALICE, REQUEST_ID, ROUTING, ROUTING_LEN, fromBob));
    becomeNode(alice);
    TEST_ASSERT_TRUE(crypto->ackProofCompute(bob.pub, BOB, ALICE, REQUEST_ID, ROUTING, ROUTING_LEN, fromAlice));
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(fromBob, fromAlice, ACK_PROOF_SIZE,
                                          "both endpoints must derive the same proof from the shared secret");

    // A third party holding both public keys cannot reproduce it.
    becomeNode(mallory);
    TEST_ASSERT_TRUE(crypto->ackProofCompute(alice.pub, BOB, ALICE, REQUEST_ID, ROUTING, ROUTING_LEN, fromMallory));
    TEST_ASSERT_TRUE_MESSAGE(memcmp(fromBob, fromMallory, ACK_PROOF_SIZE) != 0, "a non-peer must not derive the proof");
}

// request_id is an input, so a captured proof cannot be aimed at a different pending packet - the
// retargeting problem that the signature scheme needed an explicit buffer change to solve.
void test_proof_binds_request_id(void)
{
    const Identity alice = makeIdentity();
    const Identity bob = makeIdentity();

    uint8_t a[ACK_PROOF_SIZE], b[ACK_PROOF_SIZE];
    becomeNode(bob);
    TEST_ASSERT_TRUE(crypto->ackProofCompute(alice.pub, BOB, ALICE, REQUEST_ID, ROUTING, ROUTING_LEN, a));
    TEST_ASSERT_TRUE(crypto->ackProofCompute(alice.pub, BOB, ALICE, REQUEST_ID + 1, ROUTING, ROUTING_LEN, b));
    TEST_ASSERT_TRUE_MESSAGE(memcmp(a, b, ACK_PROOF_SIZE) != 0, "proof must differ per request_id");
}

// Without an identity there is nothing to prove with, and we must fail rather than emit a constant.
void test_proof_requires_private_key(void)
{
    const Identity peer = makeIdentity();
    uint8_t zero[32] = {0}, proof[ACK_PROOF_SIZE];
    crypto->setDHPrivateKey(zero);
    TEST_ASSERT_FALSE(crypto->ackProofCompute(peer.pub, BOB, ALICE, REQUEST_ID, ROUTING, ROUTING_LEN, proof));
}

// An all-zero peer key is a weak point; Curve25519::dh2 rejects it and so must we.
void test_proof_rejects_weak_peer_key(void)
{
    const Identity self = makeIdentity();
    becomeNode(self);
    uint8_t zeroPub[32] = {0}, proof[ACK_PROOF_SIZE];
    TEST_ASSERT_FALSE(crypto->ackProofCompute(zeroPub, BOB, ALICE, REQUEST_ID, ROUTING, ROUTING_LEN, proof));
}

// The proof rides as a protobuf field appended to the encoded Routing payload. A decoder still
// reads the ack correctly with it present - which held when ack_proof was an unknown field to this
// build and must keep holding now that it is generated, since older firmware sees it as unknown.
void test_wire_roundtrip_and_unknown_field_tolerance(void)
{
    const Identity alice = makeIdentity();
    const Identity bob = makeIdentity();

    becomeNode(bob);
    meshtastic_MeshPacket ack = makeAck(BOB, ALICE, REQUEST_ID);
    const pb_size_t bare = ack.decoded.payload.size;
    TEST_ASSERT_TRUE(ackProofAttachWithKey(&ack, alice.pub));

    // Encoded cost is the proof plus a tag and a length byte, and only on acks.
    TEST_ASSERT_EQUAL_MESSAGE(bare + ACK_PROOF_SIZE + 2, ack.decoded.payload.size, "proof must cost exactly 10 encoded bytes");

    uint8_t extracted[ACK_PROOF_SIZE], expected[ACK_PROOF_SIZE];
    TEST_ASSERT_TRUE(ackProofExtract(ack.decoded.payload.bytes, ack.decoded.payload.size, extracted));
    uint8_t bareRouting[64];
    memcpy(bareRouting, ack.decoded.payload.bytes, bare);
    TEST_ASSERT_TRUE(crypto->ackProofCompute(alice.pub, BOB, ALICE, REQUEST_ID, bareRouting, bare, expected));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, extracted, ACK_PROOF_SIZE);

    // Parsing the ack is unaffected by the proof - the case older firmware, which does not know the
    // field, will be in.
    meshtastic_Routing decoded = meshtastic_Routing_init_default;
    TEST_ASSERT_TRUE_MESSAGE(
        pb_decode_from_bytes(ack.decoded.payload.bytes, ack.decoded.payload.size, &meshtastic_Routing_msg, &decoded),
        "a decoder must still parse the Routing message with a proof present");
    TEST_ASSERT_EQUAL(meshtastic_Routing_error_reason_tag, decoded.which_variant);
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, decoded.error_reason);
}

// A bare ack (every current firmware) is ABSENT, not INVALID - this is what keeps the scheme
// deployable alongside nodes that know nothing about it.
void test_verify_verdicts(void)
{
    const Identity alice = makeIdentity();
    const Identity bob = makeIdentity();
    const Identity mallory = makeIdentity();

    becomeNode(bob);
    meshtastic_MeshPacket bare = makeAck(BOB, ALICE, REQUEST_ID);
    meshtastic_MeshPacket proven = makeAck(BOB, ALICE, REQUEST_ID);
    TEST_ASSERT_TRUE(ackProofAttachWithKey(&proven, alice.pub));

    becomeNode(alice);
    TEST_ASSERT_EQUAL(AckProofResult::ABSENT, ackProofVerifyWithKey(&bare, REQUEST_ID, bob.pub));
    TEST_ASSERT_EQUAL(AckProofResult::VALID, ackProofVerifyWithKey(&proven, REQUEST_ID, bob.pub));

    // Present but unjudgeable, because we hold no key for the acker.
    TEST_ASSERT_EQUAL(AckProofResult::NO_KEY, ackProofVerifyWithKey(&proven, REQUEST_ID, nullptr));

    // Right proof, wrong pending packet: retargeting is caught.
    TEST_ASSERT_EQUAL(AckProofResult::INVALID, ackProofVerifyWithKey(&proven, REQUEST_ID + 1, bob.pub));

    // Same ack attributed to a different signer's key.
    TEST_ASSERT_EQUAL(AckProofResult::INVALID, ackProofVerifyWithKey(&proven, REQUEST_ID, mallory.pub));
}

// A forger with the channel PSK can rewrite the ack, but cannot produce the proof.
void test_verify_rejects_tampered_proof(void)
{
    const Identity alice = makeIdentity();
    const Identity bob = makeIdentity();

    becomeNode(bob);
    meshtastic_MeshPacket ack = makeAck(BOB, ALICE, REQUEST_ID);
    TEST_ASSERT_TRUE(ackProofAttachWithKey(&ack, alice.pub));

    becomeNode(alice);
    TEST_ASSERT_EQUAL(AckProofResult::VALID, ackProofVerifyWithKey(&ack, REQUEST_ID, bob.pub));
    ack.decoded.payload.bytes[ack.decoded.payload.size - 1] ^= 0x01;
    TEST_ASSERT_EQUAL(AckProofResult::INVALID, ackProofVerifyWithKey(&ack, REQUEST_ID, bob.pub));
}

// A truncated or over-long field is malformed, not a proof - an honest sender emits exactly
// ACK_PROOF_SIZE bytes, so anything else is rejected outright rather than compared.
void test_malformed_proof_field_is_absent(void)
{
    meshtastic_MeshPacket ack = makeAck(BOB, ALICE, REQUEST_ID);
    uint8_t junk[ACK_PROOF_SIZE - 1];
    memset(junk, 0x5A, sizeof(junk));

    pb_ostream_t stream = pb_ostream_from_buffer(ack.decoded.payload.bytes + ack.decoded.payload.size,
                                                 sizeof(ack.decoded.payload.bytes) - ack.decoded.payload.size);
    TEST_ASSERT_TRUE(pb_encode_tag(&stream, PB_WT_STRING, ACK_PROOF_FIELD_NUMBER));
    TEST_ASSERT_TRUE(pb_encode_string(&stream, junk, sizeof(junk)));
    ack.decoded.payload.size += stream.bytes_written;

    uint8_t out[ACK_PROOF_SIZE];
    TEST_ASSERT_FALSE(ackProofExtract(ack.decoded.payload.bytes, ack.decoded.payload.size, out));
}

// Nothing is attached to packets that are not explicit acks, so no other traffic pays for this.
void test_attach_only_applies_to_routing_unicasts(void)
{
    const Identity peer = makeIdentity();
    const Identity self = makeIdentity();
    becomeNode(self);

    meshtastic_MeshPacket broadcast = makeAck(BOB, NODENUM_BROADCAST, REQUEST_ID);
    TEST_ASSERT_FALSE_MESSAGE(ackProofAttachWithKey(&broadcast, peer.pub), "broadcasts have no single peer to prove to");

    meshtastic_MeshPacket noRequestId = makeAck(BOB, ALICE, 0);
    TEST_ASSERT_FALSE_MESSAGE(ackProofAttachWithKey(&noRequestId, peer.pub), "nothing to bind without a request_id");

    meshtastic_MeshPacket text = makeAck(BOB, ALICE, REQUEST_ID);
    text.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_FALSE_MESSAGE(ackProofAttachWithKey(&text, peer.pub), "only ROUTING acks carry a proof");
}

// THE load-bearing property for multi-hop: a repeater that holds the channel key relays by
// decoding and re-encoding the Data message (perhapsDecode decodes in place, then perhapsEncode
// re-runs pb_encode on p->decoded for any packet not from us). That cycle strips unknown fields at
// the Data level - so the proof survives only because it is nested inside Data.payload, which is an
// opaque bytes field the decoder copies through verbatim. Both halves are asserted here: if anyone
// ever "simplifies" this by moving the proof up alongside xeddsa_signature, the second half fails.
void test_proof_survives_relay_reencode(void)
{
    const Identity alice = makeIdentity();
    const Identity bob = makeIdentity();

    becomeNode(bob);
    meshtastic_MeshPacket ack = makeAck(BOB, ALICE, REQUEST_ID);
    TEST_ASSERT_TRUE(ackProofAttachWithKey(&ack, alice.pub));

    // A field appended at the *Data* level, as a control for what a hop destroys.
    uint8_t control[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t ctrlBuf[16];
    pb_ostream_t ctrl = pb_ostream_from_buffer(ctrlBuf, sizeof(ctrlBuf));
    TEST_ASSERT_TRUE(pb_encode_tag(&ctrl, PB_WT_STRING, 101)); // not a field of meshtastic_Data
    TEST_ASSERT_TRUE(pb_encode_string(&ctrl, control, sizeof(control)));

    uint8_t wire[512];
    const size_t wireLen = pb_encode_to_bytes(wire, sizeof(wire), &meshtastic_Data_msg, &ack.decoded);
    TEST_ASSERT_GREATER_THAN(0, wireLen);
    memcpy(wire + wireLen, ctrlBuf, ctrl.bytes_written);
    const size_t withControl = wireLen + ctrl.bytes_written;

    // Exactly what a repeater does with what it received.
    meshtastic_MeshPacket relayed = meshtastic_MeshPacket_init_zero;
    relayed.from = BOB;
    relayed.to = ALICE;
    relayed.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(wire, withControl, &meshtastic_Data_msg, &relayed.decoded));
    uint8_t reencoded[512];
    const size_t reencodedLen = pb_encode_to_bytes(reencoded, sizeof(reencoded), &meshtastic_Data_msg, &relayed.decoded);
    TEST_ASSERT_GREATER_THAN(0, reencodedLen);

    // The Data-level field is gone after one hop...
    TEST_ASSERT_LESS_THAN_MESSAGE(withControl, reencodedLen, "a Data-level unknown field must NOT survive the relay re-encode");

    // ...but the proof, nested in the payload blob, is still there and still verifies downstream.
    meshtastic_MeshPacket downstream = meshtastic_MeshPacket_init_zero;
    downstream.from = BOB;
    downstream.to = ALICE;
    downstream.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(reencoded, reencodedLen, &meshtastic_Data_msg, &downstream.decoded));
    TEST_ASSERT_EQUAL(REQUEST_ID, downstream.decoded.request_id);

    becomeNode(alice);
    TEST_ASSERT_EQUAL_MESSAGE(AckProofResult::VALID, ackProofVerifyWithKey(&downstream, REQUEST_ID, bob.pub),
                              "proof must survive a repeater's decode/re-encode cycle");
}

// REGRESSION: an ack and a nak for the same packet share from/to/portnum/request_id and differ
// only in the Routing payload. Channel crypto is CTR with no MAC, so if the payload were not bound
// a PSK holder could bit-flip a proven "delivered" into a "failed" and the proof would still
// verify. The easy direction is success -> failure; manufacturing a fake success is much narrower,
// needing a captured proofed nak, and the naks that exist either never reach the air or come from
// a node that by definition holds no key.
void test_proof_binds_error_reason(void)
{
    const Identity alice = makeIdentity();
    const Identity bob = makeIdentity();

    becomeNode(bob);
    meshtastic_MeshPacket ack = makeAck(BOB, ALICE, REQUEST_ID);
    TEST_ASSERT_EQUAL_MESSAGE(0x18, ack.decoded.payload.bytes[0], "expected error_reason to be field 3, varint");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Routing_Error_NONE, ack.decoded.payload.bytes[1], "expected a success ack");
    TEST_ASSERT_TRUE(ackProofAttachWithKey(&ack, alice.pub));

    becomeNode(alice);
    TEST_ASSERT_EQUAL(AckProofResult::VALID, ackProofVerifyWithKey(&ack, REQUEST_ID, bob.pub));

    // Exactly what a CTR bit-flip buys the attacker: same proof bytes, different verdict.
    ack.decoded.payload.bytes[1] = meshtastic_Routing_Error_MAX_RETRANSMIT;
    TEST_ASSERT_EQUAL_MESSAGE(AckProofResult::INVALID, ackProofVerifyWithKey(&ack, REQUEST_ID, bob.pub),
                              "flipping a proven ack into a nak must invalidate the proof");
}

// X25519 is symmetric - DH(a_priv, B_pub) == DH(b_priv, A_pub) - so without the direction bound an
// A->B proof for a request_id would equal the B->A proof for it. Cheap insurance; this pins it.
void test_proof_binds_direction(void)
{
    const Identity alice = makeIdentity();
    const Identity bob = makeIdentity();

    becomeNode(bob);
    uint8_t forward[ACK_PROOF_SIZE], reverse[ACK_PROOF_SIZE];
    TEST_ASSERT_TRUE(crypto->ackProofCompute(alice.pub, BOB, ALICE, REQUEST_ID, ROUTING, ROUTING_LEN, forward));
    TEST_ASSERT_TRUE(crypto->ackProofCompute(alice.pub, ALICE, BOB, REQUEST_ID, ROUTING, ROUTING_LEN, reverse));
    TEST_ASSERT_TRUE_MESSAGE(memcmp(forward, reverse, ACK_PROOF_SIZE) != 0,
                             "A->B and B->A proofs must differ despite the symmetric shared secret");
}

// Two proof fields mean two possible excisions and so two possible verdicts. Refuse rather than
// silently pick one, since an attacker with the channel key can append a second field at will.
void test_duplicate_proof_field_is_rejected(void)
{
    const Identity alice = makeIdentity();
    const Identity bob = makeIdentity();

    becomeNode(bob);
    meshtastic_MeshPacket ack = makeAck(BOB, ALICE, REQUEST_ID);
    TEST_ASSERT_TRUE(ackProofAttachWithKey(&ack, alice.pub));

    uint8_t first[ACK_PROOF_SIZE];
    TEST_ASSERT_TRUE(ackProofExtract(ack.decoded.payload.bytes, ack.decoded.payload.size, first));

    // Append a second, byte-identical proof field.
    pb_ostream_t stream = pb_ostream_from_buffer(ack.decoded.payload.bytes + ack.decoded.payload.size,
                                                 sizeof(ack.decoded.payload.bytes) - ack.decoded.payload.size);
    TEST_ASSERT_TRUE(pb_encode_tag(&stream, PB_WT_STRING, ACK_PROOF_FIELD_NUMBER));
    TEST_ASSERT_TRUE(pb_encode_string(&stream, first, ACK_PROOF_SIZE));
    ack.decoded.payload.size += stream.bytes_written;

    uint8_t out[ACK_PROOF_SIZE];
    TEST_ASSERT_FALSE_MESSAGE(ackProofExtract(ack.decoded.payload.bytes, ack.decoded.payload.size, out),
                              "a message with two proof fields must be rejected as malformed");

    becomeNode(alice);
    TEST_ASSERT_EQUAL(AckProofResult::ABSENT, ackProofVerifyWithKey(&ack, REQUEST_ID, bob.pub));
}

// The proof must cover fields this build does not understand, so that a future Routing field
// outside the oneof is protected and old and new nodes still agree on the hashed bytes. Excision
// gives that for free; re-encoding from the decoded struct would silently drop them.
void test_proof_covers_unknown_routing_fields(void)
{
    const Identity alice = makeIdentity();
    const Identity bob = makeIdentity();

    becomeNode(bob);
    meshtastic_MeshPacket ack = makeAck(BOB, ALICE, REQUEST_ID);

    // A field this build's Routing schema does not define, as a newer sender would emit.
    const pb_size_t beforeUnknown = ack.decoded.payload.size;
    uint8_t extra[3] = {0x11, 0x22, 0x33};
    pb_ostream_t pre = pb_ostream_from_buffer(ack.decoded.payload.bytes + ack.decoded.payload.size,
                                              sizeof(ack.decoded.payload.bytes) - ack.decoded.payload.size);
    TEST_ASSERT_TRUE(pb_encode_tag(&pre, PB_WT_STRING, 9)); // not a field of meshtastic_Routing
    TEST_ASSERT_TRUE(pb_encode_string(&pre, extra, sizeof(extra)));
    ack.decoded.payload.size += pre.bytes_written;
    // Its contents start after the one-byte tag and one-byte length.
    const size_t unknownDataStart = beforeUnknown + 2;

    TEST_ASSERT_TRUE(ackProofAttachWithKey(&ack, alice.pub));

    becomeNode(alice);
    TEST_ASSERT_EQUAL(AckProofResult::VALID, ackProofVerifyWithKey(&ack, REQUEST_ID, bob.pub));

    // Tamper with the field's CONTENTS, not its framing: the message must stay parseable so that
    // the verdict is INVALID (the proof no longer matches) rather than ABSENT (no readable proof).
    ack.decoded.payload.bytes[unknownDataStart] ^= 0x01;
    TEST_ASSERT_EQUAL_MESSAGE(AckProofResult::INVALID, ackProofVerifyWithKey(&ack, REQUEST_ID, bob.pub),
                              "an unknown field must be covered by the proof");
}

void setup()
{
    initializeTestEnvironment();
    testEnsureCryptLock();
    UNITY_BEGIN();
    RUN_TEST(test_proof_is_pairwise_and_symmetric);
    RUN_TEST(test_proof_binds_request_id);
    RUN_TEST(test_proof_binds_error_reason);
    RUN_TEST(test_proof_binds_direction);
    RUN_TEST(test_proof_requires_private_key);
    RUN_TEST(test_proof_rejects_weak_peer_key);
    RUN_TEST(test_wire_roundtrip_and_unknown_field_tolerance);
    RUN_TEST(test_verify_verdicts);
    RUN_TEST(test_verify_rejects_tampered_proof);
    RUN_TEST(test_malformed_proof_field_is_absent);
    RUN_TEST(test_duplicate_proof_field_is_rejected);
    RUN_TEST(test_proof_covers_unknown_routing_fields);
    RUN_TEST(test_attach_only_applies_to_routing_unicasts);
    RUN_TEST(test_proof_survives_relay_reencode);
    exit(UNITY_END());
}

void loop() {}

#else // PKI excluded

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
