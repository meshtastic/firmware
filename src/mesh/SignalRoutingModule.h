#pragma once
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"

class Graph;

// Routing protocol version for compatibility checking
#define SIGNAL_ROUTING_VERSION 1

// Maximum neighbors we track/broadcast
#define MAX_SIGNAL_ROUTING_NEIGHBORS 10

// Broadcast interval for signal routing info (5 minutes)
#define SIGNAL_ROUTING_BROADCAST_SECS 300

class SignalRoutingModule : public ProtobufModule<meshtastic_SignalRoutingInfo>, private concurrency::OSThread
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
     * Update neighbor information from a directly received packet
     */
    void updateNeighborInfo(NodeNum nodeId, int32_t rssi, int32_t snr, uint32_t lastRxTime, uint32_t variance = 0);

    /**
     * Handle speculative retransmit for unicast packets
     */
    void handleSpeculativeRetransmit(const meshtastic_MeshPacket *p);

    /**
     * Send our signal routing info to the mesh
     */
    void sendSignalRoutingInfo(NodeNum dest = NODENUM_BROADCAST);

protected:
    /** Called to handle received SignalRoutingInfo protobuf */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_SignalRoutingInfo *p) override;

    /** Called to handle any received packet (for updating neighbor info from RSSI/SNR) */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    /** We want to see all packets to update neighbor info from signal quality */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }

    /** Periodic broadcast of our routing info */
    virtual int32_t runOnce() override;

private:
    Graph *routingGraph;
    uint32_t lastGraphUpdate = 0;
    static constexpr uint32_t GRAPH_UPDATE_INTERVAL_MS = 300 * 1000; // 300 seconds

    // Signal-based routing enabled by default
    bool signalBasedRoutingEnabled = true;

    // Last time we sent signal routing info
    uint32_t lastBroadcast = 0;

    /**
     * Check if a node is signal-based routing capable
     */
    bool isSignalBasedCapable(NodeNum nodeId);

    /**
     * Calculate percentage of signal-based capable nodes
     */
    float getSignalBasedCapablePercentage();

    /**
     * Build a SignalRoutingInfo packet with our current neighbor data
     */
    void buildSignalRoutingInfo(meshtastic_SignalRoutingInfo &info);
};

extern SignalRoutingModule *signalRoutingModule;
