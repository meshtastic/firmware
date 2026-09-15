#include "configuration.h"

#if HAS_BLE_MESH

#include "BLEMeshHandler.h"
#include "main.h"

BLEMeshHandler *bleMeshHandler = nullptr;

// AD type constants, spelled locally so this file does not have to pick between the NimBLE and
// SoftDevice headers - the values are from the Bluetooth Core Supplement, not from either stack.
#define BLE_MESH_AD_TYPE_FLAGS 0x01
#define BLE_MESH_AD_TYPE_MFG_DATA 0xFF
#define BLE_MESH_AD_FLAGS_LE_GENERAL_DISC_BREDR_UNSUP 0x06

namespace
{
/**
 * The packet as it should go on the air: everything the far side is going to overwrite, removed.
 *
 * deliverToRouter() rewrites transport_mechanism, via_mqtt, tx_after, priority, pki_encrypted,
 * public_key, rx_snr and rx_rssi on every arrival, and Router::handleReceived stamps rx_time
 * (Router.cpp:1493). Every one of those is budget spent on bytes the receiver throws away, and this
 * bearer has 243 of them against LoRa's 239 of ciphertext. rx_rssi is the worst of them twice over:
 * a negative int32 is a ten-byte varint, and it publishes the relayer's own link quality.
 *
 * Keep this in step with the ingress guards. A field added to one belongs in the other, and the
 * native suite asserts the two agree.
 */
meshtastic_MeshPacket strippedForAir(const meshtastic_MeshPacket &mp)
{
    meshtastic_MeshPacket out = mp;
    out.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL;
    out.via_mqtt = false;
    out.tx_after = 0;
    out.priority = meshtastic_MeshPacket_Priority_UNSET;
    out.pki_encrypted = false;
    out.public_key.size = 0;
    out.rx_snr = 0;
    out.rx_rssi = 0;
    out.has_rx_rssi = false;
    out.rx_time = 0;
    out.has_rx_time = false;
    return out;
}
} // namespace

uint8_t BLEMeshHandler::buildAdvPayload(const meshtastic_MeshPacket *mp, uint8_t *out, size_t outCap)
{
    // Router::send() encrypts before it reaches any transport, so an unencrypted packet here is a
    // bug upstream, not something to quietly put on the air.
    if (mp->which_payload_variant != meshtastic_MeshPacket_encrypted_tag) {
        LOG_WARN("BLE mesh: refusing to broadcast an unencrypted packet 0x%08x", mp->id);
        return 0;
    }
    if (mp->from == 0) {
        LOG_WARN("BLE mesh: refusing to broadcast a packet with no sender");
        return 0;
    }

    const meshtastic_MeshPacket air = strippedForAir(*mp);

    // Sized before encoding rather than inferred from a short buffer, because pb_encode_to_bytes
    // returns 0 for a genuine encode failure and for an over-budget packet alike - and the two want
    // different answers. Over-budget is routine and countable; an encode failure is a bug.
    size_t needed = 0;
    if (!pb_get_encoded_size(&needed, &meshtastic_MeshPacket_msg, &air)) {
        LOG_ERROR("BLE mesh: cannot size packet 0x%08x", mp->id);
        return 0;
    }
    if (needed > BLE_MESH_MAX_PROTO_LEN) {
        // It still goes out over LoRa; Router::send handed it to the radio before reaching us.
        txDroppedTooLarge++;
        LOG_WARN("BLE mesh: drop 0x%08x, %u bytes over the %u-byte advertisement budget", mp->id,
                 (unsigned)(needed - BLE_MESH_MAX_PROTO_LEN), (unsigned)BLE_MESH_MAX_PROTO_LEN);
        return 0;
    }

    uint8_t proto[BLE_MESH_MAX_PROTO_LEN];
    size_t protoLen = pb_encode_to_bytes(proto, sizeof(proto), &meshtastic_MeshPacket_msg, &air);
    if (protoLen == 0) {
        LOG_ERROR("BLE mesh: encode failed for 0x%08x at %u bytes", mp->id, (unsigned)needed);
        return 0;
    }

    const size_t total = BLE_MESH_ADV_OVERHEAD + protoLen;
    if (total > outCap || total > BLE_MESH_ADV_TOTAL_MAX) {
        txDroppedTooLarge++;
        return 0;
    }

    uint8_t *p = out;
    // Flags AD structure.
    *p++ = 2;
    *p++ = BLE_MESH_AD_TYPE_FLAGS;
    *p++ = BLE_MESH_AD_FLAGS_LE_GENERAL_DISC_BREDR_UNSUP;

    // Manufacturer-specific data AD structure: length covers everything after the length byte.
    *p++ = (uint8_t)(1 /* type */ + 2 /* company */ + 1 /* version */ + protoLen);
    *p++ = BLE_MESH_AD_TYPE_MFG_DATA;
    *p++ = (uint8_t)(BLE_MESH_COMPANY_ID & 0xFF);
    *p++ = (uint8_t)((BLE_MESH_COMPANY_ID >> 8) & 0xFF);
    *p++ = BLE_MESH_PROTOCOL_VERSION;

    memcpy(p, proto, protoLen);
    p += protoLen;

    return (uint8_t)(p - out);
}

