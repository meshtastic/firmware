#include "DropzoneModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h"

#include <assert.h>

#include "telemetry/UnitConversions.h"
#include "telemetry/Sensor/DFRobotLarkSensor.h"

void DropzoneModule::alterReceived(meshtastic_MeshPacket &mp)
{
    char matchCompare[54];
    auto incomingMessage = reinterpret_cast<const char *>(&mp.decoded.payload.bytes);
    sprintf(matchCompare, "%s conditions", owner.short_name);
    if (strcmp(incomingMessage, matchCompare) == 0)
    {
        mp.decoded.want_response = true;
        mp.from = NODENUM_BROADCAST;
    }

    sprintf(matchCompare, "%s conditions", owner.long_name);
    if (strcmp(incomingMessage, matchCompare) == 0)
    {
        mp.decoded.want_response = true;
        mp.from = NODENUM_BROADCAST;
    }
}

ProcessMessage DropzoneModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    return ProcessMessage::CONTINUE;
}

meshtastic_MeshPacket *DropzoneModule::allocReply()
{
    assert(currentRequest); // should always be !NULL
    auto req = *currentRequest;
    auto &p = req.decoded;
    // The incoming message is in p.payload
    LOG_INFO("Allocating reply for message from=0x%0x, id=%d, msg=%.*s\n", req.from, req.id, p.payload.size, p.payload.bytes);

    char matchCompare[54];
    auto incomingMessage = reinterpret_cast<const char *>(p.payload.bytes);
    sprintf(matchCompare, "%s conditions", owner.short_name);
    if (strcmp(incomingMessage, matchCompare) == 0)
    {
        return sendConditions();
    }

    sprintf(matchCompare, "%s conditions", owner.long_name);
    if (strcmp(incomingMessage, matchCompare) == 0)
    {
        return sendConditions();
    }
    return NULL;
}

meshtastic_MeshPacket *DropzoneModule::sendConditions()
{
    LOG_DEBUG("Received dropzone conditions request\n");
    char replyStr[200];
    /*
    {DZ / node name} conditions @ {HH:MM:SS}
    Wind 2mph @ 125° ESE
    Temp 75°F Hum 56%
    39.123456, -94.023123
    */
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
    int hour = 0, min = 0, sec = 0;
    if (rtc_sec > 0)
    {
        long hms = rtc_sec % SEC_PER_DAY;
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        hour = hms / SEC_PER_HOUR;
        min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        sec = (hms % SEC_PER_HOUR) % SEC_PER_MIN;
    }

    auto isDropzoneClear = true ? "Clear" : "Closed";
    auto reply = allocDataPacket();

    auto node = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (sensor.hasSensor())
    {
        meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
        sensor.getMetrics(&telemetry);
        LOG_DEBUG("Gathered metrics\n");
        auto windSpeed = UnitConversions::MetersPerSecondToMilesPerHour(telemetry.variant.environment_metrics.wind_speed);
        auto windDirection = telemetry.variant.environment_metrics.wind_direction;
        auto temp = UnitConversions::CelsiusToFahrenheit(telemetry.variant.environment_metrics.temperature);
        auto hum = telemetry.variant.environment_metrics.relative_humidity;
        auto baro = telemetry.variant.environment_metrics.barometric_pressure;

        sprintf(replyStr, "%s - %s @ %02d:%02d:%02d\nWind %.2fmph @ %d°\n%.2f hpA  %.2f°F %.2f%% humidity\n%f, %f", owner.long_name, isDropzoneClear, hour, min, sec, windSpeed, windDirection, baro, temp, hum, node->position.latitude_i * 1e-7, node->position.longitude_i * 1e-7);
        LOG_DEBUG("Conditions reply: %s\n", replyStr);
    }
    else
    {
        LOG_ERROR("No sensor found\n");
        sprintf(replyStr, "%s - %s @ %02d:%02d:%02d\nConditions unavailable\n%d, %d", owner.long_name, isDropzoneClear, hour, min, sec, node->position.latitude_i * 1e-7, node->position.longitude_i * 1e-7);
    }
    reply->decoded.payload.size = strlen(replyStr); // You must specify how many bytes are in the reply
    memcpy(reply->decoded.payload.bytes, replyStr, reply->decoded.payload.size);

    return reply;
}
