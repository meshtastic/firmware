#include "DebugConfiguration.h"
#include "UptimeClock.h"
#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "AirQualityTelemetry.h"
#include "Default.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Router.h"
#include "TelemetryPublish.h"
#include "TelemetrySensorPower.h"
#include "TransmitHistory.h"
#include "UnitConversions.h"
#include "detect/ScanI2CTwoWire.h"
// SENSOR role keeps its readings in a file, so a batch survives reboot and deep sleep. Not on STM32: tiny flash.
#if !defined(AIR_QUALITY_TELEMETRY_HISTORY_PATH) && defined(FSCom) && !defined(ARCH_STM32)
#define AIR_QUALITY_TELEMETRY_HISTORY_PATH "/prefs/aq_history.bin"
#endif
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
        enterSharedDeepSleep(); // returns only when a read or publish is too close to be worth sleeping
    }

    uint32_t result = UINT32_MAX;

    if (!(moduleConfig.telemetry.air_quality_enabled || moduleConfig.telemetry.air_quality_screen_enabled ||
          AIR_QUALITY_TELEMETRY_MODULE_ENABLE)) {
        // If this module is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return disable();
    }

    if (!history)
        return disable();

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

        if (restoreScheduleAfterSleep(TELEMETRY_SLEEP_SLOT_AIR_QUALITY, schedule))
            LOG_DEBUG("AQ: schedule restored after deep sleep");

        // it's possible to have this module enabled, only for displaying values on the screen.
        // therefore, we should only enable the sensor loop if measurement is also enabled.
        // A power-saving SENSOR woke up for this read, so it does not wait out the broadcast spacing.
        if (result == UINT32_MAX)
            return disable();
        measuring = true;
        return isPowerSavingSensor() ? result : setStartDelay();
    }

    // if we somehow got to a second run of this module with measurement disabled, then just wait forever
    if (!moduleConfig.telemetry.air_quality_enabled && !AIR_QUALITY_TELEMETRY_MODULE_ENABLE) {
        measuring = false;
        return disable();
    }

    const TelemetryCadence c = cadence();
    const bool phoneConnected = service->api_state != MeshService::STATE_DISCONNECTED && !isPowerSavingSensor();
    uint32_t nowMs = Time::getMillis();

    const uint32_t msUntilRead = schedule.msUntilRead(nowMs, c, phoneConnected);
    const uint32_t waitMs = prepareSensorsForRead(sensors, msUntilRead);
    warmingUp = msUntilRead == 0 && waitMs > 0;
    if (warmingUp) {
        LOG_DEBUG("AQ: sensors warming up, %ums", (unsigned)waitMs);
        return waitMs;
    }

    if (msUntilRead == 0) {
        captureReading(nowMs, c, phoneConnected);
        nowMs = Time::getMillis();
        sleepSensorsUntilNextRead(sensors, schedule.msUntilRead(nowMs, c, phoneConnected));
    }

    const uint32_t lastMesh = lastMeshMs();
    const bool meshAllowed = airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
                             airTime->isTxAllowedAirUtil();
    const bool meshDue = schedule.meshDue(nowMs, lastMesh, c) && !schedule.storedReadSoon(nowMs, c);

    if (meshDue) {
        bool sent = false;
        if (shouldSendToMesh(history->hasUnpublishedNewest(TELEMETRY_PUBLISHED_MESH), meshDue, meshAllowed))
            sent = c.batchReadings ? sendBufferedTelemetry() : sendTelemetry(NODENUM_BROADCAST, false);
        if (sent) {
            schedule.onMeshSent(nowMs);
            noteSentToMesh();
            if (transmitHistory)
                transmitHistory->setLastSentToMesh(TX_HISTORY_KEY_AIR_QUALITY_TELEMETRY);
        } else {
            LOG_DEBUG("AQ: mesh due, %s, retry later", meshAllowed ? "nothing new to send" : "airtime limit");
            schedule.onMeshSkipped(nowMs,
                                   meshAllowed ? schedule.meshRetryWhenEmpty(nowMs, c) : TelemetryScheduler::MESH_RETRY_MS);
        }
    }

    // SENSOR role: readings the broker has not had, straight to it without going on air.
    // Unreachable broker: the readings stay pending for MQTT. Awake, that counts as an attempt so the due
    // uplink does not spin; on reads, the next read's wake-up tries again.
    if (schedule.mqttDue(nowMs, c)) {
        if (telemetryMqttUplinkAvailable()) {
            if (c.batchReadings)
                sendTelemetryBatch(*history, allocDataPacket(), TELEMETRY_PUBLISHED_MQTT);
            else
                publishTelemetryReadings(*history, [this](const meshtastic_Telemetry &t) { return allocDataProtobuf(t); });
            schedule.onMqttPublished(nowMs);
        } else if (!c.publishOnReads) {
            schedule.onMqttPublished(nowMs);
        }
    }

    if (isPowerSavingSensor() && sharedSleepBudgetMs() >= TELEMETRY_MIN_DEEP_SLEEP_MS) {
        // Grace period so the queued packet leaves before the radio is powered down
        sleepOnNextExecution = true;
        preflightSleepDeferrals = 0;
        LOG_DEBUG("Start next execution in 5s, then sleep");
        return FIVE_SECONDS_MS;
    }

    nowMs = Time::getMillis();
    const uint32_t nextMs = schedule.msUntilNextAction(nowMs, c, phoneConnected, lastMeshMs(), sensorWakeLeadMs(sensors));
    const uint32_t nextWakeMs = prepareSensorsForRead(sensors, schedule.msUntilRead(nowMs, c, phoneConnected));
    return max((uint32_t)DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS, min(nextMs, nextWakeMs));
}

