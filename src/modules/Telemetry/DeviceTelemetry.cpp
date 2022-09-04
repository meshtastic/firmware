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

int32_t DeviceTelemetryModule::runOnce()
{
#ifndef ARCH_PORTDUINO
    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = 0;
        DEBUG_MSG("Device Telemetry: Initializing\n");
    }
    sendOurTelemetry();
    // OSThread library.  Multiply the preference value by 1000 to convert seconds to miliseconds

    return getConfiguredOrDefaultMs(moduleConfig.telemetry.device_update_interval);
#endif
}

bool DeviceTelemetryModule::handleReceivedProtobuf(const MeshPacket &mp, Telemetry *t)
{
    if (t->which_variant == Telemetry_device_metrics_tag) {
        const char *sender = getSenderShortName(mp);

        DEBUG_MSG("-----------------------------------------\n");
        DEBUG_MSG("Device Telemetry: Received data from %s\n", sender);
        DEBUG_MSG("Telemetry->time: %i\n", t->time);
        DEBUG_MSG("Telemetry->air_util_tx: %f\n", t->variant.device_metrics.air_util_tx);
        DEBUG_MSG("Telemetry->battery_level: %i\n", t->variant.device_metrics.battery_level);
        DEBUG_MSG("Telemetry->channel_utilization: %f\n", t->variant.device_metrics.channel_utilization);
        DEBUG_MSG("Telemetry->voltage: %f\n", t->variant.device_metrics.voltage);

        lastMeasurementPacket = packetPool.allocCopy(mp);

        nodeDB.updateTelemetry(getFrom(&mp), *t, RX_SRC_RADIO);
    }
    return false; // Let others look at this message also if they want
}

bool DeviceTelemetryModule::sendOurTelemetry(NodeNum dest, bool wantReplies)
{
    Telemetry t;

    t.time = getTime();
    t.which_variant = Telemetry_device_metrics_tag;

    t.variant.device_metrics.air_util_tx = myNodeInfo.air_util_tx;
    t.variant.device_metrics.battery_level = powerStatus->getBatteryChargePercent();
    t.variant.device_metrics.channel_utilization = myNodeInfo.channel_utilization;
    t.variant.device_metrics.voltage = powerStatus->getBatteryVoltageMv() / 1000.0;

    DEBUG_MSG("-----------------------------------------\n");
    DEBUG_MSG("Device Telemetry: Read data\n");

    DEBUG_MSG("Telemetry->time: %i\n", t.time);
    DEBUG_MSG("Telemetry->air_util_tx: %f\n", t.variant.device_metrics.air_util_tx);
    DEBUG_MSG("Telemetry->battery_level: %i\n", t.variant.device_metrics.battery_level);
    DEBUG_MSG("Telemetry->channel_utilization: %f\n", t.variant.device_metrics.channel_utilization);
    DEBUG_MSG("Telemetry->voltage: %f\n", t.variant.device_metrics.voltage);

    MeshPacket *p = allocDataProtobuf(t);
    p->to = dest;
    p->decoded.want_response = wantReplies;

    lastMeasurementPacket = packetPool.allocCopy(*p);
    DEBUG_MSG("Device Telemetry: Sending packet to mesh\n");
    service.sendToMesh(p, RX_SRC_LOCAL, true);
    nodeDB.updateTelemetry(nodeDB.getNodeNum(), t, RX_SRC_LOCAL);
    return true;
}
