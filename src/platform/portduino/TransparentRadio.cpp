#include "TransparentRadio.h"

#if HAS_TRANSPARENT_RADIO

#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RadioTxHook.h"
#include "Router.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include "airtime.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
constexpr uint8_t FRAME_START1 = 0x94;
constexpr uint8_t FRAME_START2 = 0xc3;
constexpr size_t WAKE_BYTES = 32; // a sleeping device's serial API wakes on a run of START2

// First release with LoRaConfig.transparent_mode. Older firmware would apply our settings, then route on its own.
constexpr unsigned MIN_FIRMWARE[3] = {2, 8, 3};

bool firmwareSupportsTransparentMode(const char *version)
{
    unsigned v[3] = {0, 0, 0};
    if (sscanf(version, "%u.%u.%u", &v[0], &v[1], &v[2]) < 3)
        return true; // unparseable: let the read-back decide
    for (int i = 0; i < 3; i++)
        if (v[i] != MIN_FIRMWARE[i])
            return v[i] > MIN_FIRMWARE[i];
    return true;
}

/// The first setting that puts the two configs on different air, or nullptr if they agree.
const char *firstRfDifference(const meshtastic_Config_LoRaConfig &a, const meshtastic_Config_LoRaConfig &b, bool withPower)
{
    if (a.region != b.region)
        return "region";
    if (a.use_preset != b.use_preset || (a.use_preset && a.modem_preset != b.modem_preset))
        return "modem preset";
    if (!a.use_preset && (a.bandwidth != b.bandwidth || a.spread_factor != b.spread_factor))
        return "bandwidth or spreading factor";
    if (a.coding_rate != b.coding_rate)
        return "coding rate";
    if (a.override_frequency != b.override_frequency || (!a.override_frequency && a.channel_num != b.channel_num))
        return "frequency slot";
    if (a.tx_enabled != b.tx_enabled)
        return "tx_enabled";
    if (withPower && a.tx_power != b.tx_power) // the device may clamp this to its own limits, so never fatal
        return "tx_power";
    return nullptr;
}
} // namespace

TransparentRadio::TransparentRadio(std::unique_ptr<TransparentLink> link)
    : concurrency::OSThread("TransparentRadio"), link(std::move(link))
{
}

TransparentRadio::~TransparentRadio()
{
    while (meshtastic_MeshPacket *p = txQueue.dequeue())
        releaseFrame(p);
}

bool TransparentRadio::init()
{
    RadioInterface::init();
    LOG_INFO("Transparent radio via %s", link->describe().c_str());
    return true;
}

bool TransparentRadio::reconfigure()
{
    RadioInterface::reconfigure();
    if (state == State::WaitingForRegion || state == State::Refused || state == State::Pushing || state == State::Ready)
        pushConfig();
    return true;
}

uint32_t TransparentRadio::getPacketTime(uint32_t totalPacketLen, bool received)
{
    return computeTimeOnAirMsec(totalPacketLen);
}

meshtastic_QueueStatus TransparentRadio::getQueueStatus()
{
    meshtastic_QueueStatus qs;
    qs.res = qs.mesh_packet_id = 0;
    qs.free = txQueue.getFree();
    qs.maxlen = txQueue.getMaxLen();
    return qs;
}

ErrorCode TransparentRadio::send(meshtastic_MeshPacket *p)
{
    if (disabled || !config.lora.tx_enabled || config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
        packetPool.release(p);
        return ERRNO_DISABLED;
    }
    if (p->to == NODENUM_BROADCAST_NO_LORA)
        return ERRNO_SHOULD_RELEASE;

    bool dropped = false;
    if (!txQueue.enqueue(p, &dropped)) {
        txDrop++;
        packetPool.release(p);
        return ERRNO_UNKNOWN;
    }
    if (dropped)
        txDrop++;
    if (txQueue.getFront() != scheduledPacket)
        scheduleTx();
    return ERRNO_OK;
}

bool TransparentRadio::cancelSending(NodeNum from, PacketId id)
{
    meshtastic_MeshPacket *p = txQueue.remove(from, id);
    if (!p)
        return false;
    releaseFrame(p);
    return true;
}

bool TransparentRadio::findInTxQueue(NodeNum from, PacketId id)
{
    return txQueue.find(from, id);
}

bool TransparentRadio::removePendingTXPacket(NodeNum from, PacketId id, uint32_t hop_limit_lt)
{
    meshtastic_MeshPacket *p = txQueue.remove(from, id, true, true, hop_limit_lt);
    if (!p)
        return false;
    releaseFrame(p);
    return true;
}

