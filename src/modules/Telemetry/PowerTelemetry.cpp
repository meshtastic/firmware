#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "PowerTelemetry.h"
#include "RTC.h"
#include "Router.h"
#include "main.h"
#include "power.h"
#include "sleep.h"
#include "target_specific.h"

#define FAILED_STATE_SENSOR_READ_MULTIPLIER 10
#define DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN true

#include "graphics/ScreenFonts.h"
#include <Throttle.h>

int32_t PowerTelemetryModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        sleepOnNextExecution = false;
        uint32_t nightyNightMs = Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.power_update_interval,
                                                                   default_telemetry_broadcast_interval_secs);
        LOG_DEBUG("Sleep for %ims, then awake to send metrics again", nightyNightMs);
        doDeepSleep(nightyNightMs, true, false);
    }

    uint32_t result = UINT32_MAX;
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.telemetry.power_measurement_enabled = 1;
    // moduleConfig.telemetry.power_screen_enabled = 1;
    // moduleConfig.telemetry.power_update_interval = 45;

    if (!(moduleConfig.telemetry.power_measurement_enabled)) {
        // If this module is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return disable();
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = 0;
#if HAS_TELEMETRY && !defined(ARCH_PORTDUINO)
        if (moduleConfig.telemetry.power_measurement_enabled) {
            LOG_INFO("Power Telemetry: init");
            // it's possible to have this module enabled, only for displaying values on the screen.
            // therefore, we should only enable the sensor loop if measurement is also enabled
            if (ina219Sensor.hasSensor() && !ina219Sensor.isInitialized())
                result = ina219Sensor.runOnce();
            if (ina260Sensor.hasSensor() && !ina260Sensor.isInitialized())
                result = ina260Sensor.runOnce();
            if (ina3221Sensor.hasSensor() && !ina3221Sensor.isInitialized())
                result = ina3221Sensor.runOnce();
            if (max17048Sensor.hasSensor() && !max17048Sensor.isInitialized())
                result = max17048Sensor.runOnce();
        }
        return result;
#else
        return disable();
#endif
    } else {
        // if we somehow got to a second run of this module with measurement disabled, then just wait forever
        if (!moduleConfig.telemetry.power_measurement_enabled)
            return disable();

        if (((lastSentToMesh == 0) ||
             !Throttle::isWithinTimespanMs(lastSentToMesh, Default::getConfiguredOrDefaultMsScaled(
                                                               moduleConfig.telemetry.power_update_interval,
                                                               default_telemetry_broadcast_interval_secs, numOnlineNodes))) &&
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
bool PowerTelemetryModule::wantUIFrame()
{
    return moduleConfig.telemetry.power_screen_enabled;
}

void PowerTelemetryModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x, y, "Power Telemetry");
    if (lastMeasurementPacket == nullptr) {
        display->setFont(FONT_SMALL);
        display->drawString(x, y += _fontHeight(FONT_MEDIUM), "No measurement");
        return;
    }

    meshtastic_Telemetry lastMeasurement;

    uint32_t agoSecs = service->GetTimeSinceMeshPacket(lastMeasurementPacket);
    const char *lastSender = getSenderShortName(*lastMeasurementPacket);

    const meshtastic_Data &p = lastMeasurementPacket->decoded;
    if (!pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &lastMeasurement)) {
        display->setFont(FONT_SMALL);
        display->drawString(x, y += _fontHeight(FONT_MEDIUM), "Measurement Error");
        LOG_ERROR("Unable to decode last packet");
        return;
    }

    // Display current and voltage based on ...power_metrics.has_[channel/voltage/current]... flags
    display->setFont(FONT_SMALL);
    display->drawString(x, y += _fontHeight(FONT_MEDIUM) - 2, "From: " + String(lastSender) + "(" + String(agoSecs) + "s)");
    if (lastMeasurement.variant.power_metrics.has_ch1_voltage || lastMeasurement.variant.power_metrics.has_ch1_current) {
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "Ch1 Volt: " + String(lastMeasurement.variant.power_metrics.ch1_voltage, 2) +
                                "V / Curr: " + String(lastMeasurement.variant.power_metrics.ch1_current, 0) + "mA");
    }
    if (lastMeasurement.variant.power_metrics.has_ch2_voltage || lastMeasurement.variant.power_metrics.has_ch2_current) {
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "Ch2 Volt: " + String(lastMeasurement.variant.power_metrics.ch2_voltage, 2) +
                                "V / Curr: " + String(lastMeasurement.variant.power_metrics.ch2_current, 0) + "mA");
    }
    if (lastMeasurement.variant.power_metrics.has_ch3_voltage || lastMeasurement.variant.power_metrics.has_ch3_current) {
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "Ch3 Volt: " + String(lastMeasurement.variant.power_metrics.ch3_voltage, 2) +
                                "V / Curr: " + String(lastMeasurement.variant.power_metrics.ch3_current, 0) + "mA");
    }
}

