#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_NODELISTREPORT

#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "NodeListReportModule.h"
#include "RTC.h"
#include "airtime.h"
#include <Arduino.h>
#include <Throttle.h>
#include <algorithm>
#include <cmath>

NodeListReportModule *nodeListReportModule;

namespace
{
constexpr uint32_t fnvOffset = 2166136261UL;
constexpr uint32_t fnvPrime = 16777619UL;
constexpr uint8_t payloadFlagFullSnapshot = 0x01;
constexpr uint8_t payloadFlagFinalChunk = 0x02;
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

uint8_t boundedStringLen(const char *value, uint8_t maxLen)
{
    uint8_t len = 0;
    while (value && value[len] != '\0' && len < maxLen) {
        len++;
    }
    return len;
}

void writeU16(uint8_t *dest, uint16_t value)
{
    dest[0] = value & 0xff;
    dest[1] = value >> 8;
}

void writeU32(uint8_t *dest, uint32_t value)
{
    dest[0] = value & 0xff;
    dest[1] = (value >> 8) & 0xff;
    dest[2] = (value >> 16) & 0xff;
    dest[3] = (value >> 24) & 0xff;
}
} // namespace

NodeListReportModule::NodeListReportModule()
    : SinglePortModule("nodeListReport", meshtastic_PortNum_PRIVATE_APP), concurrency::OSThread("NodeListReport")
{
    cache.reserve(defaultMaxNodesPerReport);

    if (moduleConfig.has_node_list_report && moduleConfig.node_list_report.enabled) {
        const uint32_t startupDelayMs = random(2 * 60 * 1000, 10 * 60 * 1000);
        setIntervalFromNow(startupDelayMs);
    } else {
        disable();
    }
}

uint32_t NodeListReportModule::intervalMs() const
{
    const uint32_t seconds = std::max(moduleConfig.node_list_report.interval_seconds, minIntervalSeconds);
    return Default::getConfiguredOrDefaultMs(seconds, defaultIntervalSeconds);
}

uint32_t NodeListReportModule::fullSnapshotIntervalMs() const
{
    const uint32_t seconds =
        std::max(moduleConfig.node_list_report.full_snapshot_interval_seconds, minFullSnapshotIntervalSeconds);
    return Default::getConfiguredOrDefaultMs(seconds, defaultFullSnapshotIntervalSeconds);
}

uint8_t NodeListReportModule::maxNodesPerReport() const
{
    uint32_t configured = moduleConfig.node_list_report.max_nodes_per_report;
    if (configured == 0) {
        configured = defaultMaxNodesPerReport;
    }
    return static_cast<uint8_t>(std::min(configured, static_cast<uint32_t>(hardMaxNodesPerReport)));
}

uint8_t NodeListReportModule::minChangedNodesBeforeSend() const
{
    return std::max<uint32_t>(moduleConfig.node_list_report.min_changed_nodes_before_send, 1);
}

bool NodeListReportModule::isConfigured() const
{
    return moduleConfig.has_node_list_report && moduleConfig.node_list_report.enabled &&
           moduleConfig.node_list_report.destination_node != 0 && !isBroadcast(moduleConfig.node_list_report.destination_node);
}

bool NodeListReportModule::shouldSendFullSnapshot() const
{
    return lastFullSnapshotMs == 0 || !Throttle::isWithinTimespanMs(lastFullSnapshotMs, fullSnapshotIntervalMs());
}

uint16_t NodeListReportModule::hashString16(const char *value) const
{
    uint32_t hash = fnvOffset;
    for (size_t i = 0; value && value[i] != '\0'; i++) {
        hash = hashByte(hash, value[i]);
    }
    return static_cast<uint16_t>((hash >> 16) ^ hash);
}

uint16_t NodeListReportModule::positionHash(const meshtastic_NodeInfoLite &node) const
{
    if (!moduleConfig.node_list_report.include_position || !nodeDB->hasValidPosition(&node)) {
        return 0;
    }

    // Hash roughly 0.01 degree buckets, avoiding raw coordinate disclosure in this report.
    const int32_t latBucket = node.position.latitude_i / 1000000;
    const int32_t lonBucket = node.position.longitude_i / 1000000;
    uint32_t hash = hashUint32(fnvOffset, static_cast<uint32_t>(latBucket));
    hash = hashUint32(hash, static_cast<uint32_t>(lonBucket));
    return static_cast<uint16_t>((hash >> 16) ^ hash);
}

uint8_t NodeListReportModule::ageBucket(const meshtastic_NodeInfoLite &node) const
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

int8_t NodeListReportModule::snrBucket(float snr) const
{
    if (std::isnan(snr)) {
        return INT8_MIN;
    }
    return static_cast<int8_t>(std::max(-128, std::min(127, static_cast<int>(lroundf(snr)))));
}

