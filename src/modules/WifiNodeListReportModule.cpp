#include "configuration.h"
#if HAS_WIFI && !MESHTASTIC_EXCLUDE_NODELISTREPORT

#include "Default.h"
#include "PowerStatus.h"
#include "RTC.h"
#include "WifiNodeListReportModule.h"
#include "mesh/wifi/WiFiAPClient.h"
#include <Arduino.h>
#include <Throttle.h>
#include <algorithm>
#include <cmath>

#if defined(ARCH_ESP32)
#include <HTTPClient.h>
#endif

WifiNodeListReportModule *wifiNodeListReportModule;

namespace
{
constexpr uint32_t fnvOffset = 2166136261UL;
constexpr uint32_t fnvPrime = 16777619UL;
constexpr uint8_t recordFlagNew = 0x01;
constexpr uint8_t recordFlagUpdated = 0x02;
constexpr uint8_t recordFlagStale = 0x04;
constexpr uint8_t recordFlagHasNames = 0x08;
constexpr uint8_t recordFlagHasPositionHash = 0x10;

uint32_t hashByte(uint32_t hash, uint8_t value)
{
    return (hash ^ value) * fnvPrime;
}

uint32_t hashUint32(uint32_t hash, uint32_t value)
{
    for (uint8_t i = 0; i < sizeof(value); i++) {
        hash = hashByte(hash, (value >> (8 * i)) & 0xff);
    }
    return hash;
}

uint32_t hashString(uint32_t hash, const char *value)
{
    for (size_t i = 0; value && value[i] != '\0'; i++) {
        hash = hashByte(hash, value[i]);
    }
    return hash;
}
} // namespace

WifiNodeListReportModule::WifiNodeListReportModule() : concurrency::OSThread("WifiNodeListReport")
{
    cache.reserve(16);

    if (isConfigured()) {
        const uint32_t startupDelayMs = random(2 * 60 * 1000, 10 * 60 * 1000);
        setIntervalFromNow(startupDelayMs);
    } else {
        disable();
    }
}

uint32_t WifiNodeListReportModule::intervalMs() const
{
    const uint32_t seconds = std::max(moduleConfig.wifi_node_list_report.interval_seconds, minIntervalSeconds);
    return Default::getConfiguredOrDefaultMs(seconds, defaultIntervalSeconds);
}

uint32_t WifiNodeListReportModule::fullSnapshotIntervalMs() const
{
    const uint32_t seconds =
        std::max(moduleConfig.wifi_node_list_report.full_snapshot_interval_seconds, minFullSnapshotIntervalSeconds);
    return Default::getConfiguredOrDefaultMs(seconds, defaultFullSnapshotIntervalSeconds);
}

uint32_t WifiNodeListReportModule::connectTimeoutMs() const
{
    const uint32_t seconds =
        Default::getConfiguredOrDefault(moduleConfig.wifi_node_list_report.connect_timeout_seconds, defaultConnectTimeoutSeconds);
    return std::min<uint32_t>(seconds, 120) * 1000;
}

uint8_t WifiNodeListReportModule::minChangedNodesBeforeSend() const
{
    return std::max<uint32_t>(moduleConfig.wifi_node_list_report.min_changed_nodes_before_send, 1);
}

bool WifiNodeListReportModule::isConfigured() const
{
    return moduleConfig.has_wifi_node_list_report && moduleConfig.wifi_node_list_report.enabled &&
           moduleConfig.wifi_node_list_report.url[0] != '\0' && config.network.wifi_ssid[0] != '\0';
}

bool WifiNodeListReportModule::powerGateAllowsWifi() const
{
    if (powerStatus && (powerStatus->getHasUSB() || powerStatus->getIsCharging())) {
        return true;
    }

    const uint8_t threshold = std::min<uint32_t>(
        Default::getConfiguredOrDefault(moduleConfig.wifi_node_list_report.battery_threshold_percent,
                                        default_wifi_node_list_report_battery_threshold_percent),
        100);
    return powerStatus && powerStatus->getBatteryChargePercent() >= threshold;
}

bool WifiNodeListReportModule::shouldSendFullSnapshot() const
{
    return lastFullSnapshotMs == 0 || !Throttle::isWithinTimespanMs(lastFullSnapshotMs, fullSnapshotIntervalMs());
}

bool WifiNodeListReportModule::ensureWifiConnected(bool &startedWifi)
{
    startedWifi = false;
#if defined(ARCH_ESP32)
    if (WiFi.isConnected()) {
        return true;
    }

    const char *wifiPsw = config.network.wifi_psk;
    if (!*wifiPsw) {
        wifiPsw = nullptr;
    }

    startedWifi = true;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.begin(config.network.wifi_ssid, wifiPsw);

    const uint32_t start = millis();
    while (!WiFi.isConnected() && millis() - start < connectTimeoutMs()) {
        delay(250);
    }
    return WiFi.isConnected();
#else
    return false;
#endif
}

