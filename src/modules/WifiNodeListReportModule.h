#pragma once

#if HAS_WIFI && !MESHTASTIC_EXCLUDE_NODELISTREPORT
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"
#include <Arduino.h>
#include <vector>

class WifiNodeListReportModule : private concurrency::OSThread
{
  public:
    WifiNodeListReportModule();

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
    static constexpr uint32_t defaultConnectTimeoutSeconds = 30;

    uint32_t lastIncrementalMs = 0;
    uint32_t lastFullSnapshotMs = 0;
    uint16_t sequence = 0;
    std::vector<CachedNode> cache;

    uint32_t intervalMs() const;
    uint32_t fullSnapshotIntervalMs() const;
    uint32_t connectTimeoutMs() const;
    uint8_t minChangedNodesBeforeSend() const;
    bool isConfigured() const;
    bool powerGateAllowsWifi() const;
    bool shouldSendFullSnapshot() const;
    bool ensureWifiConnected(bool &startedWifi);
    void restoreWifi(bool startedWifi);
    uint8_t ageBucket(const meshtastic_NodeInfoLite &node) const;
    int8_t snrBucket(float snr) const;
    uint16_t positionHash(const meshtastic_NodeInfoLite &node) const;
    uint32_t computeMetricSignature(const meshtastic_NodeInfoLite &node) const;
    uint32_t computeNameSignature(const meshtastic_NodeInfoLite &node) const;
    CachedNode *cachedNode(NodeNum nodeNum);
    void appendEscaped(String &out, const char *value) const;
    void appendRecordJson(String &out, const meshtastic_NodeInfoLite &node, uint8_t flags, bool includeNames) const;
    bool buildJson(String &json, bool fullSnapshot, uint8_t &recordCount);
    void markJsonSent(bool fullSnapshot);
    bool postReport(bool fullSnapshot);
};

extern WifiNodeListReportModule *wifiNodeListReportModule;
#endif
