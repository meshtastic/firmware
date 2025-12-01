#pragma once
#include "MeshModule.h"
#include <vector>

class Graph;

class SignalRoutingModule : public MeshModule
{
public:
    SignalRoutingModule();

    /**
     * Check if signal-based routing should be used for a packet
     */
    bool shouldUseSignalBasedRouting(const meshtastic_MeshPacket *p);

    /**
     * Get the next hop for signal-based routing
     */
    NodeNum getNextHop(NodeNum destination);

    /**
     * Update neighbor information
     */
    void updateNeighborInfo(NodeNum nodeId, int32_t rssi, int32_t snr, uint32_t lastRxTime);

    /**
     * Handle speculative retransmit for unicast packets
     */
    void handleSpeculativeRetransmit(const meshtastic_MeshPacket *p);

protected:
    /** Called to handle a particular incoming message */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    /** We want to see all packets to update neighbor info */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }

private:
    Graph *routingGraph;
    uint32_t lastGraphUpdate = 0;
    static constexpr uint32_t GRAPH_UPDATE_INTERVAL_MS = 300 * 1000; // 300 seconds

    // Signal-based routing enabled by default (until protobuf config is available)
    bool signalBasedRoutingEnabled = true;

    /**
     * Check if a node is signal-based routing capable
     */
    bool isSignalBasedCapable(NodeNum nodeId);

    /**
     * Calculate percentage of signal-based capable nodes
     */
    float getSignalBasedCapablePercentage();

    /**
     * Update our own signal_based_capable flag in NodeInfo
     */
    void updateSignalBasedCapable();
};

extern SignalRoutingModule *signalRoutingModule;
