#pragma once

/**
 * Example: How to Query the Telemetry Database via Admin Commands
 *
 * This file demonstrates potential implementations for admin commands
 * that query the air quality telemetry database.
 */

#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MeshModule.h"

/**
 * Example 1: Get Database Statistics via Admin Message
 *
 * This could be integrated into handleAdminMessageForModule() to support
 * queries like: meshtastic --dest ^all --sendadmin get_air_quality_stats
 */
void handleGetAirQualityStats(AirQualityTelemetryModule *module, meshtastic_AdminMessage *response)
{
    char statsBuffer[256];
    module->getDatabaseStatsString(statsBuffer, sizeof(statsBuffer));

    // Format response
    LOG_INFO("Air Quality Stats: %s", statsBuffer);

    // Could set response->which_payload_variant to appropriate type
    // and copy statsBuffer into response message
}

/**
 * Example 2: Get Recent Records (Last N records)
 *
 * Query format: Get last 10 records from air quality database
 */
void handleGetRecentRecords(AirQualityTelemetryModule *module, uint32_t lastN)
{
    auto &db = module->getDatabase();
    auto stats = db.getStatistics();

    LOG_INFO("Air Quality Database - Last %u records:", lastN);
    LOG_INFO("Total records: %lu, Mesh delivered: %lu, MQTT delivered: %lu", stats.record_count, stats.delivered_mesh,
             stats.delivered_mqtt);

    uint32_t startIdx = (stats.record_count > lastN) ? (stats.record_count - lastN) : 0;

    for (uint32_t i = startIdx; i < stats.record_count; i++) {
        AirQualityDatabase::DatabaseRecord record;
        if (db.getRecord(i, record)) {
            time_t ts = record.timestamp;
            struct tm *timeinfo = localtime(&ts);
            char timeStr[30];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);

            LOG_INFO("  [%u] %s - PM2.5:%u PM10:%u (Mesh:%s MQTT:%s)", i, timeStr, record.telemetry.pm25_standard,
                     record.telemetry.pm100_standard, record.flags.delivered_to_mesh ? "✓" : "✗",
                     record.flags.delivered_to_mqtt ? "✓" : "✗");
        }
    }
}

/**
 * Example 3: Get Aggregated Metrics
 *
 * Returns mean, min, max values for PM2.5 from database
 */
void handleGetAggregateMetrics(AirQualityTelemetryModule *module)
{
    float meanPM25 = module->getDatabaseMeanPM25();
    uint32_t minPM25 = module->getDatabaseMinPM25();
    uint32_t maxPM25 = module->getDatabaseMaxPM25();

    LOG_INFO("Air Quality Metrics (PM2.5):");
    LOG_INFO("  Mean: %.1f µg/m³", meanPM25);
    LOG_INFO("  Min:  %u µg/m³", minPM25);
    LOG_INFO("  Max:  %u µg/m³", maxPM25);

    // Classify air quality based on PM2.5
    const char *quality = "Unknown";
    if (meanPM25 <= 12.0f)
        quality = "Good";
    else if (meanPM25 <= 35.4f)
        quality = "Moderate";
    else if (meanPM25 <= 55.4f)
        quality = "Unhealthy for Sensitive Groups";
    else if (meanPM25 <= 150.4f)
        quality = "Unhealthy";
    else
        quality = "Very Unhealthy";

    LOG_INFO("  Air Quality: %s", quality);
}

/**
 * Example 4: Get Delivery Status
 *
 * Shows which records have been delivered to mesh/MQTT
 */
void handleGetDeliveryStatus(AirQualityTelemetryModule *module)
{
    auto &db = module->getDatabase();
    auto stats = db.getStatistics();

    LOG_INFO("Air Quality Database Delivery Status:");
    LOG_INFO("  Total records:        %lu", stats.record_count);
    LOG_INFO("  Delivered to mesh:    %lu (%.1f%%)", stats.delivered_mesh,
             (stats.record_count > 0) ? (100.0f * stats.delivered_mesh / stats.record_count) : 0.0f);
    LOG_INFO("  Delivered to MQTT:    %lu (%.1f%%)", stats.delivered_mqtt,
             (stats.record_count > 0) ? (100.0f * stats.delivered_mqtt / stats.record_count) : 0.0f);
    LOG_INFO("  Pending delivery:     %lu", stats.record_count - stats.delivered_mesh);
}