bool BLEMeshHandler::onSend(const meshtastic_MeshPacket *mp)
{
    if (!isRunning || !mp)
        return false;

    // Deliberately NOT the guard UdpMulticastHandler carries. A packet that arrived over BLE and
    // comes back through Router::send is a rebroadcast: NextHopRouter::perhapsRebroadcast allocCopy()s
    // the received packet, and nothing on the TX path rewrites transport_mechanism (RadioInterface
    // stamps TRANSPORT_LORA in deliverToReceiver, which is RX-only). Refusing it caps the BLE mesh at
    // a single hop. Loop protection is the same as LoRa's: PacketHistory drops a packet seen
    // recently, hop_limit decrements per relay, and deliverToRouter ignores frames sent by us.
    if (mp->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV)
        LOG_DEBUG("BLE mesh: re-advertising relayed packet 0x%08x", mp->id);

    AdvSlot slot;
    slot.len = buildAdvPayload(mp, slot.data.data(), slot.data.size());
    if (slot.len == 0)
        return false;
    // mp->from, not getFrom(mp): buildAdvPayload has already refused from == 0, and this has to be
    // the same key perhapsCancelDupe cancels with.
    slot.from = mp->from;
    slot.id = mp->id;

    if (txCount >= BLE_MESH_TX_QUEUE_SIZE) {
        LOG_WARN("BLE mesh: TX queue full, dropping 0x%08x", mp->id);
        return false;
    }
    txQueue[txTail] = slot;
    txTail = (txTail + 1) % BLE_MESH_TX_QUEUE_SIZE;
    txCount++;

    setIntervalFromNow(0);
    concurrency::mainDelay.interrupt();
    return true;
}

int32_t BLEMeshHandler::runOnce()
{
    if (!isRunning || !platformReady())
        return 500;

    if (!readyHandled) {
        readyHandled = true;
        onBluetoothReady();
    }

    if (advertising) {
        if (platformAdvertisingActive())
            return 10;
        platformEndAdvertising();
        advertising = false;
    }

    if (txCount == 0)
        return 100;
    AdvSlot slot = txQueue[txHead];
    txHead = (txHead + 1) % BLE_MESH_TX_QUEUE_SIZE;
    txCount--;

    if (platformBeginAdvertising(slot.data.data(), slot.len)) {
        advertising = true;
        advertisingFrom = slot.from;
        advertisingId = slot.id;
    }

    return 10;
}