void WifiNodeListReportModule::restoreWifi(bool startedWifi)
{
#if defined(ARCH_ESP32)
    if (startedWifi && !config.network.wifi_enabled) {
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
    }
#else
    (void)startedWifi;
#endif
}

uint8_t WifiNodeListReportModule::ageBucket(const meshtastic_NodeInfoLite &node) const
{
    if (node.last_heard == 0) {
        return 0;
    }

    const uint32_t age = sinceLastSeen(&node);
    if (age < 15 * 60) {
        return 1;
    }
    if (age < 60 * 60) {
        return 2;
    }
    if (age < 6 * 60 * 60) {
        return 3;
    }
    if (age < 24 * 60 * 60) {
        return 4;
    }
    if (age < 7 * 24 * 60 * 60) {
        return 5;
    }
    return 6;
}

int8_t WifiNodeListReportModule::snrBucket(float snr) const
{
    if (std::isnan(snr)) {
        return INT8_MIN;
    }
    return static_cast<int8_t>(std::max(-128, std::min(127, static_cast<int>(lroundf(snr)))));
}

uint16_t WifiNodeListReportModule::positionHash(const meshtastic_NodeInfoLite &node) const
{
    if (!moduleConfig.wifi_node_list_report.include_position || !nodeDB->hasValidPosition(&node)) {
        return 0;
    }

    const int32_t latBucket = node.position.latitude_i / 1000000;
    const int32_t lonBucket = node.position.longitude_i / 1000000;
    uint32_t hash = hashUint32(fnvOffset, static_cast<uint32_t>(latBucket));
    hash = hashUint32(hash, static_cast<uint32_t>(lonBucket));
    return static_cast<uint16_t>((hash >> 16) ^ hash);
}

uint32_t WifiNodeListReportModule::computeMetricSignature(const meshtastic_NodeInfoLite &node) const
{
    uint32_t hash = hashUint32(fnvOffset, node.num);
    hash = hashByte(hash, ageBucket(node));
    hash = hashByte(hash, node.has_hops_away ? node.hops_away : 0xff);
    hash = hashByte(hash, static_cast<uint8_t>(snrBucket(node.snr)));
    hash = hashUint32(hash, positionHash(node));
    return hash;
}

uint32_t WifiNodeListReportModule::computeNameSignature(const meshtastic_NodeInfoLite &node) const
{
    uint32_t hash = hashUint32(fnvOffset, node.num);
    if (node.has_user) {
        hash = hashString(hash, node.user.short_name);
        hash = hashString(hash, node.user.long_name);
        for (uint8_t b : node.user.macaddr) {
            hash = hashByte(hash, b);
        }
    }
    return hash;
}

WifiNodeListReportModule::CachedNode *WifiNodeListReportModule::cachedNode(NodeNum nodeNum)
{
    for (auto &entry : cache) {
        if (entry.nodeNum == nodeNum) {
            return &entry;
        }
    }
    return nullptr;
}

void WifiNodeListReportModule::appendEscaped(String &out, const char *value) const
{
    out += '"';
    for (size_t i = 0; value && value[i] != '\0'; i++) {
        const char c = value[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<uint8_t>(c) >= 0x20) {
            out += c;
        }
    }
    out += '"';
}

void WifiNodeListReportModule::appendRecordJson(String &out, const meshtastic_NodeInfoLite &node, uint8_t flags,
                                                bool includeNames) const
{
    const uint16_t posHash = positionHash(node);
    if (ageBucket(node) >= 6) {
        flags |= recordFlagStale;
    }
    if (posHash != 0) {
        flags |= recordFlagHasPositionHash;
    }
    if (includeNames && node.has_user) {
        flags |= recordFlagHasNames;
    }

    out += "{\"node_id\":\"!";
    char nodeId[9];
    snprintf(nodeId, sizeof(nodeId), "%08x", node.num);
    out += nodeId;
    out += "\",\"num\":";
    out += node.num;
    out += ",\"flags\":";
    out += static_cast<uint32_t>(flags);
    out += ",\"age_bucket\":";
    out += static_cast<uint32_t>(ageBucket(node));
    out += ",\"hops_away\":";
    if (node.has_hops_away) {
        out += static_cast<uint32_t>(node.hops_away);
    } else {
        out += "null";
    }
    out += ",\"snr_bucket\":";
    out += static_cast<int32_t>(snrBucket(node.snr));
    if (flags & recordFlagHasPositionHash) {
        out += ",\"position_hash\":";
        out += posHash;
    }
    if (flags & recordFlagHasNames) {
        out += ",\"short_name\":";
        appendEscaped(out, node.user.short_name);
        out += ",\"long_name\":";
        appendEscaped(out, node.user.long_name);
    }
    out += '}';
}

