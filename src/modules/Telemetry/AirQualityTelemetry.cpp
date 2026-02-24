#include "DebugConfiguration.h"
#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "AirQualityTelemetry.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "UnitConversions.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "main.h"
#include "sleep.h"
#include <Throttle.h>

// Sensors
#include "Sensor/AddI2CSensorTemplate.h"
#include "Sensor/PMSA003ISensor.h"
#include "Sensor/SEN5XSensor.h"
#if __has_include(<SensirionI2cScd4x.h>)
#include "Sensor/SCD4XSensor.h"
#endif
#if __has_include(<SensirionI2cSfa3x.h>)
#include "Sensor/SFA30Sensor.h"
#endif
#if __has_include(<SensirionI2cScd30.h>)
#include "Sensor/SCD30Sensor.h"
#endif

void AirQualityTelemetryModule::i2cScanFinished(ScanI2C *i2cScanner)
{
    if (!moduleConfig.telemetry.air_quality_enabled && !AIR_QUALITY_TELEMETRY_MODULE_ENABLE) {
        return;
    }
    LOG_INFO("Air Quality Telemetry adding I2C devices...");

    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
        Note: this was previously on runOnce, which didnt take effect
        as other modules already had already been initialized (screen)
    */

    // moduleConfig.telemetry.air_quality_enabled = 1;
    // moduleConfig.telemetry.air_quality_screen_enabled = 1;
    // moduleConfig.telemetry.air_quality_interval = 15;

    // order by priority of metrics/values (low top, high bottom)
    addSensor<PMSA003ISensor>(i2cScanner, ScanI2C::DeviceType::PMSA003I);
    addSensor<SEN5XSensor>(i2cScanner, ScanI2C::DeviceType::SEN5X);
#if __has_include(<SensirionI2cScd4x.h>)
    addSensor<SCD4XSensor>(i2cScanner, ScanI2C::DeviceType::SCD4X);
#endif
#if __has_include(<SensirionI2cSfa3x.h>)
    addSensor<SFA30Sensor>(i2cScanner, ScanI2C::DeviceType::SFA30);
#endif
#if __has_include(<SensirionI2cScd30.h>)
    addSensor<SCD30Sensor>(i2cScanner, ScanI2C::DeviceType::SCD30);
#endif
}

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

    if (!(moduleConfig.telemetry.air_quality_enabled || moduleConfig.telemetry.air_quality_screen_enabled ||
          AIR_QUALITY_TELEMETRY_MODULE_ENABLE)) {
        // If this module is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return disable();
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so
        // do some setup
        firstTime = false;

        if (moduleConfig.telemetry.air_quality_enabled) {
            LOG_INFO("Air quality Telemetry: init");

            // check if we have at least one sensor
            if (!sensors.empty()) {
                result = DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
            }
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
        LOG_INFO("Waking up sensors...");
        for (TelemetrySensor *sensor : sensors) {
            if (!sensor->canSleep()) {
                LOG_DEBUG("%s sensor doesn't have sleep feature. Skipping", sensor->sensorName);
            } else if (((lastSentToMesh == 0) ||
                        !Throttle::isWithinTimespanMs(lastSentToMesh - sensor->wakeUpTimeMs(),
                                                      Default::getConfiguredOrDefaultMsScaled(
                                                          moduleConfig.telemetry.air_quality_interval,
                                                          default_telemetry_broadcast_interval_secs, numOnlineNodes))) &&
                       airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
                       airTime->isTxAllowedAirUtil()) {
                if (!sensor->isActive()) {
                    LOG_DEBUG("Waking up: %s", sensor->sensorName);
                    return sensor->wakeUp();
                } else {
                    int32_t pendingForReadyMs = sensor->pendingForReadyMs();
                    LOG_DEBUG("%s. Pending for ready %ums", sensor->sensorName, pendingForReadyMs);
                    if (pendingForReadyMs) {
                        return pendingForReadyMs;
                    }
                }
            }
        }

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

        // Send to sleep sensors that consume power
        LOG_DEBUG("Sending sensors to sleep");
        for (TelemetrySensor *sensor : sensors) {
            if (sensor->isActive() && sensor->canSleep()) {
                if (sensor->wakeUpTimeMs() <
                    (int32_t)Default::getConfiguredOrDefaultMsScaled(moduleConfig.telemetry.air_quality_interval,
                                                                     default_telemetry_broadcast_interval_secs, numOnlineNodes)) {
                    LOG_DEBUG("Disabling %s until next period", sensor->sensorName);
                    sensor->sleep();
                } else {
                    LOG_DEBUG("Sensor stays enabled due to warm up period");
                }
            }
        }
    }
    return min(sendToPhoneIntervalMs, result);
}

bool AirQualityTelemetryModule::wantUIFrame()
{
    return moduleConfig.telemetry.air_quality_screen_enabled;
}