uint32_t NodeListReportModule::computeMetricSignature(const meshtastic_NodeInfoLite &node) const
{
    uint32_t hash = hashUint32(fnvOffset, node.num);
    hash = hashByte(hash, ageBucket(node));
    hash = hashByte(hash, node.has_hops_away ? node.hops_away : 0xff);
    hash = hashByte(hash, static_cast<uint8_t>(snrBucket(node.snr)));
    hash = hashUint32(hash, positionHash(node));
    return hash;
}

uint32_t NodeListReportModule::computeNameSignature(const meshtastic_NodeInfoLite &node) const
{
    uint32_t hash = hashUint32(fnvOffset, node.num);
    if (node.has_user) {
        for (uint8_t b : node.user.macaddr) {
            hash = hashByte(hash, b);
        }
        hash = hashUint32(hash, hashString16(node.user.short_name));
        hash = hashUint32(hash, hashString16(node.user.long_name));
    }
    return hash;
}

NodeListReportModule::CachedNode *NodeListReportModule::cachedNode(NodeNum nodeNum)
{
    for (auto &entry : cache) {
        if (entry.nodeNum == nodeNum) {
            return &entry;
        }
    }
    return nullptr;
}

bool NodeListReportModule::appendRecord(uint8_t *payload, size_t &payloadSize, const meshtastic_NodeInfoLite &node, uint8_t flags,
                                        bool includeNames)
{
    const uint16_t posHash = positionHash(node);
    if (ageBucket(node) >= 6) {
        flags |= recordFlagStale;
    }
    if (posHash != 0) {
        flags |= recordFlagHasPositionHash;
    }

    uint8_t shortLen = 0;
    uint8_t longLen = 0;
    if (includeNames && node.has_user) {
        shortLen = boundedStringLen(node.user.short_name, maxShortNameBytes);
        longLen = boundedStringLen(node.user.long_name, maxLongNameBytes);
        if (shortLen || longLen) {
            flags |= recordFlagHasNames;
        }
    }

    const size_t requiredSize = fixedRecordSize + ((flags & recordFlagHasPositionHash) ? 2 : 0) +
                                ((flags & recordFlagHasNames) ? (2 + shortLen + longLen) : 0);
    if (payloadSize + requiredSize > maxPayloadSize) {
        return false;
    }

    uint8_t *record = payload + payloadSize;
    writeU32(record, node.num);
    record[4] = flags;
    record[5] = ageBucket(node);
    record[6] = node.has_hops_away ? node.hops_away : 0xff;
    record[7] = static_cast<uint8_t>(snrBucket(node.snr));
    payloadSize += fixedRecordSize;

    if (flags & recordFlagHasPositionHash) {
        writeU16(payload + payloadSize, posHash);
        payloadSize += 2;
    }
    if (flags & recordFlagHasNames) {
        payload[payloadSize++] = shortLen;
        if (shortLen) {
            memcpy(payload + payloadSize, node.user.short_name, shortLen);
            payloadSize += shortLen;
        }
        payload[payloadSize++] = longLen;
        if (longLen) {
            memcpy(payload + payloadSize, node.user.long_name, longLen);
            payloadSize += longLen;
        }
    }

    return true;
}

bool NodeListReportModule::buildPayload(uint8_t *payload, size_t &payloadSize, bool fullSnapshot, bool &snapshotComplete)
{
    const uint8_t maxRecords = maxNodesPerReport();
    uint8_t count = 0;
    uint32_t readIndex = fullSnapshot ? fullSnapshotReadIndex : 0;

    snapshotComplete = false;
    memcpy(payload, "NLR2", 4);
    payload[4] = fullSnapshot ? payloadFlagFullSnapshot : 0;
    payload[5] = 0;
    writeU16(payload + 6, sequence);
    writeU32(payload + 8, fullSnapshot ? fullSnapshotReportId : sequence);
    writeU16(payload + 12, fullSnapshot ? fullSnapshotChunkIndex : 0);
    writeU16(payload + 14, static_cast<uint16_t>(nodeDB->getNumMeshNodes()));
    payloadSize = headerSize;

    while (count < maxRecords) {
        const uint32_t nodeReadIndex = readIndex;
        const meshtastic_NodeInfoLite *node = nodeDB->readNextMeshNode(readIndex);
        if (!node) {
            snapshotComplete = true;
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

        uint8_t flags = isNew ? recordFlagNew : recordFlagUpdated;
        const bool includeNames = fullSnapshot || isNew || namesChanged;
        if (!appendRecord(payload, payloadSize, *node, flags, includeNames)) {
            if (fullSnapshot) {
                readIndex = nodeReadIndex;
            }
            break;
        }

        count++;
    }

    if (fullSnapshot) {
        fullSnapshotReadIndex = readIndex;
        if (snapshotComplete) {
            payload[4] |= payloadFlagFinalChunk;
        }
    }

    payload[5] = count;
    return fullSnapshot ? count > 0 : count >= minChangedNodesBeforeSend();
}

void NodeListReportModule::markPayloadSent(const uint8_t *payload, size_t payloadSize)
{
    const uint8_t count = payload[5];
    size_t offset = headerSize;
    for (uint8_t i = 0; i < count; i++) {
        if (offset + fixedRecordSize > payloadSize) {
            break;
        }
        const uint8_t *record = payload + offset;
        const NodeNum nodeNum = static_cast<NodeNum>(record[0]) | (static_cast<NodeNum>(record[1]) << 8) |
                                (static_cast<NodeNum>(record[2]) << 16) | (static_cast<NodeNum>(record[3]) << 24);
        const uint8_t flags = record[4];
        offset += fixedRecordSize;
        if (flags & recordFlagHasPositionHash) {
            offset += 2;
        }
        if (flags & recordFlagHasNames) {
            if (offset >= payloadSize) {
                break;
            }
            offset += 1 + payload[offset];
            if (offset >= payloadSize) {
                break;
            }
            offset += 1 + payload[offset];
        }

        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeNum);
        if (!node) {
            continue;
        }

        const uint32_t metricSignature = computeMetricSignature(*node);
        const uint32_t nameSignature = computeNameSignature(*node);
        CachedNode *cached = cachedNode(nodeNum);
        if (cached) {
            cached->metricSignature = metricSignature;
            cached->nameSignature = nameSignature;
        } else {
            cache.push_back({nodeNum, metricSignature, nameSignature});
        }
    }

    (void)payloadSize;
}

