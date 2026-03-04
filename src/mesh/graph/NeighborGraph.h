#pragma once
/**
 * NeighborGraph - Unified graph implementation replacing Graph + GraphLite
 *
 * Each node stores only:
 * 1. Direct neighbors (with full metrics) in neighbors[] slots
 * 2. Downstream routing table (which neighbor reaches which remote node)
 *
 * Memory: ~4.2 KB (down from 18.5 KB for GraphLite)
 * Single class, no #ifdefs, works on all platforms including ESP32-C3.
 */

#include "NodeDB.h"
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_set>

// Compile-time configuration
#ifndef NEIGHBOR_GRAPH_MAX_NEIGHBORS
#define NEIGHBOR_GRAPH_MAX_NEIGHBORS 16
#endif

#ifndef NEIGHBOR_GRAPH_MAX_EDGES_PER_NODE
#define NEIGHBOR_GRAPH_MAX_EDGES_PER_NODE 12
#endif

#ifndef NEIGHBOR_GRAPH_MAX_DOWNSTREAM
#define NEIGHBOR_GRAPH_MAX_DOWNSTREAM 100
#endif

#ifndef NEIGHBOR_GRAPH_MAX_RELAY_STATES
#define NEIGHBOR_GRAPH_MAX_RELAY_STATES 16
#endif

#ifndef NEIGHBOR_GRAPH_MAX_CACHED_ROUTES
#define NEIGHBOR_GRAPH_MAX_CACHED_ROUTES 16
#endif

// Return values for updateEdge()
static constexpr int EDGE_NO_CHANGE = 0;
static constexpr int EDGE_NEW = 1;
static constexpr int EDGE_SIGNIFICANT_CHANGE = 2;

// Threshold for significant ETX change
static constexpr float ETX_CHANGE_THRESHOLD = 0.50f;

struct EdgeLite {
    enum class Source : uint8_t { Mirrored = 0, Reported = 1 };

    NodeNum to;
    uint16_t etxFixed;   // ETX * 100 (fixed-point, range 1.00-655.35)
    uint32_t lastUpdate; // Full timestamp (seconds since boot)
    uint8_t variance;    // Position variance (0-255, scaled)
    Source source;

    EdgeLite() : to(0), etxFixed(100), lastUpdate(0), variance(0), source(Source::Mirrored) {}

    float getEtx() const { return etxFixed / 100.0f; }
    void setEtx(float etx) { etxFixed = static_cast<uint16_t>(etx * 100.0f); }
};

struct NodeEdgesLite {
    NodeNum nodeId;
    EdgeLite edges[NEIGHBOR_GRAPH_MAX_EDGES_PER_NODE];
    uint8_t edgeCount;
    uint32_t lastFullUpdate; // Full timestamp for aging

    NodeEdgesLite() : nodeId(0), edgeCount(0), lastFullUpdate(0) {}
};

struct RouteLite {
    NodeNum destination;
    NodeNum nextHop;
    uint16_t costFixed; // Cost * 100 (fixed-point)
    uint32_t timestamp;

    RouteLite() : destination(0), nextHop(0), costFixed(0), timestamp(0) {}

    float getCost() const { return costFixed / 100.0f; }
};

struct RelayCandidateLite {
    NodeNum nodeId;
    uint8_t coverageCount;
    uint16_t avgCostFixed;
    uint8_t tier;

    RelayCandidateLite() : nodeId(0), coverageCount(0), avgCostFixed(0), tier(0) {}
    RelayCandidateLite(NodeNum node, uint8_t coverage, uint16_t cost, uint8_t t)
        : nodeId(node), coverageCount(coverage), avgCostFixed(cost), tier(t) {}

    float getAvgCost() const { return avgCostFixed / 100.0f; }

    bool operator<(const RelayCandidateLite &other) const
    {
        if (tier != other.tier)
            return tier < other.tier;
        if (coverageCount != other.coverageCount)
            return coverageCount > other.coverageCount;
        return avgCostFixed < other.avgCostFixed;
    }
};

struct RelayStateLite {
    NodeNum nodeId;
    uint32_t packetId;
    uint16_t timestampLo; // Lower 16 bits

    RelayStateLite() : nodeId(0), packetId(0), timestampLo(0) {}
};

struct DownstreamEntry {
    NodeNum destination; // Remote node
    NodeNum relay;       // Which direct neighbor reaches it
    uint16_t costFixed;  // Cumulative ETX * 100
    uint32_t lastUpdate; // For aging

    DownstreamEntry() : destination(0), relay(0), costFixed(0), lastUpdate(0) {}
};

class NeighborGraph {
  public:
    static uint32_t getContentionWindowMs();
    static constexpr uint32_t EDGE_AGING_TIMEOUT_SECS = 600;

    NeighborGraph();

