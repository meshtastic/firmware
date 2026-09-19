#include "configuration.h"

#if HAS_BLE_GATT_MESH

#include "BLEGattMeshHandler.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include "main.h"

BLEGattMeshHandler *bleGattMeshHandler = nullptr;

// --- framing ------------------------------------------------------------------------------------

bool BLEGattMeshHandler::parseFragment(const uint8_t *chunk, size_t len, FragmentHeader &hdr)
{
    if (!chunk || len < BLE_GATT_MESH_FRAG_HEADER || chunk[0] != BLE_GATT_MESH_FRAG_VERSION)
        return false;
    hdr.id = (uint16_t)(chunk[1] | (chunk[2] << 8));
    hdr.index = chunk[3];
    hdr.total = chunk[4];
    // A total of zero describes nothing and an index outside it can never complete; either would sit
    // in the table until expiry, which is the buffer a hostile peer wants to fill.
    return hdr.total != 0 && hdr.index < hdr.total;
}

uint8_t BLEGattMeshHandler::fragmentCount(size_t packetLen, uint16_t chunk)
{
    if (chunk <= BLE_GATT_MESH_FRAG_HEADER)
        return 0;
    const size_t payloadPerChunk = chunk - BLE_GATT_MESH_FRAG_HEADER;
    const size_t total = packetLen == 0 ? 1 : (packetLen + payloadPerChunk - 1) / payloadPerChunk;
    return total > BLE_GATT_MESH_MAX_FRAGMENTS ? 0 : (uint8_t)total;
}

size_t BLEGattMeshHandler::buildFragment(const uint8_t *packet, size_t packetLen, uint16_t fragId, uint8_t index, uint8_t total,
                                         uint16_t chunk, uint8_t *out, size_t outCap)
{
    if (!packet || !out || chunk <= BLE_GATT_MESH_FRAG_HEADER || index >= total)
        return 0;
    const size_t payloadPerChunk = chunk - BLE_GATT_MESH_FRAG_HEADER;
    const size_t from = (size_t)index * payloadPerChunk;
    if (from > packetLen)
        return 0;
    const size_t n = std::min(payloadPerChunk, packetLen - from);
    if (outCap < BLE_GATT_MESH_FRAG_HEADER + n)
        return 0;

    out[0] = BLE_GATT_MESH_FRAG_VERSION;
    out[1] = (uint8_t)(fragId & 0xFF);
    out[2] = (uint8_t)(fragId >> 8);
    out[3] = index;
    out[4] = total;
    memcpy(out + BLE_GATT_MESH_FRAG_HEADER, packet + from, n);
    return BLE_GATT_MESH_FRAG_HEADER + n;
}

// --- egress -------------------------------------------------------------------------------------

bool BLEGattMeshHandler::onSend(const meshtastic_MeshPacket *mp)
{
    if (!isRunning || !mp)
        return false;

    // Router::send() encrypts before it reaches any transport, so plaintext here is a bug upstream.
    if (mp->which_payload_variant != meshtastic_MeshPacket_encrypted_tag) {
        LOG_WARN("BLE GATT mesh: refusing to send an unencrypted packet 0x%08x", mp->id);
        return false;
    }
    if (mp->from == 0) {
        LOG_WARN("BLE GATT mesh: refusing to send a packet with no sender");
        return false;
    }
    if (txCount >= BLE_GATT_MESH_TX_QUEUE_SIZE) {
        LOG_WARN("BLE GATT mesh: TX queue full, dropping 0x%08x", mp->id);
        return false;
    }

    TxSlot &slot = txQueue[txTail];
    const size_t n = pb_encode_to_bytes(slot.data.data(), slot.data.size(), &meshtastic_MeshPacket_msg, mp);
    if (n == 0) {
        LOG_WARN("BLE GATT mesh: failed to encode 0x%08x", mp->id);
        return false;
    }
    slot.len = (uint16_t)n;
    slot.fragId = nextFragId++;
    // A relay must not go back to the peer that handed it over - unless that peer originated it, in
    // which case the echo is its implicit ack, the one a shared medium gives for free.
    slot.exclude = relayExclusion(mp->from, mp->id);

    txTail = (txTail + 1) % BLE_GATT_MESH_TX_QUEUE_SIZE;
    txCount++;
    wake();
    return true;
}