TelemetryCadence AirQualityTelemetryModule::cadence() const
{
    const uint32_t meshSecs = moduleConfig.telemetry.air_quality_interval;
    TelemetryCadence c;
    c.meshMs = Default::getConfiguredOrDefaultMsScaled(meshSecs, default_telemetry_broadcast_interval_secs, numOnlineNodes);
    // Unset: read as often as the mesh publishes, as before the read interval existed
    const uint32_t readSecs = moduleConfig.telemetry.air_quality_telemetry_read_interval;
    c.readMs = readSecs ? Default::getConfiguredOrMinimumValue(readSecs, min_telemetry_read_interval_secs) * 1000
                        : Default::getConfiguredOrDefaultMs(meshSecs, default_telemetry_broadcast_interval_secs);
    c.phoneMs = Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.air_quality_phone_interval,
                                                  default_telemetry_phone_interval_secs);
    // Deep sleeping, each uplink costs a Wi-Fi join: batch it like the mesh, on the read wake-ups
    c.publishOnReads = isPowerSavingSensor();
    c.mqttMs = config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && moduleConfig.mqtt.telemetry_uplink_enabled
                   ? (c.publishOnReads ? c.meshMs : c.readMs)
                   : 0;
    // A SENSOR batches only when it reads more often than it publishes; otherwise each reading goes out
    // on its own as Telemetry, which every client and node already understands
    c.batchReadings = config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && c.readMs < c.meshMs;
    return c;
}

uint32_t AirQualityTelemetryModule::lastMeshMs() const
{
    return schedule.lastMeshMs(transmitHistory ? transmitHistory->getLastSentToMeshMillis(TX_HISTORY_KEY_AIR_QUALITY_TELEMETRY)
                                               : 0);
}

bool AirQualityTelemetryModule::shouldSendToMesh(bool haveUnsentReading, bool meshDue, bool meshAllowed)
{
    return meshDue && meshAllowed && haveUnsentReading;
}

uint32_t AirQualityTelemetryModule::msUntilNeededAwake()
{
    if (!measuring)
        return UINT32_MAX;
    return schedule.msUntilNextAction(Time::getMillis(), cadence(), false, lastMeshMs(), sensorWakeLeadMs(sensors));
}

bool AirQualityTelemetryModule::isTelemetryBusy()
{
    // Not yet initialised: it may still want a read before the node sleeps
    return warmingUp || (firstTime && (moduleConfig.telemetry.air_quality_enabled || AIR_QUALITY_TELEMETRY_MODULE_ENABLE));
}

void AirQualityTelemetryModule::onTelemetryDeepSleep(uint32_t sleepMs)
{
    sleepSensorsUntilNextRead(sensors, sleepMs);
    saveScheduleForSleep(TELEMETRY_SLEEP_SLOT_AIR_QUALITY, schedule);
}

void AirQualityTelemetryModule::openHistory()
{
#ifdef AIR_QUALITY_TELEMETRY_HISTORY_PATH
    // A SENSOR batches between publishes, so its readings have to outlive a reboot or deep sleep
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR) {
        auto *persistent = makeFileTelemetryStore<meshtastic_AirQualityMetrics>(
            AIR_QUALITY_TELEMETRY_HISTORY_PATH, AIR_QUALITY_TELEMETRY_HISTORY_SIZE, AIR_QUALITY_TELEMETRY_HISTORY_FS);
        if (persistent && persistent->isUsable()) {
            history.reset(persistent);
            return;
        }

        // No card, full filesystem, whatever: measuring into RAM beats not measuring
        delete persistent;
        LOG_WARN("AQ history: %s unusable, keeping readings in RAM", AIR_QUALITY_TELEMETRY_HISTORY_PATH);
    }
