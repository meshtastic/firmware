# SignalRouting Algorithm

## Overview

SignalRouting is an advanced mesh networking protocol for Meshtastic that replaces traditional SNR-based flooding delays with graph-based topology awareness and coordinated packet forwarding. It uses Expected Transmission Count (ETX) metrics to make intelligent routing decisions, significantly improving network efficiency and reliability over both random and SNR-based flooding approaches.

## Comparison with Other Routing Approaches

### vs Stock Flooding (FloodingRouter)
- **Flooding**: Every node rebroadcasts received packets with SNR-based delays (poorer links = shorter delays)
- **SignalRouting**: Coordinates relays to prevent redundant transmissions while ensuring coverage
- **Advantage**: Reduces broadcast storms and provides more reliable delivery than SNR-based prioritization

### vs NextHopRouter
- **NextHopRouter**: Learns single next hop per destination reactively through ACK observations
- **SignalRouting**: Uses graph-based multi-hop routing with ETX quality metrics
- **Advantage**: More reliable routes and coordinated broadcast behavior

## Node Role Behavior Comparison

### Rebroadcast Behavior by Role

| Role | Stock Flooding | With SignalRouting |
|------|----------------|-------------------|
| **CLIENT_MUTE** | Never rebroadcasts | Never rebroadcasts (legacy role) |
| **CLIENT** | Rebroadcasts, can cancel duplicates | Uses SR coordination for broadcasts |
| **ROUTER/ROUTER_LATE** | Always rebroadcasts, never cancels | SR coordinates broadcasts, always relays when needed |
| **ROUTER_CLIENT** | Rebroadcasts, can cancel duplicates | Uses SR coordination for broadcasts |
| **REPEATER** | Rebroadcasts, can cancel duplicates | Uses SR coordination for broadcasts |
| **CLIENT_BASE** | Special: acts as ROUTER for favorited nodes | Uses SR coordination for broadcasts |
| **TRACKER/SENSOR/TAK** | Rebroadcasts, can cancel duplicates | Treated as legacy, no SR participation |

### Retransmission Behavior

| Router Type | Unicast Retransmissions | Notes |
|-------------|------------------------|-------|
| **ReliableRouter** | Up to 3 retransmissions | For want_ack packets only |
| **NextHopRouter** | 2 for intermediate hops, 3 for origin | Route reset on final failure |
| **SignalRouting** | Adds speculative retransmit (600ms timeout) | For SR-selected unicast routes |

### Routing Delays and Timing

| Router Type | Broadcast Delays | Unicast Timing |
|-------------|-----------------|---------------|
| **FloodingRouter** | SNR-based delays (poorer SNR = shorter delay) | Immediate send |
| **NextHopRouter** | SNR-based delays + next hop preference | iface->getRetransmissionMsec() timing |
| **SignalRouting** | Backup relay contention windows (1-2s based on LoRa preset) | ETX-based route selection + speculative retransmit |

## Core Concepts

### Expected Transmission Count (ETX)

ETX measures the expected number of transmissions needed to successfully deliver a packet over a wireless link. It's calculated from RSSI (Received Signal Strength Indicator) and SNR (Signal-to-Noise Ratio) measurements:

- **ETX = 1 / (Delivery Probability)**
- Lower ETX = Better link quality
- ETX of 1.0 = Perfect link (100% delivery probability)
- ETX > 1.0 = Lossy link requiring retransmissions

### Topology Graph

SignalRouting maintains a network topology graph where:
- **Nodes** = Mesh devices
- **Edges** = Wireless links with ETX weights
- **Sources** = Reported (measured by peer) vs Mirrored (estimated from our perspective)

## Unicast Routing

### Route Calculation Algorithm

For unicast packets, SignalRouting calculates optimal multi-hop routes using a Dijkstra-like algorithm:

```cpp
NodeNum SignalRoutingModule::getNextHop(NodeNum destination, ...) {
    // 1. Check direct connection (1-hop route)
    const NodeEdgesLite* myEdges = routingGraph->getEdgesFrom(myNode);
    for each direct neighbor:
        if (neighbor == destination) return destination; // Direct route

    // 2. Find 2-hop routes through neighbors
    for each direct neighbor:
        for each neighbor's neighbor:
            if (neighbor's neighbor == destination):
                calculate total ETX cost
                select lowest cost 2-hop route

    return best next hop or 0 (no route found)
}
```

### Unicast Route Selection Priority

1. **Direct routes** (1-hop) - when destination is a direct neighbor
2. **Gateway routes** - prefer direct connections to known gateways
3. **2-hop routes** - through neighbors with lowest total ETX
4. **Opportunistic forwarding** - when topology is unhealthy, forward to best direct neighbor

### Speculative Retransmission

For unicast packets, SignalRouting implements speculative retransmission:
- Monitors unicast packet transmissions for ACK responses
- Retransmits after 600ms timeout if no ACK received
- Helps recover from temporary link failures or interference

## Broadcast Routing

### Coordinated Flooding Algorithm

