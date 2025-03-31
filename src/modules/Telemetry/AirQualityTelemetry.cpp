#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "AirQualityTelemetry.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "UnitConversions.h"
#include "detect/ScanI2CTwoWire.h"
#include "graphics/ScreenFonts.h"
#include "main.h"
#include "sleep.h"
#include <Throttle.h>

// Sensors
#include "Sensor/PMSA0031Sensor.h"
#include "Sensor/SCD4XSensor.h"

SCD4XSensor scd4xSensor;
PMSA0031Sensor pmsa0031Sensor;

int32_t AirQualityTelemetryModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        sleepOnNextExecution = false;
        uint32_t nightyNightMs = Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.environment_update_interval,
                                                                   default_telemetry_broadcast_interval_secs);
        LOG_DEBUG("Sleeping for %ims, then awaking to send metrics again.", nightyNightMs);
        doDeepSleep(nightyNightMs, true);
    }

    uint32_t result = UINT32_MAX;

    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */
    // moduleConfig.telemetry.air_quality_enabled = 1;

    if (!(moduleConfig.telemetry.air_quality_enabled)) {
        // If this module is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return disable();
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = false;

        if (moduleConfig.telemetry.air_quality_enabled) {
            LOG_INFO("Air quality Telemetry: init");
            if (aqi_found.address == 0x00) {
#ifndef I2C_NO_RESCAN
                LOG_WARN("Rescan for I2C AQI Sensor");
                // rescan for late arriving sensors. AQI Module starts about 10 seconds into the boot so this is plenty.
                uint8_t i2caddr_scan[] = {PMSA0031_ADDR, SCD4X_ADDR};
                uint8_t i2caddr_asize = 2;
                auto i2cScanner = std::unique_ptr<ScanI2CTwoWire>(new ScanI2CTwoWire());

#if WIRE_INTERFACES_COUNT == 2
                i2cScanner->scanPort(ScanI2C::I2CPort::WIRE1, i2caddr_scan, i2caddr_asize);
#endif
                i2cScanner->scanPort(ScanI2C::I2CPort::WIRE, i2caddr_scan, i2caddr_asize);

                auto found = i2cScanner->find(ScanI2C::DeviceType::PMSA0031);
                if (found.type != ScanI2C::DeviceType::NONE) {
                    nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_PMSA003I].first = found.address.address;
                    nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_PMSA003I].second =
                        i2cScanner->fetchI2CBus(found.address);
                    return setStartDelay();
                }
                if (aqi_found.address == 0x00) {
                    return disable();
                }
 #endif
            }
            if (scd4xSensor.hasSensor())
                result = scd4xSensor.runOnce();
            if (pmsa0031Sensor.hasSensor())
                result = pmsa0031Sensor.runOnce();
            return result;

        } else {
            // if we somehow got to a second run of this module with measurement disabled, then just wait forever
            if (!moduleConfig.telemetry.air_quality_enabled)
                return disable();

            if (((lastSentToMesh == 0) ||
                 !Throttle::isWithinTimespanMs(lastSentToMesh, Default::getConfiguredOrDefaultMsScaled(
                                                                   moduleConfig.telemetry.air_quality_interval,
                                                                   default_telemetry_broadcast_interval_secs, numOnlineNodes))) &&
                airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
                airTime->isTxAllowedAirUtil()) {
                sendTelemetry();
                lastSentToMesh = millis();
            } else if (((lastSentToPhone == 0) || !Throttle::isWithinTimespanMs(lastSentToPhone, sendToPhoneIntervalMs)) &&
                       (service->isToPhoneQueueEmpty())) {
                // Just send to phone when it's not our time to send to mesh yet
                // Only send while queue is empty (phone assumed connected)
                sendTelemetry(NODENUM_BROADCAST, true);
                lastSentToPhone = millis();
            }
            return setStartDelay();
        }
        return disable();
    } else {
        // if we somehow got to a second run of this module with measurement disabled, then just wait forever
        if (!moduleConfig.telemetry.air_quality_enabled)
            return disable();

        if (((lastSentToMesh == 0) ||
             !Throttle::isWithinTimespanMs(lastSentToMesh, Default::getConfiguredOrDefaultMsScaled(
                                                               moduleConfig.telemetry.air_quality_interval,
                                                               default_telemetry_broadcast_interval_secs, numOnlineNodes))) &&
            airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
            airTime->isTxAllowedAirUtil()) {
            sendTelemetry();
            lastSentToMesh = millis();
        } else if (service->isToPhoneQueueEmpty()) {
            // Just send to phone when it's not our time to send to mesh yet
            // Only send while queue is empty (phone assumed connected)
            sendTelemetry(NODENUM_BROADCAST, true);
        }
        return min(sendToPhoneIntervalMs, result);
    }
}

