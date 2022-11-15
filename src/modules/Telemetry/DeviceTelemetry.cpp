#include "DeviceTelemetry.h"
#include "../mesh/generated/telemetry.pb.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#include "MeshService.h"

int32_t DeviceTelemetryModule::runOnce()
{
#ifndef ARCH_PORTDUINO
    uint32_t now = millis();
    if ((lastSentToMesh == 0 || 
        (now - lastSentToMesh) >= getConfiguredOrDefaultMs(moduleConfig.telemetry.device_update_interval)) &&
        airTime->channelUtilizationPercent() < max_channel_util_percent) {
        sendTelemetry();
        lastSentToMesh = now;
    } else if (service.isToPhoneQueueEmpty()) {
        // Just send to phone when it's not our time to send to mesh yet
        // Only send while queue is empty (phone assumed connected)
        sendTelemetry(NODENUM_BROADCAST, true);
    }
    return sendToPhoneIntervalMs;
#endif
}

bool DeviceTelemetryModule::handleReceivedProtobuf(const MeshPacket &mp, Telemetry *t)
{
    if (t->which_variant == Telemetry_device_metrics_tag) {
        const char *sender = getSenderShortName(mp);
    
        DEBUG_MSG("(Received from %s): air_util_tx=%f, channel_utilization=%f, battery_level=%i, voltage=%f\n",
            sender,
            t->variant.device_metrics.air_util_tx,
            t->variant.device_metrics.channel_utilization,
            t->variant.device_metrics.battery_level,
            t->variant.device_metrics.voltage);

        lastMeasurementPacket = packetPool.allocCopy(mp);

        nodeDB.updateTelemetry(getFrom(&mp), *t, RX_SRC_RADIO);
    }
    return false; // Let others look at this message also if they want
}

bool DeviceTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
{
    Telemetry t;

    t.time = getTime();
    t.which_variant = Telemetry_device_metrics_tag;

    t.variant.device_metrics.air_util_tx = myNodeInfo.air_util_tx;
    t.variant.device_metrics.battery_level = powerStatus->getBatteryChargePercent();
    t.variant.device_metrics.channel_utilization = myNodeInfo.channel_utilization;
    t.variant.device_metrics.voltage = powerStatus->getBatteryVoltageMv() / 1000.0;

    DEBUG_MSG("(Sending): air_util_tx=%f, channel_utilization=%f, battery_level=%i, voltage=%f\n", 
        t.variant.device_metrics.air_util_tx,
        t.variant.device_metrics.channel_utilization,
        t.variant.device_metrics.battery_level,
        t.variant.device_metrics.voltage);

    MeshPacket *p = allocDataProtobuf(t);
    p->to = dest;
    p->decoded.want_response = false;
    p->priority = MeshPacket_Priority_MIN;

    lastMeasurementPacket = packetPool.allocCopy(*p);
    nodeDB.updateTelemetry(nodeDB.getNodeNum(), t, RX_SRC_LOCAL);
    if (phoneOnly) {
        DEBUG_MSG("Sending packet to phone\n");
        service.sendToPhone(p);
    } else {
        DEBUG_MSG("Sending packet to mesh\n");
        service.sendToMesh(p, RX_SRC_LOCAL, true);
    }
    return true;
}