void BLEGattMeshHandler::wake()
{
    setIntervalFromNow(0);
    concurrency::mainDelay.interrupt();
}

int32_t BLEGattMeshHandler::runOnce()
{
    if (!isRunning || !platformReady())
        return 500;

    pumpRx(Time::getMillis());
    pumpGreet();
    return pumpTx() ? 10 : 100;
}

bool BLEGattMeshHandler::isControl(const uint8_t *chunk, size_t len)
{
    return len >= 2 && chunk[0] == BLE_GATT_MESH_CTRL_MARKER;
}

bool BLEGattMeshHandler::parseHello(const uint8_t *chunk, size_t len, uint8_t *idOut)
{
    if (!isControl(chunk, len) || chunk[1] != BLE_GATT_MESH_CTRL_HELLO || len != BLE_GATT_MESH_HELLO_SIZE)
        return false;
    memcpy(idOut, chunk + 2, BLE_GATT_MESH_LINK_ID_SIZE);
    return true;
}

// Drawn on first use, not in the constructor: a peer's HELLO can land before this node's first greet,
// and the election it starts must compare against the id the greet will carry, not zeros.
void BLEGattMeshHandler::ensureLinkId()
{
    if (linkIdReady)
        return;
    for (auto &b : linkId)
        b = (uint8_t)random(256);
    linkIdReady = true;
}

size_t BLEGattMeshHandler::buildHello(uint8_t *out, size_t cap)
{
    if (cap < BLE_GATT_MESH_HELLO_SIZE)
        return 0;
    ensureLinkId();
    out[0] = BLE_GATT_MESH_CTRL_MARKER;
    out[1] = BLE_GATT_MESH_CTRL_HELLO;
    memcpy(out + 2, linkId, BLE_GATT_MESH_LINK_ID_SIZE);
    return BLE_GATT_MESH_HELLO_SIZE;
}

BLEGattMeshHandler::Greeting *BLEGattMeshHandler::greeting(BLEGattPeerId peer, bool create)
{
    for (auto &g : greetings) {
        if (g.used && g.peer == peer)
            return &g;
    }
    if (!create)
        return nullptr;
    for (auto &g : greetings) {
        if (g.used)
            continue;
        g.used = true;
        g.peer = peer;
        g.sent = false;
        g.heard = false;
        return &g;
    }
    return nullptr;
}

void BLEGattMeshHandler::pumpGreet()
{
    std::array<BLEGattMeshPeer, BLE_GATT_MESH_MAX_PEERS> all{};
    const size_t n = platformPeers(all.data(), all.size());
    uint8_t hello[BLE_GATT_MESH_HELLO_SIZE];
    const size_t len = buildHello(hello, sizeof(hello));
    for (size_t i = 0; i < n; i++) {
        Greeting *g = greeting(all[i].id, true);
        if (!g || g->sent)
            continue;
        if (platformNotify(all[i].id, hello, len))
            g->sent = true; // a refusal is the stack busy; the next pump offers it again
    }
}

void BLEGattMeshHandler::handleHello(BLEGattPeerId peer, const uint8_t *id)
{
    Greeting *g = greeting(peer, true);
    if (!g)
        return;
    ensureLinkId();
    memcpy(g->id, id, BLE_GATT_MESH_LINK_ID_SIZE);
    g->heard = true;
    LOG_INFO("BLE GATT mesh: conn %u is link %02x%02x%02x%02x%02x%02x%02x%02x", peer, id[0], id[1], id[2], id[3], id[4], id[5],
             id[6], id[7]);
    // The same name on another link: one node reached both ways. Same rule as the client library -
    // the lower id is the central. As the winner nothing is done; the peer sheds the link it dialled.
    for (const auto &o : greetings) {
        if (!o.used || !o.heard || o.peer == peer || memcmp(o.id, id, BLE_GATT_MESH_LINK_ID_SIZE) != 0)
            continue;
        const bool lose = memcmp(linkId, id, BLE_GATT_MESH_LINK_ID_SIZE) > 0;
        LOG_INFO("BLE GATT mesh: conns %u and %u are one node; this node %s the election", o.peer, peer, lose ? "loses" : "wins");
        if (lose) {
            platformShedOutbound(peer);
            platformShedOutbound(o.peer);
        }
        return;
    }
}

