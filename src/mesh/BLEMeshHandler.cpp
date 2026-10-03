#include "configuration.h"

#if HAS_BLE_MESH

#include "BLEMeshHandler.h"
#include "main.h"

BLEMeshHandler *bleMeshHandler = nullptr;

// AD type constants from the Bluetooth Core Supplement, spelled locally so this file need not pick
// between the NimBLE and SoftDevice headers.
#define BLE_MESH_AD_TYPE_FLAGS 0x01
#define BLE_MESH_AD_TYPE_MFG_DATA 0xFF
#define BLE_MESH_AD_FLAGS_LE_GENERAL_DISC_BREDR_UNSUP 0x06

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

    // Every field the receiver overwrites costs budget for bytes the far side discards.
    const meshtastic_MeshPacket air = stripForTransmit(*mp);

    // Sized before encoding: pb_encode_to_bytes returns 0 for an encode failure and for an
    // over-budget packet alike, and only one of those is a bug.
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
    *p++ = 2;
    *p++ = BLE_MESH_AD_TYPE_FLAGS;
    *p++ = BLE_MESH_AD_FLAGS_LE_GENERAL_DISC_BREDR_UNSUP;

    // Manufacturer-specific data: the length byte covers everything after itself.
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

    // Deliberately NOT the "arrived on this medium" guard UdpMulticastHandler carries: a rebroadcast
    // still carries TRANSPORT_BLE_ADV, because nothing on the TX path rewrites transport_mechanism,
    // so refusing it would cap the BLE mesh at one hop. Loop protection is LoRa's: PacketHistory,
    // hop_limit, and deliverToRouter ignoring frames this node sent.
    if (mp->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV)
        LOG_DEBUG("BLE mesh: re-advertising relayed packet 0x%08x", mp->id);

    AdvSlot slot;
    slot.len = buildAdvPayload(mp, slot.data.data(), slot.data.size());
    if (slot.len == 0)
        return false;
    // mp->from, not getFrom(mp): the same key perhapsCancelDupe cancels with.
    slot.from = mp->from;
    slot.id = mp->id;

    slot.priority = (uint8_t)mp->priority;

    if (txCount >= BLE_MESH_TX_QUEUE_SIZE) {
        // Displace only something strictly less important, as
        // MeshPacketQueue::replaceLowerPriorityPacket does for LoRa.
        const size_t worst = lowestPrioritySlot();
        if (txQueue[worst].priority >= slot.priority) {
            txDroppedQueueFull++;
            LOG_WARN("BLE mesh: TX queue full of priority >= %u, dropping 0x%08x", (unsigned)slot.priority, mp->id);
            return false;
        }
        LOG_WARN("BLE mesh: dropping queued 0x%08x (priority %u) for 0x%08x (priority %u)", txQueue[worst].id,
                 (unsigned)txQueue[worst].priority, mp->id, (unsigned)slot.priority);
        txDroppedQueueFull++;
        removeSlot(worst);
    }
    txQueue[txCount++] = slot;

    setIntervalFromNow(0);
    concurrency::mainDelay.interrupt();
    return true;
}

/// Highest priority, oldest first within a priority.
size_t BLEMeshHandler::highestPrioritySlot() const
{
    size_t best = 0;
    for (size_t i = 1; i < txCount; i++) {
        if (txQueue[i].priority > txQueue[best].priority)
            best = i;
    }
    return best;
}

/// Lowest priority, newest first, so a frame that has already waited is not the one displaced.
size_t BLEMeshHandler::lowestPrioritySlot() const
{
    size_t worst = 0;
    for (size_t i = 1; i < txCount; i++) {
        if (txQueue[i].priority <= txQueue[worst].priority)
            worst = i;
    }
    return worst;
}

void BLEMeshHandler::removeSlot(size_t index)
{
    for (size_t i = index + 1; i < txCount; i++)
        txQueue[i - 1] = txQueue[i];
    txCount--;
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
    const size_t next = highestPrioritySlot();
    AdvSlot slot = txQueue[next];
    removeSlot(next);

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

    // Arrival order is kept, so equal priorities still leave oldest-first.
    size_t kept = 0;
    for (size_t i = 0; i < txCount; i++) {
        if (txQueue[i].from == from && txQueue[i].id == id) {
            canceled = true;
            continue;
        }
        if (kept != i)
            txQueue[kept] = txQueue[i];
        kept++;
    }
    txCount = kept;

    // runOnce() pops before it advertises, so a frame already on air is no longer in the queue.
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

    // Our own advertisement, heard by our own scanner: dropped below too, but quietly here.
    if (mp.from == nodeDB->getNodeNum())
        return;
    if (!sanitizeIngress(mp, meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV))
        return;
    // No LoRa measurement exists for a BLE arrival, but the BLE hop itself is measured.
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
