#pragma once

#include "NodeDB.h"

#define NUM_RELAYERS                                                                                                             \
    3 // Number of relayer we keep track of. Use 3 to be efficient with memory alignment of PacketRecord to 16 bytes

/**
 * This is a mixin that adds a record of past packets we have seen
 */
class PacketHistory
{
  private:
    struct PacketRecord { // A record of a recent message broadcast, no need to be visible outside this class.
        NodeNum sender;
        PacketId id;
        uint32_t rxTimeMsec;              // Unix time in msecs - the time we received it,  0 means empty
        uint8_t next_hop;                 // The next hop asked for this packet
        uint8_t relayed_by[NUM_RELAYERS]; // Array of nodes that relayed this packet
    };                                    // 4B + 4B + 4B + 1B + 3B = 16B

    uint32_t recentPacketsCapacity =
        0; // Can be set in constructor, no need to recompile. Used to allocate memory for mx_recentPackets.
    PacketRecord *recentPackets = NULL; // Simple and fixed in size. Debloat.

    /** Find a packet record in history.
     * @param sender NodeNum
     * @param id PacketId
     * @return pointer to PacketRecord if found, NULL if not found */
    PacketRecord *find(NodeNum sender, PacketId id);

    /** Insert/Replace oldest PacketRecord in mx_recentPackets.
     * @param r PacketRecord to insert or replace */
    void insert(const PacketRecord &r); // Insert or replace a packet record in the history

    /* Check if a certain node was a relayer of a packet in the history given iterator
     * @return true if node was indeed a relayer, false if not */
    bool wasRelayer(const uint8_t relayer, const PacketRecord &r);

    PacketHistory(const PacketHistory &);            // non construction-copyable
    PacketHistory &operator=(const PacketHistory &); // non copyable
  public:
    explicit PacketHistory(uint32_t size = -1); // Constructor with size parameter, default is PACKETHISTORY_MAX
    ~PacketHistory();

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

    // Remove a relayer from the list of relayers of a packet in the history given an ID and sender
    void removeRelayer(const uint8_t relayer, const uint32_t id, const NodeNum sender);

    // To check if the PacketHistory was initialized correctly by constructor
    bool initOk(void) { return recentPackets != NULL && recentPacketsCapacity != 0; }
};
