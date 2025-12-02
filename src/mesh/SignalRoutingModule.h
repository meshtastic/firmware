#pragma once
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"

class Graph;

// Routing protocol version for compatibility checking
#define SIGNAL_ROUTING_VERSION 1

// Maximum neighbors we track/broadcast
#define MAX_SIGNAL_ROUTING_NEIGHBORS 10

// Broadcast interval for signal routing info (2 minutes)
#define SIGNAL_ROUTING_BROADCAST_SECS 120

// Speculative retransmit timeout (600ms)
#define SPECULATIVE_RETRANSMIT_TIMEOUT_MS 600

class SignalRoutingModule : public ProtobufModule<meshtastic_SignalRoutingInfo>, private concurrency::OSThread
{
public:
    SignalRoutingModule();

    /**
     * Check if signal-based routing should be used for a packet
     * Works for both unicast (route to destination) and broadcast (coordinated flooding)
     */
    bool shouldUseSignalBasedRouting(const meshtastic_MeshPacket *p);

    /**
     * Check if we should relay a broadcast packet
     * Uses graph-based coverage calculation to coordinate with other nodes
     */
    bool shouldRelayBroadcast(const meshtastic_MeshPacket *p);

    /**
     * Get the next hop for signal-based routing (unicast only)
     */
    NodeNum getNextHop(NodeNum destination);

    /**
     * Update neighbor information from a directly received packet
     */
    void updateNeighborInfo(NodeNum nodeId, int32_t rssi, float snr, uint32_t lastRxTime, uint32_t variance = 0);

    /**
     * Handle speculative retransmit for unicast packets
     */
    void handleSpeculativeRetransmit(const meshtastic_MeshPacket *p);

    /**
     * Send our signal routing info to the mesh
     */
    void sendSignalRoutingInfo(NodeNum dest = NODENUM_BROADCAST);

    /**
     * Get the routing graph (for external access)
     */
    Graph* getGraph() { return routingGraph; }

    /**
     * Pre-process a SignalRoutingInfo packet to update graph BEFORE relay decision
     * This ensures we have up-to-date neighbor data when deciding whether to relay
     */
    void preProcessSignalRoutingPacket(const meshtastic_MeshPacket *p);

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

    /**
     * Flash RGB LED for Signal Routing notifications
     */
    void flashRgbLed(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms = 200);

    /**
     * Turn off RGB LED when timer expires
     */
    void updateRgbLed();

    // RGB LED timing
    bool rgbLedActive = false;
    uint32_t rgbLedOffTime = 0;
};

extern SignalRoutingModule *signalRoutingModule;
