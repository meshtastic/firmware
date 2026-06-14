#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "mesh/ProtobufModule.h"
#include "mesh/generated/meshtastic/protobufs/meshtastic/control_point.pb.h"

struct ControlPointPeerMetric {
    NodeNum node_id = 0;
    uint8_t relay_id = 0;

    uint32_t route_cost = 0;
    uint32_t load = 0;
    uint32_t priority = 0;

    uint32_t last_update_ms = 0;
    bool valid = false;
};

static constexpr uint32_t CONTROL_POINT_METRIC_TTL_MS = 30000;
static constexpr uint32_t CONTROL_POINT_MIN_ROUTE_COST = 1;
static constexpr uint32_t CONTROL_POINT_MAX_ROUTE_COST = 1000000;
static constexpr uint32_t CONTROL_POINT_MAX_LOAD = 100;
static constexpr uint32_t CONTROL_POINT_MIN_PRIORITY = 1;

class ControlPointModule : public ProtobufModule<meshtastic_ControlPointMessage>, private concurrency::OSThread
{
public:
    ControlPointModule();

    const ControlPointPeerMetric *findMetric(NodeNum node_id) const;
    const ControlPointPeerMetric *findMetricByRelayId(uint8_t relay_id) const;
    bool isPreferredRelay(uint8_t relay_id) const;
    std::optional<uint8_t> choosePreferredRelay(NodeNum to) const;
    
protected:
    bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp,
                            meshtastic_ControlPointMessage *msg) override;

    bool wantPacket(const meshtastic_MeshPacket *p) override;
    int32_t runOnce() override;
private:
    std::vector<ControlPointPeerMetric> peerMetrics;

    void upsertMetric(const meshtastic_ControlPointMessage &msg, NodeNum from_node);
    bool isMetricFresh(const ControlPointPeerMetric &metric) const;
    bool isMetricUsable(const ControlPointPeerMetric &metric) const;
    void pruneExpiredMetrics();
    void sendControlPointBroadcast();
};

extern ControlPointModule *controlPointModule;