void BLEGattMeshHandler::pumpRx(uint32_t nowMs)
{
    BLEGattPeerId peer;
    uint8_t chunk[BLE_GATT_MESH_MAX_CHUNK];
    size_t len;
    while (platformPollInbound(peer, chunk, sizeof(chunk), len))
        handleChunk(peer, chunk, len, nowMs);
}

bool BLEGattMeshHandler::pumpTx()
{
    if (!txActive) {
        if (txCount == 0)
            return false;
        // Snapshot the peers once per packet, minus the one it arrived from. A peer joining mid-packet
        // waits for the next one rather than receiving half of this one.
        const TxSlot &slot = txQueue[txHead];
        std::array<BLEGattMeshPeer, BLE_GATT_MESH_MAX_PEERS> all{};
        const size_t n = platformPeers(all.data(), all.size());
        txPeerCount = 0;
        for (size_t i = 0; i < n; i++) {
            if (all[i].id != slot.exclude)
                txPeers[txPeerCount++] = all[i];
        }
        txPeerIdx = 0;
        txFragIdx = 0;
        txAttempts = 0;
        txActive = true;
    }

    const TxSlot &slot = txQueue[txHead];
    while (txPeerIdx < txPeerCount) {
        const BLEGattMeshPeer &peer = txPeers[txPeerIdx];
        const uint16_t chunk =
            std::min<uint16_t>(std::max<uint16_t>(peer.chunk, BLE_GATT_MESH_MIN_CHUNK), BLE_GATT_MESH_MAX_CHUNK);
        const uint8_t total = fragmentCount(slot.len, chunk);
        if (total == 0) {
            LOG_WARN("BLE GATT mesh: %u bytes do not fit peer %u at %u-byte chunks", (unsigned)slot.len, peer.id, chunk);
        } else {
            while (txFragIdx < total) {
                uint8_t frag[BLE_GATT_MESH_MAX_CHUNK];
                const size_t fragLen =
                    buildFragment(slot.data.data(), slot.len, slot.fragId, txFragIdx, total, chunk, frag, sizeof(frag));
                if (fragLen == 0)
                    break;
                if (!platformNotify(peer.id, frag, fragLen)) {
                    if (++txAttempts < BLE_GATT_MESH_TX_ATTEMPTS)
                        return true; // the stack is busy: retry this fragment on the next tick
                    LOG_WARN("BLE GATT mesh: peer %u is not accepting notifies, skipping it", peer.id);
                    break;
                }
                txAttempts = 0;
                txFragIdx++;
            }
        }
        txPeerIdx++;
        txFragIdx = 0;
        txAttempts = 0;
    }

    txHead = (txHead + 1) % BLE_GATT_MESH_TX_QUEUE_SIZE;
    txCount--;
    txActive = false;
    return txCount > 0;
}

// --- ingress ------------------------------------------------------------------------------------

void BLEGattMeshHandler::handleChunk(BLEGattPeerId peer, const uint8_t *chunk, size_t len, uint32_t nowMs)
{
    if (!isRunning)
        return;
    if (len == 0) {
        forgetPeer(peer);
        return;
    }

    uint8_t id[BLE_GATT_MESH_LINK_ID_SIZE];
    if (parseHello(chunk, len, id)) {
        handleHello(peer, id);
        return;
    }
    if (isControl(chunk, len))
        return; // an opcode this build does not know is not a fragment either

    uint8_t packet[meshtastic_MeshPacket_size];
    const size_t n = reassemble(peer, chunk, len, nowMs, packet, sizeof(packet));
    if (n > 0)
        deliverToRouter(peer, packet, n);
}

