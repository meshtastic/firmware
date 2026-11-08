#pragma once

#include "MeshService.h"
#include "TelemetryBatch.h"
#include "mesh/Channels.h"
#if !MESHTASTIC_EXCLUDE_MQTT
#include "mqtt/MQTT.h"
#endif

/// The direct uplink is configured and the broker is reachable
inline bool telemetryMqttUplinkAvailable()
{
#if !MESHTASTIC_EXCLUDE_MQTT
    return moduleConfig.mqtt.enabled && moduleConfig.mqtt.telemetry_uplink_enabled && mqtt &&
           mqtt->uplinksOwnPacketsOn(channels.getPrimaryIndex());
#else
    return false;
#endif
}

/// Send the readings target has not had yet as one TelemetryRecordHistory, taking ownership of p
/// @return readings sent, 0 if none
template <typename T>
uint16_t sendTelemetryBatch(TelemetryStore<T> &store, meshtastic_MeshPacket *p, TelemetryPublishChannel target)
{
    if (!p)
        return 0;

    const uint16_t maxReadings =
        target == TELEMETRY_PUBLISHED_MESH ? TELEMETRY_MAX_READINGS_PER_MESH_PACKET : TELEMETRY_MAX_READINGS_PER_BATCH;
    uint16_t indices[TELEMETRY_MAX_READINGS_PER_BATCH];
    size_t size = 0;
    const uint16_t taken = encodeTelemetryBatch(store, target, maxReadings, p->decoded.payload.bytes,
                                                sizeof(p->decoded.payload.bytes), size, indices);
    if (taken == 0) {
        packetPool.release(p);
        return 0;
    }

    p->decoded.portnum = meshtastic_PortNum_TELEMETRY_HISTORY_APP;
    p->decoded.payload.size = size;
    p->decoded.want_response = false;
    p->to = NODENUM_BROADCAST;
    p->priority = meshtastic_MeshPacket_Priority_RELIABLE;

    if (target == TELEMETRY_PUBLISHED_MESH) {
        // MQTT::onSend() leaves our batches to the direct uplink when it is on, so no MQTT mark here
        LOG_INFO("Telemetry: send %u buffered readings to mesh, %u bytes", (unsigned)taken, (unsigned)size);
        service->sendToMesh(p, RX_SRC_LOCAL, false);
    } else {
#if !MESHTASTIC_EXCLUDE_MQTT
        const bool sent = mqtt && mqtt->publishOwnPacket(*p);
#else
        const bool sent = false;
#endif
        packetPool.release(p);
        if (!sent)
            return 0;
        LOG_INFO("Telemetry: publish %u buffered readings to MQTT, %u bytes", (unsigned)taken, (unsigned)size);
    }

    for (uint16_t i = 0; i < taken; i++)
        store.markPublished(indices[i], target);
    return taken;
}
