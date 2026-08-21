#include "DebugConfiguration.h"
#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "AirQualityTelemetry.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Router.h"
#include "TransmitHistory.h"
#include "UnitConversions.h"
#include "UptimeClock.h"
#include "detect/ScanI2CTwoWire.h"
#ifdef AIR_QUALITY_TELEMETRY_HISTORY_PATH
#include "FileTelemetryStore.h"
#endif
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "main.h"
#include "sleep.h"
#include <Throttle.h>

static constexpr uint16_t TX_HISTORY_KEY_AIR_QUALITY_TELEMETRY = 0x8004;

// Sensors
#include "Sensor/AddI2CSensorTemplate.h"
#include "Sensor/PMSA003ISensor.h"
#include "Sensor/SEN5XSensor.h"
#include "Sensor/SEN6XSensor.h"
#if __has_include(<SensirionI2cScd4x.h>)
#include "Sensor/SCD4XSensor.h"
#endif
#if __has_include(<SensirionI2cSfa3x.h>)
#include "Sensor/SFA30Sensor.h"
#endif
#if __has_include(<SensirionI2cScd30.h>)
#include "Sensor/SCD30Sensor.h"
#endif
#if __has_include(<Seeed_HM330X.h>)
#include "Sensor/HM330XSensor.h"
#endif

void AirQualityTelemetryModule::i2cScanFinished(ScanI2C *i2cScanner)
{
    if (!moduleConfig.telemetry.air_quality_enabled && !AIR_QUALITY_TELEMETRY_MODULE_ENABLE) {
        return;
    }

    LOG_INFO("Air Quality Telemetry adding I2C devices");

    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
        Note: this was previously on runOnce, which didn't take effect
        as other modules already had already been initialized (screen)
    */

    // moduleConfig.telemetry.air_quality_enabled = 1;
    // moduleConfig.telemetry.air_quality_screen_enabled = 1;
    // moduleConfig.telemetry.air_quality_interval = 15;

    // Add here supported sensors in the Air Quality module
    // These sensors will be scanned twice, once in the first scan,
    // and secondly in the first run of the module
    if (!supportedSensors.count(PMSA003I_ADDR))
        supportedSensors[PMSA003I_ADDR] = ScanI2C::DeviceType::PMSA003I;
    if (!supportedSensors.count(SEN5X_ADDR))
        supportedSensors[SEN5X_ADDR] = ScanI2C::DeviceType::SEN5X;
    if (!supportedSensors.count(SEN6X_ADDR))
        supportedSensors[SEN6X_ADDR] = ScanI2C::DeviceType::SEN6X;
#if __has_include(<SensirionI2cScd4x.h>)
    if (!supportedSensors.count(SCD4X_ADDR))
        supportedSensors[SCD4X_ADDR] = ScanI2C::DeviceType::SCD4X;
#endif
#if __has_include(<SensirionI2cSfa3x.h>)
    if (!supportedSensors.count(SFA30_ADDR))
        supportedSensors[SFA30_ADDR] = ScanI2C::DeviceType::SFA30;
#endif
#if __has_include(<SensirionI2cScd30.h>)
    if (!supportedSensors.count(SCD30_ADDR))
        supportedSensors[SCD30_ADDR] = ScanI2C::DeviceType::SCD30;
#endif

    if (!firstTime) {
        // Re-scan for late comming sensors
        LOG_INFO("Re-scanning supported sensors");

        for (const auto &[address, type] : supportedSensors) {

            if (!i2cScanner->exists(type)) {
                LOG_INFO("Re-scanning on address 0x%x", address);
                uint8_t array_address[1] = {address};
#if defined(I2C_SDA1) || (defined(NRF52840_XXAA) && (WIRE_INTERFACES_COUNT == 2))
                i2cScanner->scanPort(ScanI2C::I2CPort::WIRE1, array_address, sizeof(array_address));
#endif

#if defined(I2C_SDA)
                i2cScanner->scanPort(ScanI2C::I2CPort::WIRE, array_address, sizeof(array_address));
#elif defined(ARCH_PORTDUINO)
                if (portduino_config.i2cdev != "") {
                    i2cScanner->scanPort(ScanI2C::I2CPort::WIRE, array_address, sizeof(array_address));
                }
#elif HAS_WIRE
                i2cScanner->scanPort(ScanI2C::I2CPort::WIRE, array_address, sizeof(array_address));
#endif
            }
        }
    }

    // order by priority of metrics/values (low top, high bottom)
    addSensor<PMSA003ISensor>(i2cScanner, ScanI2C::DeviceType::PMSA003I);
    addSensor<SEN5XSensor>(i2cScanner, ScanI2C::DeviceType::SEN5X);
    addSensor<SEN6XSensor>(i2cScanner, ScanI2C::DeviceType::SEN6X);
#if __has_include(<SensirionI2cScd4x.h>)
    addSensor<SCD4XSensor>(i2cScanner, ScanI2C::DeviceType::SCD4X);
#endif
#if __has_include(<SensirionI2cSfa3x.h>)
    addSensor<SFA30Sensor>(i2cScanner, ScanI2C::DeviceType::SFA30);
#endif
#if __has_include(<SensirionI2cScd30.h>)
    addSensor<SCD30Sensor>(i2cScanner, ScanI2C::DeviceType::SCD30);
#endif
#if __has_include(<Seeed_HM330X.h>)
    addSensor<HM330XSensor>(i2cScanner, ScanI2C::DeviceType::HM330X);
#endif
}

