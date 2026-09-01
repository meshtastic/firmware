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

    uint8_t proto[BLE_MESH_MAX_PROTO_LEN];
    size_t protoLen = pb_encode_to_bytes(proto, sizeof(proto), &meshtastic_MeshPacket_msg, mp);
    if (protoLen == 0) {
        // pb_encode_to_bytes returns 0 both for a genuine encode failure and for a packet that does
        // not fit the buffer. Either way it cannot ride BLE; it still goes out over LoRa.
        LOG_WARN("BLE mesh: drop 0x%08x, does not fit %u-byte advertisement budget", mp->id, (unsigned)BLE_MESH_MAX_PROTO_LEN);
        return 0;
    }

    const size_t total = BLE_MESH_ADV_OVERHEAD + protoLen;
    if (total > outCap || total > BLE_MESH_ADV_TOTAL_MAX)
        return 0;

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

    if (platformBeginAdvertising(slot.data.data(), slot.len))
        advertising = true;

    return 10;
}

void BLEMeshHandler::deliverToRouter(const uint8_t *data, size_t len, int8_t rssi)
{
    if (!isRunning || !router || !nodeDB || !data)
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
    router->enqueueReceivedMessage(p.release());
}

#endif // HAS_BLE_MESH
