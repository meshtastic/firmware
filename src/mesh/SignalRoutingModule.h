#pragma once
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include <vector>

#include "graph/NeighborGraph.h"

// Routing protocol version for compatibility checking
#define SIGNAL_ROUTING_VERSION 1

// Maximum neighbors we track/broadcast (optimized to fit 14 in 233 byte payload)
#define MAX_SIGNAL_ROUTING_NEIGHBORS 14

// Broadcast interval for signal routing info (3 minutes)
#define SIGNAL_ROUTING_BROADCAST_SECS 180

class SignalRoutingModule : public ProtobufModule<meshtastic_SignalRoutingInfo>, private concurrency::OSThread
{
public:
    SignalRoutingModule();

    // Delete copy constructor and assignment operator since this class manages dynamic memory and threading
    SignalRoutingModule(const SignalRoutingModule&) = delete;
    SignalRoutingModule& operator=(const SignalRoutingModule&) = delete;

    bool shouldUseSignalBasedRouting(const meshtastic_MeshPacket *p);
    void updateNodeActivityForPacket(NodeNum nodeId);
    void updateNodeActivityForPacketAndRelay(const meshtastic_MeshPacket *p);
    bool shouldRelay(const meshtastic_MeshPacket *p);
    bool shouldRelayBroadcast(const meshtastic_MeshPacket *p);
    NodeNum getNextHop(NodeNum destination, NodeNum sourceNode = 0, NodeNum heardFrom = 0, bool allowOpportunistic = true);
    NodeNum findBetterPositionedNeighbor(NodeNum destination, NodeNum sourceNode, NodeNum heardFrom,
                                       float ourRouteCost, uint32_t currentTime);
    bool shouldRelayUnicastForCoordination(const meshtastic_MeshPacket *p);
    bool hasDirectConnectivity(NodeNum nodeA, NodeNum nodeB);
    bool hasVerifiedConnectivity(NodeNum transmitter, NodeNum receiver, bool* unknownOut = nullptr);
    void updateNeighborInfo(NodeNum nodeId, int32_t rssi, float snr, uint32_t lastRxTime, uint32_t variance = 0);
    void sendSignalRoutingInfo(NodeNum dest = NODENUM_BROADCAST);
    void preProcessSignalRoutingPacket(const meshtastic_MeshPacket *p, uint32_t packetReceivedTimestamp = 0);

protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_SignalRoutingInfo *p) override;
    void updateGraphWithNeighbor(NodeNum sender, const meshtastic_SignalNeighbor &neighbor);
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }
    virtual int32_t runOnce() override;

