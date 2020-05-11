#include "PacketHistory.h"
#include "configuration.h"

/// We clear our old flood record five minute after we see the last of it
#define FLOOD_EXPIRE_TIME (5 * 60 * 1000L)

PacketHistory::PacketHistory()
{
    recentPackets.reserve(MAX_NUM_NODES); // Prealloc the worst case # of records - to prevent heap fragmentation
                                          // setup our periodic task
}

/**
 * Update recentBroadcasts and return true if we have already seen this packet
 */
bool PacketHistory::wasSeenRecently(const MeshPacket *p)
{
    if (p->id == 0) {
        DEBUG_MSG("Ignoring message with zero id\n");
        return false; // Not a floodable message ID, so we don't care
    }

    uint32_t now = millis();
    for (size_t i = 0; i < recentPackets.size();) {
        PacketRecord &r = recentPackets[i];

        if ((now - r.rxTimeMsec) >= FLOOD_EXPIRE_TIME) {
            // DEBUG_MSG("Deleting old broadcast record %d\n", i);
            recentPackets.erase(recentPackets.begin() + i); // delete old record
        } else {
            if (r.id == p->id && r.sender == p->from) {
                DEBUG_MSG("Found existing broadcast record for fr=0x%x,to=0x%x,id=%d\n", p->from, p->to, p->id);

                // Update the time on this record to now
                r.rxTimeMsec = now;
                return true;
            }

            i++;
        }
    }

    // Didn't find an existing record, make one
    PacketRecord r;
    r.id = p->id;
    r.sender = p->from;
    r.rxTimeMsec = now;
    recentPackets.push_back(r);
    DEBUG_MSG("Adding broadcast record for fr=0x%x,to=0x%x,id=%d\n", p->from, p->to, p->id);

    return false;
}