void TransparentRadio::clampToLateRebroadcastWindow(NodeNum from, PacketId id)
{
    meshtastic_MeshPacket *p = txQueue.remove(from, id, true, false);
    if (!p)
        return;
    p->tx_after = Time::timerEndsAtMillis(getTxDelayMsecWeightedWorst(p->rx_snr));
    bool dropped = false;
    if (!txQueue.enqueue(p, &dropped)) {
        txDrop++;
        releaseFrame(p);
        return;
    }
    if (dropped)
        txDrop++;
    if (txQueue.getFront() == p) // its due time moved; any other front is picked up by serviceTxQueue()
        scheduleTx();
}

void TransparentRadio::releaseFrame(meshtastic_MeshPacket *p)
{
    if (p == scheduledPacket) // its address can come back from the pool as a different packet
        scheduledPacket = nullptr;
    RadioTxHooks::packetReleased(this, p);
    packetPool.release(p);
}

int32_t TransparentRadio::runOnce()
{
    if (state == State::Disconnected && (!openAttempted || Throttle::hasElapsed(lastOpenAttemptMs, reconnectIntervalMs))) {
        openAttempted = true;
        lastOpenAttemptMs = Time::getMillis();
        if (link->open()) {
            LOG_INFO("Transparent radio: connected to %s", link->describe().c_str());
            openFailureLogged = false;
            reconnectIntervalMs = RECONNECT_MIN_MS;
            startHandshake();
        } else if (link->openPending()) {
            openAttempted = false; // still resolving the host: ask again on the next poll, without backing off
        } else {
            if (!openFailureLogged)
                LOG_WARN("Transparent radio: can't open %s, retrying", link->describe().c_str());
            openFailureLogged = true;
            reconnectIntervalMs = std::min(reconnectIntervalMs * 2, RECONNECT_MAX_MS);
        }
    }

    if (link->isOpen() && !pumpRx())
        linkLost();

    if ((state == State::Configuring || state == State::Pushing) && Throttle::hasElapsed(stateSinceMs, HANDSHAKE_TIMEOUT_MS)) {
        LOG_WARN("Transparent radio: %s did not finish its handshake, asking again", link->describe().c_str());
        startHandshake();
    } else if (state == State::Refused && Throttle::hasElapsed(stateSinceMs, REFUSED_RETRY_MS)) {
        startHandshake();
    }

    if (link->isOpen() && Throttle::hasElapsed(lastWriteMs, HEARTBEAT_INTERVAL_MS))
        sendHeartbeat();

    serviceTxQueue();
    return (link->isOpen() || link->openPending() || scheduledPacket) ? POLL_INTERVAL_MS : reconnectIntervalMs;
}

void TransparentRadio::setState(State next)
{
    state = next;
    stateSinceMs = Time::getMillis();
}

void TransparentRadio::startHandshake()
{
    rxPtr = 0;
    deviceNodeNum = 0;
    deviceFirmwareTooOld = false;
    deviceLocked = false;
    haveDeviceLora = false;
    verifyRequestId = 0;
    deviceQueueFree = -1;
    setState(State::Configuring);

    memset(txFrame, FRAME_START2, WAKE_BYTES);
    if (!link->write(txFrame, WAKE_BYTES)) {
        linkLost();
        return;
    }
    toRadio = meshtastic_ToRadio_init_zero;
    toRadio.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    toRadio.want_config_id = SPECIAL_NONCE_ONLY_CONFIG; // our config only, no node database
    writeToRadio();
}

void TransparentRadio::linkLost()
{
    if (link->isOpen())
        LOG_WARN("Transparent radio: lost %s", link->describe().c_str());
    link->close();
    if (state != State::Unsupported)
        setState(State::Disconnected);
}

bool TransparentRadio::pumpRx()
{
    uint8_t buf[256];
    for (int chunk = 0; chunk < 8 && link->isOpen(); chunk++) { // bounded, so a chatty device can't starve the main loop
        const int n = link->read(buf, sizeof(buf));
        if (n < 0)
            return false;
        if (n == 0)
            break;
        for (int i = 0; i < n && link->isOpen(); i++)
            parseByte(buf[i]);
    }
    return true;
}