#endif

    history.reset(new (std::nothrow) RamTelemetryStore<meshtastic_AirQualityMetrics>(AIR_QUALITY_TELEMETRY_HISTORY_SIZE));
    if (!history)
        LOG_ERROR("AQ history: no memory for a store, telemetry disabled");
}

void AirQualityTelemetryModule::captureReading(uint32_t nowMs, const TelemetryCadence &cadence, bool phoneConnected)
{
    const bool stored = schedule.readIsStored(nowMs, cadence);
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    const bool valid = getAirQualityTelemetry(&m);

    if (valid) {
        latest = m;
        hasLatest = true;
        rememberForScreen(m);
        if (stored)
            history->push(m.variant.air_quality_metrics, m.time, schedule.deltaSecs(nowMs));
        LOG_INFO("AQ: read %s, %u stored", stored ? "stored" : "for phone", (unsigned)history->size());
    } else {
        LOG_WARN("AQ read failed, keep the previous reading");
    }
    schedule.onRead(nowMs, stored, valid, cadence.readMs, sharedReadAnchorMs(this));

    if (valid && phoneConnected && service->isToPhoneQueueEmpty() && sendTelemetry(NODENUM_BROADCAST, true) && stored)
        history->markNewestPublished(TELEMETRY_PUBLISHED_PHONE);
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
            // From the last read: reading here would block on I2C and return nothing useful while
            // the sensors are asleep or warming up
            if (!hasLatest) {
                LOG_INFO("No air quality reading yet, no reply to request");
                return NULL;
            }
            LOG_INFO("Air quality telemetry reply to request");
            return allocDataProtobuf(latest);
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
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    if (phoneOnly) {
        // The phone gets every read, stored or not
        if (!hasLatest)
            return false;
        m = latest;
    } else {
        // One fetch then check the mask: on a file-backed store each read costs an open
        TelemetryReading<meshtastic_AirQualityMetrics> reading;
        if (!history->newest(reading) || (reading.publishedMask & TELEMETRY_PUBLISHED_MESH))
            return false;
        m.which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
        m.time = reading.time;
        m.variant.air_quality_metrics = reading.metrics;
    }

    logTelemetry(m);

    meshtastic_MeshPacket *p = allocDataProtobuf(m);
    if (!p)
        return false;
    p->to = dest;
    p->decoded.want_response = false;
    p->priority = config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR ? meshtastic_MeshPacket_Priority_RELIABLE
                                                                                   : meshtastic_MeshPacket_Priority_BACKGROUND;

    if (phoneOnly) {
        LOG_INFO("Sending packet to phone");
        service->sendToPhone(p);
        return true;
    }

    LOG_INFO("Sending packet to mesh");
    service->sendToMesh(p, RX_SRC_LOCAL, true);
    history->markNewestPublished(TELEMETRY_PUBLISHED_MESH);
    // ccToPhone above hands the phone this very reading, no separate phone send needed
    history->markNewestPublished(TELEMETRY_PUBLISHED_PHONE);
    notifySleepAfterSend();
    return true;
}

bool AirQualityTelemetryModule::sendBufferedTelemetry()
{
    if (sendTelemetryBatch(*history, allocDataPacket(), TELEMETRY_PUBLISHED_MESH) == 0)
        return false;
    // The batch is not copied to the client, so it gets the newest reading on its own
    if (service->api_state != MeshService::STATE_DISCONNECTED && hasLatest && sendTelemetry(NODENUM_BROADCAST, true))
        history->markNewestPublished(TELEMETRY_PUBLISHED_PHONE);
    notifySleepAfterSend();
    return true;
}

void AirQualityTelemetryModule::rememberForScreen(const meshtastic_Telemetry &m)
{
    meshtastic_MeshPacket *p = allocDataProtobuf(m);
    if (!p)
        return;
    if (lastMeasurementPacket != nullptr)
        packetPool.release(lastMeasurementPacket);
    lastMeasurementPacket = p;
}

void AirQualityTelemetryModule::notifySleepAfterSend()
{
    if (!isPowerSavingSensor())
        return;
    meshtastic_ClientNotification *notification = clientNotificationPool.allocZeroed();
    if (!notification)
        return;
    notification->level = meshtastic_LogRecord_Level_INFO;
    notification->type = meshtastic_ClientNotification_NotificationType_TELEMETRY_SENT_SLEEPING;
    notification->time = getValidTime(RTCQualityFromNet);
    snprintf(notification->message, sizeof(notification->message), "Sending telemetry and sleeping until the next read");
    service->sendClientNotification(notification);
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