    // --- Core methods (same signatures as GraphLite) ---

    int updateEdge(NodeNum from, NodeNum to, float etx, uint32_t timestamp, uint32_t variance = 0,
                   EdgeLite::Source source = EdgeLite::Source::Mirrored, bool updateTimestamp = true);

    const NodeEdgesLite *getEdgesFrom(NodeNum node) const;

    void updateNodeActivity(NodeNum nodeId, uint32_t timestamp);

    void ageEdges(uint32_t currentTimeSecs, std::function<uint32_t(NodeNum)> getTtlForNode = nullptr);

    RouteLite calculateRoute(NodeNum destination, uint32_t currentTime, std::function<bool(NodeNum)> nodeFilter = nullptr);

    RouteLite getCachedRoute(NodeNum destination, uint32_t currentTime);

    void clearCache();

    static float calculateETX(int32_t rssi, float snr);

    static void etxToSignal(float etx, int32_t &rssi, int32_t &snr);

    // --- Downstream methods (new) ---

    void updateDownstream(NodeNum destination, NodeNum relay, float totalCost, uint32_t timestamp);

    NodeNum getDownstreamRelay(NodeNum destination) const;

    bool isDownstream(NodeNum destination) const;

    size_t getDownstreamCountForRelay(NodeNum relay) const;

    size_t getDownstreamNodesForRelay(NodeNum relay, NodeNum *outArray, uint16_t *outCosts, size_t maxCount) const;

    bool isRelayFor(NodeNum myNode, NodeNum destination) const;

    void clearDownstreamForRelay(NodeNum relay);

    void clearDownstreamForDestination(NodeNum destination);

    // --- Relay decisions (ported from GraphLite) ---

    bool shouldRelayEnhanced(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime, uint32_t packetId,
                             uint32_t packetRxTime = 0) const;

    bool shouldRelayEnhancedConservative(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime,
                                         uint32_t packetId, uint32_t packetRxTime = 0) const;

    bool shouldRelaySimple(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime) const;

    bool shouldRelaySimpleConservative(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime) const;

    RelayCandidateLite findBestRelayCandidate(const std::unordered_set<NodeNum> &candidates,
                                              const std::unordered_set<NodeNum> &alreadyCovered, uint32_t currentTime,
                                              uint32_t packetId) const;

    size_t getCoverageIfRelays(NodeNum relay, NodeNum *coveredNodes, size_t maxNodes, const NodeNum *alreadyCovered,
                               size_t alreadyCoveredCount) const;

    bool isGatewayNode(NodeNum nodeId, NodeNum sourceNode) const;

    bool shouldRelayWithContention(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t packetId,
                                   uint32_t currentTime) const;

    void recordNodeTransmission(NodeNum nodeId, uint32_t packetId, uint32_t currentTime);

    bool hasNodeTransmitted(NodeNum nodeId, uint32_t packetId, uint32_t currentTime) const;

    // --- Node management ---

    void removeNode(NodeNum nodeId);

    void clearEdgesForNode(NodeNum nodeId);

    void clearInferredEdgesToNode(NodeNum nodeId);

    uint8_t getNeighborCount(NodeNum node) const;

    size_t getNodeCount() const { return neighborCount; }

    size_t getAllNodeIds(NodeNum *outArray, size_t maxCount) const;

    static constexpr size_t getMemoryUsage() { return sizeof(NeighborGraph); }

  private:
    NodeEdgesLite neighbors[NEIGHBOR_GRAPH_MAX_NEIGHBORS];
    uint8_t neighborCount;

    DownstreamEntry downstream[NEIGHBOR_GRAPH_MAX_DOWNSTREAM];
    uint8_t downstreamCount;

    RelayStateLite relayStates[NEIGHBOR_GRAPH_MAX_RELAY_STATES];
    uint8_t relayStateCount;

    RouteLite routeCache[NEIGHBOR_GRAPH_MAX_CACHED_ROUTES];
    uint8_t routeCacheCount;
    static constexpr uint32_t ROUTE_CACHE_TIMEOUT_SECS = 300;

    // Find or create neighbor slot (returns nullptr if full)
    NodeEdgesLite *findOrCreateNeighbor(NodeNum nodeId);

    // Find neighbor slot (returns nullptr if not found)
    NodeEdgesLite *findNeighbor(NodeNum nodeId);
    const NodeEdgesLite *findNeighbor(NodeNum nodeId) const;

    // Find edge in node (returns nullptr if not found)
    EdgeLite *findEdge(NodeEdgesLite *node, NodeNum to);
    const EdgeLite *findEdge(const NodeEdgesLite *node, NodeNum to) const;

    // Check if a node is our direct neighbor (has a slot)
    bool isOurDirectNeighbor(NodeNum nodeId) const;
};