Traditional flooding sends redundant copies everywhere. SignalRouting coordinates relays to ensure coverage while minimizing redundancy:

```cpp
bool SignalRoutingModule::shouldRelayBroadcast(const meshtastic_MeshPacket *p) {
    // Calculate which nodes would be covered if we relay
    coverage = routingGraph->getCoverageIfRelays(myNode, alreadyCovered);

    // Only relay if we provide significant new coverage
    // Consider gateway relationships and legacy node compatibility
    // Use contention windows to avoid collisions

    return shouldRelay;
}
```

### Relay Decision Factors

1. **Coverage Analysis**: Does relaying reach nodes not already covered?
2. **Gateway Awareness**: Prioritize packets from stock gateways for branch coverage
3. **Legacy Compatibility**: Defer to legacy nodes when present
4. **Backup Relay Logic**: Uses contention windows (1-2s) to allow backup relays if primary relays fail
5. **Topology Health**: Only coordinate when sufficient capable nodes exist

### Broadcast Coordination Example

```
Network: A ── B ── C
           │    │
           D ── E

Packet from A to BROADCAST:

1. A transmits first
2. B and D hear it, calculate relay decisions
3. B determines: "If I relay, I cover C,E"
4. D determines: "If I relay, I cover E" (redundant with B)
5. Only B relays, D stays silent
6. Result: Full coverage with 1 relay instead of 2
```

## Benefits for Mesh Network Reliability

### Improved Deliverability

**Dense Node Scenarios:**
SignalRouting coordinates broadcast relays to prevent redundant transmissions while ensuring full network coverage. Instead of every node blindly rebroadcasting, the algorithm calculates which nodes provide unique coverage and uses contention windows to avoid collisions.

### Mesh Branch Handling

**Gateway-Aware Routing:**
```
Network Gateway
     │
     G (Stock Node)
    / \
   /   \
  A ── B
  │    │
  C ── D

SignalRouting automatically:
1. Identifies G as a gateway node bridging network segments
2. Routes unicasts through G when destination is in another network segment
3. Prioritizes broadcasts from G for full branch coverage
4. Prevents redundant relays within branches
```

### Reliability Improvements

**Link Quality Awareness:**
- Routes around poor quality links automatically
- Prefers stable, high-SNR paths
- Adapts to changing radio conditions

**Failure Recovery:**
- Speculative retransmission for unicasts
- Automatic fallback to flooding when topology unhealthy
- Route recalculation when links degrade

**Congestion Control:**
- Reduces broadcast storms in dense networks
- Backup relay contention windows ensure reliability when primary relays fail
- Graph-based coordination prevents redundant transmissions

## Graph vs GraphLite Implementation

### Graph (Full Mode)

**Memory Structure:**
```cpp
class Graph {
    std::unordered_map<NodeNum, std::vector<Edge>> adjacencyList;
    // Dynamic allocation, hash table overhead
    // ~64 bytes per node + ~32 bytes per edge
};
```

**Features:**
- Full Dijkstra algorithm for route calculation
- Enhanced relay coordination with coverage analysis
- Dynamic memory allocation (grows with network)
- Support for 100+ nodes in large networks

**Hardware Requirements:**
- **RAM**: 25-35KB for graph storage (100 nodes, 6 edges each)
- **Flash**: ~15KB additional code space
- **CPU**: Higher processing for complex algorithms
- **Best for**: ESP32, larger microcontrollers

### GraphLite (Lite Mode)

**Memory Structure:**
```cpp
class GraphLite {
    static constexpr size_t MAX_NODES = 100;
    static constexpr size_t MAX_EDGES_PER_NODE = 8;
    NodeEdgesLite nodes[MAX_NODES]; // Fixed-size arrays
};
```

**Features:**
- Simplified routing (direct + 1-hop-through-neighbor)
- Basic relay coordination with contention windows
- Fixed memory footprint, no dynamic allocation
- Optimized for memory-constrained devices

**Hardware Requirements:**
- **RAM**: ~8-12KB fixed usage (predictable memory)
- **Flash**: ~10KB additional code space
- **CPU**: Lower processing requirements
- **Best for**: ESP32-C3, RP2040, STM32WL, constrained devices

### Key Differences

| Aspect | Graph (Full) | GraphLite (Lite) |
|--------|-------------|------------------|
| **Route Length** | Multi-hop (full Dijkstra) | 1-2 hops only |
| **Relay Coordination** | Advanced coverage analysis | Simple contention windows |
| **Memory Usage** | Variable (grows with network) | Fixed (~8-12KB) |
| **Node Capacity** | 100+ nodes | ~50-60 nodes practical |
| **CPU Usage** | Higher (complex algorithms) | Lower (simplified logic) |
| **Memory Safety** | Dynamic allocation risks | Fixed arrays (safer) |
| **Scalability** | Better for large networks | Optimized for small networks |

### Automatic Mode Selection

```cpp
#ifdef SIGNAL_ROUTING_LITE_MODE
    routingGraph = new GraphLite();  // Always lite mode
#else
    routingGraph = new Graph();       // Full mode by default
#endif
```

