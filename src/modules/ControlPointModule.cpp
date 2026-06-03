#include "ControlPointModule.h"

#include <algorithm>

ControlPointModule *controlPointModule = nullptr;

ControlPointModule::ControlPointModule()
    : ProtobufModule("control_point", meshtastic_PortNum_PRIVATE_APP, &meshtastic_ControlPointMessage_msg)
{
}

const ControlPointPeerMetric *ControlPointModule::findMetric(NodeNum node_id) const
{
    for (const auto &metric : peerMetrics) {
        if (metric.valid && metric.node_id == node_id) {
            return &metric;
        }
    }
    return nullptr;
}

const ControlPointPeerMetric *ControlPointModule::findMetricByRelayId(uint8_t relay_id) const
{
    for (const auto &metric : peerMetrics) {
        if (metric.valid && metric.relay_id == relay_id) {
            return &metric;
        }
    }
    return nullptr;
}

std::optional<uint8_t> ControlPointModule::choosePreferredRelay(NodeNum to) const
{
    (void)to;

    const ControlPointPeerMetric *best = nullptr;

    for (const auto &metric : peerMetrics) {
        if (!metric.valid) {
            continue;
        }

        if (!isMetricUsable(metric) || !isMetricFresh(metric) || metric.relay_id == 0) {
            continue;
        }

        if (!best) {
            best = &metric;
            continue;
        }

        if (metric.route_cost < best->route_cost) {
            best = &metric;
            continue;
        }

        if (metric.route_cost == best->route_cost && metric.load < best->load) {
            best = &metric;
            continue;
        }

        if (metric.route_cost == best->route_cost &&
            metric.load == best->load &&
            metric.priority > best->priority) {
            best = &metric;
            continue;
        }

        if (metric.route_cost == best->route_cost &&
            metric.load == best->load &&
            metric.priority == best->priority &&
            metric.relay_id < best->relay_id) {
            best = &metric;
            continue;
        }
    }

    if (best) {
        return best->relay_id;
    }

    return std::nullopt;
}
bool ControlPointModule::isPreferredRelay(uint8_t relay_id) const
{
    const auto *metric = findMetricByRelayId(relay_id);
    return metric != nullptr && isMetricUsable(*metric);
}

ProcessMessage ControlPointModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp,
                                                          meshtastic_ControlPointMessage *msg)
{
    if (!msg) {
        return ProcessMessage::CONTINUE;
    }

    upsertMetric(*msg, mp.from);
    return ProcessMessage::CONTINUE;
}

bool ControlPointModule::wantPacket(const meshtastic_MeshPacket *p)
{
    if (!p) {
        return false;
    }

    return p->decoded.portnum == meshtastic_PortNum_PRIVATE_APP;
}

int32_t ControlPointModule::runOnce()
{
    pruneExpiredMetrics();
    sendControlPointBroadcast();
    return 30000;
}

void ControlPointModule::upsertMetric(const meshtastic_ControlPointMessage &msg, NodeNum from_node)
{
    auto *existing = const_cast<ControlPointPeerMetric *>(findMetric(from_node));

    if (!existing) {
        ControlPointPeerMetric metric;
        metric.node_id = from_node;
        metric.relay_id = static_cast<uint8_t>(from_node & 0xFF);
        metric.route_cost = msg.route_cost;
        metric.load = msg.current_load;
        metric.priority = msg.node_priority;
        metric.last_update_ms = millis();
        metric.valid = true;
        peerMetrics.push_back(metric);
        return;
    }

    existing->relay_id = static_cast<uint8_t>(from_node & 0xFF);
    existing->route_cost = msg.route_cost;
    existing->load = msg.current_load;
    existing->priority = msg.node_priority;
    existing->last_update_ms = millis();
    existing->valid = true;
}

bool ControlPointModule::isMetricFresh(const ControlPointPeerMetric &metric) const
{
    return metric.valid && (millis() - metric.last_update_ms) <= CONTROL_POINT_METRIC_TTL_MS;
}

bool ControlPointModule::isMetricUsable(const ControlPointPeerMetric &metric) const
{
    return metric.valid &&
           metric.route_cost >= CONTROL_POINT_MIN_ROUTE_COST &&
           metric.route_cost <= CONTROL_POINT_MAX_ROUTE_COST &&
           metric.load <= CONTROL_POINT_MAX_LOAD &&
           metric.priority >= CONTROL_POINT_MIN_PRIORITY;
}

void ControlPointModule::pruneExpiredMetrics()
{
    for (auto &metric : peerMetrics) {
        if (!isMetricFresh(metric)) {
            metric.valid = false;
        }
    }
}

void ControlPointModule::sendControlPointBroadcast()
{
    meshtastic_ControlPointMessage msg = {};
    msg.schema_version = 1;
    msg.is_control_point = true;
    msg.node_priority = 100;
    msg.route_cost = 1;
    msg.current_load = 0;
    msg.aggregation_enabled = true;

    meshtastic_MeshPacket *p = allocDataProtobuf(msg);
    if (!p) {
        LOG_WARN("ControlPoint: failed to allocate broadcast packet");
        return;
    }

    p->to = NODENUM_BROADCAST;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

    service->sendToMesh(p);
}