size_t BLEGattMeshHandler::reassemble(BLEGattPeerId peer, const uint8_t *chunk, size_t len, uint32_t nowMs, uint8_t *out,
                                      size_t outCap)
{
    expireAssemblies(nowMs);

    FragmentHeader hdr;
    if (!parseFragment(chunk, len, hdr))
        return 0;
    const uint8_t *payload = chunk + BLE_GATT_MESH_FRAG_HEADER;
    const size_t payloadLen = len - BLE_GATT_MESH_FRAG_HEADER;
    if (payloadLen > outCap)
        return 0;

    // The common case, and the one that must not take a slot: a packet that fitted one write.
    if (hdr.total == 1) {
        memcpy(out, payload, payloadLen);
        return payloadLen;
    }

    Assembly *a = findAssembly(peer, hdr.id);
    // A peer reusing an id with a different length is not a fragment of what we were building.
    if (a && a->total != hdr.total) {
        a->used = false;
        a = nullptr;
    }
    if (!a) {
        // ATT delivers a peer's writes in order, so a packet always starts at index 0 here.
        if (hdr.index != 0)
            return 0;
        a = newAssembly(peer, hdr.id, hdr.total, nowMs);
        if (!a)
            return 0;
    }

    if (hdr.index < a->received)
        return 0; // a re-delivered fragment on a retrying link: no-op
    if (hdr.index > a->received || a->bytes + payloadLen > a->data.size()) {
        // A gap can never be filled on an ordered link, and an oversize assembly can never legitimately
        // complete; either way the slot must not sit occupied until expiry.
        a->used = false;
        return 0;
    }

    memcpy(a->data.data() + a->bytes, payload, payloadLen);
    a->bytes += payloadLen;
    a->received++;
    if (a->received < a->total)
        return 0;

    const size_t n = a->bytes;
    a->used = false;
    if (n > outCap)
        return 0;
    memcpy(out, a->data.data(), n);
    return n;
}

BLEGattMeshHandler::Assembly *BLEGattMeshHandler::findAssembly(BLEGattPeerId peer, uint16_t id)
{
    for (auto &a : assemblies) {
        if (a.used && a.peer == peer && a.id == id)
            return &a;
    }
    return nullptr;
}

BLEGattMeshHandler::Assembly *BLEGattMeshHandler::newAssembly(BLEGattPeerId peer, uint16_t id, uint8_t total, uint32_t nowMs)
{
    // Per-peer first, so one loud peer cannot crowd out everyone else's traffic.
    size_t mine = 0;
    size_t otherPeers = 0;
    BLEGattPeerId seen[BLE_GATT_MESH_MAX_PEERS];
    for (const auto &a : assemblies) {
        if (!a.used)
            continue;
        if (a.peer == peer) {
            mine++;
            continue;
        }
        bool counted = false;
        for (size_t i = 0; i < otherPeers; i++)
            counted |= seen[i] == a.peer;
        if (!counted && otherPeers < BLE_GATT_MESH_MAX_PEERS)
            seen[otherPeers++] = a.peer;
    }
    if (mine >= BLE_GATT_MESH_MAX_IN_FLIGHT_PER_PEER)
        return nullptr;
    if (mine == 0 && otherPeers >= BLE_GATT_MESH_MAX_PEERS)
        return nullptr;

    for (auto &a : assemblies) {
        if (a.used)
            continue;
        a.used = true;
        a.peer = peer;
        a.id = id;
        a.total = total;
        a.received = 0;
        a.bytes = 0;
        a.startedMs = nowMs;
        return &a;
    }
    return nullptr; // unreachable while the bounds above hold: the table is sized to their product
}

void BLEGattMeshHandler::forgetPeer(BLEGattPeerId peer)
{
    if (Greeting *g = greeting(peer, false))
        g->used = false;
    for (auto &a : assemblies) {
        if (a.used && a.peer == peer)
            a.used = false;
    }
}

void BLEGattMeshHandler::expireAssemblies(uint32_t nowMs)
{
    for (auto &a : assemblies) {
        if (a.used && Throttle::deadlinePassedAt(nowMs, a.startedMs + BLE_GATT_MESH_REASSEMBLY_EXPIRY_MS))
            a.used = false;
    }
}

size_t BLEGattMeshHandler::pendingAssemblies() const
{
    size_t n = 0;
    for (const auto &a : assemblies)
        n += a.used ? 1 : 0;
    return n;
}

void BLEGattMeshHandler::rememberArrival(NodeNum from, PacketId id, BLEGattPeerId peer, bool fromOrigin)
{
    arrivals[arrivalNext] = {from, id, peer, fromOrigin};
    arrivalNext = (arrivalNext + 1) % arrivals.size();
}

