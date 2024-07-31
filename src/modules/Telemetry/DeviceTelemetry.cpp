#include "DeviceTelemetry.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "Default.h"
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
    refreshUptime();
    if (((lastSentToMesh == 0) ||
         ((uptimeLastMs - lastSentToMesh) >=
          Default::getConfiguredOrDefaultMsScaled(moduleConfig.telemetry.device_update_interval,
                                                  default_telemetry_broadcast_interval_secs, numOnlineNodes))) &&
        airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
        airTime->isTxAllowedAirUtil() && config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) {
        sendTelemetry();
        lastSentToMesh = uptimeLastMs;
    } else if (service.isToPhoneQueueEmpty()) {
        // Just send to phone when it's not our time to send to mesh yet
        // Only send while queue is empty (phone assumed connected)
        sendTelemetry(NODENUM_BROADCAST, true);
    }
    return sendToPhoneIntervalMs;
}

bool DeviceTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    // Don't worry about storing telemetry in NodeDB if we're a repeater
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER)
        return false;

    if (t->which_variant == meshtastic_Telemetry_device_metrics_tag) {
#ifdef DEBUG_PORT
        const char *sender = getSenderShortName(mp);

        LOG_INFO("(Received from %s): air_util_tx=%f, channel_utilization=%f, battery_level=%i, voltage=%f\n", sender,
                 t->variant.device_metrics.air_util_tx, t->variant.device_metrics.channel_utilization,
                 t->variant.device_metrics.battery_level, t->variant.device_metrics.voltage);
#endif
        nodeDB->updateTelemetry(getFrom(&mp), *t, RX_SRC_RADIO);
    }
    return false; // Let others look at this message also if they want
}

meshtastic_MeshPacket *DeviceTelemetryModule::allocReply()
{
    if (currentRequest) {
        auto req = *currentRequest;
        const auto &p = req.decoded;
        meshtastic_Telemetry scratch;
        meshtastic_Telemetry *decoded = NULL;
        memset(&scratch, 0, sizeof(scratch));
        if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &scratch)) {
            decoded = &scratch;
        } else {
            LOG_ERROR("Error decoding DeviceTelemetry module!\n");
            return NULL;
        }
        // Check for a request for device metrics
        if (decoded->which_variant == meshtastic_Telemetry_device_metrics_tag) {
            LOG_INFO("Device telemetry replying to request\n");

            meshtastic_Telemetry telemetry = getDeviceTelemetry();
            return allocDataProtobuf(telemetry);
        }
    }
    return NULL;
}

meshtastic_Telemetry DeviceTelemetryModule::getDeviceTelemetry()
{
    meshtastic_Telemetry t = meshtastic_Telemetry_init_zero;

    t.time = getTime();
    t.which_variant = meshtastic_Telemetry_device_metrics_tag;
    t.variant.device_metrics.air_util_tx = airTime->utilizationTXPercent();
#if ARCH_PORTDUINO
    t.variant.device_metrics.battery_level = MAGIC_USB_BATTERY_LEVEL;
#else
    t.variant.device_metrics.battery_level =
        powerStatus->getIsCharging() ? MAGIC_USB_BATTERY_LEVEL : powerStatus->getBatteryChargePercent();
#endif
    t.variant.device_metrics.channel_utilization = airTime->channelUtilizationPercent();
    t.variant.device_metrics.voltage = powerStatus->getBatteryVoltageMv() / 1000.0;
    t.variant.device_metrics.uptime_seconds = getUptimeSeconds();

    return t;
}

bool DeviceTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
{
    meshtastic_Telemetry telemetry = getDeviceTelemetry();
    LOG_INFO("(Sending): air_util_tx=%f, channel_utilization=%f, battery_level=%i, voltage=%f, uptime=%i\n",
             telemetry.variant.device_metrics.air_util_tx, telemetry.variant.device_metrics.channel_utilization,
             telemetry.variant.device_metrics.battery_level, telemetry.variant.device_metrics.voltage,
             telemetry.variant.device_metrics.uptime_seconds);

    meshtastic_MeshPacket *p = allocDataProtobuf(telemetry);
    p->to = dest;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

    nodeDB->updateTelemetry(nodeDB->getNodeNum(), telemetry, RX_SRC_LOCAL);
    if (phoneOnly) {
        LOG_INFO("Sending packet to phone\n");
        service.sendToPhone(p);
    } else {
        LOG_INFO("Sending packet to mesh\n");
        service.sendToMesh(p, RX_SRC_LOCAL, true);
    }
    return true;
}