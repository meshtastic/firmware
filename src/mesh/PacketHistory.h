#pragma once

#include "Router.h"
#include <vector>

/**
 * A record of a recent message broadcast
 */
struct PacketRecord {
    NodeNum sender;
    PacketId id;
    uint32_t rxTimeMsec; // Unix time in msecs - the time we received it
};

/**
 * This is a mixin that adds a record of past packets we have seen
 */
class PacketHistory
{
  private:
    /** FIXME: really should be a std::unordered_set with the key being sender,id.
     * This would make checking packets in wasSeenRecently faster.
     */
    std::vector<PacketRecord> recentPackets;

  public:
    PacketHistory();

    /**
     * Update recentBroadcasts and return true if we have already seen this packet
     */
    bool wasSeenRecently(const MeshPacket *p);
};