int32_t AirQualityTelemetryModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        if (shouldDeferDeepSleep())
            return PREFLIGHT_SLEEP_RETRY_MS;
        sleepOnNextExecution = false;
        uint32_t nightyNightMs = Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.air_quality_interval,
                                                                   default_telemetry_broadcast_interval_secs);
        LOG_DEBUG("Sleep %ims until next send", nightyNightMs);
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

#if !MESHTASTIC_EXCLUDE_I2C
            // Re-scan I2C bus
            auto i2cScanner = std::unique_ptr<ScanI2CTwoWire>(new ScanI2CTwoWire());
            i2cScanFinished(i2cScanner.get());
#endif

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

        // air_quality_interval paces only what goes on air; the local loop has its own cadence.
        const uint32_t meshIntervalMs = Default::getConfiguredOrDefaultMsScaled(
            moduleConfig.telemetry.air_quality_interval, default_telemetry_broadcast_interval_secs, numOnlineNodes);

        const uint32_t lastMeshTelemetry =
            transmitHistory ? transmitHistory->getLastSentToMeshMillis(TX_HISTORY_KEY_AIR_QUALITY_TELEMETRY) : 0;

        const bool meshAllowed =
            airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
            airTime->isTxAllowedAirUtil();
        const bool meshDue = (lastMeshTelemetry == 0) || Throttle::hasElapsed(lastMeshTelemetry, meshIntervalMs);

        if (shouldReadSensors(everRead, Time::getMillis(), lastReadMs, localLoopIntervalMs, service->isToPhoneQueueEmpty(),
                              meshDue && meshAllowed)) {
            const int32_t warmingUpMs = warmUpSensors();
            if (warmingUpMs > 0)
                return warmingUpMs; // come back once the slowest sensor is ready

            captureReading();
        }

        // Send to sleep sensors that can be to save power
        for (TelemetrySensor *sensor : sensors) {
            if (sensor->isActive() && sensor->canSleep()) {
                // Against the local loop, not the on-air interval: a sensor that cannot warm back up
                // within one loop would never produce a reading if it were slept
                if (sensor->wakeUpTimeMs() < (int32_t)localLoopIntervalMs) {
                    LOG_DEBUG("Disabling %s until next period", sensor->sensorName);
                    sensor->sleep();
                } else {
                    LOG_DEBUG("Keep %s enabled, warm up outlasts the local loop", sensor->sensorName);
                }
            }
        }

        if (shouldSendToMesh(history->hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH), meshDue, meshAllowed,
                             isPowerSavingSensor())) {
            // Called even with nothing to send: sendTelemetry() arms the power-saving SENSOR's sleep
            if (sendTelemetry(NODENUM_BROADCAST, false) && transmitHistory)
                transmitHistory->setLastSentToMesh(TX_HISTORY_KEY_AIR_QUALITY_TELEMETRY);
        } else if (history->hasUnpublishedNewest(TELEMETRY_PUBLISHED_PHONE) && service->isToPhoneQueueEmpty()) {
            // Mesh transmission isn't due yet, but we can still update the phone
            sendTelemetry(NODENUM_BROADCAST, true);
        }
    }
    if (sleepOnNextExecution) {
        // Honor the pre-sleep grace period armed in sendTelemetry(): OSThread reschedules with
        // this return value, which would otherwise override setIntervalFromNow() and delay or
        // mistime the pending deep sleep
        return FIVE_SECONDS_MS;
    }

    // Cadence runs from the read itself, so a warm-up sits between two reads. Costs a few seconds
    // of drift per cycle and saves tracking it.
    return min(localLoopIntervalMs, result);
}