bool BLEMeshHandler::onCancelSending(meshtastic_MeshPacket_TransportMechanism medium, NodeNum from, PacketId id)
{
    // A duplicate overheard on another medium says nothing about who heard this one.
    if (medium != meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV)
        return false;

    bool canceled = false;

    // Compact the ring in place, keeping order. A cancel is rare and the ring is eight deep, so a
    // copy costs less than threading a tombstone through runOnce().
    size_t kept = 0;
    for (size_t i = 0; i < txCount; i++) {
        const AdvSlot &slot = txQueue[(txHead + i) % BLE_MESH_TX_QUEUE_SIZE];
        if (slot.from == from && slot.id == id) {
            canceled = true;
            continue;
        }
        if (kept != i)
            txQueue[(txHead + kept) % BLE_MESH_TX_QUEUE_SIZE] = slot;
        kept++;
    }
    txCount = kept;
    txTail = (txHead + kept) % BLE_MESH_TX_QUEUE_SIZE;

    // Cut a burst already on air short too. Extended advertising repeats one payload for
    // BLE_MESH_ADV_EVENTS events, so the copies still to come are exactly what the overhear says are
    // unnecessary. runOnce() pops before it advertises, so this frame is no longer in the ring.
    if (advertising && advertisingFrom == from && advertisingId == id) {
        platformEndAdvertising();
        advertising = false;
        advertisingFrom = 0;
        advertisingId = 0;
        canceled = true;
    }

    if (canceled)
        LOG_DEBUG("BLE mesh: canceling our copy of 0x%08x, a neighbour relayed it", id);
    return canceled;
}

void BLEMeshHandler::deliverToRouter(const uint8_t *data, size_t len, int8_t rssi)
{
    if (!isRunning || !nodeDB || !data)
        return;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    if (!pb_decode_from_bytes(data, len, &meshtastic_MeshPacket_msg, &mp))
        return;
    if (mp.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
        return;

    // Guard 1 (mirrors UdpMulticastHandler): spoofed local origin. Nothing legitimate advertises
    // from=0, and our own advertisement echoing back into our own scanner would loop.
    if (mp.from == 0) {
        LOG_WARN("BLE mesh: advertisement with no sender, dropping");
        return;
    }
    if (mp.from == nodeDB->getNodeNum())
        return; // our own advertisement, heard by our own scanner

    // Guard 2 (mirrors UdpMulticastHandler): an out-of-range hop count is not relayable.
    if (mp.hop_limit > HOP_MAX || mp.hop_start > HOP_MAX) {
        LOG_WARN("BLE mesh: invalid hop_limit(%u)/hop_start(%u), dropping", mp.hop_limit, mp.hop_start);
        return;
    }

    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV;
    // Wire-carried flags that only the local stack may set: a sender must not suppress our MQTT uplink
    // or schedule our transmit.
    mp.via_mqtt = false;
    mp.tx_after = 0;
    // priority is local-only too, and unlike want_ack/next_hop/relay_node it is NOT a field the LoRa
    // header carries - so fixPriority() always derives it locally for a LoRa arrival, and this bearer
    // is the first that lets a sender choose it. Left as sent, a crafted frame with priority MAX
    // outranks ACK (the ceiling fixPriority assigns) and, once perhapsRebroadcast copies it into the
    // TX queue, replaceLowerPriorityPacket evicts one of ours to make room for it.
    mp.priority = meshtastic_MeshPacket_Priority_UNSET;

    // Guard 3 (mirrors UdpMulticastHandler): authentication metadata is local-only. The Router
    // re-establishes it after a successful PKI decrypt; carrying it in from the wire would let a
    // sender assert its own packet was PKI-authenticated.
    mp.pki_encrypted = false;
    mp.public_key.size = 0;
    memset(mp.public_key.bytes, 0, sizeof(mp.public_key.bytes));

    // Guard 4: no LoRa measurement exists for a BLE arrival. Unlike the UDP case there IS a real
    // measurement of this hop, so rx_rssi is populated and has_rx_rssi set rather than cleared.
    mp.rx_snr = 0;
    mp.rx_rssi = rssi;
    mp.has_rx_rssi = true;

    UniquePacketPoolPacket p = packetPool.allocUniqueCopy(mp);
    if (!p)
        return;

    LOG_DEBUG("BLE mesh RX from=0x%08x to=0x%08x id=0x%08x rssi=%d len=%u", mp.from, mp.to, mp.id, rssi, (unsigned)len);
    enqueueReceived(p.release());
}

void BLEMeshHandler::enqueueReceived(meshtastic_MeshPacket *p)
{
    router->enqueueReceivedMessage(p);
}

#endif // HAS_BLE_MESH