bool PowerTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    if (t->which_variant == meshtastic_Telemetry_power_metrics_tag) {
#ifdef DEBUG_PORT
        const char *sender = getSenderShortName(mp);

        LOG_INFO("(Received from %s): ch1_voltage=%.1f, ch1_current=%.1f, ch2_voltage=%.1f, ch2_current=%.1f, "
                 "ch3_voltage=%.1f, ch3_current=%.1f",
                 sender, t->variant.power_metrics.ch1_voltage, t->variant.power_metrics.ch1_current,
                 t->variant.power_metrics.ch2_voltage, t->variant.power_metrics.ch2_current, t->variant.power_metrics.ch3_voltage,
                 t->variant.power_metrics.ch3_current);
#endif
        // release previous packet before occupying a new spot
        if (lastMeasurementPacket != nullptr)
            packetPool.release(lastMeasurementPacket);

        lastMeasurementPacket = packetPool.allocCopy(mp);
    }

    return false; // Let others look at this message also if they want
}

bool PowerTelemetryModule::getPowerTelemetry(meshtastic_Telemetry *m)
{
    bool valid = false;
    m->time = getTime();
    m->which_variant = meshtastic_Telemetry_power_metrics_tag;

    m->variant.power_metrics = meshtastic_PowerMetrics_init_zero;
#if HAS_TELEMETRY && !defined(ARCH_PORTDUINO)
    if (ina219Sensor.hasSensor())
        valid = ina219Sensor.getMetrics(m);
    if (ina260Sensor.hasSensor())
        valid = ina260Sensor.getMetrics(m);
    if (ina3221Sensor.hasSensor())
        valid = ina3221Sensor.getMetrics(m);
    if (max17048Sensor.hasSensor())
        valid = max17048Sensor.getMetrics(m);
#endif

    return valid;
}

meshtastic_MeshPacket *PowerTelemetryModule::allocReply()
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
            LOG_ERROR("Error decoding PowerTelemetry module!");
            return NULL;
        }
        // Check for a request for power metrics
        if (decoded->which_variant == meshtastic_Telemetry_power_metrics_tag) {
            meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
            if (getPowerTelemetry(&m)) {
                LOG_INFO("Power telemetry reply to request");
                return allocDataProtobuf(m);
            } else {
                return NULL;
            }
        }
    }

    return NULL;
}

bool PowerTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
{
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    m.which_variant = meshtastic_Telemetry_power_metrics_tag;
    m.time = getTime();
    if (getPowerTelemetry(&m)) {
        LOG_INFO("Send: ch1_voltage=%f, ch1_current=%f, ch2_voltage=%f, ch2_current=%f, "
                 "ch3_voltage=%f, ch3_current=%f",
                 m.variant.power_metrics.ch1_voltage, m.variant.power_metrics.ch1_current, m.variant.power_metrics.ch2_voltage,
                 m.variant.power_metrics.ch2_current, m.variant.power_metrics.ch3_voltage, m.variant.power_metrics.ch3_current);

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
                LOG_DEBUG("Start next execution in 5s then sleep");
                sleepOnNextExecution = true;
                setIntervalFromNow(5000);
            }
        }
        return true;
    }
    return false;
}

#endif
