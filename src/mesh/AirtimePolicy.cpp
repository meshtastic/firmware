#include "AirtimePolicy.h"

#include "NodeDB.h"
#include "configuration.h"
#include "meshUtils.h"

#include <algorithm>
#include <math.h>

AirtimePolicy *airtimePolicy = nullptr;

uint8_t AirtimePolicy::clampCr(uint8_t cr, uint8_t fallback)
{
    if (cr >= DCR_CR_SLIM && cr <= DCR_CR_RESCUE)
        return cr;

    return fallback;
}

uint8_t AirtimePolicy::crIndex(uint8_t cr)
{
    cr = clampCr(cr);
    return cr - DCR_CR_SLIM;
}

DcrSettings AirtimePolicy::settingsFromConfig(const meshtastic_Config_LoRaConfig &loraConfig) const
{
    DcrSettings settings;
    settings.mode = loraConfig.dcr_mode;
    if (settings.mode < _meshtastic_Config_LoRaConfig_DynamicCodingRateMode_MIN ||
        settings.mode > _meshtastic_Config_LoRaConfig_DynamicCodingRateMode_MAX)
        settings.mode = meshtastic_Config_LoRaConfig_DynamicCodingRateMode_DCR_OFF;
    settings.minCr = clampCr(loraConfig.dcr_min_cr, DCR_CR_SLIM);
    settings.maxCr = clampCr(loraConfig.dcr_max_cr, DCR_CR_RESCUE);
    if (settings.minCr > settings.maxCr)
        std::swap(settings.minCr, settings.maxCr);

    // Config value 0 means "use firmware default" so older clients can enable
    // DCR without learning every advanced clamp at once.
    settings.robustAirtimePct = loraConfig.dcr_robust_airtime_pct
                                    ? static_cast<uint8_t>(std::min<uint32_t>(loraConfig.dcr_robust_airtime_pct, 100))
                                    : 10;
    settings.trackNeighborCr = !loraConfig.dcr_disable_neighbor_tracking;
    settings.telemetryMaxCr = clampCr(loraConfig.dcr_telemetry_max_cr, DCR_CR_NORMAL);
    settings.userMinCr = clampCr(loraConfig.dcr_user_min_cr, DCR_CR_SLIM);
    settings.alertMinCr = clampCr(loraConfig.dcr_alert_min_cr, DCR_CR_ROBUST);
    return settings;
}

DcrPacketClass AirtimePolicy::classifyPacket(const meshtastic_MeshPacket &packet, meshtastic_PortNum *portnum, bool *known) const
{
    if (known)
        *known = false;
    if (portnum)
        *portnum = meshtastic_PortNum_UNKNOWN_APP;

    if (packet.priority >= meshtastic_MeshPacket_Priority_ALERT)
        return DcrPacketClass::Urgent;
    if (packet.priority >= meshtastic_MeshPacket_Priority_ACK)
        return DcrPacketClass::Control;

    // Repeaters may forward encrypted packets, or packets they intentionally
    // did not decode. In that case priority is the only payload-independent
    // signal we can trust here.
    if (packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        if (packet.priority <= meshtastic_MeshPacket_Priority_BACKGROUND)
            return DcrPacketClass::Expendable;
        if (packet.priority >= meshtastic_MeshPacket_Priority_HIGH)
            return DcrPacketClass::Normal;
        return DcrPacketClass::Normal;
    }

    if (known)
        *known = true;
    if (portnum)
        *portnum = packet.decoded.portnum;

    switch (packet.decoded.portnum) {
    case meshtastic_PortNum_TELEMETRY_APP:
    case meshtastic_PortNum_POSITION_APP:
    case meshtastic_PortNum_NODEINFO_APP:
    case meshtastic_PortNum_NEIGHBORINFO_APP:
    case meshtastic_PortNum_MAP_REPORT_APP:
    case meshtastic_PortNum_RANGE_TEST_APP:
    case meshtastic_PortNum_STORE_FORWARD_APP:
    case meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP:
        return DcrPacketClass::Expendable;

    case meshtastic_PortNum_ROUTING_APP:
        return DcrPacketClass::Control;

    case meshtastic_PortNum_ALERT_APP:
    case meshtastic_PortNum_DETECTION_SENSOR_APP:
        return DcrPacketClass::Urgent;

    case meshtastic_PortNum_TEXT_MESSAGE_APP:
    case meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP:
    case meshtastic_PortNum_WAYPOINT_APP:
    case meshtastic_PortNum_ADMIN_APP:
    case meshtastic_PortNum_TRACEROUTE_APP:
        return DcrPacketClass::Normal;

    default:
        break;
    }

    if (packet.want_ack || packet.priority >= meshtastic_MeshPacket_Priority_RELIABLE)
        return DcrPacketClass::Control;

    return DcrPacketClass::Normal;
}

