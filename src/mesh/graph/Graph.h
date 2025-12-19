#pragma once
#include "NodeDB.h"
#include "memGet.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <limits>
#include <cstdint>

struct Edge {
    NodeNum from;
    NodeNum to;
    float etx; // Expected Transmission Count
    uint32_t lastUpdate; // Timestamp of last update
    float stability; // Stability weighting factor (1.0 = stable, lower = less stable)
    uint32_t variance; // Position variance - higher means more mobile/unreliable
    enum class Source : uint8_t { Mirrored = 0, Reported = 1 };
    Source source;

    Edge(NodeNum f, NodeNum t, float e, uint32_t timestamp, Source s = Source::Mirrored)
        : from(f), to(t), etx(e), lastUpdate(timestamp), stability(1.0f), variance(0), source(s) {}
};


struct Route {
    NodeNum destination;
    NodeNum nextHop;
    float cost;
    uint32_t timestamp;

    Route() : destination(0), nextHop(0), cost(0), timestamp(0) {}
    Route(NodeNum dest, NodeNum hop, float c, uint32_t ts)
        : destination(dest), nextHop(hop), cost(c), timestamp(ts) {}
};

struct RelayCandidate {
    NodeNum nodeId;
    size_t coverageCount;
    float avgCost;
    int tier;  // 0 = primary, 1 = backup, etc.

    RelayCandidate() : nodeId(0), coverageCount(0), avgCost(0), tier(0) {}
    RelayCandidate(NodeNum node, size_t coverage, float cost, int t)
        : nodeId(node), coverageCount(coverage), avgCost(cost), tier(t) {}

    // Sort by tier first (lower is better), then coverage (higher is better), then cost (lower is better)
    bool operator<(const RelayCandidate& other) const {
        if (tier != other.tier) return tier < other.tier;
        if (coverageCount != other.coverageCount) return coverageCount > other.coverageCount;
        return avgCost < other.avgCost;
    }
};

class Graph {
public:
    // Return values for updateEdge()
    static constexpr int EDGE_NO_CHANGE = 0;
    static constexpr int EDGE_NEW = 1;
    static constexpr int EDGE_SIGNIFICANT_CHANGE = 2;

    // Threshold for significant ETX change (20%)
    static constexpr float ETX_CHANGE_THRESHOLD = 0.20f;

    // Memory management - dynamic limits based on available heap
    static constexpr size_t MAX_EDGES_PER_NODE = 10;              // Max edges (neighbors) per node
    static constexpr uint32_t MIN_FREE_HEAP_FOR_GRAPH = 8 * 1024; // Keep at least 8KB free for other operations
    static constexpr size_t EDGE_MEMORY_ESTIMATE = 32;            // Approximate bytes per Edge struct
    static constexpr size_t NODE_OVERHEAD_ESTIMATE = 64;          // Approximate overhead per node in adjacency list

    // Relay algorithm constants
    static uint32_t getContentionWindowMs();                      // Dynamic contention window based on LoRa preset
    static constexpr size_t MAX_RELAY_TIERS = 3;                  // Primary + 2 backup tiers

    Graph();
    ~Graph();

    /**
     * Add or update an edge in the graph
     * @param variance Position variance (0 = stationary/reliable, higher = mobile/unreliable)
     * @return EDGE_NO_CHANGE, EDGE_NEW, or EDGE_SIGNIFICANT_CHANGE
     */
    int updateEdge(NodeNum from, NodeNum to, float etx, uint32_t timestamp, uint32_t variance = 0,
                   Edge::Source source = Edge::Source::Mirrored);

    /**
     * Remove edges and inactive nodes that haven't been active in the last 5 minutes
     */
    void ageEdges(uint32_t currentTime);

    /**
     * Calculate route to destination using Dijkstra with ETX costs
     */
    Route calculateRoute(NodeNum destination, uint32_t currentTime);

    /**
     * Get cached route if still valid
     */
    Route getCachedRoute(NodeNum destination, uint32_t currentTime);

    /**
     * Clear all cached routes
     */
    void clearCache();

    /**
     * Calculate ETX from RSSI and SNR values
     */
    static float calculateETX(int32_t rssi, float snr);

    /**
     * Update stability weighting for an edge
     */
    void updateStability(NodeNum from, NodeNum to, float newStability);

    /**
     * Get all edges originating from a node
     * @return pointer to vector of edges, or nullptr if node has no edges
     */
    const std::vector<Edge>* getEdgesFrom(NodeNum node) const;

    /**
     * Reverse calculate RSSI and SNR from ETX (approximate)
     * Used when populating NeighborLink from stored ETX values
     */
    static void etxToSignal(float etx, int32_t &rssi, int32_t &snr);

    /**
     * Get all nodes reachable from a given node (direct neighbors)
     * @return set of node IDs that can be reached
     */
    std::unordered_set<NodeNum> getDirectNeighbors(NodeNum node) const;

    /**
     * Get all nodes in the graph
     * @return set of all known node IDs
     */
    std::unordered_set<NodeNum> getAllNodes() const;

    /**
     * Remove a node and all its edges from the graph
     * @param nodeId Node to remove
     */
    void removeNode(NodeNum nodeId);

    /**
     * Clear all edges to/from a specific node (used for graph merging)
     * @param nodeId Node whose edges to clear
     */
    void clearEdgesForNode(NodeNum nodeId);