private:
    NeighborGraph *routingGraph = nullptr;
    uint32_t lastGraphUpdate = 0;
    static constexpr uint32_t GRAPH_UPDATE_INTERVAL_SECS = 300;
    static constexpr uint32_t EARLY_BROADCAST_DELAY_MS = 15 * 1000;
    static constexpr uint32_t ACTIVE_NODE_TTL_SECS = 2700;   // 45 min for SR nodes
    static constexpr uint32_t MUTE_NODE_TTL_SECS = 5400;    // 90 min for legacy/stock nodes
    static constexpr uint32_t CAPABILITY_TTL_SECS = 2700;   // 45 min
    static constexpr uint32_t RELAY_ID_CACHE_TTL_MS = 600 * 1000;  // 10 min

    bool signalBasedRoutingEnabled = true;
    bool topologyDirty = false; // Set when topology changes; log dump deferred to runOnce
    uint32_t lastBroadcast = 0;
    uint8_t currentTopologyVersion = 0;
    std::unordered_map<NodeNum, uint8_t> lastTopologyVersion;
    std::unordered_map<NodeNum, uint8_t> lastPreProcessedVersion;

    bool hasNodeBeenExcludedFromRelay(NodeNum nodeId, PacketId packetId);
    void processContentionWindows(uint32_t nowMs);
    void scheduleContentionWindowCheck(NodeNum expectedRelay, PacketId packetId, NodeNum destination, const meshtastic_MeshPacket *packet);
    void excludeNodeFromRelay(NodeNum nodeId, PacketId packetId);
    void clearRelayExclusionsForPacket(PacketId packetId);
    bool isSignalBasedCapable(NodeNum nodeId) const;
    float getDirectNeighborsSignalActivePercentage() const;
    void collectNeighborsForBroadcast(std::vector<meshtastic_SignalNeighbor> &allNeighbors);
    void sendTopologyPacket(NodeNum dest, const std::vector<meshtastic_SignalNeighbor> &neighbors, uint8_t topologyVersion = 0);
    void buildSignalRoutingInfo(meshtastic_SignalRoutingInfo &info);
    void setRgbLed(uint8_t r, uint8_t g, uint8_t b);
    void turnOffRgbLed();

    uint32_t lastNotificationTime = 0;
    uint32_t heartbeatEndTime = 0;

    enum class CapabilityStatus : uint8_t {
        Unknown = 0,
        SRactive,
        Passive,
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

    // Fixed-size arrays (unified, no #ifdefs)
    static constexpr size_t MAX_CAPABILITY_RECORDS = 24;
    static constexpr size_t MAX_RELAY_IDENTITY_ENTRIES = 16;

    struct CapabilityRecordEntry {
        NodeNum nodeId = 0;
        CapabilityRecord record;
    };
    CapabilityRecordEntry capabilityRecords[MAX_CAPABILITY_RECORDS];
    uint8_t capabilityRecordCount = 0;

    struct RelayIdentityCacheEntry {
        uint8_t relayId = 0;
        RelayIdentityEntry entries[4];
        uint8_t entryCount = 0;
    };
    RelayIdentityCacheEntry relayIdentityCache[MAX_RELAY_IDENTITY_ENTRIES];
    uint8_t relayIdentityCacheCount = 0;

    struct RelayExclusion {
        uint64_t packetKey;
        NodeNum excludedNodes[4];
        uint8_t exclusionCount;
    };
    RelayExclusion relayExclusions[4];
    uint8_t relayExclusionCount = 0;

    struct ContentionCheck {
        NodeNum expectedRelay;
        PacketId packetId;
        NodeNum destination;
        NodeNum sourceNode;
        NodeNum heardFrom;
        uint8_t hopLimit;
        uint8_t hopStart;
        uint32_t expiryMs;
        bool needsRelay;
    };
    ContentionCheck contentionChecks[4];
    uint8_t contentionCheckCount = 0;

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
    static uint64_t makeSpeculativeKey(NodeNum origin, uint32_t packetId);
    uint32_t getNodeLastActivityTime(NodeNum nodeId) const;
    bool isActiveRoutingRole() const;
    bool canSendTopology() const;
    void handleNodeInfoPacket(const meshtastic_MeshPacket &mp);
    CapabilityStatus capabilityFromRole(meshtastic_Config_DeviceConfig_Role role) const;
    void handleSniffedPayload(const meshtastic_MeshPacket &mp, bool isDirectNeighbor);
    void handlePositionPacket(const meshtastic_MeshPacket &mp, bool isDirectNeighbor);
    void handleTelemetryPacket(const meshtastic_MeshPacket &mp);
    void handleRoutingControlPacket(const meshtastic_MeshPacket &mp);

    bool isPlaceholderNode(NodeNum nodeId) const;
    NodeNum createPlaceholderNode(uint8_t relayId);
    bool resolvePlaceholder(NodeNum placeholderId, NodeNum realNodeId);
    NodeNum getPlaceholderForRelay(uint8_t relayId) const;
    void replaceGatewayNode(NodeNum oldNode, NodeNum newNode);
    bool isPlaceholderConnectedToUs(NodeNum placeholderId) const;
    bool shouldRelayForStockNeighbors(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime);
    bool isDownstreamOfHeardRelay(NodeNum destination, NodeNum myNode);
    uint32_t getNodeTtlSeconds(CapabilityStatus status) const;
    bool isNodeRoutable(NodeNum nodeId) const;
    void logNetworkTopology();
    bool evaluateContentionExpiry(const ContentionCheck& check, NodeNum myNode);
    void queueUnicastRelay(const ContentionCheck& check);
};

extern SignalRoutingModule *signalRoutingModule;
