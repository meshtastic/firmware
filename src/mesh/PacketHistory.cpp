#include "PacketHistory.h"
#include "configuration.h"
#include "mesh-pb-constants.h"

#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

PacketHistory::PacketHistory()
{
    recentPackets.reserve(MAX_NUM_NODES); // Prealloc the worst case # of records - to prevent heap fragmentation
                                          // setup our periodic task
}

/**
 * Update recentBroadcasts and return true if we have already seen this packet
 */
bool PacketHistory::wasSeenRecently(const meshtastic_MeshPacket *p, bool withUpdate)
{
    if (p->id == 0) {
        LOG_DEBUG("Ignoring message with zero id\n");
        return false; // Not a floodable message ID, so we don't care
    }

    uint32_t now = millis();

    PacketRecord r;
    r.id = p->id;
    r.sender = getFrom(p);
    r.rxTimeMsec = now;
    r.next_hop = p->next_hop;
    r.relayed_by = p->relay_node;

    auto found = recentPackets.find(r);
    bool seenRecently = (found != recentPackets.end()); // found not equal to .end() means packet was seen recently

    if (seenRecently && (now - found->rxTimeMsec) >= FLOOD_EXPIRE_TIME) { // Check whether found packet has already expired
        recentPackets.erase(found);                                       // Erase and pretend packet has not been seen recently
        found = recentPackets.end();
        seenRecently = false;
    }

    if (seenRecently) {
        // If it was seen with a next-hop not set to us, and now it's NO_NEXT_HOP_PREFERENCE, it's a fallback to flooding, so we
        // consider it unseen because we might need to handle it now
        if (found->next_hop != NO_NEXT_HOP_PREFERENCE && found->next_hop != nodeDB->getLastByteOfNodeNum(nodeDB->getNodeNum()) &&
            p->next_hop == NO_NEXT_HOP_PREFERENCE) {
            seenRecently = false;
        }
    }

    if (seenRecently) {
        LOG_DEBUG("Found existing packet record for fr=0x%x,to=0x%x,id=0x%x\n", p->from, p->to, p->id);
    }

    if (withUpdate) {
        if (found != recentPackets.end()) { // delete existing to updated timestamp and next-hop/relayed_by (re-insert)
            recentPackets.erase(found);     // as unsorted_set::iterator is const (can't update - so re-insert..)
        }
        recentPackets.insert(r);
        printPacket("Add packet record", p);
    }

    // Capacity is reerved, so only purge expired packets if recentPackets fills past 90% capacity
    // Expiry is normally dealt with after having searched/found a packet (above)
    if (recentPackets.size() > (MAX_NUM_NODES * 0.9)) {
        clearExpiredRecentPackets();
    }

    return seenRecently;
}

/**
 * Iterate through all recent packets, and remove all older than FLOOD_EXPIRE_TIME
 */
void PacketHistory::clearExpiredRecentPackets()
{
    uint32_t now = millis();

    LOG_DEBUG("recentPackets size=%ld\n", recentPackets.size());

    for (auto it = recentPackets.begin(); it != recentPackets.end();) {
        if ((now - it->rxTimeMsec) >= FLOOD_EXPIRE_TIME) {
            it = recentPackets.erase(it); // erase returns iterator pointing to element immediately following the one erased
        } else {
            ++it;
        }
    }

    LOG_DEBUG("recentPackets size=%ld (after clearing expired packets)\n", recentPackets.size());
}

/* Find the relayer of a packet in the history given an ID and sender
 * @return the 1-byte relay identifier, or NULL if not found */
uint8_t PacketHistory::getRelayerFromHistory(const uint32_t id, const NodeNum sender)
{
    PacketRecord r;
    r.id = id;
    r.sender = sender;
    auto found = recentPackets.find(r);

    if (found == recentPackets.end()) {
        return NULL;
    }

    return found->relayed_by;
}