/**
 * Example 5: Clear Database
 *
 * Admin command to clear all records
 */
void handleClearDatabase(AirQualityTelemetryModule *module)
{
    auto &db = module->getDatabase();
    auto stats = db.getStatistics();

    LOG_WARN("Clearing air quality database (%lu records)...", stats.record_count);

    if (db.clearAll()) {
        LOG_INFO("Air quality database cleared successfully");
    } else {
        LOG_ERROR("Failed to clear air quality database");
    }
}

/**
 * Example 6: Mark All as Delivered
 *
 * Admin command to mark all records as delivered
 */
void handleMarkAllDelivered(AirQualityTelemetryModule *module, bool toMesh, bool toMqtt)
{
    auto &db = module->getDatabase();
    auto stats = db.getStatistics();

    if (toMesh && db.markAllDeliveredToMesh()) {
        LOG_INFO("Marked all %lu records as delivered to mesh", stats.record_count);
    }

    if (toMqtt && db.markAllDeliveredToMqtt()) {
        LOG_INFO("Marked all %lu records as delivered to MQTT", stats.record_count);
    }
}

/**
 * Example 7: Export Records as JSON
 *
 * Format database records as JSON for external systems
 */
void handleExportAsJson(AirQualityTelemetryModule *module)
{
    auto &db = module->getDatabase();
    auto records = db.getAllRecords();

    LOG_INFO("{");
    LOG_INFO("  \"type\": \"air_quality_records\",");
    LOG_INFO("  \"count\": %lu,", records.size());
    LOG_INFO("  \"records\": [");

    for (size_t i = 0; i < records.size(); i++) {
        const auto &record = records[i];
        time_t ts = record.timestamp;

        LOG_INFO("    {");
        LOG_INFO("      \"index\": %lu,", i);
        LOG_INFO("      \"timestamp\": %lu,", ts);
        LOG_INFO("      \"pm25_standard\": %u,", record.telemetry.pm25_standard);
        LOG_INFO("      \"pm100_standard\": %u,", record.telemetry.pm100_standard);
        LOG_INFO("      \"pm10_standard\": %u,", record.telemetry.pm10_standard);
        LOG_INFO("      \"delivered_to_mesh\": %s,", record.flags.delivered_to_mesh ? "true" : "false");
        LOG_INFO("      \"delivered_to_mqtt\": %s", record.flags.delivered_to_mqtt ? "true" : "false");
        LOG_INFO("    }%s", (i < records.size() - 1) ? "," : "");
    }

    LOG_INFO("  ]");
    LOG_INFO("}");
}

/**
 * Example 8: Integrated Admin Message Handler
 *
 * This shows how to integrate these commands into the module's admin handler
 */
AdminMessageHandleResult exampleAdminHandler(AirQualityTelemetryModule *module, const meshtastic_MeshPacket &mp,
                                             meshtastic_AdminMessage *request, meshtastic_AdminMessage *response)
{
    // Parse request type and dispatch to appropriate handler
    // This is pseudocode - actual implementation depends on protobuf structure

    /*
    if (request->type == "get_air_quality_stats") {
        handleGetAirQualityStats(module, response);
        return AdminMessageHandleResult::HANDLED;
    }

    if (request->type == "get_recent_records") {
        uint32_t lastN = request->param1;
        handleGetRecentRecords(module, lastN);
        return AdminMessageHandleResult::HANDLED;
    }

    if (request->type == "get_metrics") {
        handleGetAggregateMetrics(module);
        return AdminMessageHandleResult::HANDLED;
    }

    if (request->type == "get_delivery_status") {
        handleGetDeliveryStatus(module);
        return AdminMessageHandleResult::HANDLED;
    }

    if (request->type == "clear_database") {
        handleClearDatabase(module);
        return AdminMessageHandleResult::HANDLED;
    }

    if (request->type == "export_json") {
        handleExportAsJson(module);
        return AdminMessageHandleResult::HANDLED;
    }
    */

    return AdminMessageHandleResult::NOT_HANDLED;
}

#endif // HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR
