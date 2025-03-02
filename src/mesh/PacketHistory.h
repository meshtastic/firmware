#pragma once

#include "NodeDB.h"
#include <unordered_set>

/// We clear our old flood record 10 minutes after we see the last of it
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#define FLOOD_EXPIRE_TIME (5 * 1000L) // Don't allow too many packets to accumulate when fuzzing.
#else
#define FLOOD_EXPIRE_TIME (10 * 60 * 1000L)
#endif

#define NUM_RELAYERS                                                                                                             \
    3 // Number of relayer we keep track of. Use 3 to be efficient with memory alignment of PacketRecord to 16 bytes

/**
 * A record of a recent message broadcast
 */
struct PacketRecord {
    NodeNum sender;
    PacketId id;
    uint32_t rxTimeMsec;              // Unix time in msecs - the time we received it
    uint8_t next_hop;                 // The next hop asked for this packet
    uint8_t relayed_by[NUM_RELAYERS]; // Array of nodes that relayed this packet

    bool operator==(const PacketRecord &p) const { return sender == p.sender && id == p.id; }
};

class PacketRecordHashFunction
{
  public:
    size_t operator()(const PacketRecord &p) const { return (std::hash<NodeNum>()(p.sender)) ^ (std::hash<PacketId>()(p.id)); }
};

/**
 * This is a mixin that adds a record of past packets we have seen
 */
class PacketHistory
{
  private:
    std::unordered_set<PacketRecord, PacketRecordHashFunction> recentPackets;

    void clearExpiredRecentPackets(); // clear all recentPackets older than FLOOD_EXPIRE_TIME

  public:
    PacketHistory();

    /**
     * Update recentBroadcasts and return true if we have already seen this packet
     *
     * @param withUpdate if true and not found we add an entry to recentPackets
     * @param wasFallback if not nullptr, packet will be checked for fallback to flooding and value will be set to true if so
     * @param weWereNextHop if not nullptr, packet will be checked for us being the next hop and value will be set to true if so
     */
    bool wasSeenRecently(const meshtastic_MeshPacket *p, bool withUpdate = true, bool *wasFallback = nullptr,
                         bool *weWereNextHop = nullptr);

    /* Check if a certain node was a relayer of a packet in the history given an ID and sender
     * @return true if node was indeed a relayer, false if not */
    bool wasRelayer(const uint8_t relayer, const uint32_t id, const NodeNum sender);

    /* Check if a certain node was a relayer of a packet in the history given iterator
     * @return true if node was indeed a relayer, false if not */
    bool wasRelayer(const uint8_t relayer, std::unordered_set<PacketRecord, PacketRecordHashFunction>::iterator r);

    // Remove a relayer from the list of relayers of a packet in the history given an ID and sender
    void removeRelayer(const uint8_t relayer, const uint32_t id, const NodeNum sender);
};