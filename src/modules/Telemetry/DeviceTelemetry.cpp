#include "DeviceTelemetry.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

#define MAGIC_USB_BATTERY_LEVEL 101

int32_t DeviceTelemetryModule::runOnce()
{
    uint32_t now = millis();
    if (((lastSentToMesh == 0) ||
         ((now - lastSentToMesh) >= getConfiguredOrDefaultMs(moduleConfig.telemetry.device_update_interval))) &&
        airTime->isTxAllowedChannelUtil() && airTime->isTxAllowedAirUtil()) {
        sendTelemetry();
        lastSentToMesh = now;
    } else if (service.isToPhoneQueueEmpty()) {
        // Just send to phone when it's not our time to send to mesh yet
        // Only send while queue is empty (phone assumed connected)
        sendTelemetry(NODENUM_BROADCAST, true);
    }
    return sendToPhoneIntervalMs;
}

bool DeviceTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    if (t->which_variant == meshtastic_Telemetry_device_metrics_tag) {
        const char *sender = getSenderShortName(mp);

        LOG_INFO("(Received from %s): air_util_tx=%f, channel_utilization=%f, battery_level=%i, voltage=%f\n", sender,
                 t->variant.device_metrics.air_util_tx, t->variant.device_metrics.channel_utilization,
                 t->variant.device_metrics.battery_level, t->variant.device_metrics.voltage);

        nodeDB.updateTelemetry(getFrom(&mp), *t, RX_SRC_RADIO);
    }
    return false; // Let others look at this message also if they want
}

bool DeviceTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
{
    meshtastic_Telemetry t;

    t.time = getTime();
    t.which_variant = meshtastic_Telemetry_device_metrics_tag;

    t.variant.device_metrics.air_util_tx = myNodeInfo.air_util_tx;
    if (powerStatus->getIsCharging()) {
        t.variant.device_metrics.battery_level = MAGIC_USB_BATTERY_LEVEL;
    } else {
        t.variant.device_metrics.battery_level = powerStatus->getBatteryChargePercent();
    }

    t.variant.device_metrics.channel_utilization = myNodeInfo.channel_utilization;
    t.variant.device_metrics.voltage = powerStatus->getBatteryVoltageMv() / 1000.0;

    LOG_INFO("(Sending): air_util_tx=%f, channel_utilization=%f, battery_level=%i, voltage=%f\n",
             t.variant.device_metrics.air_util_tx, t.variant.device_metrics.channel_utilization,
             t.variant.device_metrics.battery_level, t.variant.device_metrics.voltage);

    meshtastic_MeshPacket *p = allocDataProtobuf(t);
    p->to = dest;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_MIN;

    nodeDB.updateTelemetry(nodeDB.getNodeNum(), t, RX_SRC_LOCAL);
    if (phoneOnly) {
        LOG_INFO("Sending packet to phone\n");
        service.sendToPhone(p);
    } else {
        LOG_INFO("Sending packet to mesh\n");
        service.sendToMesh(p, RX_SRC_LOCAL, true);
    }
    return true;
}
