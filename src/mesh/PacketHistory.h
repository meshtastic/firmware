#pragma once

#include "Router.h"
#include <unordered_set>

/// We clear our old flood record 10 minutes after we see the last of it
#define FLOOD_EXPIRE_TIME (10 * 60 * 1000L)

/**
 * A record of a recent message broadcast
 */
struct PacketRecord {
    NodeNum sender;
    PacketId id;
    uint32_t rxTimeMsec; // Unix time in msecs - the time we received it

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
     */
    bool wasSeenRecently(const meshtastic_MeshPacket *p, bool withUpdate = true);
};
