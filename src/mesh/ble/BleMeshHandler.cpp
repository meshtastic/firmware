#include "configuration.h"
#if HAS_BLE_MESH

#include "BleMeshHandler.h"
#include "main.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "mesh/mesh-pb-constants.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/hci_common.h" // BLE_HCI_ADV_DATA_STATUS_COMPLETE
#include "os/os_mbuf.h"

static_assert(BLE_MESH_SINGLE_PDU_BUDGET == BLE_HCI_MAX_EXT_ADV_DATA_LEN,
              "single-PDU budget must track NimBLE's unfragmented ext-adv data limit");

BleMeshHandler *bleMeshHandler = nullptr;

/// NimBLE hands us both advertising-instance completions and scan reports through the same
/// signature, so one trampoline covers both and dispatches on event type.
static int bleMeshGapEvent(struct ble_gap_event *event, void *arg)
{
    auto *self = static_cast<BleMeshHandler *>(arg);
    if (!self)
        return 0;

    switch (event->type) {
    case BLE_GAP_EVENT_EXT_DISC:
        // data_status tells us whether this report is the whole advertisement. We never chain on
        // send, so anything INCOMPLETE came from some other advertiser and cannot be one of ours.
        if (event->ext_disc.data_status == BLE_HCI_ADV_DATA_STATUS_COMPLETE)
            self->onScanReport(event->ext_disc.data, event->ext_disc.length_data, event->ext_disc.rssi);
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        // The bounded burst for the current frame finished; runOnce will clock in the next one.
        break;

    default:
        break;
    }
    return 0;
}

BleMeshHandler::BleMeshHandler() : concurrency::OSThread("BleMesh") {}

void BleMeshHandler::start()
{
    if (isRunning) {
        LOG_DEBUG("BLE mesh transport already running");
        return;
    }
    if (!ble_hs_synced()) {
        LOG_DEBUG("BLE host not synced yet, deferring BLE mesh start");
        return;
    }
    if (!configureAdvInstance())
        return;
    if (!startScanning())
        return;

    isRunning = true;
    LOG_INFO("BLE mesh transport listening (instance %d, max frame %d bytes)", BLE_MESH_ADV_INSTANCE, BLE_MESH_MAX_FRAME_LEN);
}

void BleMeshHandler::stop()
{
    if (!isRunning)
        return;
    LOG_DEBUG("Stopping BLE mesh transport");
    stopAdvertising();
    ble_gap_disc_cancel();
    ble_gap_ext_adv_remove(BLE_MESH_ADV_INSTANCE);
    isRunning = false;
}

bool BleMeshHandler::configureAdvInstance()
{
    struct ble_gap_ext_adv_params params = {};

    // Non-connectable, non-scannable, non-legacy: a pure broadcast. legacy_pdu must stay 0 or we
    // are back to the 31-byte limit, which cannot hold a mesh frame at all.
    params.connectable = 0;
    params.scannable = 0;
    params.directed = 0;
    params.legacy_pdu = 0;
    params.anonymous = 0;
    params.include_tx_power = 0;

    params.itvl_min = BLE_MESH_ADV_MIN_ITVL;
    params.itvl_max = BLE_MESH_ADV_MAX_ITVL;
    params.own_addr_type = BLE_OWN_ADDR_PUBLIC;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_1M;
    params.tx_power = 127; // let the controller pick its maximum
    params.sid = 0;

    int rc = ble_gap_ext_adv_configure(BLE_MESH_ADV_INSTANCE, &params, NULL, bleMeshGapEvent, this);
    if (rc != 0) {
        LOG_ERROR("ble_gap_ext_adv_configure failed rc=%d", rc);
        return false;
    }
    return true;
}

