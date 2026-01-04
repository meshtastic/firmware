#pragma once
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"

// SIGNAL_ROUTING_LITE_MODE:
// = 1: Use GraphLite (lite mode)
// = 0 or undefined: Use Graph (full mode)

// Include graph headers for type definitions
#include "graph/Graph.h"
#include "graph/GraphLite.h"

// Routing protocol version for compatibility checking
#define SIGNAL_ROUTING_VERSION 1

// Maximum neighbors we track/broadcast (optimized to fit 14 in 233 byte payload)
#define MAX_SIGNAL_ROUTING_NEIGHBORS 14

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
    NodeNum getNextHop(NodeNum destination, NodeNum sourceNode = 0, NodeNum heardFrom = 0, bool allowOpportunistic = true);

    /**
     * Find a better positioned neighbor for unicast forwarding
     */
    NodeNum findBetterPositionedNeighbor(NodeNum destination, NodeNum sourceNode, NodeNum heardFrom,
                                       float ourRouteCost, uint32_t currentTime);

    /**
     * Decide whether to relay a unicast packet that was broadcast for coordination
     */
    bool shouldRelayUnicastForCoordination(const meshtastic_MeshPacket *p);

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
     * Check if using lite mode
     */
    bool isLiteMode() const {
        #ifdef SIGNAL_ROUTING_LITE_MODE
            return SIGNAL_ROUTING_LITE_MODE != 0;
        #else
            return false;
        #endif
    }


    /**
     * Pre-process a SignalRoutingInfo packet to update graph BEFORE relay decision
     * This ensures we have up-to-date neighbor data when deciding whether to relay
     */
    void preProcessSignalRoutingPacket(const meshtastic_MeshPacket *p, uint32_t packetReceivedTimestamp = 0);

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
    // Graph type depends on SIGNAL_ROUTING_LITE_MODE flag
    #ifdef SIGNAL_ROUTING_LITE_MODE
        GraphLite *routingGraph = nullptr;
    #else
        Graph *routingGraph = nullptr;
    #endif
    uint32_t lastGraphUpdate = 0;
    static constexpr uint32_t GRAPH_UPDATE_INTERVAL_SECS = 300; // 300 seconds
    static constexpr uint32_t EARLY_BROADCAST_DELAY_MS = 15 * 1000; // 15 seconds
    static constexpr uint32_t ACTIVE_NODE_TTL_SECS = 240;    // 4 minutes for active nodes
    static constexpr uint32_t MUTE_NODE_TTL_SECS = 960;     // 16 minutes for mute/inactive nodes
    static constexpr uint32_t CAPABILITY_TTL_SECS = 300;     // Fallback for legacy compatibility
    static constexpr uint32_t RELAY_ID_CACHE_TTL_MS = 120 * 1000;

    // Signal-based routing enabled by default
    bool signalBasedRoutingEnabled = true;

    // Last time we sent signal routing info
    uint32_t lastBroadcast = 0;

    /**
     * Check if a node is signal-based routing capable
     */
    bool isSignalBasedCapable(NodeNum nodeId) const;

    /**
     * Calculate percentage of signal-based capable nodes
     */
    float getSignalBasedCapablePercentage() const;

    /**
     * Build a SignalRoutingInfo packet with our current neighbor data
     */
    void buildSignalRoutingInfo(meshtastic_SignalRoutingInfo &info);

    /**
     * Set RGB LED color for Signal Routing notifications
     *
     * Color meanings:
     *   White (dim)            - Heartbeat (idle indicator)
     *   Purple (128,0,128)     - Direct packet received from neighbor
     *   Orange (255,128,0)     - Relay decision: YES (relaying broadcast)
     *   Red (255,0,0)          - Relay decision: NO (suppressing relay)
     *   Green (0,255,0)        - New neighbor detected
     *   Blue (0,0,255)         - Significant signal quality change
     *   Cyan (0,255,255)       - Topology update received (SignalRoutingInfo)
     *   Off (0,0,0)            - Idle (no active operation)
     */
    void setRgbLed(uint8_t r, uint8_t g, uint8_t b);

    /**
     * Turn off RGB LED
     */
    void turnOffRgbLed();

    // LED operation feedback timing
    uint32_t lastNotificationTime = 0;
    uint32_t heartbeatEndTime = 0;                           // When to turn off operation feedback LED

    enum class CapabilityStatus : uint8_t {
        Unknown = 0,
        Capable,
        Legacy
    };

    struct CapabilityRecord {
        CapabilityStatus status = CapabilityStatus::Unknown;
        uint32_t lastUpdated = 0;
    };

    struct RelayIdentityEntry {
        NodeNum nodeId = 0;
        uint32_t lastHeardMs = 0;
    };

    struct SpeculativeRetransmitEntry {
        uint64_t key = 0;
        NodeNum origin = 0;
        uint32_t packetId = 0;
        uint32_t expiryMs = 0;
        meshtastic_MeshPacket *packetCopy = nullptr;
    };

    // Data structures depend on SIGNAL_ROUTING_LITE_MODE
    #ifdef SIGNAL_ROUTING_LITE_MODE
        // Lite mode structures: fixed-size arrays
        static constexpr size_t MAX_CAPABILITY_RECORDS = 24;
        static constexpr size_t MAX_RELAY_IDENTITY_ENTRIES = 16;
        static constexpr size_t MAX_SPECULATIVE_RETRANSMITS = 4;
        static constexpr size_t MAX_GATEWAY_RELATIONS = 24;
        static constexpr size_t MAX_GATEWAY_DOWNSTREAM = 8;

        struct CapabilityRecordEntry {
            NodeNum nodeId = 0;
            CapabilityRecord record;
        };
        CapabilityRecordEntry capabilityRecords[MAX_CAPABILITY_RECORDS];
        uint8_t capabilityRecordCount = 0;

        struct RelayIdentityCacheEntry {
            uint8_t relayId = 0;
            RelayIdentityEntry entries[4]; // Max 4 nodes per relay ID
            uint8_t entryCount = 0;
        };
        RelayIdentityCacheEntry relayIdentityCache[MAX_RELAY_IDENTITY_ENTRIES];
        uint8_t relayIdentityCacheCount = 0;

        SpeculativeRetransmitEntry speculativeRetransmits[MAX_SPECULATIVE_RETRANSMITS];
        uint8_t speculativeRetransmitCount = 0;

        struct GatewayRelation {
            NodeNum gateway = 0;
            NodeNum downstream = 0;
            uint32_t lastSeen = 0;
        };
        GatewayRelation gatewayRelations[MAX_GATEWAY_RELATIONS];
        uint8_t gatewayRelationCount = 0;

        struct GatewayDownstreamSet {
            NodeNum gateway = 0;
            NodeNum downstream[MAX_GATEWAY_DOWNSTREAM];
            uint8_t count = 0;
            uint32_t lastSeen = 0;
        };
        GatewayDownstreamSet gatewayDownstream[MAX_GATEWAY_RELATIONS];
        uint8_t gatewayDownstreamCount = 0;
    #else
        // Full mode structures: dynamic hash maps
        std::unordered_map<NodeNum, CapabilityRecord> capabilityRecords;
        std::unordered_map<uint8_t, std::vector<RelayIdentityEntry>> relayIdentityCache;
        std::unordered_map<uint64_t, SpeculativeRetransmitEntry> speculativeRetransmits;
        struct GatewayRelationEntry {
            NodeNum gateway;
            uint32_t lastSeen;
        };
        std::unordered_map<NodeNum, GatewayRelationEntry> downstreamGateway; // downstream -> gateway entry
        std::unordered_map<NodeNum, std::unordered_set<NodeNum>> gatewayDownstream; // gateway -> downstream set
    #endif

    void trackNodeCapability(NodeNum nodeId, CapabilityStatus status);
    void pruneCapabilityCache(uint32_t nowSecs);
    void pruneGatewayRelations(uint32_t nowSecs);
    CapabilityStatus getCapabilityStatus(NodeNum nodeId) const;
    bool topologyHealthyForBroadcast() const;
    bool topologyHealthyForUnicast(NodeNum destination) const;
    bool isLegacyRouter(NodeNum nodeId) const;
    void rememberRelayIdentity(NodeNum nodeId, uint8_t relayId);
    void pruneRelayIdentityCache(uint32_t nowMs);
    NodeNum resolveRelayIdentity(uint8_t relayId) const;
    NodeNum resolveHeardFrom(const meshtastic_MeshPacket *p, NodeNum sourceNode) const;
    void processSpeculativeRetransmits(uint32_t nowMs);
    void cancelSpeculativeRetransmit(NodeNum origin, uint32_t packetId);
    static uint64_t makeSpeculativeKey(NodeNum origin, uint32_t packetId);
    void recordGatewayRelation(NodeNum gateway, NodeNum downstream);
    void removeGatewayRelationship(NodeNum gateway, NodeNum downstream);
    void clearGatewayRelationsFor(NodeNum node);
    void clearDownstreamFromAllGateways(NodeNum downstream);
    NodeNum getGatewayFor(NodeNum downstream) const;
    size_t getGatewayDownstreamCount(NodeNum gateway) const;

    /**
     * Get the last activity time for a node from capability records
     * @return last activity timestamp, or 0 if unknown/no recent activity
     */
    uint32_t getNodeLastActivityTime(NodeNum nodeId) const;

    bool isActiveRoutingRole() const;
    void handleNodeInfoPacket(const meshtastic_MeshPacket &mp);
    CapabilityStatus capabilityFromRole(meshtastic_Config_DeviceConfig_Role role) const;
    void handleSniffedPayload(const meshtastic_MeshPacket &mp, bool isDirectNeighbor);
    void handlePositionPacket(const meshtastic_MeshPacket &mp, bool isDirectNeighbor);
    void handleTelemetryPacket(const meshtastic_MeshPacket &mp);
    void handleRoutingControlPacket(const meshtastic_MeshPacket &mp);

    /**
     * Placeholder node system for unknown relays
     */
    bool isPlaceholderNode(NodeNum nodeId) const;
    NodeNum createPlaceholderNode(uint8_t relayId);
    bool resolvePlaceholder(NodeNum placeholderId, NodeNum realNodeId);
    NodeNum getPlaceholderForRelay(uint8_t relayId) const;
    void replaceGatewayNode(NodeNum oldNode, NodeNum newNode);
    bool isPlaceholderConnectedToUs(NodeNum placeholderId) const;

    /**
     * Stock neighbor coverage
     */
    bool shouldRelayForStockNeighbors(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime);

    /**
     * Check if destination is downstream of relays we can hear directly
     */
    bool isDownstreamOfHeardRelay(NodeNum destination, NodeNum myNode);

    /**
     * Get TTL for node based on its capability status
     * Active nodes: 4 minutes, Mute/Legacy nodes: 16 minutes
     */
    uint32_t getNodeTtlSeconds(CapabilityStatus status) const;

    /**
     * Check if a node is routable (can be used as intermediate hop for routing)
     * Mute nodes are not routable since they don't relay packets
     */
    bool isNodeRoutable(NodeNum nodeId) const;

    /**
     * Log the current network topology graph in a readable format
     */
    void logNetworkTopology();
};

extern SignalRoutingModule *signalRoutingModule;