// The same framing state machine as StreamAPI, mirrored for the other end of the link.
void TransparentRadio::parseByte(uint8_t c)
{
    const size_t ptr = rxPtr++;
    rxBuf[ptr] = c;
    if (ptr == 0) {
        if (c != FRAME_START1)
            rxPtr = 0;
    } else if (ptr == 1) {
        if (c != FRAME_START2)
            rxPtr = (c == FRAME_START1) ? 1 : 0;
    } else if (ptr >= FRAME_HEADER_LEN - 1) {
        const uint32_t len = (rxBuf[2] << 8) | rxBuf[3];
        if (ptr == FRAME_HEADER_LEN - 1 && len > MAX_TO_FROM_RADIO_SIZE) {
            rxPtr = 0;
            return;
        }
        if (ptr + 1 >= len + FRAME_HEADER_LEN) {
            rxPtr = 0;
            fromRadio = meshtastic_FromRadio_init_zero;
            if (pb_decode_from_bytes(rxBuf + FRAME_HEADER_LEN, len, &meshtastic_FromRadio_msg, &fromRadio))
                handleFromRadio();
            else
                LOG_WARN("Transparent radio: undecodable FromRadio (%u bytes)", (unsigned)len);
        }
    }
}

void TransparentRadio::handleFromRadio()
{
    switch (fromRadio.which_payload_variant) {
    case meshtastic_FromRadio_my_info_tag:
        deviceNodeNum = fromRadio.my_info.my_node_num;
        break;
    case meshtastic_FromRadio_metadata_tag:
        deviceFirmwareTooOld = !firmwareSupportsTransparentMode(fromRadio.metadata.firmware_version);
        break;
    case meshtastic_FromRadio_lockdown_status_tag:
        // A locked device drops an unauthenticated client's admin traffic and mesh packets alike.
        deviceLocked = fromRadio.lockdown_status.state == meshtastic_LockdownStatus_State_LOCKED ||
                       fromRadio.lockdown_status.state == meshtastic_LockdownStatus_State_UNLOCK_FAILED;
        if (deviceLocked && (state == State::Pushing || state == State::Ready))
            pushConfig(); // reports it and backs off
        break;
    case meshtastic_FromRadio_config_tag:
        if (fromRadio.config.which_payload_variant == meshtastic_Config_lora_tag) {
            deviceLora = fromRadio.config.payload_variant.lora;
            haveDeviceLora = true;
        }
        break;
    case meshtastic_FromRadio_config_complete_id_tag:
        if (state == State::Configuring && fromRadio.config_complete_id == SPECIAL_NONCE_ONLY_CONFIG)
            pushConfig();
        break;
    case meshtastic_FromRadio_queueStatus_tag:
        deviceQueueFree = fromRadio.queueStatus.free;
        if (fromRadio.queueStatus.res != ERRNO_OK && fromRadio.queueStatus.mesh_packet_id)
            LOG_WARN("Transparent radio: device refused 0x%08x, result %d", fromRadio.queueStatus.mesh_packet_id,
                     fromRadio.queueStatus.res);
        break;
    case meshtastic_FromRadio_rebooted_tag:
        LOG_INFO("Transparent radio: %s rebooted", link->describe().c_str());
        startHandshake();
        break;
    case meshtastic_FromRadio_packet_tag: {
        const meshtastic_MeshPacket &p = fromRadio.packet;
        if (p.which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
            if (state == State::Ready) // before that the device may still be a normal node, relaying on its own
                deliverReceived(p);
        } else if (p.decoded.portnum == meshtastic_PortNum_ADMIN_APP) {
            handleAdminReply(p);
        }
        break;
    }
    default:
        break;
    }
}

meshtastic_Config_LoRaConfig TransparentRadio::desiredLoraConfig() const
{
    meshtastic_Config_LoRaConfig lora = config.lora;
    lora.transparent_mode = true;
    // The device's AdminModule rewrites these when out of range; match it, or the read-back never agrees.
    lora.coding_rate = clampCodingRate(lora.coding_rate);
    lora.spread_factor = clampSpreadFactor(lora.spread_factor);
    if (!lora.use_preset)
        lora.bandwidth = clampBandwidthCode(lora.bandwidth);
    // The slot comes from the primary channel name, and the device's name is not ours: pin the slot we resolved.
    if (!lora.override_frequency)
        lora.channel_num = savedChannelNum + 1;
    // Calibration and front-end settings describe the device's hardware, not this node's.
    lora.frequency_offset = deviceLora.frequency_offset;
    lora.sx126x_rx_boosted_gain = deviceLora.sx126x_rx_boosted_gain;
    lora.pa_fan_disabled = deviceLora.pa_fan_disabled;
    lora.fem_lna_mode = deviceLora.fem_lna_mode;
    lora.serial_hal_only = deviceLora.serial_hal_only;
    return lora;
}