bool AirQualityTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    if (t->which_variant == meshtastic_Telemetry_air_quality_metrics_tag) {
#ifdef DEBUG_PORT
        const char *sender = getSenderShortName(mp);

        LOG_INFO("(Received from %s): pm10_standard=%i, pm25_standard=%i, pm100_standard=%i, co2=%i ppm", sender,
                 t->variant.air_quality_metrics.pm10_standard, t->variant.air_quality_metrics.pm25_standard,
                 t->variant.air_quality_metrics.pm100_standard, t->variant.air_quality_metrics.co2);

        LOG_INFO("                  | PM1.0(Environmental)=%i, PM2.5(Environmental)=%i, PM10.0(Environmental)=%i",
                 t->variant.air_quality_metrics.pm10_environmental, t->variant.air_quality_metrics.pm25_environmental,
                 t->variant.air_quality_metrics.pm100_environmental);
#endif
        // release previous packet before occupying a new spot
        if (lastMeasurementPacket != nullptr)
            packetPool.release(lastMeasurementPacket);

        lastMeasurementPacket = packetPool.allocCopy(mp);
    }

    return false; // Let others look at this message also if they want
}

bool AirQualityTelemetryModule::wantUIFrame()
{
    return moduleConfig.telemetry.environment_screen_enabled;
}

void AirQualityTelemetryModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    if (lastMeasurementPacket == nullptr) {
        // If there's no valid packet, display "Environment"
        display->drawString(x, y, "Air Quality");
        display->drawString(x, y += _fontHeight(FONT_SMALL), "No measurement");
        return;
    }

    // Decode the last measurement packet
    meshtastic_Telemetry lastMeasurement;
    uint32_t agoSecs = service->GetTimeSinceMeshPacket(lastMeasurementPacket);
    const char *lastSender = getSenderShortName(*lastMeasurementPacket);

    const meshtastic_Data &p = lastMeasurementPacket->decoded;
    if (!pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &lastMeasurement)) {
        display->drawString(x, y, "Measurement Error");
        LOG_ERROR("Unable to decode last packet");
        return;
    }

    // Display "Env. From: ..." on its own
    display->drawString(x, y, "AQ. From: " + String(lastSender) + "(" + String(agoSecs) + "s)");

    m->time = getTime();
    m->which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
    m->variant.air_quality_metrics.has_pm10_standard = true;
    m->variant.air_quality_metrics.pm10_standard = data.pm10_standard;
    m->variant.air_quality_metrics.has_pm25_standard = true;
    m->variant.air_quality_metrics.pm25_standard = data.pm25_standard;
    m->variant.air_quality_metrics.has_pm100_standard = true;
    m->variant.air_quality_metrics.pm100_standard = data.pm100_standard;

    m->variant.air_quality_metrics.has_pm10_environmental = true;
    m->variant.air_quality_metrics.pm10_environmental = data.pm10_env;
    m->variant.air_quality_metrics.has_pm25_environmental = true;
    m->variant.air_quality_metrics.pm25_environmental = data.pm25_env;
    m->variant.air_quality_metrics.has_pm100_environmental = true;
    m->variant.air_quality_metrics.pm100_environmental = data.pm100_env;

    LOG_INFO("Send: PM1.0(Standard)=%i, PM2.5(Standard)=%i, PM10.0(Standard)=%i", m->variant.air_quality_metrics.pm10_standard,
             m->variant.air_quality_metrics.pm25_standard, m->variant.air_quality_metrics.pm100_standard);

    if (lastMeasurement.variant.air_quality_metrics.has_pm10_standard) {
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "PM1.0(Standard): " + String(lastMeasurement.variant.air_quality_metrics.pm10_standard, 0));
    }
    if (lastMeasurement.variant.air_quality_metrics.has_pm25_standard) {
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "PM2.5(Standard): " + String(lastMeasurement.variant.air_quality_metrics.pm25_standard, 0));
    }
    if (lastMeasurement.variant.air_quality_metrics.has_pm10_environmental) {
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "PM10.0(Standard): " + String(lastMeasurement.variant.air_quality_metrics.pm100_standard, 0));
    }
    if (lastMeasurement.variant.air_quality_metrics.has_co2) {
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "CO2: " + String(lastMeasurement.variant.air_quality_metrics.co2, 0) + " ppm");
    }
}