void AirtimePolicy::rememberPacketClass(const meshtastic_MeshPacket &packet, uint32_t nowMsec)
{
    bool known = false;
    meshtastic_PortNum portnum = meshtastic_PortNum_UNKNOWN_APP;
    DcrPacketClass packetClass = classifyPacket(packet, &portnum, &known);

    PacketClassCache &slot = classCache[nextClassCache++ % PACKET_CLASS_CACHE_SIZE];
    slot.packetPtr = &packet;
    slot.from = getFrom(&packet);
    slot.id = packet.id;
    slot.portnum = portnum;
    slot.packetClass = packetClass;
    slot.priority = packet.priority;
    slot.updatedMsec = nowMsec;
    slot.classKnown = known;
}

const AirtimePolicy::PacketClassCache *AirtimePolicy::findPacketClass(const meshtastic_MeshPacket &packet) const
{
    for (const auto &slot : classCache) {
        if (slot.packetPtr == &packet)
            return &slot;
    }

    NodeNum from = getFrom(&packet);
    if (packet.id == 0)
        return nullptr;

    for (const auto &slot : classCache) {
        if (slot.id == packet.id && slot.from == from)
            return &slot;
    }

    return nullptr;
}

DcrDecision AirtimePolicy::choose(const DcrPacketContext &ctx, const ChannelAirtimeStats &channel, const DcrSettings &settings,
                                  uint32_t (*airtimeForCr)(uint32_t packetLen, uint8_t cr, void *context), void *airtimeContext)
{
    DcrDecision decision;
    decision.cr = clampCr(ctx.baseCr);
    decision.changed = false;
    decision.predictedAirtimeMs = ctx.predictedAirtimeMs;

    if (settings.mode == meshtastic_Config_LoRaConfig_DynamicCodingRateMode_DCR_OFF)
        return decision;

    DcrPacketClass packetClass = ctx.packetClass;
    bool classKnown = ctx.classKnown;

    if (ctx.packet) {
        if (const PacketClassCache *cached = findPacketClass(*ctx.packet)) {
            // Prefer the pre-encryption classification for local-origin packets.
            // It can know the real portnum even after Router::send() encrypts.
            packetClass = cached->packetClass;
            classKnown = cached->classKnown;
        }
    }

    decision.packetClass = packetClass;

    // The score is intentionally small and saturates into four CR levels below.
    // Large, clever-looking weights are dangerous here: they make one signal
    // dominate and can turn "more FEC" into channel poisoning.
    int8_t score = 0;
    switch (packetClass) {
    case DcrPacketClass::Expendable:
        score -= 1;
        decision.reasonFlags |= DCR_REASON_PERIODIC;
        break;
    case DcrPacketClass::Normal:
        decision.reasonFlags |= DCR_REASON_USER;
        break;
    case DcrPacketClass::Control:
        score += 1;
        decision.reasonFlags |= DCR_REASON_CONTROL;
        break;
    case DcrPacketClass::Urgent:
        score += 2;
        decision.reasonFlags |= DCR_REASON_URGENT;
        break;
    }

    bool idle = channel.channelUtilizationPercent <= 2.0f && channel.queueDepth <= 1;
    bool congested = channel.channelUtilizationPercent >= 25.0f || channel.queueDepth >= 6;
    bool busy = !congested && (channel.channelUtilizationPercent >= 10.0f || channel.queueDepth >= 3);

    // FEC helps weak links, not collisions. Congestion therefore pushes CR down
    // even for some important packets; retries can still add robustness later
    // only when the loss looked quiet/link-related.
    if (idle) {
        score += 1;
        decision.reasonFlags |= DCR_REASON_IDLE;
    } else if (congested) {
        score -= 2;
        decision.reasonFlags |= DCR_REASON_CONGESTED | DCR_REASON_COLLISION_PRESSURE;
    } else if (busy) {
        score -= 1;
        decision.reasonFlags |= DCR_REASON_BUSY;
    }

    if (ctx.relay) {
        score -= 1;
        decision.reasonFlags |= DCR_REASON_RELAY;
    }
    if (ctx.lateRelay) {
        score += 1;
        decision.reasonFlags |= DCR_REASON_LATE_RELAY;
    }
    if (ctx.lastHop) {
        score += 1;
        decision.reasonFlags |= DCR_REASON_LAST_HOP;
    }

    // RX SNR is only a hint for the next hop, especially on asymmetric links.
    // Keep it as a light bias rather than a hard decision.
    if (ctx.rxSnr < -8.0f && (ctx.rxSnr != 0.0f || ctx.rxRssi != 0)) {
        score += 1;
        decision.reasonFlags |= DCR_REASON_WEAK_LINK;
    } else if (ctx.rxSnr > 5.0f) {
        score -= 1;
        decision.reasonFlags |= DCR_REASON_STRONG_LINK;
    }

    if (ctx.retry.attempt > 0) {
        decision.reasonFlags |= DCR_REASON_RETRY;
        // Quiet loss means "more redundancy may help"; busy/congested means
        // "longer airtime may hurt everyone". Do not grant both at once.
        if (ctx.retry.quietLoss && !busy && !congested) {
            score += std::min<uint8_t>(ctx.retry.attempt, 2);
            decision.reasonFlags |= DCR_REASON_QUIET_LOSS;
        } else {
            decision.reasonFlags |= DCR_REASON_COLLISION_PRESSURE;
        }
        if (ctx.retry.finalRetry && ctx.retry.quietLoss && !busy && !congested) {
            score += 1;
            decision.reasonFlags |= DCR_REASON_FINAL_RETRY;
        }
    }

    if (channel.dutyCyclePercent < 100.0f) {
        float politeDuty = channel.dutyCyclePercent * 0.5f;
        if (channel.txUtilizationPercent >= politeDuty) {
            // This is not legal enforcement; Router/AirTime already enforce TX.
            // DCR just avoids selecting expensive CRs while local TX airtime is
            // approaching the region's allowance.
            score -= 2;
            decision.reasonFlags |= DCR_REASON_DUTY_CYCLE;
        }
    }

    uint8_t wantedCr = DCR_CR_NORMAL;
    if (score <= -2)
        wantedCr = DCR_CR_SLIM;
    else if (score == -1)
        wantedCr = DCR_CR_SLIM;
    else if (score == 0)
        wantedCr = DCR_CR_NORMAL;
    else if (score == 1)
        wantedCr = DCR_CR_ROBUST;
    else
        wantedCr = DCR_CR_RESCUE;

    // Class clamps apply before global min/max so user configuration remains
    // the final authority. Example: telemetry normally caps at CR 4/6, but an
    // operator can still force min=max=8 for a controlled experiment.
    if (packetClass == DcrPacketClass::Expendable)
        wantedCr = std::min(wantedCr, settings.telemetryMaxCr);
    if (packetClass == DcrPacketClass::Normal)
        wantedCr = std::max(wantedCr, settings.userMinCr);
    if (packetClass == DcrPacketClass::Urgent)
        wantedCr = std::max(wantedCr, settings.alertMinCr);

    // Header-only relay context should not jump to rescue CR without knowing
    // whether the payload is a human alert or a stale background replay.
    if (!classKnown && ctx.relay && wantedCr > DCR_CR_ROBUST)
        wantedCr = DCR_CR_ROBUST;

    wantedCr = std::max(settings.minCr, std::min(settings.maxCr, wantedCr));

    bool urgent = packetClass == DcrPacketClass::Urgent;
    uint32_t predictedAirtime = ctx.predictedAirtimeMs;
    if (airtimeForCr)
        predictedAirtime = airtimeForCr(ctx.packetLen, wantedCr, airtimeContext);

    uint32_t airtimeCap = 0;
    if (packetClass == DcrPacketClass::Expendable)
        airtimeCap = 750;
    else if (packetClass == DcrPacketClass::Normal)
        airtimeCap = idle ? 2200 : 1400;
    else if (packetClass == DcrPacketClass::Control)
        airtimeCap = 1600;

    while (airtimeCap && wantedCr > settings.minCr && predictedAirtime > airtimeCap) {
        wantedCr--;
        decision.reasonFlags |= DCR_REASON_AIRTIME_CAP;
        counters.forcedCompactAirtimeCap++;
        if (airtimeForCr)
            predictedAirtime = airtimeForCr(ctx.packetLen, wantedCr, airtimeContext);
    }

    if (wantedCr == DCR_CR_RESCUE && !robustTokenAllows(predictedAirtime, urgent, settings.robustAirtimePct, ctx.nowMsec)) {
        // CR 4/8 is useful but socially expensive. Non-urgent packets must
        // spend from a local rolling budget so idle telemetry cannot become
        // slow background noise.
        uint8_t beforeClamp = wantedCr;
        wantedCr = std::max<uint8_t>(settings.minCr, std::min<uint8_t>(settings.maxCr, DCR_CR_ROBUST));
        decision.reasonFlags |= DCR_REASON_TOKEN_BUCKET;
        if (wantedCr < beforeClamp)
            counters.forcedCompactTokenBucket++;
        if (airtimeForCr)
            predictedAirtime = airtimeForCr(ctx.packetLen, wantedCr, airtimeContext);
    }

    if ((busy || congested) && wantedCr > DCR_CR_NORMAL && packetClass == DcrPacketClass::Expendable) {
        // Background traffic should be first to go compact when the air gets
        // crowded. User-specified minCr can still override this for lab tests.
        uint8_t beforeClamp = wantedCr;
        wantedCr = std::max<uint8_t>(settings.minCr, DCR_CR_NORMAL);
        decision.reasonFlags |= congested ? DCR_REASON_CONGESTED : DCR_REASON_BUSY;
        if (wantedCr < beforeClamp) {
            counters.forcedCompactCongestion++;
            if (airtimeForCr)
                predictedAirtime = airtimeForCr(ctx.packetLen, wantedCr, airtimeContext);
        }
    }

    wantedCr = std::max(settings.minCr, std::min(settings.maxCr, wantedCr));
    decision.cr = wantedCr;
    decision.changed = wantedCr != ctx.baseCr;
    decision.score = score;
    decision.predictedAirtimeMs = predictedAirtime;
    return decision;
}

