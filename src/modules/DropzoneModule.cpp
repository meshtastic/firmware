#include "DropzoneModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h"
#include "main.h"

#include <assert.h>

#include "telemetry/Sensor/DFRobotLarkSensor.h"
#include "telemetry/UnitConversions.h"

#include <string>

DropzoneModule *dropzoneModule;

int32_t DropzoneModule::runOnce()
{
    // Send on a 5 second delay from rec
    if (startSendConditions != 0 && (startSendConditions + 5000U) < millis()) {
        service.sendToMesh(sendConditions(), RX_SRC_LOCAL);
        startSendConditions = 0;
    }
    // Run every second
    return 1000;
}

ProcessMessage DropzoneModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    auto &p = mp.decoded;
    LOG_DEBUG("DropzoneModule::handleReceived\n");
    char matchCompare[54];
    auto incomingMessage = reinterpret_cast<const char *>(p.payload.bytes);
    sprintf(matchCompare, "%s conditions", owner.short_name);
    if (strncasecmp(incomingMessage, matchCompare, strlen(matchCompare)) == 0) {
        LOG_DEBUG("Received dropzone conditions request\n");
        startSendConditions = millis();
    }

    sprintf(matchCompare, "%s conditions", owner.long_name);
    if (strncasecmp(incomingMessage, matchCompare, strlen(matchCompare)) == 0) {
        LOG_DEBUG("Received dropzone conditions request\n");
        startSendConditions = millis();
    }
    return ProcessMessage::CONTINUE;
}

meshtastic_MeshPacket *DropzoneModule::sendConditions()
{
    char replyStr[200];
    /*
        CLOSED @ {HH:MM:SS}
        Wind 2 kts @ 125°
        29.25 inHg 72°C
    */
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true);
    int hour = 0, min = 0, sec = 0;
    if (rtc_sec > 0) {
        long hms = rtc_sec % SEC_PER_DAY;
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        hour = hms / SEC_PER_HOUR;
        min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        sec = (hms % SEC_PER_HOUR) % SEC_PER_MIN;
    }

    auto dropzoneStatus = analogRead(A1) < 100 ? "OPEN" : "CLOSED";
    LOG_DEBUG("Dropzone status is %s\n", dropzoneStatus);
    auto reply = allocDataPacket();

    auto node = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (sensor.hasSensor()) {
        meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
        sensor.getMetrics(&telemetry);
        LOG_DEBUG("Gathered metrics\n");
        auto windSpeed = UnitConversions::MetersPerSecondToKnots(telemetry.variant.environment_metrics.wind_speed);
        auto windDirection = telemetry.variant.environment_metrics.wind_direction;
        auto temp = telemetry.variant.environment_metrics.temperature;
        auto baro = UnitConversions::HectoPascalToInchesOfMercury(telemetry.variant.environment_metrics.barometric_pressure);

        sprintf(replyStr, "%s @ %02d:%02d:%02d\nWind %.2f kts @ %d°\nBaro %.2f inHg %.2f°C", dropzoneStatus, hour, min, sec,
                windSpeed, windDirection, baro, temp);
        LOG_DEBUG("Conditions reply: %s\n", replyStr);
    } else {
        LOG_ERROR("No sensor found\n");
    }
    reply->decoded.payload.size = strlen(replyStr); // You must specify how many bytes are in the reply
    memcpy(reply->decoded.payload.bytes, replyStr, reply->decoded.payload.size);

    return reply;
}