bool AirQualityTelemetryModule::getAirQualityTelemetry(meshtastic_Telemetry *m)
{
    bool valid = true;
    bool hasSensor = false;
    m->time = getTime();
    m->which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
    m->variant.air_quality_metrics = meshtastic_AirQualityMetrics_init_zero;

    if (scd4xSensor.hasSensor()) {
        valid = valid && scd4xSensor.getMetrics(m);
        hasSensor = true;
    }
    if (pmsa0031Sensor.hasSensor()) {
        valid = valid && pmsa0031Sensor.getMetrics(m);
        hasSensor = true;
    }

    return valid && hasSensor;
}

meshtastic_MeshPacket *AirQualityTelemetryModule::allocReply()
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
            LOG_ERROR("Error decoding AirQualityTelemetry module!");
            return NULL;
        }
        // Check for a request for air quality metrics
        if (decoded->which_variant == meshtastic_Telemetry_air_quality_metrics_tag) {
            meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
            if (getAirQualityTelemetry(&m)) {
                LOG_INFO("Air quality telemetry reply to request");
                return allocDataProtobuf(m);
            } else {
                return NULL;
            }
        }
    }
    return NULL;
}

bool AirQualityTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
{
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    if (getAirQualityTelemetry(&m)) {
        LOG_INFO("(Sending): PM1.0(Standard)=%i, PM2.5(Standard)=%i, PM10.0(Standard)=%i, cO2=%i ppm",
                 m.variant.air_quality_metrics.pm10_standard, m.variant.air_quality_metrics.pm25_standard,
                 m.variant.air_quality_metrics.pm100_standard, m.variant.air_quality_metrics.co2);

        LOG_INFO("         | PM1.0(Environmental)=%i, PM2.5(Environmental)=%i, PM10.0(Environmental)=%i",
                 m.variant.air_quality_metrics.pm10_environmental, m.variant.air_quality_metrics.pm25_environmental,
                 m.variant.air_quality_metrics.pm100_environmental);

        meshtastic_MeshPacket *p = allocDataProtobuf(m);
        p->to = dest;
        p->decoded.want_response = false;
        if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR)
            p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
        else
            p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

        // release previous packet before occupying a new spot
        if (lastMeasurementPacket != nullptr)
            packetPool.release(lastMeasurementPacket);

        lastMeasurementPacket = packetPool.allocCopy(*p);
        if (phoneOnly) {
            LOG_INFO("Send packet to phone");
            service->sendToPhone(p);
        } else {
            LOG_INFO("Send packet to mesh");
            service->sendToMesh(p, RX_SRC_LOCAL, true);
        }
        return true;
    }

    return false;
}

#endif