bool WifiNodeListReportModule::buildJson(String &json, bool fullSnapshot, uint8_t &recordCount)
{
    recordCount = 0;
    json = "{\"type\":\"";
    json += fullSnapshot ? "full_snapshot" : "diff";
    json += "\",\"version\":1,\"sequence\":";
    json += sequence;
    json += ",\"from\":\"!";
    char fromId[9];
    snprintf(fromId, sizeof(fromId), "%08x", nodeDB->getNodeNum());
    json += fromId;
    json += "\",\"known_node_count\":";
    json += static_cast<uint32_t>(nodeDB->getNumMeshNodes());
    json += ",\"records\":[";

    uint32_t readIndex = 0;
    bool first = true;
    while (true) {
        const meshtastic_NodeInfoLite *node = nodeDB->readNextMeshNode(readIndex);
        if (!node) {
            break;
        }
        if (node->num == 0 || node->num == nodeDB->getNodeNum()) {
            continue;
        }

        const uint32_t metricSignature = computeMetricSignature(*node);
        const uint32_t nameSignature = computeNameSignature(*node);
        CachedNode *cached = cachedNode(node->num);
        const bool isNew = cached == nullptr;
        const bool metricsChanged = isNew || cached->metricSignature != metricSignature;
        const bool namesChanged = isNew || cached->nameSignature != nameSignature;
        if (!fullSnapshot && !metricsChanged && !namesChanged) {
            continue;
        }

        if (!first) {
            json += ',';
        }
        first = false;

        const uint8_t flags = isNew ? recordFlagNew : recordFlagUpdated;
        appendRecordJson(json, *node, flags, fullSnapshot || isNew || namesChanged);
        recordCount++;
    }

    json += "]}";
    return fullSnapshot ? recordCount > 0 : recordCount >= minChangedNodesBeforeSend();
}

void WifiNodeListReportModule::markJsonSent(bool fullSnapshot)
{
    uint32_t readIndex = 0;
    while (true) {
        const meshtastic_NodeInfoLite *node = nodeDB->readNextMeshNode(readIndex);
        if (!node) {
            break;
        }
        if (node->num == 0 || node->num == nodeDB->getNodeNum()) {
            continue;
        }

        const uint32_t metricSignature = computeMetricSignature(*node);
        const uint32_t nameSignature = computeNameSignature(*node);
        CachedNode *cached = cachedNode(node->num);
        if (cached) {
            cached->metricSignature = metricSignature;
            cached->nameSignature = nameSignature;
        } else {
            cache.push_back({node->num, metricSignature, nameSignature});
        }
    }

    if (fullSnapshot) {
        lastFullSnapshotMs = millis();
    }
    lastIncrementalMs = millis();
    sequence++;
}

bool WifiNodeListReportModule::postReport(bool fullSnapshot)
{
#if defined(ARCH_ESP32)
    if (!powerGateAllowsWifi()) {
        LOG_DEBUG("WifiNodeListReport: power gate blocked WiFi report");
        return false;
    }

    uint8_t recordCount = 0;
    String body;
    body.reserve(fullSnapshot ? 8192 : 2048);
    if (!buildJson(body, fullSnapshot, recordCount)) {
        return false;
    }

    bool startedWifi = false;
    if (!ensureWifiConnected(startedWifi)) {
        LOG_WARN("WifiNodeListReport: WiFi connect failed");
        restoreWifi(startedWifi);
        return false;
    }

    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(moduleConfig.wifi_node_list_report.url)) {
        LOG_WARN("WifiNodeListReport: invalid URL");
        restoreWifi(startedWifi);
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    const int code = http.POST(body);
    http.end();
    restoreWifi(startedWifi);

    if (code < 200 || code >= 300) {
        LOG_WARN("WifiNodeListReport: HTTP POST failed, code=%d", code);
        return false;
    }

    markJsonSent(fullSnapshot);
    LOG_INFO("WifiNodeListReport: posted %u %s records", recordCount, fullSnapshot ? "snapshot" : "changed");
    return true;
#else
    return false;
#endif
}

int32_t WifiNodeListReportModule::runOnce()
{
    if (!isConfigured()) {
        LOG_DEBUG("WifiNodeListReportModule is disabled or missing URL/WiFi SSID");
        return disable();
    }

    const bool fullSnapshot = shouldSendFullSnapshot();
    if (fullSnapshot || lastIncrementalMs == 0 || !Throttle::isWithinTimespanMs(lastIncrementalMs, intervalMs())) {
        postReport(fullSnapshot);
    }

    return intervalMs() + random(0, 5 * 60 * 1000);
}

#endif