void TransparentRadio::pushConfig()
{
    if (!deviceNodeNum) {
        LOG_WARN("Transparent radio: %s sent no node number; waiting to retry", link->describe().c_str());
        return; // the handshake timeout asks again
    }
    if (deviceFirmwareTooOld) {
        LOG_ERROR("Transparent radio: %s runs firmware older than %u.%u.%u, which has no transparent mode",
                  link->describe().c_str(), MIN_FIRMWARE[0], MIN_FIRMWARE[1], MIN_FIRMWARE[2]);
        link->close();
        setState(State::Unsupported);
        return;
    }
    if (deviceLocked) {
        LOG_ERROR("Transparent radio: %s is locked, and meshtasticd can't unlock it; unlock it or turn lockdown off",
                  link->describe().c_str());
        setState(State::Refused);
        return;
    }
    if (!haveDeviceLora) {
        // Without it we would overwrite the device's own calibration fields with ours.
        LOG_WARN("Transparent radio: %s sent no LoRa config; waiting to retry", link->describe().c_str());
        return; // the handshake timeout asks again
    }
    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
        if (state != State::WaitingForRegion)
            LOG_WARN("Transparent radio: no LoRa region set, so %s is left as it is", link->describe().c_str());
        setState(State::WaitingForRegion);
        return;
    }

    const meshtastic_Config_LoRaConfig want = desiredLoraConfig();
    if (deviceLora.transparent_mode && !firstRfDifference(want, deviceLora, true)) {
        setState(State::Ready);
        LOG_INFO("Transparent radio: %s ready", link->describe().c_str());
        return;
    }

    meshtastic_AdminMessage set = meshtastic_AdminMessage_init_zero;
    set.which_payload_variant = meshtastic_AdminMessage_set_config_tag;
    set.set_config.which_payload_variant = meshtastic_Config_lora_tag;
    set.set_config.payload_variant.lora = want;
    if (!sendAdmin(set, false))
        return;
    pushedLora = want;

    meshtastic_AdminMessage get = meshtastic_AdminMessage_init_zero;
    get.which_payload_variant = meshtastic_AdminMessage_get_config_request_tag;
    get.get_config_request = meshtastic_AdminMessage_ConfigType_LORA_CONFIG;
    setState(State::Pushing);
    sendAdmin(get, true, &verifyRequestId);
}

void TransparentRadio::handleAdminReply(const meshtastic_MeshPacket &p)
{
    if (state != State::Pushing || (verifyRequestId && p.decoded.request_id != verifyRequestId))
        return;
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    if (!pb_decode_from_bytes(p.decoded.payload.bytes, p.decoded.payload.size, &meshtastic_AdminMessage_msg, &admin) ||
        admin.which_payload_variant != meshtastic_AdminMessage_get_config_response_tag ||
        admin.get_config_response.which_payload_variant != meshtastic_Config_lora_tag)
        return;

    deviceLora = admin.get_config_response.payload_variant.lora;
    haveDeviceLora = true;
    // A device that rejects a LoRa config restores its old one whole, transparent_mode included, so check the radio
    // settings first: only a device that took them and still reads transparent_mode false lacks the feature.
    if (const char *difference = firstRfDifference(pushedLora, deviceLora, false)) {
        LOG_WARN("Transparent radio: %s did not take our LoRa settings (%s differs); retrying in %u min or on the next "
                 "config change",
                 link->describe().c_str(), difference, (unsigned)(REFUSED_RETRY_MS / 60000));
        setState(State::Refused);
        return;
    }
    if (!deviceLora.transparent_mode) {
        LOG_ERROR("Transparent radio: %s kept transparent_mode off; its firmware predates transparent mode",
                  link->describe().c_str());
        link->close();
        setState(State::Unsupported);
        return;
    }
    setState(State::Ready);
    LOG_INFO("Transparent radio: %s ready", link->describe().c_str());
}

bool TransparentRadio::sendAdmin(const meshtastic_AdminMessage &msg, bool wantResponse, PacketId *idOut)
{
    toRadio = meshtastic_ToRadio_init_zero;
    toRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    meshtastic_MeshPacket &p = toRadio.packet;
    p.to = deviceNodeNum;
    p.id = generatePacketId();
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    p.decoded.want_response = wantResponse;
    p.decoded.payload.size =
        pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_AdminMessage_msg, &msg);
    if (idOut)
        *idOut = p.id;
    return writeToRadio();
}

bool TransparentRadio::sendHeartbeat()
{
    toRadio = meshtastic_ToRadio_init_zero;
    toRadio.which_payload_variant = meshtastic_ToRadio_heartbeat_tag; // nonce 0: a plain keepalive
    return writeToRadio();
}

