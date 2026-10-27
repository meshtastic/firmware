#include "AckProof.h"

#if !(MESHTASTIC_EXCLUDE_PKI)

#include "CryptoEngine.h"
#include "NodeDB.h"
#include "concurrency/LockGuard.h"
#include <pb_decode.h>
#include <pb_encode.h>

/** Is this a packet shaped like an explicit ack/nak we could attach a proof to? */
static bool isProvableAck(const meshtastic_MeshPacket *p)
{
    return p && p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
           p->decoded.portnum == meshtastic_PortNum_ROUTING_APP && p->decoded.request_id != 0 && !isBroadcast(p->to);
}

bool ackProofExtract(const uint8_t *payload, size_t payloadLen, uint8_t *proofOut, size_t *fieldStart, size_t *fieldLen)
{
    // Walk the encoded Routing message with the real parser rather than assuming the proof is the
    // trailing field: it is only last today because nanopb emits in field-number order and 4 is the
    // highest tag, which stops being true the moment Routing gains a field 5.
    pb_istream_t stream = pb_istream_from_buffer(payload, payloadLen);
    uint32_t tag;
    pb_wire_type_t wireType;
    bool eof = false;
    bool found = false;

    while (true) {
        const size_t tagStart = payloadLen - stream.bytes_left;
        // pb_decode_tag returns false at a CLEAN end of stream, with eof set - so eof has to be
        // checked before treating the false as a parse error, or a well-formed message reads as
        // malformed. This mirrors nanopb's own decode loop (pb_decode.c, pb_decode_inner).
        if (!pb_decode_tag(&stream, &wireType, &tag, &eof)) {
            if (eof)
                break;
            return false;
        }

        if (tag == ACK_PROOF_FIELD_NUMBER && wireType == PB_WT_STRING) {
            // Two proofs mean two possible excisions and so two possible verdicts. Refuse rather
            // than pick one.
            if (found)
                return false;

            pb_istream_t substream;
            if (!pb_make_string_substream(&stream, &substream))
                return false;
            // A wrong length is malformed, not a near miss - an honest sender emits exactly this.
            if (substream.bytes_left != ACK_PROOF_SIZE || !pb_read(&substream, proofOut, ACK_PROOF_SIZE))
                return false;
            if (!pb_close_string_substream(&stream, &substream))
                return false;

            found = true;
            if (fieldStart)
                *fieldStart = tagStart;
            if (fieldLen)
                *fieldLen = (payloadLen - stream.bytes_left) - tagStart;
            continue;
        }

        if (!pb_skip_field(&stream, wireType))
            return false;
    }
    return found;
}

bool ackProofAttachWithKey(meshtastic_MeshPacket *p, const uint8_t *peerPubKey)
{
    if (!isProvableAck(p))
        return false;

    uint8_t proof[ACK_PROOF_SIZE];
    {
        // ackProofCompute clobbers the engine's shared_key. cryptLock is not held on the ack
        // allocation path (perhapsEncode takes it later, further down the send), so take it here.
        concurrency::LockGuard g(cryptLock);
        // The payload as it stands IS this message without the ack_proof field - it has not been
        // appended yet - so the sender never has to reconstruct anything.
        if (!crypto->ackProofCompute(peerPubKey, getFrom(p), p->to, p->decoded.request_id, p->decoded.payload.bytes,
                                     p->decoded.payload.size, proof))
            return false;
    }

    // Append the field rather than setting it on a decoded struct and re-encoding. A re-encode
    // would drop any field this build does not know, and the receiver derives the MAC input by
    // excising ack_proof from the bytes as received - so the two would disagree the first time a
    // newer sender includes something we cannot parse. Appending leaves every other byte untouched.
    pb_ostream_t stream = pb_ostream_from_buffer(p->decoded.payload.bytes + p->decoded.payload.size,
                                                 sizeof(p->decoded.payload.bytes) - p->decoded.payload.size);
    if (!pb_encode_tag(&stream, PB_WT_STRING, ACK_PROOF_FIELD_NUMBER) || !pb_encode_string(&stream, proof, ACK_PROOF_SIZE))
        return false; // out of room: send the ack unproven rather than truncating it

    p->decoded.payload.size += stream.bytes_written;
    return true;
}

AckProofResult ackProofVerifyWithKey(const meshtastic_MeshPacket *p, uint32_t requestId, const uint8_t *peerPubKey)
{
    if (!p || p->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return AckProofResult::ABSENT;

    uint8_t claimed[ACK_PROOF_SIZE];
    size_t fieldStart = 0, fieldLen = 0;
    if (!ackProofExtract(p->decoded.payload.bytes, p->decoded.payload.size, claimed, &fieldStart, &fieldLen))
        return AckProofResult::ABSENT;
    if (!peerPubKey)
        return AckProofResult::NO_KEY;

    // This encoded Routing message without the ack_proof field: the received bytes with that one
    // range removed. Never re-encoded, so the value depends only on what arrived - a re-encode
    // would make it depend on this build's encoder agreeing with the sender's, and would silently
    // drop any field this build does not know about.
    uint8_t routing[sizeof(p->decoded.payload.bytes)];
    const size_t tail = p->decoded.payload.size - (fieldStart + fieldLen);
    memcpy(routing, p->decoded.payload.bytes, fieldStart);
    memcpy(routing + fieldStart, p->decoded.payload.bytes + fieldStart + fieldLen, tail);
    const size_t routingLen = fieldStart + tail;

    uint8_t expected[ACK_PROOF_SIZE];
    {
        concurrency::LockGuard g(cryptLock);
        if (!crypto->ackProofCompute(peerPubKey, getFrom(p), p->to, requestId, routing, routingLen, expected))
            return AckProofResult::NO_KEY;
    }

    // memcmp is fine here: both sides are already public once transmitted, and an attacker gets no
    // timing oracle they could not get by simply sending a guess.
    return memcmp(claimed, expected, ACK_PROOF_SIZE) == 0 ? AckProofResult::VALID : AckProofResult::INVALID;
}

void ackProofAttach(meshtastic_MeshPacket *p)
{
    if (!isProvableAck(p))
        return;

    // Authoritative keys only. Proving against an opportunistically cached key would let a planted
    // key validate its own acks - the same trust loop the decrypt path already closes.
    meshtastic_NodeInfoLite_public_key_t peerKey = {0, {0}};
    if (!nodeDB->copyPublicKeyAuthoritative(p->to, peerKey) || peerKey.size != 32)
        return;

    if (ackProofAttachWithKey(p, peerKey.bytes))
        LOG_DEBUG("Attached ACK proof for 0x%08x to 0x%08x", p->decoded.request_id, p->to);
}

AckProofResult ackProofVerify(const meshtastic_MeshPacket *p, uint32_t requestId)
{
    meshtastic_NodeInfoLite_public_key_t peerKey = {0, {0}};
    const bool haveKey = nodeDB->copyPublicKeyAuthoritative(getFrom(p), peerKey) && peerKey.size == 32;
    return ackProofVerifyWithKey(p, requestId, haveKey ? peerKey.bytes : nullptr);
}

#endif