void AirtimePolicy::rotateRobustWindow(uint32_t nowMsec)
{
    if (robustWindowStartMsec == 0) {
        robustWindowStartMsec = nowMsec ? nowMsec : 1;
        return;
    }
    if (nowMsec - robustWindowStartMsec >= ROBUST_WINDOW_MSEC) {
        robustWindowStartMsec = nowMsec ? nowMsec : 1;
        robustTotalAirtimeMs = 0;
        robustRescueAirtimeMs = 0;
    }
}

bool AirtimePolicy::robustTokenAllows(uint32_t predictedAirtimeMs, bool urgent, uint8_t robustAirtimePct, uint32_t nowMsec)
{
    if (urgent)
        return true;

    rotateRobustWindow(nowMsec);
    if (robustTotalAirtimeMs == 0)
        return true;

    uint32_t total = robustTotalAirtimeMs + predictedAirtimeMs;
    uint32_t rescue = robustRescueAirtimeMs + predictedAirtimeMs;
    return rescue * 100 <= total * robustAirtimePct;
}

void AirtimePolicy::observeRx(const DcrRxObservation &obs, bool trackNeighborCr)
{
    if (obs.rxCr < DCR_CR_SLIM || obs.rxCr > DCR_CR_RESCUE) {
        counters.rxCrUnknown++;
        return;
    }

    counters.rxCr[crIndex(obs.rxCr)]++;
    if (!trackNeighborCr)
        return;

    NodeNum node = 0;
    if (obs.hopStart != 0 && obs.hopStart == obs.hopLimit) {
        // Direct packet: the RF header CR belongs to the original sender.
        node = obs.from;
    } else if (obs.relayNode != NO_RELAY_NODE) {
        // Relayed packet: the RF header CR belongs to the last transmitter.
        // Only attribute it when the one-byte relay id maps unambiguously.
        node = resolveRelayNode(obs.relayNode);
    }

    if (!node)
        return;

    NeighborCrStats *stats = getOrCreateNeighborStats(node);
    if (!stats)
        return;

    uint8_t idx = crIndex(obs.rxCr);
    if (stats->rxCrHist[idx] < UINT8_MAX)
        stats->rxCrHist[idx]++;
    stats->lastRxCr = obs.rxCr;
    stats->rxCrEwma = stats->rxCrEwma == 0.0f ? obs.rxCr : stats->rxCrEwma * 0.75f + obs.rxCr * 0.25f;
    stats->lastSnr = obs.snr;
    stats->lastRssi = obs.rssi;
    stats->lastSeenMsec = obs.nowMsec;
}

