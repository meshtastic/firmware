#pragma once

#include "configuration.h"
#include "mesh/CryptoEngine.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#if !(MESHTASTIC_EXCLUDE_PKI)

/**
 * Pairwise ACK proof: an authenticated delivery receipt from the actual recipient.
 *
 * Explicit acks are ROUTING_APP packets, and ROUTING_APP is excluded from PKC, so an ack is
 * protected by nothing but the channel PSK - public on the default channel. Anyone in range can
 * forge one, and the client grants its strongest delivery claim ("delivered to recipient") on the
 * strength of the ack's unauthenticated `from`.
 *
 * Where the acknowledged packet was PKI-encrypted the two endpoints already share a Curve25519
 * secret, so the recipient can prove receipt with a short MAC:
 *
 *     proof = HMAC-SHA256( sharedKey,
 *                          "ack" | LE32(from) | LE32(to) | LE32(request_id) | routing )
 *             [0 .. ACK_PROOF_SIZE)
 *
 * where `routing` is this encoded Routing message without the ack_proof field. See
 * CryptoEngine::ackProofCompute for why each of those is bound.
 *
 * Properties, and the honest limits:
 *
 *  - ~10 encoded bytes instead of 66, and one HMAC instead of Ed25519 sign+verify.
 *  - Nothing is added to the original message: the secret is derived, not transmitted. (Shipping a
 *    nonce in the payload would also work, but costs bytes on every DM and gains nothing here.)
 *  - request_id is an input, so a captured proof cannot be retargeted at another pending packet,
 *    and the Routing bytes are an input, so a proven ack cannot be bit-flipped into a nak.
 *  - The ack stays channel-encrypted, so relays keep reading request_id for next-hop learning and
 *    retransmission cancel. Authentication without confidentiality is the point.
 *  - This does NOT protect the retransmission loop, and must not be described as if it does.
 *    ReliableRouter::perhapsGenerateImplicitAckForOwnOverheard clears a pending retransmission on
 *    any overheard rebroadcast of our own (from, id) - header-only, no key, and it fires even on a
 *    PKI DM we cannot read. Replaying the originator's own ciphertext stops their retries more
 *    cheaply than forging an ack, so no ack authentication of any kind closes that path. The one
 *    property on offer here is the receipt.
 *  - Only the original sender can verify. A relay cannot, so a forged ack still propagates and
 *    still cancels intermediates' retransmissions; the endpoint just stops believing it.
 *  - "From the actual recipient" is only true because the caller checks it. The MAC proves the
 *    sender holds a pairwise key with us, and every keyed peer holds one - so the verifier must
 *    look up the key of the node it ADDRESSED, not the node the ack claims to be from, or a proof
 *    minted by any other keyed peer reads as VALID. ReliableRouter::ackProofPermitsAction does
 *    that check.
 *  - What gates a proof is whether both endpoints hold PKI keys, NOT how the acked packet was
 *    encrypted. isProvableAck() tests only the ack's shape, so a DM that travelled under channel
 *    encryption still gets a proven ack when we hold the peer's key - the secret comes from X25519,
 *    not from the channel. That is more coverage than the receipt strictly needs, and it costs one
 *    X25519 on every such ack we generate.
 *  - A secret that rode under channel encryption would be worthless, since the forger holds the PSK
 *    too. This derives the secret instead, which is why channel-encrypted DMs can be covered at all.
 *  - PKI_UNKNOWN_PUBKEY and NO_CHANNEL naks are emitted precisely when we could not decrypt, so no
 *    shared secret exists and they can never carry a proof. They stay forgeable.
 *  - Verification costs one X25519 (there is no shared-secret cache), and an attacker chooses when
 *    we pay it. Callers MUST gate on cheap checks first - see ReliableRouter::ackProofPermitsAction,
 *    which will not verify unless we actually have a matching packet outstanding.
 */

// The generated field (meshtastic/protobufs#1094). ACK_PROOF_SIZE comes from CryptoEngine.h, which
// derives it from the generated type so the protocol owns the number.
#define ACK_PROOF_FIELD_NUMBER meshtastic_Routing_ack_proof_tag

// Advisory by design, and this is not a placeholder for later enforcement. A rule that requires a
// proof once a peer has sent one would make a missing proof destroy the only delivery signal we
// have, on state the user cannot see: the proof needs the PEER to hold OUR key, and peer-side
// NodeDB eviction, a firmware downgrade or a factory reset are all invisible to us and would
// silently kill acks forever. A valid proof marks the ack verified; anything else behaves exactly
// as today and is reported unverified, and the client renders the difference.
static constexpr bool ACK_PROOF_ENFORCE = false;

enum class AckProofResult {
    ABSENT,  // no proof attached - today's behavior, and every peer we have no pairwise key with
    VALID,   // present and correct
    INVALID, // present and wrong - a forgery, or a key/identity mismatch
    NO_KEY,  // present, but we hold no authoritative key for the peer, so we cannot judge it
};

/**
 * Attach a proof to a freshly built ROUTING ack/nak. No-op unless the packet is a ROUTING unicast
 * with a request_id and we hold an authoritative public key for the destination.
 */
void ackProofAttach(meshtastic_MeshPacket *p);

/** Check the proof on a received ack/nak against the id of the packet it claims to acknowledge. */
AckProofResult ackProofVerify(const meshtastic_MeshPacket *p, uint32_t requestId);

// Mechanism, split out from the NodeDB-backed policy above so both halves are unit-testable
// without standing up a node database.
bool ackProofAttachWithKey(meshtastic_MeshPacket *p, const uint8_t *peerPubKey);
AckProofResult ackProofVerifyWithKey(const meshtastic_MeshPacket *p, uint32_t requestId, const uint8_t *peerPubKey);

/**
 * Read the proof out of an encoded Routing payload. False if the field is absent or malformed,
 * which includes a message carrying more than one of them. fieldStart/fieldLen (optional) report
 * the byte range it occupies, which the verifier removes to rebuild the bytes the sender hashed.
 */
bool ackProofExtract(const uint8_t *payload, size_t payloadLen, uint8_t *proofOut, size_t *fieldStart = nullptr,
                     size_t *fieldLen = nullptr);

#endif