**Automatic Hardware Adaptation:**
- **STM32WL**: Disabled entirely (64KB RAM limit)
- **RP2040**: RAM guard (< 30KB free disables SR)
- **ESP32-C3**: Uses GraphLite automatically
- **ESP32/others**: Uses full Graph mode

## Configuration and Tuning

### Key Parameters

```cpp
// Broadcast interval for topology updates
#define SIGNAL_ROUTING_BROADCAST_SECS 120

// Maximum neighbors tracked per node
#define MAX_SIGNAL_ROUTING_NEIGHBORS 14

// Speculative retransmission timeout
#define SPECULATIVE_RETRANSMIT_TIMEOUT_MS 600
```

### Topology Health Assessment

SignalRouting only activates when network has sufficient capable nodes:

```cpp
// For broadcast: requires at least 1 direct SR-capable neighbor
bool topologyHealthy = capableNeighbors >= 1;

// For unicast: requires knowledge of destination node
bool topologyHealthy = nodeDB->getMeshNode(destination) != nullptr;
```

## Real-World Examples

### Urban Mesh Network
```
Coffee Shop ── Street Node ── Park Node
     │             │             │
     └──── Apartment ──── Office ─────
                    │
               Subway Station (Gateway)
```

**SignalRouting Benefits:**
- Coordinates broadcasts to minimize redundant transmissions
- Routes unicasts through optimal paths based on link quality
- Uses gateway relationships for extended network reach
- Adapts routing when network topology changes

### Emergency Response Network
```
Command Post (Gateway)
        │
    ┌───┴───┐
Mobile Unit  Ambulance  Fire Truck
    │          │          │
    └──────┬───┴───┬──────┘
           │
       Incident Site
```

**SignalRouting Benefits:**
- Coordinated broadcast routing ensures full coverage
- ETX-based unicast routing prioritizes reliable communication paths
- Reduces redundant transmissions during critical operations
- Gateway awareness for command centers bridging network segments

### Rural Mesh with Branches
```
Main Road
    │
Farm House ── Barn ── Equipment Shed
    │                    │
    └───────── Grain Silo ──────┐
                                │
                     Network Gateway (Town)
```

**SignalRouting Benefits:**
- Efficient broadcast coverage across multiple structures
- Routes through intermediate nodes for better connectivity
- Gateway integration for external network access
- Topology-aware routing adapts to physical layout

## Performance Metrics

### Efficiency Improvements

**Broadcast Coordination:**
- Prevents redundant relay transmissions through coverage analysis
- Uses contention windows to avoid transmission collisions
- Reduces broadcast storms in dense node configurations
- Maintains full network coverage with fewer transmissions

**Unicast Optimization:**
- Routes via highest quality links (lowest ETX)
- Considers gateway relationships for extended reach
- Speculative retransmission recovers from temporary failures
- Fallback to opportunistic forwarding when topology incomplete

**Network Adaptation:**
- Automatic topology health assessment
- Graceful degradation to traditional flooding
- Adapts to changing link conditions and node movement
- Works alongside legacy routing for mixed networks

## Implementation Notes

### Fallback Mechanisms

SignalRouting gracefully degrades when conditions aren't met:

1. **Topology Unhealthy**: Falls back to contention-window flooding
2. **Memory Pressure**: Disables on low-RAM devices
3. **Legacy Networks**: Works alongside traditional routers
4. **Hardware Limits**: Automatic feature disabling for constrained devices

### Compatibility

- **Backward Compatible**: Works with all existing Meshtastic nodes
- **Progressive Enhancement**: Benefits increase with more SR-capable nodes
- **Mixed Networks**: Automatically detects and adapts to legacy nodes
- **Gateway Integration**: Special handling for nodes bridging network segments

## Future Enhancements

Potential improvements for SignalRouting:

1. **Adaptive Broadcast Intervals**: Shorter intervals for dynamic networks
2. **Quality-of-Service Classes**: Different routing for priority messages
3. **Multi-Radio Support**: Coordination across different frequency bands
4. **Energy-Aware Routing**: Battery-level considerations in route selection
5. **Machine Learning**: Predictive link quality based on historical data

## Troubleshooting

### Common Issues

**"SR disabled - insufficient RAM"**
- Device has <30KB free RAM
- Switch to GraphLite mode or upgrade hardware

**"Topology unhealthy - flooding only"**
- Too few SR-capable nodes (need at least 1 direct neighbor)
- Add more SignalRouting-capable devices or wait for topology convergence

**"No route found for unicast"**
- Destination not in topology graph
- Wait for topology convergence or use flooding

**High CPU usage**
- Large network with Graph mode
- Consider switching to GraphLite or reducing network size

**"Packet not relayed despite good coverage"**
- Primary relays may have transmitted within contention window
- Backup relays wait 1-2 seconds before transmitting

SignalRouting represents a significant advancement in mesh networking, providing intelligent, efficient routing that scales from small personal networks to large community meshes while maintaining backward compatibility and graceful degradation.