#if HAS_SCREEN
void AirQualityTelemetryModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // === Setup display ===
    display->clear();
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    int line = 1;

    // === Set Title
    const char *titleStr = (graphics::currentResolution == graphics::ScreenResolution::High) ? "Air Quality" : "AQ.";

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
    bool hasAny = m.has_pm10_standard || m.has_pm25_standard || m.has_pm100_standard || m.has_co2;

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
        entries.push_back("PM1: " + String(m.pm10_standard) + "ug/m3");
    if (m.has_pm25_standard)
        entries.push_back("PM2.5: " + String(m.pm25_standard) + "ug/m3");
    if (m.has_pm100_standard)
        entries.push_back("PM10: " + String(m.pm100_standard) + "ug/m3");
    if (m.has_co2)
        entries.push_back("CO2: " + String(m.co2) + "ppm");
    if (m.has_form_formaldehyde)
        entries.push_back("HCHO: " + String(m.form_formaldehyde) + "ppb");

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
    graphics::drawCommonFooter(display, x, y);
}
#endif

bool AirQualityTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    if (t->which_variant == meshtastic_Telemetry_air_quality_metrics_tag) {
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
        const char *sender = getSenderShortName(mp);

        if (t->variant.air_quality_metrics.has_pm10_standard)
            LOG_INFO("(Received from %s): pm10_standard=%i, pm25_standard=%i, "
                     "pm100_standard=%i",
                     sender, t->variant.air_quality_metrics.pm10_standard, t->variant.air_quality_metrics.pm25_standard,
                     t->variant.air_quality_metrics.pm100_standard);

        if (t->variant.air_quality_metrics.has_co2)
            LOG_INFO("CO2=%i, CO2_T=%.2f, CO2_H=%.2f", t->variant.air_quality_metrics.co2,
                     t->variant.air_quality_metrics.co2_temperature, t->variant.air_quality_metrics.co2_humidity);

        if (t->variant.air_quality_metrics.has_form_formaldehyde)
            LOG_INFO("HCHO=%.2f, HCHO_T=%.2f, HCHO_H=%.2f", t->variant.air_quality_metrics.form_formaldehyde,
                     t->variant.air_quality_metrics.form_temperature, t->variant.air_quality_metrics.form_humidity);
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
    // Note: this is different to the case in EnvironmentTelemetryModule
    // There, if any sensor fails to read - valid = false.
    bool valid = false;
    bool hasSensor = false;
    m->time = getTime();
    m->which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
    m->variant.air_quality_metrics = meshtastic_AirQualityMetrics_init_zero;

    bool sensor_get = false;
    for (TelemetrySensor *sensor : sensors) {
        LOG_DEBUG("Reading %s", sensor->sensorName);
        // Note - this function doesn't get properly called if within a conditional
        sensor_get = sensor->getMetrics(m);
        valid = valid || sensor_get;
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

        bool hasAnyPM =
            m.variant.air_quality_metrics.has_pm10_standard || m.variant.air_quality_metrics.has_pm25_standard ||
            m.variant.air_quality_metrics.has_pm100_standard || m.variant.air_quality_metrics.has_pm10_environmental ||
            m.variant.air_quality_metrics.has_pm25_environmental || m.variant.air_quality_metrics.has_pm100_environmental;

        if (hasAnyPM) {
            LOG_INFO("Send: pm10_standard=%u, pm25_standard=%u, pm100_standard=%u", m.variant.air_quality_metrics.pm10_standard,
                     m.variant.air_quality_metrics.pm25_standard, m.variant.air_quality_metrics.pm100_standard);
            if (m.variant.air_quality_metrics.has_pm10_environmental)
                LOG_INFO("pm10_environmental=%u, pm25_environmental=%u, pm100_environmental=%u",
                         m.variant.air_quality_metrics.pm10_environmental, m.variant.air_quality_metrics.pm25_environmental,
                         m.variant.air_quality_metrics.pm100_environmental);
        }

        bool hasAnyCO2 = m.variant.air_quality_metrics.has_co2 || m.variant.air_quality_metrics.has_co2_temperature ||
                         m.variant.air_quality_metrics.has_co2_humidity;

        if (hasAnyCO2) {
            LOG_INFO("Send: co2=%i, co2_t=%.2f, co2_rh=%.2f", m.variant.air_quality_metrics.co2,
                     m.variant.air_quality_metrics.co2_temperature, m.variant.air_quality_metrics.co2_humidity);
        }

        bool hasAnyHCHO = m.variant.air_quality_metrics.has_form_formaldehyde ||
                          m.variant.air_quality_metrics.has_form_temperature || m.variant.air_quality_metrics.has_form_humidity;

        if (hasAnyHCHO) {
            LOG_INFO("Send: hcho=%.2f, hcho_t=%.2f, hcho_rh=%.2f", m.variant.air_quality_metrics.form_formaldehyde,
                     m.variant.air_quality_metrics.form_temperature, m.variant.air_quality_metrics.form_humidity);
        }

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

    for (TelemetrySensor *sensor : sensors) {
        result = sensor->handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }

    return result;
}

#endif
