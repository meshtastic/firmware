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
        uint32_t signature = 0;
    };

    static constexpr uint32_t defaultIntervalSeconds = 60 * 60;
    static constexpr uint32_t minIntervalSeconds = 15 * 60;
    static constexpr uint32_t defaultFullSnapshotIntervalSeconds = 24 * 60 * 60;
    static constexpr uint32_t minFullSnapshotIntervalSeconds = 6 * 60 * 60;
    static constexpr uint8_t defaultMaxNodesPerReport = 10;
    static constexpr uint8_t hardMaxNodesPerReport = 14;
    static constexpr uint8_t headerSize = 10;
    static constexpr uint8_t recordSize = 12;

    uint32_t lastIncrementalMs = 0;
    uint32_t lastFullSnapshotMs = 0;
    uint16_t sequence = 0;
    std::vector<CachedNode> cache;

    uint32_t intervalMs() const;
    uint32_t fullSnapshotIntervalMs() const;
    uint8_t maxNodesPerReport() const;
    uint8_t minChangedNodesBeforeSend() const;
    bool isConfigured() const;
    bool shouldSendFullSnapshot() const;
    uint32_t computeNodeSignature(const meshtastic_NodeInfoLite &node) const;
    uint16_t hashString16(const char *value) const;
    uint16_t positionHash(const meshtastic_NodeInfoLite &node) const;
    uint8_t ageBucket(const meshtastic_NodeInfoLite &node) const;
    int8_t snrBucket(float snr) const;
    uint32_t *cachedSignature(NodeNum nodeNum);
    bool buildPayload(uint8_t *payload, size_t &payloadSize, bool fullSnapshot);
    void markPayloadSent(const uint8_t *payload, size_t payloadSize);
    bool sendReport(bool fullSnapshot);
};

extern NodeListReportModule *nodeListReportModule;
#endif
