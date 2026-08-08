#include "CellularTelemetry.h"

#if HAS_CELLULAR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "TransmitHistory.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "main.h"
#include "mesh/cell/cellDiag.h"

static constexpr uint16_t TX_HISTORY_KEY_CELLULAR_TELEMETRY = 0x8006;

int32_t CellularTelemetryModule::runOnce()
{
    uint32_t lastTelemetry = transmitHistory ? transmitHistory->getLastSentToMeshMillis(TX_HISTORY_KEY_CELLULAR_TELEMETRY) : 0;
    if (((lastTelemetry == 0) ||
         ((millis() - lastTelemetry) >= Default::getConfiguredOrDefaultMsScaled(moduleConfig.telemetry.cellular_update_interval,
                                                                                default_telemetry_broadcast_interval_secs,
                                                                                numOnlineNodes, TrafficType::TELEMETRY))) &&
        airTime->isTxAllowedChannelUtil(true) && airTime->isTxAllowedAirUtil()) {
        if (sendTelemetry() && transmitHistory)
            transmitHistory->setLastSentToMesh(TX_HISTORY_KEY_CELLULAR_TELEMETRY);
    } else if (service->isToPhoneQueueEmpty()) {
        // Just send to phone when it's not our time to send to mesh yet
        if (lastSentToPhone == 0 || (millis() - lastSentToPhone) >= sendToPhoneIntervalMs) {
            sendTelemetry(NODENUM_BROADCAST, true);
            lastSentToPhone = millis();
        }
    }
    return sendToPhoneIntervalMs;
}

bool CellularTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    if (t->which_variant == meshtastic_Telemetry_cellular_metrics_tag)
        nodeDB->updateTelemetry(getFrom(&mp), *t, RX_SRC_RADIO);
    return false; // Let others look at this message also if they want
}

meshtastic_MeshPacket *CellularTelemetryModule::allocReply()
{
    if (currentRequest) {
        if (isMultiHopBroadcastRequest() && !isSensorOrRouterRole()) {
            ignoreRequest = true;
            return NULL;
        }
        auto req = *currentRequest;
        const auto &p = req.decoded;
        meshtastic_Telemetry scratch;
        meshtastic_Telemetry *decoded = NULL;
        memset(&scratch, 0, sizeof(scratch));
        if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &scratch)) {
            decoded = &scratch;
        } else {
            LOG_ERROR("Error decoding CellularTelemetry module!");
            return NULL;
        }
        if (decoded->which_variant == meshtastic_Telemetry_cellular_metrics_tag) {
            meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
            if (getCellularTelemetry(&m)) {
                LOG_INFO("Cellular telemetry reply to request");
                return allocDataProtobuf(m);
            }
            return NULL;
        }
    }
    return NULL;
}

bool CellularTelemetryModule::getCellularTelemetry(meshtastic_Telemetry *m)
{
    const CellDiag &diag = getCellDiag();
    if (!diag.rsrpValid && !diag.bandValid && !diag.vbatValid)
        return false;

    m->time = getTime();
    m->which_variant = meshtastic_Telemetry_cellular_metrics_tag;
    m->variant.cellular_metrics = meshtastic_CellularMetrics_init_zero;

    if (diag.rsrpValid) {
        m->variant.cellular_metrics.has_rsrp = true;
        m->variant.cellular_metrics.rsrp = diag.rsrp;
        m->variant.cellular_metrics.has_rsrq = true;
        m->variant.cellular_metrics.rsrq = diag.rsrq;
    }
    if (diag.bandValid) {
        m->variant.cellular_metrics.has_band = true;
        m->variant.cellular_metrics.band = diag.band;
        m->variant.cellular_metrics.has_access_tech = true;
        m->variant.cellular_metrics.access_tech = diag.act;
        m->variant.cellular_metrics.has_operator_name = true;
        strncpy(m->variant.cellular_metrics.operator_name, diag.operatorName, sizeof(m->variant.cellular_metrics.operator_name));
    }
    if (diag.vbatValid) {
        m->variant.cellular_metrics.has_modem_voltage_mv = true;
        m->variant.cellular_metrics.modem_voltage_mv = diag.vbatMv;
    }
    return true;
}

bool CellularTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
{
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    if (!getCellularTelemetry(&m))
        return false;

    meshtastic_MeshPacket *p = allocDataProtobuf(m);
    if (!p)
        return false;
    p->to = dest;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

    nodeDB->updateTelemetry(nodeDB->getNodeNum(), m, RX_SRC_LOCAL);
    if (phoneOnly) {
        service->sendToPhone(p);
    } else {
        service->sendToMesh(p, RX_SRC_LOCAL, true);
    }
    return true;
}

#endif
