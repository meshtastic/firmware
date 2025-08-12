#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "Default.h"
#include "AirQualityTelemetry.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "UnitConversions.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "main.h"
#include "sleep.h"
#include <Throttle.h>
// Sensor includes
#include "Sensor/PMSA003ISensor.h"

// Sensors
PMSA003ISensor pmsa003iSensor;

#include "graphics/ScreenFonts.h"

int32_t AirQualityTelemetryModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        sleepOnNextExecution = false;
        uint32_t nightyNightMs = Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.air_quality_interval,
                                                                   default_telemetry_broadcast_interval_secs);
        LOG_DEBUG("Sleeping for %ims, then awaking to send metrics again.", nightyNightMs);
        doDeepSleep(nightyNightMs, true, false);
    }

    uint32_t result = UINT32_MAX;

    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.telemetry.air_quality_enabled = 1;
    // TODO there is no config in module_config.proto for air_quality_screen_enabled. Reusing environment one, although it should have its own
    // moduleConfig.telemetry.environment_screen_enabled = 1;
    // moduleConfig.telemetry.air_quality_interval = 15;

    if (!(moduleConfig.telemetry.air_quality_enabled  || moduleConfig.telemetry.environment_screen_enabled ||
          AIR_QUALITY_TELEMETRY_MODULE_ENABLE)) {
        // If this module is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return disable();
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = false;

        if (moduleConfig.telemetry.air_quality_enabled) {
            LOG_INFO("Air quality Telemetry: init");

            if (pmsa003iSensor.hasSensor())
                result = pmsa003iSensor.runOnce();
        }

        // it's possible to have this module enabled, only for displaying values on the screen.
        // therefore, we should only enable the sensor loop if measurement is also enabled
        return result == UINT32_MAX ? disable() : setStartDelay();
    } else {
        // if we somehow got to a second run of this module with measurement disabled, then just wait forever
        if (!moduleConfig.telemetry.air_quality_enabled && !AIR_QUALITY_TELEMETRY_MODULE_ENABLE) {
            return disable();
        }

        // Wake up the sensors that need it
#ifdef PMSA003I_ENABLE_PIN
        if (pmsa003iSensor.hasSensor() && !pmsa003iSensor.isActive())
            return pmsa003iSensor.wakeUp();
#endif /* PMSA003I_ENABLE_PIN */

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

#ifdef PMSA003I_ENABLE_PIN
        pmsa003iSensor.sleep();
#endif /* PMSA003I_ENABLE_PIN */

    }
    return min(sendToPhoneIntervalMs, result);
}

bool AirQualityTelemetryModule::wantUIFrame()
{
    return moduleConfig.telemetry.environment_screen_enabled;
}

void AirQualityTelemetryModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // === Setup display ===
    display->clear();
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    int line = 1;

    // === Set Title
    const char *titleStr = (graphics::isHighResolution) ? "Environment" : "Env.";

    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr);

    // === Row spacing setup ===
    const int rowHeight = FONT_HEIGHT_SMALL - 4;
    int currentY = graphics::getTextPositions(display)[line++];

    // === Show "No Telemetry" if no data available ===
    if (!lastMeasurementPacket) {
        display->drawString(x, currentY, "No Telemetry");
        return;
    }

    // Decode the telemetry message from the latest received packet
    const meshtastic_Data &p = lastMeasurementPacket->decoded;
    meshtastic_Telemetry telemetry;
    if (!pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &telemetry)) {
        display->drawString(x, currentY, "No Telemetry");
        return;
    }

    const auto &m = telemetry.variant.air_quality_metrics;

    // Check if any telemetry field has valid data
    bool hasAny = m.has_pm10_standard || m.has_pm25_standard || m.has_pm100_standard  || m.has_pm10_environmental || m.has_pm25_environmental ||
                  m.has_pm100_environmental;

    if (!hasAny) {
        display->drawString(x, currentY, "No Telemetry");
        return;
    }

    // === First line: Show sender name + time since received (left), and first metric (right) ===
    const char *sender = getSenderShortName(*lastMeasurementPacket);
    uint32_t agoSecs = service->GetTimeSinceMeshPacket(lastMeasurementPacket);
    String agoStr = (agoSecs > 864000) ? "?"
                    : (agoSecs > 3600) ? String(agoSecs / 3600) + "h"
                    : (agoSecs > 60)   ? String(agoSecs / 60) + "m"
                                       : String(agoSecs) + "s";

    String leftStr = String(sender) + " (" + agoStr + ")";
    display->drawString(x, currentY, leftStr); // Left side: who and when

    // === Collect sensor readings as label strings (no icons) ===
    std::vector<String> entries;

    if (m.has_pm10_standard)
        entries.push_back("PM1.0: " + String(m.pm10_standard) + "ug/m3");
    if (m.has_pm25_standard)
        entries.push_back("PM2.5: " + String(m.pm25_standard) + "ug/m3");
    if (m.has_pm100_standard)
        entries.push_back("PM10.0: " + String(m.pm100_standard) + "ug/m3");

    // === Show first available metric on top-right of first line ===
    if (!entries.empty()) {
        String valueStr = entries.front();
        int rightX = SCREEN_WIDTH - display->getStringWidth(valueStr);
        display->drawString(rightX, currentY, valueStr);
        entries.erase(entries.begin()); // Remove from queue
    }

    // === Advance to next line for remaining telemetry entries ===
    currentY += rowHeight;

    // === Draw remaining entries in 2-column format (left and right) ===
    for (size_t i = 0; i < entries.size(); i += 2) {
        // Left column
        display->drawString(x, currentY, entries[i]);

        // Right column if it exists
        if (i + 1 < entries.size()) {
            int rightX = SCREEN_WIDTH / 2;
            display->drawString(rightX, currentY, entries[i + 1]);
        }

        currentY += rowHeight;
    }
}

