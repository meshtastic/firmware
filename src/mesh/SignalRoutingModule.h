#pragma once
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    static constexpr uint32_t HEARTBEAT_FLASH_MS = 60;
    static constexpr uint32_t CAPABILITY_TTL_SECS = 600;
    static constexpr float MIN_CAPABLE_RATIO = 0.4f;
    static constexpr size_t MIN_CAPABLE_NODES = 3;
    static constexpr uint32_t RELAY_ID_CACHE_TTL_MS = 120 * 1000;

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
    float getSignalBasedCapablePercentage() const;

    /**
     * Build a SignalRoutingInfo packet with our current neighbor data
     */
    void buildSignalRoutingInfo(meshtastic_SignalRoutingInfo &info);

    /**
     * Flash RGB LED for Signal Routing notifications
     * 
     * Color meanings:
     *   White (dim pulse)      - Heartbeat (idle indicator)
     *   Purple (128,0,128)     - Direct packet received from neighbor
     *   Orange (255,128,0)     - Relay decision: YES (relaying broadcast)
     *   Red (255,0,0)          - Relay decision: NO (suppressing relay)
     *   Green (0,255,0)        - New neighbor detected
     *   Blue (0,0,255)         - Significant signal quality change
     *   Cyan (0,255,255)       - Topology update received (SignalRoutingInfo)
     */
    void flashRgbLed(uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms = 200, bool isNotification = false);

    /**
     * Turn off RGB LED when timer expires
     */
    void updateRgbLed();

    // RGB LED timing
    bool rgbLedActive = false;
    uint32_t rgbLedOffTime = 0;
    uint32_t lastFlashTime = 0;
    static constexpr uint32_t MIN_FLASH_INTERVAL_MS = 500;   // Minimum time between flashes
    static constexpr uint32_t MIN_EVENT_FLASH_INTERVAL_MS = 4000;

    // Heartbeat timing
    uint32_t lastHeartbeatTime = 0;
    uint32_t lastNotificationTime = 0;
    uint32_t heartbeatIntervalMs = 2000;                     // Configurable, default 2 seconds
    uint32_t lastEventFlashTime = 0;

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

    std::unordered_map<NodeNum, CapabilityRecord> capabilityRecords;
    std::unordered_map<uint8_t, std::vector<RelayIdentityEntry>> relayIdentityCache;
    std::unordered_map<uint64_t, SpeculativeRetransmitEntry> speculativeRetransmits;

    void trackNodeCapability(NodeNum nodeId, CapabilityStatus status);
    void pruneCapabilityCache(uint32_t nowSecs);
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
    bool isActiveRoutingRole() const;
    void handleNodeInfoPacket(const meshtastic_MeshPacket &mp);
    CapabilityStatus capabilityFromRole(meshtastic_Config_DeviceConfig_Role role) const;
    void handleSniffedPayload(const meshtastic_MeshPacket &mp, bool isDirectNeighbor);
    void handlePositionPacket(const meshtastic_MeshPacket &mp, bool isDirectNeighbor);
    void handleTelemetryPacket(const meshtastic_MeshPacket &mp);
    void handleRoutingControlPacket(const meshtastic_MeshPacket &mp);
};

extern SignalRoutingModule *signalRoutingModule;