void AirtimePolicy::observeTxStart(const meshtastic_MeshPacket &packet, uint8_t cr, uint32_t airtimeMs, bool urgent,
                                   uint32_t nowMsec)
{
    cr = clampCr(cr);
    counters.txCr[crIndex(cr)]++;
    counters.txAirtimeMsByCr[crIndex(cr)] += airtimeMs;

    rotateRobustWindow(nowMsec);
    robustTotalAirtimeMs += airtimeMs;
    if (cr == DCR_CR_RESCUE && !urgent)
        robustRescueAirtimeMs += airtimeMs;

    // Keep the chosen CR with the packet identity for later ACK/NAK/timeout
    // accounting. The ACK path does not know which CR the original TX used.
    TxCache &slot = txCache[nextTxCache++ % TX_CACHE_SIZE];
    slot.from = getFrom(&packet);
    slot.to = packet.to;
    slot.id = packet.id;
    slot.cr = cr;
}

void AirtimePolicy::observeTxResult(const DcrTxResult &result)
{
    uint8_t cr = result.cr ? result.cr : getLastTxCr(result.from, result.id);
    if (cr < DCR_CR_SLIM || cr > DCR_CR_RESCUE)
        return;

    if (result.success)
        counters.ackSuccessByCr[crIndex(cr)]++;
    else
        counters.ackFailByCr[crIndex(cr)]++;

    if (isBroadcast(result.to))
        return;

    NeighborCrStats *stats = getOrCreateNeighborStats(result.to);
    if (!stats)
        return;

    uint8_t idx = crIndex(cr);
    stats->lastTxCr = cr;
    stats->txAttemptsByCr[idx]++;
    if (result.success)
        stats->txSuccessByCr[idx]++;
    else
        stats->txFailByCr[idx]++;
}

