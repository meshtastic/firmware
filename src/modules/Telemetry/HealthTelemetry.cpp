#include "HealthTelemetry.h"
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_HEALTH_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "main.h"
#include "power.h"
#include "sleep.h"
#include "target_specific.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

// Sensors
// TODO
#include "Sensor/HeartRateSensor.h"
#include "Sensor/TemperatureSensor.h"
#include "Sensor/BloodPressureSensor.h"

HeartRateSensor heartRateSensor;
TemperatureSensor temperatureSensor;
BloodPressureSensor bloodPressureSensor;

#define FAILED_STATE_SENSOR_READ_MULTIPLIER 10
#define DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN true

#include "graphics/ScreenFonts.h"
#include <Throttle.h>

namespace concurrency
{
HealthTelemetryModule::HealthTelemetryModule(ScanI2C::DeviceType type) : OSThread("HealthTelemetryModule")
{
    notifyDeepSleepObserver.observe(&notifyDeepSleep); // Let us know when shutdown() is issued.
}

int32_t HealthTelemetryModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        sleepOnNextExecution = false;
        uint32_t nightyNightMs = Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.health_update_interval,
                                                                   default_telemetry_broadcast_interval_secs);
        LOG_DEBUG("Sleeping for %ims, then awaking to send metrics again.\n", nightyNightMs);
        doDeepSleep(nightyNightMs, true);
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
            LOG_INFO("Health Telemetry: Initializing\n");
            // Initialize sensors
            // TODO
            if (heartRateSensor.hasSensor())
                result = heartRateSensor.runOnce();
            if (temperatureSensor.hasSensor())
                result = temperatureSensor.runOnce();
            if (bloodPressureSensor.hasSensor())
                result = bloodPressureSensor.runOnce();
        }
        return result;
    } else {
        // if we somehow got to a second run of this module with measurement disabled, then just wait forever
        if (!moduleConfig.telemetry.health_measurement_enabled) {
            return disable();
        } else {
            if (heartRateSensor.hasSensor())
                result = heartRateSensor.runTrigger();
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

// Maybe implement a screen for health telemetry?
/*
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
    uint32_t agoSecs = GetTimeSinceMeshPacket(lastMeasurementPacket);
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
    if (moduleConfig.telemetry.health_display_fahrenheit) {
        last_temp = String(CelsiusToFahrenheit(lastMeasurement.variant.health_metrics.temperature), 0) + "°F";
    }

    // Continue with the remaining details
    display->drawString(x, y += _fontHeight(FONT_SMALL),
                        "Temp: " + last_temp);
    display->drawString(x, y += _fontHeight(FONT_SMALL),
                        "Heart Rate: " + String(lastMeasurement.variant.health_metrics.heart_rate, 0) + " bpm");
    display->drawString(x, y += _fontHeight(FONT_SMALL),
                        "Blood Pressure: " + String(lastMeasurement.variant.health_metrics.blood_pressure_systolic, 0) + "/" +
                            String(lastMeasurement.variant.health_metrics.blood_pressure_diastolic, 0) + " mmHg");
}
*/

bool HealthTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    if (t->which_variant == meshtastic_Telemetry_health_metrics_tag) {
#ifdef DEBUG_PORT
        const char *sender = getSenderShortName(mp);

        LOG_INFO("(Received from %s): temperature=%f, heart_rate=%d, blood_pressure_systolic=%d, blood_pressure_diastolic=%d\n",
                 sender, t->variant.health_metrics.temperature, t->variant.health_metrics.heart_rate,
                 t->variant.health_metrics.blood_pressure_systolic, t->variant.health_metrics.blood_pressure_diastolic);
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

    // TODO
    if (heartRateSensor.hasSensor()) {
        valid = valid && heartRateSensor.getMetrics(m);
        hasSensor = true;
    }
    if (temperatureSensor.hasSensor()) {
        valid = valid && temperatureSensor.getMetrics(m);
        hasSensor = true;
    }
    if (bloodPressureSensor.hasSensor()) {
        valid = valid && bloodPressureSensor.getMetrics(m);
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
            LOG_ERROR("Error decoding HealthTelemetry module!\n");
            return NULL;
        }
        // Check for a request for health metrics
        if (decoded->which_variant == meshtastic_Telemetry_health_metrics_tag) {
            meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
            if (getHealthTelemetry(&m)) {
                LOG_INFO("Health telemetry replying to request\n");
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
        LOG_INFO("(Sending): temperature=%f, heart_rate=%d, blood_pressure_systolic=%d, blood_pressure_diastolic=%d\n",
                 m.variant.health_metrics.temperature, m.variant.health_metrics.heart_rate,
                 m.variant.health_metrics.blood_pressure_systolic, m.variant.health_metrics.blood_pressure_diastolic);

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
            LOG_INFO("Sending packet to phone\n");
            service->sendToPhone(p);
        } else {
            LOG_INFO("Sending packet to mesh\n");
            service->sendToMesh(p, RX_SRC_LOCAL, true);

            if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && config.power.is_power_saving) {
                LOG_DEBUG("Starting next execution in 5 seconds and then going to sleep.\n");
                sleepOnNextExecution = true;
                setIntervalFromNow(5000);
            }
        }
        return true;
    }
    return false;
}
}

#endif