bool AirQualityTelemetryModule::shouldReadSensors(bool everRead, uint32_t nowMs, uint32_t lastReadMs, uint32_t localIntervalMs,
                                                  bool phoneQueueEmpty, bool meshPublishDue)
{
    if (!everRead)
        return true;
    // Subtract-first, as Throttle::hasElapsed() does, so the 32-bit wrap cancels
    return (phoneQueueEmpty || meshPublishDue) && (nowMs - lastReadMs) >= localIntervalMs;
}

bool AirQualityTelemetryModule::shouldSendToMesh(bool haveUnsentReading, bool meshDue, bool meshAllowed, bool powerSavingSensor)
{
    if (!meshDue || !meshAllowed)
        return false;
    return haveUnsentReading || powerSavingSensor;
}

int32_t AirQualityTelemetryModule::warmUpSensors()
{
    int32_t pendingMs = 0;

    // All in one pass: a long warm-up used to return early and starve the others
    for (TelemetrySensor *sensor : sensors) {
        if (!sensor->canSleep()) {
            LOG_DEBUG("%s: no sleep support, skip", sensor->sensorName);
            continue;
        }

        if (!sensor->isActive()) {
            LOG_DEBUG("Waking %s", sensor->sensorName);
            pendingMs = max(pendingMs, (int32_t)sensor->wakeUp());
        } else {
            pendingMs = max(pendingMs, sensor->pendingForReadyMs());
        }
    }

    if (pendingMs > 0)
        LOG_DEBUG("Sensors warming up, %dms", pendingMs);

    return pendingMs;
}

void AirQualityTelemetryModule::openHistory()
{
#ifdef AIR_QUALITY_TELEMETRY_HISTORY_PATH
    auto *persistent = makeFileTelemetryStore<meshtastic_AirQualityMetrics>(
        AIR_QUALITY_TELEMETRY_HISTORY_PATH, AIR_QUALITY_TELEMETRY_HISTORY_SIZE, AIR_QUALITY_TELEMETRY_HISTORY_FS);
    if (persistent->isUsable()) {
        history = persistent;
        return;
    }

    // No card, full filesystem, whatever: measuring into RAM beats not measuring
    delete persistent;
    LOG_WARN("AQ history: %s unusable, keeping readings in RAM", AIR_QUALITY_TELEMETRY_HISTORY_PATH);
#endif

    history = new RamTelemetryStore<meshtastic_AirQualityMetrics>(AIR_QUALITY_TELEMETRY_HISTORY_SIZE);
}

void AirQualityTelemetryModule::captureReading()
{
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;

    if (getAirQualityTelemetry(&m)) {
        history->push(m.variant.air_quality_metrics, m.time);
    } else {
        LOG_WARN("AQ read failed, keep the previous reading");
    }

    // Stamped either way, so a failing sensor retries on the interval rather than every poll
    lastReadMs = Time::getMillis();
    everRead = true;
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

    // Same String(float) stack-overflow guard as EnvironmentTelemetry: form_formaldehyde is the only
    // float rendered here, and its raw bytes are unvalidated on decode.
    telemetry.variant.air_quality_metrics.form_formaldehyde =
        UnitConversions::displaySafeFloat(telemetry.variant.air_quality_metrics.form_formaldehyde);

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
    // getTime() falls back to uptime-as-epoch when unset; 0 lets the receiver use its rx time
    m->time = getValidTime(RTCQualityDevice);
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
        if (isMultiHopBroadcastRequest() && !isSensorOrRouterRole()) {
            ignoreRequest = true;
            return NULL;
        }
        auto req = *currentRequest;
        const auto &p = req.decoded;
        meshtastic_Telemetry scratch;
        meshtastic_Telemetry *decoded = NULL;
        memset(&scratch, 0, sizeof(scratch));
        if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &scratch)) {
            decoded = &scratch;
        } else {
            LOG_ERROR("Error decoding AirQualityTelemetry module");
            return NULL;
        }
        // Check for a request for air quality metrics
        if (decoded->which_variant == meshtastic_Telemetry_air_quality_metrics_tag) {
            // From the loop's last reading: reading here would block on I2C and return nothing
            // useful while the sensors are asleep or warming up.
            TelemetryReading<meshtastic_AirQualityMetrics> reading;
            if (!history->newest(reading)) {
                LOG_INFO("No air quality reading yet, no reply to request");
                return NULL;
            }

            meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
            m.which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
            m.time = reading.time;
            m.variant.air_quality_metrics = reading.metrics;
            LOG_INFO("Air quality telemetry reply to request");
            return allocDataProtobuf(m);
        }
    }
    return NULL;
}