BLEGattPeerId BLEGattMeshHandler::arrivalPeer(NodeNum from, PacketId id) const
{
    for (const auto &a : arrivals) {
        if (a.peer != BLE_GATT_MESH_NO_PEER && a.from == from && a.id == id)
            return a.peer;
    }
    return BLE_GATT_MESH_NO_PEER;
}

/*
 * On LoRa an originator hears its own packet relayed and takes that as the implicit ack. A
 * point-to-point bearer has no such echo: excluding the delivering peer from the relay leaves a
 * neighbour that reached us over GATT alone with no delivery evidence at all. So the exclusion
 * applies only when we are carrying someone else's packet onward.
 *
 * This is the only bearer with an exclusion at all - BLEMeshHandler and the UDP transport are
 * broadcast media and have none - which is why GATT was the only one where an implicit ack could
 * not arrive. node-kmp's MeshNode.scheduleRelay holds the same rule as `header.hopsAway == 0`;
 * change one and change the other.
 *
 * Echoing to the originator cannot loop - it drops a packet bearing its own node number, and so do
 * we - and it costs one write per relayed broadcast per adjacent originator.
 */
BLEGattPeerId BLEGattMeshHandler::relayExclusion(NodeNum from, PacketId id) const
{
    for (const auto &a : arrivals) {
        if (a.peer != BLE_GATT_MESH_NO_PEER && a.from == from && a.id == id)
            return a.fromOrigin ? BLE_GATT_MESH_NO_PEER : a.peer;
    }
    return BLE_GATT_MESH_NO_PEER;
}

void BLEGattMeshHandler::deliverToRouter(BLEGattPeerId peer, const uint8_t *data, size_t len)
{
    if (!isRunning || !nodeDB || !data)
        return;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    if (!pb_decode_from_bytes(data, len, &meshtastic_MeshPacket_msg, &mp))
        return;
    if (mp.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
        return;

    // The same guards the UDP and advertisement transports apply: a spoofed local origin reaches
    // paths that trust isFromUs, and an out-of-range hop count is not relayable.
    if (mp.from == 0) {
        LOG_WARN("BLE GATT mesh: packet with no sender from peer %u, dropping", peer);
        return;
    }
    if (mp.from == nodeDB->getNodeNum()) {
        LOG_WARN("BLE GATT mesh: peer %u claims our own node number, dropping", peer);
        return;
    }
    if (mp.hop_limit > HOP_MAX || mp.hop_start > HOP_MAX) {
        LOG_WARN("BLE GATT mesh: invalid hop_limit(%u)/hop_start(%u), dropping", mp.hop_limit, mp.hop_start);
        return;
    }

    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_BLE_GATT;
    // Wire-carried flags that only the local stack may set: a sender must not suppress our MQTT uplink
    // or schedule our transmit.
    mp.via_mqtt = false;
    mp.tx_after = 0;
    // priority is not carried in the LoRa header, so here a sender can choose it, and priority MAX
    // outranks the ACK ceiling fixPriority assigns locally.
    mp.priority = meshtastic_MeshPacket_Priority_UNSET;

    // Authentication metadata is local-only; the Router re-establishes it after a PKI decrypt.
    mp.pki_encrypted = false;
    mp.public_key.size = 0;
    memset(mp.public_key.bytes, 0, sizeof(mp.public_key.bytes));

    // No radio measurement exists for a GATT arrival.
    mp.rx_snr = 0;
    mp.rx_rssi = 0;
    mp.has_rx_rssi = false;

    UniquePacketPoolPacket p = packetPool.allocUniqueCopy(mp);
    if (!p)
        return;

    // Read here, before the Router decrements: equal counts mean no relay has touched it, so this
    // peer is the node that sent it.
    rememberArrival(mp.from, mp.id, peer, mp.hop_start == mp.hop_limit);
    LOG_DEBUG("BLE GATT mesh RX from=0x%08x to=0x%08x id=0x%08x peer=%u len=%u", mp.from, mp.to, mp.id, peer, (unsigned)len);
    enqueueReceived(p.release());
}

void BLEGattMeshHandler::enqueueReceived(meshtastic_MeshPacket *p)
{
    router->enqueueReceivedMessage(p);
}

#endif // HAS_BLE_GATT_MESH
