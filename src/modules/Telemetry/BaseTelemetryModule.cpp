#include "BaseTelemetryModule.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR
#include "AirQualityTelemetry.h"
#endif

/**
 * Build a packet from an already-populated reading and send it to the mesh or the phone.
 * @return true if the packet was built and handed off to send
 */
bool BaseTelemetryModule::publishTelemetry(meshtastic_Telemetry &m, NodeNum dest, bool phoneOnly)
{
    meshtastic_MeshPacket *p = allocTelemetryPacket(m);
    if (!p)
        return false;

    p->to = dest;
    p->decoded.want_response = false;
    p->priority = config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR ? meshtastic_MeshPacket_Priority_RELIABLE
                                                                                   : meshtastic_MeshPacket_Priority_BACKGROUND;

    onPublishedTelemetry(*p);

    if (phoneOnly) {
        LOG_INFO("Sending packet to phone");
        service->sendToPhone(p);
    } else {
        LOG_INFO("Sending packet to mesh");
        service->sendToMesh(p, RX_SRC_LOCAL, true);
    }
    return true;
}

// Mesh is airtime-constrained, so its packets stay well under the wire array's full capacity
// regardless of how big that capacity is. Mqtt has no equivalent per-packet cost, so it can use
// the array's full capacity - the actual per-packet ceiling remains the 233-byte
// meshtastic_Data.payload limit, still enforced dynamically below by the shed-and-retry loop
// regardless of this cap; this only raises how many we're willing to *attempt* fitting.
constexpr size_t kMaxReadingsPerMeshPacket = MESHTASTIC_MAX_READINGS_PER_MESH_PACKET;
constexpr size_t kMaxReadingsPerMqttPacket =
    sizeof(meshtastic_TelemetryRecordHistory::readings) / sizeof(meshtastic_TelemetryRecordHistory::readings[0]);
static_assert(kMaxReadingsPerMeshPacket <= kMaxReadingsPerMqttPacket, "mesh's cap is meant to be the smaller of the two");

/**
 * Encode up to maxTake readings (indices, oldest first) into p's payload, shedding the newest of
 * the batch and retrying until it fits the 233-byte meshtastic_Data.payload limit.
 *
 * Deliberately its own non-inlined function to avoid stack problems.
 */
template <typename T, uint8_t N>
static __attribute__((noinline)) size_t encodeHistoryBatch(meshtastic_MeshPacket &p, const TelemetryHistoryBuffer<T, N> &history,
                                                           const uint8_t *indices, size_t maxTake, uint32_t intervalSec)
{
    size_t take = maxTake;
    while (take > 0) {
        meshtastic_TelemetryRecordHistory recordHistory = meshtastic_TelemetryRecordHistory_init_zero;

        recordHistory.interval_sec = intervalSec;
        recordHistory.readings_count = take;

        for (size_t i = 0; i < take; i++) {
            const BufferedReading<T> &reading = history.at(indices[i]);
            assignTelemetryRecord(recordHistory.readings[i], reading.metrics, reading.time);
        }

        p.decoded.payload.size = pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes),
                                                    &meshtastic_TelemetryRecordHistory_msg, &recordHistory);
        if (p.decoded.payload.size > 0)
            return take;
        take--;
    }
    return 0;
}

/**
 * Publish every reading in history not yet marked for target's channel as a single
 * TelemetryRecordHistory
 *
 * @param intervalSec seconds between individual readings in the buffer (the caller's
 *        configured/default read interval), stamped into the outgoing packet
 * @return true if a packet was sent
 */
template <typename T, uint8_t N>
bool BaseTelemetryModule::publishBufferedTelemetry(TelemetryHistoryBuffer<T, N> &history, PublishTarget target,
                                                   uint32_t intervalSec)
{
    if (history.isEmpty())
        return false;

    TelemetryPublishChannel channelBit = target == PublishTarget::Mesh ? TELEMETRY_PUBLISHED_MESH : TELEMETRY_PUBLISHED_MQTT;

    // Oldest-first indices not yet delivered to this channel; readings already sent on a
    // previous call (or on the other channel's buffer, which has its own mask) are skipped.
    uint8_t indices[N];
    uint8_t unpublishedCount = 0;

    for (uint8_t i = 0; i < history.size(); i++) {
        if (!(history.at(i).publishedMask & channelBit))
            indices[unpublishedCount++] = i;
    }

    if (unpublishedCount == 0)
        return false;

    meshtastic_MeshPacket *p = allocTelemetryHistoryPacket();
    if (!p)
        return false;

    p->decoded.portnum = meshtastic_PortNum_TELEMETRY_HISTORY_APP;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_RELIABLE;

    const size_t maxReadingsPerPacket = target == PublishTarget::Mesh ? kMaxReadingsPerMeshPacket : kMaxReadingsPerMqttPacket;
    size_t take = encodeHistoryBatch(*p, history, indices, min((size_t)unpublishedCount, maxReadingsPerPacket), intervalSec);

    if (take == 0) {
        packetPool.release(p);
        return false;
    }

    // Attempt delivery
    bool sent = false;
    if (target == PublishTarget::Mesh) {
        // TODO(mqtt-dedup): if this node also has PublishTarget::Mqtt active (moduleConfig.
        // mqtt.telemetry_uplink_enabled + mqtt->isConnectedDirectly()) AND the channel this
        // goes out on has uplink_enabled, the Router's normal channel-uplink hook
        // (MQTT::onSend) will ALSO publish this same batch to MQTT - so these same readings
        // are still marked "unpublished for Mqtt" and get sent AGAIN, on the next
        // direct-publish cycle.

        // fire-and-forget enqueue
        service->sendToMesh(p, RX_SRC_LOCAL, false);
        sent = true;
    } else if (target == PublishTarget::Mqtt) {
        // Mqtt target: publish straight to broker connection
        sent = mqtt && mqtt->publishOwnPacket(*p);
        packetPool.release(p);
    } else {
        // Phone (or anything else) isn't a supported target for buffered history
        LOG_WARN("publishBufferedTelemetry: unsupported PublishTarget, dropping");
        packetPool.release(p);
        return false;
    }

    if (!sent)
        return false;

    for (size_t i = 0; i < take; i++)
        history.markPublished(indices[i], channelBit);

    LOG_INFO("Publishing %u/%u buffered telemetry readings to %s (%u still pending for this channel)", (unsigned)take,
             (unsigned)unpublishedCount, target == PublishTarget::Mesh ? "mesh" : "mqtt", (unsigned)(unpublishedCount - take));

    return true;
}

// Register each concrete (metrics type, buffer size) combination that actually uses
// publishBufferedTelemetry.
#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR
template bool BaseTelemetryModule::publishBufferedTelemetry<meshtastic_AirQualityMetrics, AIR_QUALITY_TELEMETRY_HISTORY_SIZE>(
    TelemetryHistoryBuffer<meshtastic_AirQualityMetrics, AIR_QUALITY_TELEMETRY_HISTORY_SIZE> &, PublishTarget, uint32_t);
#endif
