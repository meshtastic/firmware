#pragma once

#include "Router.h"
#include <queue>
#include <unordered_set>

using namespace std;

/// We clear our old flood record five minute after we see the last of it
#define FLOOD_EXPIRE_TIME (5 * 60 * 1000L)

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
    size_t operator()(const PacketRecord &p) const { return (hash<NodeNum>()(p.sender)) ^ (hash<PacketId>()(p.id)); }
};

/// Order packet records by arrival time, we want the oldest packets to be in the front of our heap
class PacketRecordOrderFunction
{
  public:
    size_t operator()(const PacketRecord &p1, const PacketRecord &p2) const
    {
        // If the timer ticks have rolled over the difference between times will be _enormous_.  Handle that case specially
        uint32_t t1 = p1.rxTimeMsec, t2 = p2.rxTimeMsec;

        if (t1 - t2 > UINT32_MAX / 2) {
            // time must have rolled over, swap them because the new little number is 'bigger' than the old big number
            t1 = t2;
            t2 = p1.rxTimeMsec;
        }

        return t1 > t2;
    }
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
    vector<PacketRecord> recentPackets;
    // priority_queue<PacketRecord, vector<PacketRecord>, PacketRecordOrderFunction> arrivalTimes;
    // unordered_set<PacketRecord, PacketRecordHashFunction> recentPackets;

  public:
    PacketHistory();

    /**
     * Update recentBroadcasts and return true if we have already seen this packet
     *
     * @param withUpdate if true and not found we add an entry to recentPackets
     */
    bool wasSeenRecently(const MeshPacket *p, bool withUpdate = true);
};
