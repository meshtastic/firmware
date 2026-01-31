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
#include "Sensor/AddI2CSensorTemplate.h"
#include "UnitConversions.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "main.h"
#include "sleep.h"
#include <Throttle.h>

#if HAS_NETWORKING
#include "mqtt/MQTT.h"
#endif

// Sensors
#include "Sensor/PMSA003ISensor.h"

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

    // Check for MQTT connectivity and attempt recovery if connected
    if (mqtt && mqtt->isConnectedDirectly() && !mqttRecoveryAttempted) {
        LOG_INFO("MQTT is connected, attempting recovery of pending records");
        mqttRecoveryAttempted = true;
        recoverMQTTRecords();
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = false;

        // Initialize the telemetry database
        if (!telemetryDatabase.init()) {
            LOG_WARN("Failed to initialize air quality telemetry database");
        }

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
        LOG_INFO("Waking up sensors");
        for (TelemetrySensor *sensor : sensors) {
            if (!sensor->isActive()) {
                return sensor->wakeUp();
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
        LOG_INFO("Sending sensors to sleep");
        for (TelemetrySensor *sensor : sensors) {
            sensor->sleep();
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

    const auto &m = telemetry.variant.air_quality_metrics.pmsa003idata;

    // Check if any telemetry field has valid data
    bool hasAny = m.has_pm10_standard || m.has_pm25_standard || m.has_pm100_standard || m.has_pm10_environmental ||
                  m.has_pm25_environmental || m.has_pm100_environmental;

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

        LOG_INFO("(Received from %s): pm10_standard=%i, pm25_standard=%i, pm100_standard=%i", sender,
                 t->variant.air_quality_metrics.pmsa003idata.pm10_standard, t->variant.air_quality_metrics.pmsa003idata.pm25_standard,
                 t->variant.air_quality_metrics.pmsa003idata.pm100_standard);

        LOG_INFO("                  | PM1.0(Environmental)=%i, PM2.5(Environmental)=%i, PM10.0(Environmental)=%i",
                 t->variant.air_quality_metrics.pmsa003idata.pm10_environmental, t->variant.air_quality_metrics.pmsa003idata.pm25_environmental,
                 t->variant.air_quality_metrics.pmsa003idata.pm100_environmental);
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

    for (TelemetrySensor *sensor : sensors) {
        LOG_INFO("Reading AQ sensors");
        valid = valid && sensor->getMetrics(m);
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
                    pm10_environmental=%u, pm25_environmental=%u, pm100_environmental=%u",
                 m.variant.air_quality_metrics.pmsa003idata.pm10_standard, m.variant.air_quality_metrics.pmsa003idata.pm25_standard,
                 m.variant.air_quality_metrics.pmsa003idata.pm100_standard, m.variant.air_quality_metrics.pmsa003idata.pm10_environmental,
                 m.variant.air_quality_metrics.pmsa003idata.pm25_environmental, m.variant.air_quality_metrics.pmsa003idata.pm100_environmental);

        if (m.time == 0) {
            LOG_WARN("AirQualityTelemetry: Invalid time, not sending telemetry");
        } else {
            // Record to database
            DatabaseRecord dbRecord;
            dbRecord.telemetry = m;
            dbRecord.delivered = 0;
            if (!telemetryDatabase.addRecord(dbRecord)) {
                LOG_DEBUG("Failed to add record to air quality database");
            }
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

            // Mark last record as delivered to mesh
            uint32_t recordCount = telemetryDatabase.getRecordCount();
            if (recordCount > 0) {
                telemetryDatabase.markDelivered(recordCount - 1);
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

uint32_t AirQualityTelemetryModule::recoverMQTTRecords()
{
    // Get all records not yet delivered via MQTT
    auto recordsToRecover = telemetryDatabase.getRecordsForRecovery();

    if (recordsToRecover.empty()) {
        LOG_DEBUG("AirQualityTelemetry: No records to recover via MQTT");
        return 0;
    }

    LOG_INFO("AirQualityTelemetry: Starting MQTT recovery for %d records", recordsToRecover.size());

    uint32_t recovered = 0;

    // Get all records for marking after successful send
    std::vector<DatabaseRecord> allRecords = telemetryDatabase.getAllRecords();

    // Iterate through records and send them individually
    for (size_t i = 0; i < recordsToRecover.size(); i++) {
        const auto &record = recordsToRecover[i];

        // Create a telemetry message from the stored record
        meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
        m.which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
        m.time = record.telemetry.time;
        m.variant.air_quality_metrics = record.telemetry.variant.air_quality_metrics;

        // Send via telemetry module (which will handle MQTT queuing)
        meshtastic_MeshPacket *p = allocDataProtobuf(m);
        p->to = NODENUM_BROADCAST;
        p->decoded.want_response = false;
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

        // Send to mesh (this will also queue for MQTT if configured)
        service->sendToMesh(p, RX_SRC_LOCAL, true);

        recovered++;
        LOG_DEBUG("AirQualityTelemetry: Sent record %d/%d for recovery (timestamp: %u)", recovered, recordsToRecover.size(),
                  record.telemetry.time);

        // Mark record as delivered
        // Find the record by timestamp and mark it
        for (size_t idx = 0; idx < allRecords.size(); idx++) {
            if (allRecords[idx].telemetry.time == record.telemetry.time) {
                if (telemetryDatabase.markDelivered(idx)) {
                    LOG_DEBUG("AirQualityTelemetry: Marked record (timestamp: %u) as delivered", record.telemetry.time);
                } else {
                    LOG_WARN("AirQualityTelemetry: Failed to mark record (timestamp: %u) as delivered", record.telemetry.time);
                }
                break;
            }
        }
    }

    LOG_INFO("AirQualityTelemetry: MQTT recovery completed - sent %d records", recovered);
    return recovered;
}

AdminMessageHandleResult AirQualityTelemetryModule::handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                                meshtastic_AdminMessage *request,
                                                                                meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result = AdminMessageHandleResult::NOT_HANDLED;

    // Handle manual recovery request
    if (request->which_payload_variant == meshtastic_Telemetry_Recovery_air_quality_recovery_tag) {
        recoverMQTTRecords();

        result = AdminMessageHandleResult::HANDLED;
        return result;
    }

    for (TelemetrySensor *sensor : sensors) {
        result = sensor->handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }

    return result;
}

/**
 * Helper function to get database statistics as a formatted string
 */
void AirQualityTelemetryModule::getDatabaseStatsString(char *buffer, size_t bufferSize)
{
    auto stats = telemetryDatabase.getStatistics();
    snprintf(buffer, bufferSize, "Air Quality DB: %lu records, Mesh:%lu Age:%.1fh", stats.record_count, stats.delivered,
             (getTime() - stats.min_timestamp) / 3600.0f);
}

// /**
//  * Get mean PM2.5 value from database records
//  */
// float AirQualityTelemetryModule::getDatabaseMeanPM25()
// {
//     auto stats = telemetryDatabase.getStatistics();
//     if (stats.record_count == 0)
//         return 0.0f;

//     auto records = telemetryDatabase.getAllRecords();
//     uint64_t sum = 0;
//     for (const auto &record : records) {
//         sum += record.telemetry.pmsa003idata.pm25_standard;
//     }
//     return static_cast<float>(sum) / records.size();
// }

// /**
//  * Get max PM2.5 value from database records
//  */
// uint32_t AirQualityTelemetryModule::getDatabaseMaxPM25()
// {
//     auto records = telemetryDatabase.getAllRecords();
//     if (records.empty())
//         return 0;

//     uint32_t maxVal = 0;
//     for (const auto &record : records) {
//         if (record.telemetry.pmsa003idata.pm25_standard > maxVal) {
//             maxVal = record.telemetry.pmsa003idata.pm25_standard;
//         }
//     }
//     return maxVal;
// }

// /**
//  * Get min PM2.5 value from database records
//  */
// uint32_t AirQualityTelemetryModule::getDatabaseMinPM25()
// {
//     auto records = telemetryDatabase.getAllRecords();
//     if (records.empty())
//         return 0;

//     uint32_t minVal = records[0].telemetry.pmsa003idata.pm25_standard;
//     for (const auto &record : records) {
//         if (record.telemetry.pmsa003idata.pm25_standard < minVal) {
//             minVal = record.telemetry.pmsa003idata.pm25_standard;
//         }
//     }
//     return minVal;
// }

#endif