void AirtimePolicy::noteRetransmission(const meshtastic_MeshPacket &packet, uint8_t attempt, bool finalRetry, bool quietLoss)
{
    // Store retry intent as metadata for the next radio-layer decision. The
    // router schedules retries; DCR only consumes the context just before TX.
    RetryCache &slot = retryCache[nextRetryCache++ % RETRY_CACHE_SIZE];
    slot.from = getFrom(&packet);
    slot.id = packet.id;
    slot.attempt = attempt;
    slot.finalRetry = finalRetry;
    slot.quietLoss = quietLoss;
    counters.retryEscalations++;
}

DcrRetryContext AirtimePolicy::getRetryContext(const meshtastic_MeshPacket &packet) const
{
    NodeNum from = getFrom(&packet);
    for (const auto &slot : retryCache) {
        if (slot.from == from && slot.id == packet.id)
            return {slot.attempt, slot.finalRetry, slot.quietLoss};
    }

    return {};
}

uint8_t AirtimePolicy::getLastTxCr(NodeNum from, PacketId id) const
{
    for (const auto &slot : txCache) {
        if (slot.from == from && slot.id == id)
            return slot.cr;
    }

    return 0;
}

const NeighborCrStats *AirtimePolicy::getNeighborStats(NodeNum nodeNum) const
{
    if (!nodeNum)
        return nullptr;

    for (const auto &stats : neighborStats) {
        if (stats.nodeNum == nodeNum)
            return &stats;
    }

    return nullptr;
}

NeighborCrStats *AirtimePolicy::getOrCreateNeighborStats(NodeNum nodeNum)
{
    if (!nodeNum)
        return nullptr;

    for (auto &stats : neighborStats) {
        if (stats.nodeNum == nodeNum)
            return &stats;
    }

    for (auto &stats : neighborStats) {
        if (stats.nodeNum == 0) {
            stats = {};
            stats.nodeNum = nodeNum;
            return &stats;
        }
    }

    NeighborCrStats &slot = neighborStats[nodeNum % NEIGHBOR_CR_SLOTS];
    slot = {};
    slot.nodeNum = nodeNum;
    return &slot;
}

NodeNum AirtimePolicy::resolveRelayNode(uint8_t relayNode) const
{
    if (!nodeDB || relayNode == NO_RELAY_NODE)
        return 0;

    // The packet header stores only the last byte of the relay node. Treat
    // collisions as ambiguous instead of attributing another node's physical
    // CR to the wrong neighbor.
    NodeNum found = 0;
    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || node->num == 0)
            continue;
        if (nodeDB->getLastByteOfNodeNum(node->num) == relayNode) {
            if (found != 0)
                return 0;
            found = node->num;
        }
    }

    return found;
}