void NodeListReportModule::startFullSnapshot()
{
    fullSnapshotInProgress = true;
    fullSnapshotReadIndex = 0;
    fullSnapshotChunkIndex = 0;
    fullSnapshotReportId = (static_cast<uint32_t>(random(0xffff)) << 16) | static_cast<uint32_t>(random(0xffff));
}

bool NodeListReportModule::sendReport(bool fullSnapshot)
{
    if (!airTime || !airTime->isTxAllowedChannelUtil(true) || !airTime->isTxAllowedAirUtil()) {
        LOG_DEBUG("NodeListReport: channel busy, deferring");
        return false;
    }
    if (owner.is_licensed) {
        LOG_WARN("NodeListReport: disabled while Ham mode is active");
        return false;
    }

    NodeNum dest = moduleConfig.node_list_report.destination_node;
    const meshtastic_NodeInfoLite *destNode = nodeDB->getMeshNode(dest);
    if (!destNode || !destNode->has_user || destNode->user.public_key.size != 32) {
        LOG_WARN("NodeListReport: destination 0x%x has no known public key", dest);
        return false;
    }

    if (fullSnapshot && !fullSnapshotInProgress) {
        startFullSnapshot();
    }

    uint8_t payload[maxPayloadSize];
    size_t payloadSize = 0;
    bool snapshotComplete = false;
    if (!buildPayload(payload, payloadSize, fullSnapshot, snapshotComplete)) {
        if (fullSnapshot && snapshotComplete) {
            fullSnapshotInProgress = false;
            lastFullSnapshotMs = millis();
        }
        return false;
    }

    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = dest;
    p->channel = 0;
    p->decoded.want_response = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->decoded.payload.size = payloadSize;
    memcpy(p->decoded.payload.bytes, payload, payloadSize);

    service->sendToMesh(p, RX_SRC_LOCAL, false);
    markPayloadSent(payload, payloadSize);
    sequence++;

    if (fullSnapshot) {
        fullSnapshotChunkIndex++;
        if (snapshotComplete) {
            fullSnapshotInProgress = false;
            lastFullSnapshotMs = millis();
        }
    }
    lastIncrementalMs = millis();
    LOG_INFO("NodeListReport: sent %u %s records to 0x%x", payload[5], fullSnapshot ? "snapshot" : "changed", dest);
    return true;
}

bool NodeListReportModule::triggerReport(bool fullSnapshot)
{
    if (!isConfigured()) {
        LOG_WARN("NodeListReport: trigger ignored, module is disabled or missing destination");
        return false;
    }

    if (fullSnapshot) {
        startFullSnapshot();
    }

    return sendReport(fullSnapshot);
}

int32_t NodeListReportModule::runOnce()
{
    if (!isConfigured()) {
        LOG_DEBUG("NodeListReportModule is disabled or missing destination");
        return disable();
    }

    if (fullSnapshotInProgress) {
        sendReport(true);
        return fullSnapshotChunkIntervalMs + random(0, 60 * 1000);
    }

    const bool fullSnapshot = shouldSendFullSnapshot();
    if (fullSnapshot || lastIncrementalMs == 0 || !Throttle::isWithinTimespanMs(lastIncrementalMs, intervalMs())) {
        sendReport(fullSnapshot);
    }

    if (fullSnapshotInProgress) {
        return fullSnapshotChunkIntervalMs + random(0, 60 * 1000);
    }

    const uint32_t jitterMs = random(0, 5 * 60 * 1000);
    return intervalMs() + jitterMs;
}

#endif