bool TransparentRadio::writeToRadio()
{
    const size_t len = pb_encode_to_bytes(txFrame + FRAME_HEADER_LEN, MAX_TO_FROM_RADIO_SIZE, &meshtastic_ToRadio_msg, &toRadio);
    if (len == 0) {
        LOG_ERROR("Transparent radio: ToRadio encode failed");
        return false;
    }
    txFrame[0] = FRAME_START1;
    txFrame[1] = FRAME_START2;
    txFrame[2] = (len >> 8) & 0xff;
    txFrame[3] = len & 0xff;
    if (!link->write(txFrame, FRAME_HEADER_LEN + len)) {
        linkLost();
        return false;
    }
    lastWriteMs = Time::getMillis();
    return true;
}

bool TransparentRadio::forward(const meshtastic_MeshPacket *p)
{
    toRadio = meshtastic_ToRadio_init_zero;
    toRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    // What the radio header carries, plus the ciphertext; local bookkeeping (rx metadata, tx_after) stays here.
    meshtastic_MeshPacket &out = toRadio.packet;
    out.from = p->from;
    out.to = p->to;
    out.id = p->id;
    out.channel = p->channel;
    out.hop_limit = p->hop_limit > HOP_MAX ? HOP_RELIABLE : p->hop_limit; // as encodeRadioBuffer() does for a local chip
    out.hop_start = p->hop_start & HOP_MAX;
    out.want_ack = p->want_ack;
    out.via_mqtt = p->via_mqtt;
    out.next_hop = p->next_hop;
    out.relay_node = p->relay_node;
    out.priority = p->priority;
    out.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    out.encrypted = p->encrypted;
    return writeToRadio();
}

void TransparentRadio::deliverReceived(const meshtastic_MeshPacket &p)
{
    meshtastic_MeshPacket *mp = packetPool.allocCopy(p);
    if (!mp)
        return;
    mp->rx_time = 0; // the router stamps our own arrival time
    mp->has_rx_time = false;
    rxGood++;
    if (airTime)
        airTime->logAirtime(RX_LOG, RadioInterface::getPacketTime(mp, true));
    printPacket("Lora RX", mp);
    deliverToReceiver(mp);
}

void TransparentRadio::scheduleTx()
{
    meshtastic_MeshPacket *p = txQueue.getFront();
    scheduledPacket = p;
    if (!p)
        return;
    const uint32_t now = Time::getMillis();
    uint32_t delay;
    if (p->tx_after)
        delay = Throttle::deadlinePassedAt(now, p->tx_after) ? 0 : p->tx_after - now;
    else if (p->rx_snr == 0 && p->rx_rssi == 0) // generated here
        delay = getTxDelayMsec();
    else // a relay: SNR-weighted, so the best-placed relayer goes first and the rest can cancel
        delay = getTxDelayMsecWeighted(p);
    const uint32_t busy = (txBusySet && !Throttle::deadlinePassedAt(now, txBusyUntilMs)) ? txBusyUntilMs - now : 0;
    // An explicit tx_after is a deadline of its own, so it overlaps the busy time rather than following it.
    txDueMs = p->tx_after ? now + std::max(busy, delay) : now + busy + delay;
}

void TransparentRadio::serviceTxQueue()
{
    if (txQueue.getFront() != scheduledPacket) // cancelled, replaced or newly queued since we last looked
        scheduleTx();
    if (!scheduledPacket)
        return;
    const uint32_t now = Time::getMillis();
    if (!Throttle::deadlinePassedAt(now, txDueMs))
        return;
    if (state == State::Ready && deviceQueueFree == 0) {
        txDueMs = now + POLL_INTERVAL_MS; // the device sends a QueueStatus when space frees up
        return;
    }

    meshtastic_MeshPacket *p = txQueue.dequeue();
    if (state != State::Ready) {
        LOG_DEBUG_RADIO("Transparent radio: drop 0x%08x, device not ready", p->id);
        txDrop++;
    } else if (RadioTxHooks::holdsRadio(p)) {
        LOG_WARN("Transparent radio: drop 0x%08x, it needs its own radio settings, which the device can't switch per packet",
                 p->id);
        txDrop++;
    } else if (p->which_payload_variant != meshtastic_MeshPacket_encrypted_tag) {
        LOG_WARN("Transparent radio: drop 0x%08x, not encrypted", p->id);
        txDrop++;
    } else if (forward(p)) {
        const uint32_t airtimeMs = RadioInterface::getPacketTime(p);
        if (airTime)
            airTime->logAirtime(TX_LOG, airtimeMs);
        txBusyUntilMs = now + airtimeMs;
        txBusySet = true;
        txGood++;
        if (deviceQueueFree > 0)
            deviceQueueFree--;
        printPacket("Sent via transparent radio", p);
    } else {
        txDrop++;
    }
    releaseFrame(p);
    scheduleTx();
}

#endif
