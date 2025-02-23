#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && !MESHTASTIC_EXCLUDE_HEALTH_TELEMETRY && !defined(ARCH_PORTDUINO)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "Default.h"
#include "HealthTelemetry.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "UnitConversions.h"
#include "main.h"
#include "power.h"
#include "sleep.h"
#include "target_specific.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

// Sensors
#include "Sensor/MAX30102Sensor.h"
#include "Sensor/MLX90614Sensor.h"

MAX30102Sensor max30102Sensor;
MLX90614Sensor mlx90614Sensor;

#define FAILED_STATE_SENSOR_READ_MULTIPLIER 10
#define DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN true

#if (HAS_SCREEN)
#include "graphics/ScreenFonts.h"
#endif
#include <Throttle.h>

int32_t HealthTelemetryModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        sleepOnNextExecution = false;
        uint32_t nightyNightMs = Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.health_update_interval,
                                                                   default_telemetry_broadcast_interval_secs);
        LOG_DEBUG("Sleep for %ims, then awake to send metrics again", nightyNightMs);
        doDeepSleep(nightyNightMs, true, false);
    }

    uint32_t result = UINT32_MAX;

    if (!(moduleConfig.telemetry.health_measurement_enabled || moduleConfig.telemetry.health_screen_enabled)) {
        // If this module is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return disable();
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = false;

        if (moduleConfig.telemetry.health_measurement_enabled) {
            LOG_INFO("Health Telemetry: init");
            // Initialize sensors
            if (mlx90614Sensor.hasSensor())
                result = mlx90614Sensor.runOnce();
            if (max30102Sensor.hasSensor())
                result = max30102Sensor.runOnce();
        }
        return result;
    } else {
        // if we somehow got to a second run of this module with measurement disabled, then just wait forever
        if (!moduleConfig.telemetry.health_measurement_enabled) {
            return disable();
        }

        if (((lastSentToMesh == 0) ||
             !Throttle::isWithinTimespanMs(lastSentToMesh, Default::getConfiguredOrDefaultMsScaled(
                                                               moduleConfig.telemetry.health_update_interval,
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
    }
    return min(sendToPhoneIntervalMs, result);
}

bool HealthTelemetryModule::wantUIFrame()
{
    return moduleConfig.telemetry.health_screen_enabled;
}

void HealthTelemetryModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    if (lastMeasurementPacket == nullptr) {
        // If there's no valid packet, display "Health"
        display->drawString(x, y, "Health");
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

    // Display "Health From: ..." on its own
    display->drawString(x, y, "Health From: " + String(lastSender) + "(" + String(agoSecs) + "s)");

    String last_temp = String(lastMeasurement.variant.health_metrics.temperature, 0) + "°C";
    if (moduleConfig.telemetry.environment_display_fahrenheit) {
        last_temp = String(UnitConversions::CelsiusToFahrenheit(lastMeasurement.variant.health_metrics.temperature), 0) + "°F";
    }

    // Continue with the remaining details
    display->drawString(x, y += _fontHeight(FONT_SMALL), "Temp: " + last_temp);
    if (lastMeasurement.variant.health_metrics.has_heart_bpm) {
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "Heart Rate: " + String(lastMeasurement.variant.health_metrics.heart_bpm, 0) + " bpm");
    }
    if (lastMeasurement.variant.health_metrics.has_spO2) {
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "spO2: " + String(lastMeasurement.variant.health_metrics.spO2, 0) + " %");
    }
}

bool HealthTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    if (t->which_variant == meshtastic_Telemetry_health_metrics_tag) {
#ifdef DEBUG_PORT
        const char *sender = getSenderShortName(mp);

        LOG_INFO("(Received from %s): temperature=%f, heart_bpm=%d, spO2=%d,", sender, t->variant.health_metrics.temperature,
                 t->variant.health_metrics.heart_bpm, t->variant.health_metrics.spO2);

#endif
        // release previous packet before occupying a new spot
        if (lastMeasurementPacket != nullptr)
            packetPool.release(lastMeasurementPacket);

        lastMeasurementPacket = packetPool.allocCopy(mp);
    }

    return false; // Let others look at this message also if they want
}

bool HealthTelemetryModule::getHealthTelemetry(meshtastic_Telemetry *m)
{
    bool valid = true;
    bool hasSensor = false;
    m->time = getTime();
    m->which_variant = meshtastic_Telemetry_health_metrics_tag;
    m->variant.health_metrics = meshtastic_HealthMetrics_init_zero;

    if (max30102Sensor.hasSensor()) {
        valid = valid && max30102Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (mlx90614Sensor.hasSensor()) {
        valid = valid && mlx90614Sensor.getMetrics(m);
        hasSensor = true;
    }

    return valid && hasSensor;
}

meshtastic_MeshPacket *HealthTelemetryModule::allocReply()
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
            LOG_ERROR("Error decoding HealthTelemetry module!");
            return NULL;
        }
        // Check for a request for health metrics
        if (decoded->which_variant == meshtastic_Telemetry_health_metrics_tag) {
            meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
            if (getHealthTelemetry(&m)) {
                LOG_INFO("Health telemetry reply to request");
                return allocDataProtobuf(m);
            } else {
                return NULL;
            }
        }
    }
    return NULL;
}

bool HealthTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
{
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    m.which_variant = meshtastic_Telemetry_health_metrics_tag;
    m.time = getTime();
    if (getHealthTelemetry(&m)) {
        LOG_INFO("Send: temperature=%f, heart_bpm=%d, spO2=%d", m.variant.health_metrics.temperature,
                 m.variant.health_metrics.heart_bpm, m.variant.health_metrics.spO2);

        sensor_read_error_count = 0;

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

            if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && config.power.is_power_saving) {
                LOG_DEBUG("Start next execution in 5s, then sleep");
                sleepOnNextExecution = true;
                setIntervalFromNow(5000);
            }
        }
        return true;
    }
    return false;
}

#endif