    /**
     * Calculate which nodes would be covered if a specific relay rebroadcasts
     * @param relay The node that would relay
     * @param alreadyCovered Nodes that have already received the packet
     * @return set of NEW nodes that would be covered
     */
    std::unordered_set<NodeNum> getCoverageIfRelays(NodeNum relay, const std::unordered_set<NodeNum>& alreadyCovered) const;

    /**
     * Find the best relay node to cover uncovered nodes
     * @param alreadyCovered Nodes that have already received the packet
     * @param candidates Candidate nodes that could relay (nodes that heard the packet)
     * @param currentTime Current timestamp for cost calculation
     * @return NodeNum of best relay, or 0 if no good relay found
     */
    NodeNum findBestRelay(const std::unordered_set<NodeNum>& alreadyCovered,
                          const std::unordered_set<NodeNum>& candidates,
                          uint32_t currentTime) const;

    /**
     * Check if a specific node should relay a broadcast
     * @param myNode Our node ID
     * @param sourceNode Original sender of the packet
     * @param heardFrom Node we heard the packet from (last relayer)
     * @param currentTime Current timestamp
     * @return true if we should relay, false otherwise
     */
    bool shouldRelay(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom, uint32_t currentTime) const;

    /**
     * Get the cost to reach a node from another node (direct edge cost)
     * @return edge cost, or infinity if not directly connected
     */
    float getEdgeCost(NodeNum from, NodeNum to, uint32_t currentTime) const;

    /**
     * Check if there's enough free heap memory to add a new node to the graph
     * @return true if we have enough memory, false otherwise
     */
    bool hasMemoryForNewNode() const;

    /**
     * Get current number of nodes in the graph
     */
    size_t getNodeCount() const { return adjacencyList.size(); }

    /**
     * Record that a node has transmitted a packet (for contention window tracking)
     * @param nodeId The node that transmitted
     * @param packetId The packet ID that was transmitted
     * @param currentTime Current timestamp
     */
    void recordNodeTransmission(NodeNum nodeId, uint32_t packetId, uint32_t currentTime);

    /**
     * Check if a node has already transmitted in the current contention window
     * @param nodeId Node to check
     * @param packetId Current packet ID
     * @param currentTime Current timestamp
     * @return true if node already transmitted for this packet
     */
    bool hasNodeTransmitted(NodeNum nodeId, uint32_t packetId, uint32_t currentTime) const;

    /**
     * Find all relay candidates with their coverage and tiers
     * @param alreadyCovered Nodes that have already received the packet
     * @param candidates Candidate nodes that could relay
     * @param currentTime Current timestamp
     * @param packetId Current packet ID for contention window checking
     * @return Vector of relay candidates sorted by priority
     */
    std::vector<RelayCandidate> findAllRelayCandidates(const std::unordered_set<NodeNum>& alreadyCovered,
                                                      const std::unordered_set<NodeNum>& candidates,
                                                      uint32_t currentTime, uint32_t packetId) const;

    /**
     * Check if a node is a gateway (bridges disconnected network segments)
     * @param nodeId Node to check
     * @param sourceNode Original packet source
     * @return true if node is a gateway bridging otherwise disconnected segments
     */
    bool isGatewayNode(NodeNum nodeId, NodeNum sourceNode) const;

    /**
     * Enhanced shouldRelay with contention window awareness and gateway detection
     * @param myNode Our node ID
     * @param sourceNode Original sender of the packet
     * @param heardFrom Node we heard the packet from (last relayer)
     * @param currentTime Current timestamp
     * @param packetId Packet ID for contention window tracking
     * @return true if we should relay, false otherwise
     */
    bool shouldRelayEnhanced(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom,
                           uint32_t currentTime, uint32_t packetId) const;

    /**
     * Conservative version of shouldRelayEnhanced that defers to stock gateways
     * @return true if we should relay, false otherwise
     */
    bool shouldRelayEnhancedConservative(NodeNum myNode, NodeNum sourceNode, NodeNum heardFrom,
                           uint32_t currentTime, uint32_t packetId) const;

private:
    std::unordered_map<NodeNum, std::vector<Edge>> adjacencyList;
    std::unordered_map<NodeNum, Route> routeCache;

    // Relay state tracking for contention window management
    struct RelayState {
        uint32_t lastTxTime;     // When this node last transmitted
        uint32_t packetId;       // ID of last packet relayed
        RelayState() : lastTxTime(0), packetId(0) {}
        RelayState(uint32_t time, uint32_t pid) : lastTxTime(time), packetId(pid) {}
    };
    std::unordered_map<NodeNum, RelayState> relayStates;

    static constexpr uint32_t ROUTE_CACHE_TIMEOUT_MS = 300 * 1000; // 300 seconds
    static constexpr uint32_t EDGE_AGING_TIMEOUT_MS = 300 * 1000; // 300 seconds
    static constexpr uint32_t RELAY_STATE_TIMEOUT_MS = 2000;      // Forget relay state after 2 seconds

    /**
     * Dijkstra implementation for finding lowest cost path
     */
    Route dijkstra(NodeNum source, NodeNum destination, uint32_t currentTime);

    /**
     * Calculate weighted ETX cost including stability
     */
    float getWeightedCost(const Edge& edge, uint32_t currentTime);
};