void AirQualityTelemetryModule::logTelemetry(const meshtastic_Telemetry &m)
{
    const auto &a = m.variant.air_quality_metrics;

    if (a.has_pm10_standard || a.has_pm25_standard || a.has_pm100_standard || a.has_pm10_environmental ||
        a.has_pm25_environmental || a.has_pm100_environmental) {
        LOG_INFO("Send: pm10_standard=%u, pm25_standard=%u, pm100_standard=%u", a.pm10_standard, a.pm25_standard,
                 a.pm100_standard);
        if (a.has_pm10_environmental)
            LOG_INFO("pm10_environmental=%u, pm25_environmental=%u, pm100_environmental=%u", a.pm10_environmental,
                     a.pm25_environmental, a.pm100_environmental);
    }

    if (a.has_co2 || a.has_co2_temperature || a.has_co2_humidity)
        LOG_INFO("Send: co2=%i, co2_t=%.2f, co2_rh=%.2f", a.co2, a.co2_temperature, a.co2_humidity);

    if (a.has_form_formaldehyde || a.has_form_temperature || a.has_form_humidity)
        LOG_INFO("Send: hcho=%.2f, hcho_t=%.2f, hcho_rh=%.2f", a.form_formaldehyde, a.form_temperature, a.form_humidity);
}

bool AirQualityTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
{
    bool sent = false;
    const TelemetryPublishChannel channel = phoneOnly ? TELEMETRY_PUBLISHED_PHONE : TELEMETRY_PUBLISHED_MESH;
    TelemetryReading<meshtastic_AirQualityMetrics> reading;

    // One fetch then check the mask: on a file-backed store each read costs an open
    if (history->newest(reading) && !(reading.publishedMask & channel)) {
        meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
        m.which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
        m.time = reading.time;
        m.variant.air_quality_metrics = reading.metrics;

        logTelemetry(m);

        meshtastic_MeshPacket *p = allocDataProtobuf(m);
        if (p) {
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
            sent = true;

            if (phoneOnly) {
                LOG_INFO("Sending packet to phone");
                service->sendToPhone(p);
                history->markNewestPublished(TELEMETRY_PUBLISHED_PHONE);
            } else {
                LOG_INFO("Sending packet to mesh");
                service->sendToMesh(p, RX_SRC_LOCAL, true);
                history->markNewestPublished(TELEMETRY_PUBLISHED_MESH);
                // ccToPhone above hands the phone this very reading, no separate phone send needed
                history->markNewestPublished(TELEMETRY_PUBLISHED_PHONE);

                if (isPowerSavingSensor()) {
                    meshtastic_ClientNotification *notification = clientNotificationPool.allocZeroed();
                    if (notification) {
                        notification->level = meshtastic_LogRecord_Level_INFO;
                        notification->time = getValidTime(RTCQualityFromNet);
                        sprintf(notification->message, "Sending telemetry and sleeping for %us interval in a moment",
                                Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.air_quality_interval,
                                                                  default_telemetry_broadcast_interval_secs) /
                                    1000U);
                        service->sendClientNotification(notification);
                    }
                }
            }
        }
    }

    // Arm the pre-sleep sequence even when nothing was sent this cycle: a power-saving SENSOR
    // node must still return to deep sleep, otherwise it stays awake until the next telemetry
    // interval and drains its battery
    if (!phoneOnly && isPowerSavingSensor()) {
        if (!sent)
            LOG_WARN("AQ telemetry unavailable, sleep without send");
        sleepOnNextExecution = true;
        preflightSleepDeferrals = 0;
        LOG_DEBUG("Start next execution in 5s, then sleep");
        setIntervalFromNow(FIVE_SECONDS_MS);
    }
    return sent;
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
