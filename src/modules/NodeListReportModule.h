#pragma once

#if !MESHTASTIC_EXCLUDE_NODELISTREPORT
#include "SinglePortModule.h"
#include <vector>

class NodeListReportModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    NodeListReportModule();
    bool triggerReport(bool fullSnapshot);

  protected:
    virtual int32_t runOnce() override;

  private:
    struct CachedNode {
        NodeNum nodeNum = 0;
        uint32_t metricSignature = 0;
        uint32_t nameSignature = 0;
    };

    static constexpr uint32_t defaultIntervalSeconds = 60 * 60;
    static constexpr uint32_t minIntervalSeconds = 15 * 60;
    static constexpr uint32_t defaultFullSnapshotIntervalSeconds = 24 * 60 * 60;
    static constexpr uint32_t minFullSnapshotIntervalSeconds = 6 * 60 * 60;
    static constexpr uint8_t defaultMaxNodesPerReport = 10;
    static constexpr uint8_t hardMaxNodesPerReport = 14;
    static constexpr uint8_t headerSize = 16;
    static constexpr uint8_t fixedRecordSize = 8;
    static constexpr uint8_t maxPayloadSize = 220;
    static constexpr uint8_t maxShortNameBytes = 5;
    static constexpr uint8_t maxLongNameBytes = 32;
    static constexpr uint32_t fullSnapshotChunkIntervalMs = 2 * 60 * 1000;

    uint32_t lastIncrementalMs = 0;
    uint32_t lastFullSnapshotMs = 0;
    uint32_t fullSnapshotReadIndex = 0;
    uint32_t fullSnapshotReportId = 0;
    uint16_t sequence = 0;
    uint16_t fullSnapshotChunkIndex = 0;
    bool fullSnapshotInProgress = false;
    std::vector<CachedNode> cache;

    uint32_t intervalMs() const;
    uint32_t fullSnapshotIntervalMs() const;
    uint8_t maxNodesPerReport() const;
    uint8_t minChangedNodesBeforeSend() const;
    bool isConfigured() const;
    bool shouldSendFullSnapshot() const;
    uint32_t computeMetricSignature(const meshtastic_NodeInfoLite &node) const;
    uint32_t computeNameSignature(const meshtastic_NodeInfoLite &node) const;
    uint16_t hashString16(const char *value) const;
    uint16_t positionHash(const meshtastic_NodeInfoLite &node) const;
    uint8_t ageBucket(const meshtastic_NodeInfoLite &node) const;
    int8_t snrBucket(float snr) const;
    CachedNode *cachedNode(NodeNum nodeNum);
    bool appendRecord(uint8_t *payload, size_t &payloadSize, const meshtastic_NodeInfoLite &node, uint8_t flags,
                      bool includeNames);
    bool buildPayload(uint8_t *payload, size_t &payloadSize, bool fullSnapshot, bool &snapshotComplete);
    void markPayloadSent(const uint8_t *payload, size_t payloadSize);
    void startFullSnapshot();
    bool sendReport(bool fullSnapshot);
};

extern NodeListReportModule *nodeListReportModule;
#endif