bool BleMeshHandler::startScanning()
{
    struct ble_gap_ext_disc_params uncoded = {};
    // Scan continuously. A duty cycle below 100% drops frames outright: unlike LoRa there is no
    // retransmit-until-heard here, only the fixed BLE_MESH_ADV_EVENTS repeats the sender emits.
    uncoded.itvl = 0x0060; // 60 ms
    uncoded.window = 0x0060;
    uncoded.passive = 1; // never scan-request; the payload is all in the advertisement

    // filter_duplicates MUST be 0. The controller de-duplicates on advertiser address, not on
    // payload, so enabling it would deliver one report per neighbour and then go silent - every
    // subsequent mesh frame from that node would be filtered as a "duplicate" advertisement.
    int rc = ble_gap_ext_disc(BLE_OWN_ADDR_PUBLIC, 0 /* duration: forever */, 0 /* period */, 0 /* filter_duplicates */,
                              BLE_HCI_SCAN_FILT_NO_WL, 0 /* limited */, &uncoded, NULL, bleMeshGapEvent, this);
    if (rc != 0) {
        LOG_ERROR("ble_gap_ext_disc failed rc=%d", rc);
        return false;
    }
    return true;
}

uint8_t BleMeshHandler::encodeAdvPayload(const meshtastic_MeshPacket *mp, uint8_t *out, size_t outCap)
{
    // Mirrors RadioInterface::beginSending - the frame this puts on air is byte-identical to the
    // one the LoRa path would build for the same packet.
    if (mp->which_payload_variant != meshtastic_MeshPacket_encrypted_tag) {
        LOG_WARN("BLE mesh: refusing to broadcast an unencrypted packet 0x%08x", mp->id);
        return 0;
    }
    if (mp->from == 0) {
        LOG_WARN("BLE mesh: refusing to broadcast a packet with no sender");
        return 0;
    }

    const size_t frameLen = sizeof(PacketHeader) + mp->encrypted.size;
    if (frameLen > BLE_MESH_MAX_FRAME_LEN) {
        LOG_WARN("BLE mesh: drop 0x%08x, frame %u exceeds single-PDU budget %u", mp->id, (unsigned)frameLen,
                 (unsigned)BLE_MESH_MAX_FRAME_LEN);
        return 0;
    }
    if (BLE_MESH_AD_OVERHEAD + frameLen > outCap)
        return 0;

    uint8_t *p = out;
    *p++ = (uint8_t)(BLE_MESH_AD_OVERHEAD - 1 + frameLen); // AD length covers everything after itself
    *p++ = 0xFF;                                           // manufacturer specific data
    *p++ = (uint8_t)(BLE_MESH_COMPANY_ID & 0xFF);
    *p++ = (uint8_t)(BLE_MESH_COMPANY_ID >> 8);
    *p++ = BLE_MESH_PROTO_VERSION;

    PacketHeader header = {};
    header.from = mp->from;
    header.to = mp->to;
    header.id = mp->id;
    header.channel = mp->channel;
    header.next_hop = mp->next_hop;
    header.relay_node = mp->relay_node;
    header.flags = (mp->hop_limit & PACKET_FLAGS_HOP_LIMIT_MASK) | (mp->want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0) |
                   (mp->via_mqtt ? PACKET_FLAGS_VIA_MQTT_MASK : 0);
    header.flags |= (mp->hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK;

    memcpy(p, &header, sizeof(header));
    p += sizeof(header);
    memcpy(p, mp->encrypted.bytes, mp->encrypted.size);
    p += mp->encrypted.size;

    return (uint8_t)(p - out);
}

bool BleMeshHandler::onSend(const meshtastic_MeshPacket *mp)
{
    if (!isRunning || !mp)
        return false;

    // Deliberately NOT the guard UdpMulticastHandler carries here. A packet that arrived over BLE
    // and comes back through Router::send is a rebroadcast - NextHopRouter::perhapsRebroadcast
    // allocCopy()s the received packet, and nothing on the TX path rewrites transport_mechanism
    // (RadioInterface stamps TRANSPORT_LORA in deliverToReceiver, which is RX-only). Refusing it
    // would cap the BLE mesh at a single hop. Loop protection is the same as LoRa's: PacketHistory
    // drops a packet seen recently, hop_limit decrements per relay, and onScanReport ignores frames
    // whose sender is us.
    if (mp->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV)
        LOG_DEBUG("BLE mesh: re-advertising relayed packet 0x%08x", mp->id);

    AdvSlot slot;
    slot.len = encodeAdvPayload(mp, slot.data.data(), slot.data.size());
    if (slot.len == 0)
        return false;

    {
        std::lock_guard<std::mutex> lock(txMutex);
        if (txCount.load() >= BLE_MESH_TX_QUEUE_SIZE) {
            LOG_WARN("BLE mesh: TX queue full, dropping 0x%08x", mp->id);
            return false;
        }
        txQueue[txTail] = slot;
        txTail = (txTail + 1) % BLE_MESH_TX_QUEUE_SIZE;
        txCount.fetch_add(1);
    }

    // Clock the queue promptly rather than waiting out the idle interval.
    setIntervalFromNow(0);
    concurrency::mainDelay.interrupt();
    return true;
}

bool BleMeshHandler::beginAdvertising(const AdvSlot &slot)
{
    struct os_mbuf *buf = os_msys_get_pkthdr(slot.len, 0);
    if (!buf) {
        LOG_WARN("BLE mesh: no mbuf for advertisement");
        return false;
    }
    int rc = os_mbuf_append(buf, slot.data.data(), slot.len);
    if (rc != 0) {
        os_mbuf_free_chain(buf);
        LOG_WARN("BLE mesh: os_mbuf_append failed rc=%d", rc);
        return false;
    }

    // set_data takes ownership of buf on success and frees it on failure, so there is no leak path
    // here and no double free - do not touch buf after this call either way.
    rc = ble_gap_ext_adv_set_data(BLE_MESH_ADV_INSTANCE, buf);
    if (rc != 0) {
        LOG_WARN("BLE mesh: ble_gap_ext_adv_set_data failed rc=%d", rc);
        return false;
    }

    rc = ble_gap_ext_adv_start(BLE_MESH_ADV_INSTANCE, 0 /* duration */, BLE_MESH_ADV_EVENTS);
    if (rc != 0) {
        LOG_WARN("BLE mesh: ble_gap_ext_adv_start failed rc=%d", rc);
        return false;
    }

    isAdvertising = true;
    advStartedAtMsec = millis();
    return true;
}

void BleMeshHandler::stopAdvertising()
{
    if (!isAdvertising)
        return;
    ble_gap_ext_adv_stop(BLE_MESH_ADV_INSTANCE);
    isAdvertising = false;
}

int32_t BleMeshHandler::runOnce()
{
    if (!isRunning) {
        // The host may not have been synced when start() first ran (BLE comes up asynchronously),
        // so keep retrying rather than requiring a precise ordering in main().
        if (config.network.enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_BLE_BROADCAST)
            start();
        return 1000;
    }

    if (isAdvertising) {
        // ble_gap_ext_adv_active() is the authority on whether the burst finished. Poll it rather
        // than trusting BLE_GAP_EVENT_ADV_COMPLETE alone: with duration 0 and max_events set, a
        // controller that never reports completion would wedge the queue forever.
        if (ble_gap_ext_adv_active(BLE_MESH_ADV_INSTANCE))
            return 10;
        isAdvertising = false;
    }

    AdvSlot slot;
    {
        std::lock_guard<std::mutex> lock(txMutex);
        if (txCount.load() == 0)
            return 100;
        slot = txQueue[txHead];
        txHead = (txHead + 1) % BLE_MESH_TX_QUEUE_SIZE;
        txCount.fetch_sub(1);
    }

    beginAdvertising(slot);
    return 10;
}

void BleMeshHandler::onScanReport(const uint8_t *data, uint8_t len, int8_t rssi)
{
    // Reached from the NimBLE host task, not the main one. That is safe here: Allocator::allocZeroed
    // is documented callable from ISR context and fromRadioQueue is a FreeRTOS queue, so no
    // marshalling of the kind NimbleBluetooth does for PhoneAPI writes is needed.
    if (!isRunning || !router || !nodeDB || !data)
        return;

    // Walk AD structures looking for ours. Advertisements routinely carry several.
    size_t offset = 0;
    while (offset + 1 < len) {
        const uint8_t adLen = data[offset];
        if (adLen == 0 || offset + 1 + adLen > len)
            return; // malformed or truncated - stop, do not guess
        const uint8_t adType = data[offset + 1];

        if (adType == 0xFF && adLen >= BLE_MESH_AD_OVERHEAD - 1 + sizeof(PacketHeader)) {
            const uint8_t *body = &data[offset + 2];
            const uint16_t company = (uint16_t)body[0] | ((uint16_t)body[1] << 8);
            if (company == BLE_MESH_COMPANY_ID && body[2] == BLE_MESH_PROTO_VERSION) {
                const uint8_t *frame = body + 3;
                const size_t frameLen = adLen - 1 /* type */ - 3 /* company + version */;
                if (frameLen >= sizeof(PacketHeader)) {
                    PacketHeader header;
                    memcpy(&header, frame, sizeof(header));
                    const size_t payloadLen = frameLen - sizeof(PacketHeader);

                    // Guard 1 (mirrors UDP): a spoofed local origin. Nothing legitimate advertises
                    // from=0 or from us - our own advertisement echoing back would loop forever.
                    if (header.from == 0) {
                        LOG_WARN("BLE mesh: advertisement with no sender, dropping");
                        return;
                    }
                    if (header.from == nodeDB->getNodeNum())
                        return; // our own advertisement, heard by our own scanner

                    meshtastic_MeshPacket *mp = packetPool.allocZeroed();
                    if (!mp)
                        return;

                    mp->from = header.from;
                    mp->to = header.to;
                    mp->id = header.id;
                    mp->channel = header.channel;
                    mp->hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
                    mp->hop_start = (header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
                    mp->want_ack = !!(header.flags & PACKET_FLAGS_WANT_ACK_MASK);
                    mp->via_mqtt = !!(header.flags & PACKET_FLAGS_VIA_MQTT_MASK);
                    mp->next_hop = mp->hop_start == 0 ? NO_NEXT_HOP_PREFERENCE : header.next_hop;
                    mp->relay_node = mp->hop_start == 0 ? NO_RELAY_NODE : header.relay_node;

                    // Guard 2 (mirrors UDP): an out-of-range hop count is not relayable.
                    if (mp->hop_limit > HOP_MAX || mp->hop_start > HOP_MAX) {
                        LOG_WARN("BLE mesh: invalid hop_limit(%u)/hop_start(%u), dropping", mp->hop_limit, mp->hop_start);
                        packetPool.release(mp);
                        return;
                    }

                    mp->transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_ADV;

                    // Guard 3 (mirrors UDP): authentication metadata is local-only. Router
                    // re-establishes it after a successful PKI decrypt; carrying it in from the
                    // wire would let a sender assert its own packet was PKI-authenticated.
                    mp->pki_encrypted = false;
                    mp->public_key.size = 0;

                    // Guard 4 (mirrors UDP): no LoRa measurement exists for a BLE arrival. rx_rssi
                    // has explicit presence, so has_rx_rssi is set alongside it - BLE RSSI is a
                    // real measurement of this hop, unlike the UDP case where there is none.
                    mp->rx_snr = 0;
                    mp->rx_rssi = rssi;
                    mp->has_rx_rssi = true;

                    mp->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
                    if (payloadLen > sizeof(mp->encrypted.bytes)) {
                        packetPool.release(mp);
                        return;
                    }
                    memcpy(mp->encrypted.bytes, frame + sizeof(PacketHeader), payloadLen);
                    mp->encrypted.size = payloadLen;

                    LOG_DEBUG("BLE mesh RX from=0x%08x to=0x%08x id=0x%08x rssi=%d len=%u", mp->from, mp->to, mp->id, rssi,
                              (unsigned)payloadLen);
                    router->enqueueReceivedMessage(mp);
                    return;
                }
            }
        }
        offset += 1 + adLen;
    }
}

#endif // HAS_BLE_MESH