bool AirQualityTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    if (t->which_variant == meshtastic_Telemetry_air_quality_metrics_tag) {
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
        const char *sender = getSenderShortName(mp);

        LOG_INFO("(Received from %s): pm10_standard=%i, pm25_standard=%i, pm100_standard=%i", sender,
                 t->variant.air_quality_metrics.pm10_standard, t->variant.air_quality_metrics.pm25_standard,
                 t->variant.air_quality_metrics.pm100_standard);

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

bool AirQualityTelemetryModule::getAirQualityTelemetry(meshtastic_Telemetry *m)
{
    bool valid = true;
    bool hasSensor = false;
    m->time = getTime();
    m->which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
    m->variant.air_quality_metrics = meshtastic_AirQualityMetrics_init_zero;

    if (pmsa003iSensor.hasSensor()) {
        // TODO - Should we check for sensor state here?
        // If a sensor is sleeping, we should know and check to wake it up
        valid = valid && pmsa003iSensor.getMetrics(m);
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
    m.which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
    m.time = getTime();
    if (getAirQualityTelemetry(&m)) {
        LOG_INFO("Send: pm10_standard=%u, pm25_standard=%u, pm100_standard=%u, \
                    pm10_environmental=%u, pm25_environmental=%u, pm100_environmental=%u", \
                    m.variant.air_quality_metrics.pm10_standard, m.variant.air_quality_metrics.pm25_standard, \
                    m.variant.air_quality_metrics.pm100_standard, m.variant.air_quality_metrics.pm10_environmental, \
                    m.variant.air_quality_metrics.pm25_environmental, m.variant.air_quality_metrics.pm100_environmental);

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
            LOG_INFO("Sending packet to phone");
            service->sendToPhone(p);
        } else {
            LOG_INFO("Sending packet to mesh");
            service->sendToMesh(p, RX_SRC_LOCAL, true);

            if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && config.power.is_power_saving) {
                meshtastic_ClientNotification *notification = clientNotificationPool.allocZeroed();
                notification->level = meshtastic_LogRecord_Level_INFO;
                notification->time = getValidTime(RTCQualityFromNet);
                sprintf(notification->message, "Sending telemetry and sleeping for %us interval in a moment",
                        Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.air_quality_interval,
                                                          default_telemetry_broadcast_interval_secs) /
                            1000U);
                service->sendClientNotification(notification);
                sleepOnNextExecution = true;
                LOG_DEBUG("Start next execution in 5s, then sleep");
                setIntervalFromNow(FIVE_SECONDS_MS);
            }
        }
        return true;
    }
    return false;
}

AdminMessageHandleResult AirQualityTelemetryModule::handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                                 meshtastic_AdminMessage *request,
                                                                                 meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result = AdminMessageHandleResult::NOT_HANDLED;
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR_EXTERNAL
    if (pmsa003iSensor.hasSensor()) {
        // TODO - Potentially implement an admin message to choose between pm_standard
        // and pm_environmental. This could be configurable as it doesn't make sense so
        // have both
        result = pmsa003iSensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }


#endif
    return result;
}

#endif