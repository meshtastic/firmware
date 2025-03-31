#include "PacketHistory.h"
#include "configuration.h"
#include "mesh-pb-constants.h"

#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif
#include "Throttle.h"

PacketHistory::PacketHistory()
{
    recentPackets.reserve(MAX_NUM_NODES); // Prealloc the worst case # of records - to prevent heap fragmentation
                                          // setup our periodic task
}

/**
 * Update recentBroadcasts and return true if we have already seen this packet
 */
bool PacketHistory::wasSeenRecently(const meshtastic_MeshPacket *p, bool withUpdate, bool *wasFallback, bool *weWereNextHop)
{
    if (p->id == 0) {
        LOG_DEBUG("Ignore message with zero id");
        return false; // Not a floodable message ID, so we don't care
    }

    PacketRecord r;
    r.id = p->id;
    r.sender = getFrom(p);
    r.rxTimeMsec = millis();
    r.next_hop = p->next_hop;
    r.relayed_by[0] = p->relay_node;
    // LOG_INFO("Add relayed_by 0x%x for id=0x%x", p->relay_node, r.id);

    auto found = recentPackets.find(r);
    bool seenRecently = (found != recentPackets.end()); // found not equal to .end() means packet was seen recently

    if (seenRecently &&
        !Throttle::isWithinTimespanMs(found->rxTimeMsec, FLOOD_EXPIRE_TIME)) { // Check whether found packet has already expired
        recentPackets.erase(found); // Erase and pretend packet has not been seen recently
        found = recentPackets.end();
        seenRecently = false;
    }

    if (seenRecently) {
        LOG_DEBUG("Found existing packet record for fr=0x%x,to=0x%x,id=0x%x", p->from, p->to, p->id);
        uint8_t ourRelayID = nodeDB->getLastByteOfNodeNum(nodeDB->getNodeNum());
        if (wasFallback) {
            // If it was seen with a next-hop not set to us and now it's NO_NEXT_HOP_PREFERENCE, and the relayer relayed already
            // before, it's a fallback to flooding. If we didn't already relay and the next-hop neither, we might need to handle
            // it now.
            if (found->sender != nodeDB->getNodeNum() && found->next_hop != NO_NEXT_HOP_PREFERENCE &&
                found->next_hop != ourRelayID && p->next_hop == NO_NEXT_HOP_PREFERENCE && wasRelayer(p->relay_node, found) &&
                !wasRelayer(ourRelayID, found) && !wasRelayer(found->next_hop, found)) {
                *wasFallback = true;
            }
        }

        // Check if we were the next hop for this packet
        if (weWereNextHop) {
            *weWereNextHop = found->next_hop == ourRelayID;
        }
    }

    if (withUpdate) {
        if (found != recentPackets.end()) { // delete existing to updated timestamp and relayed_by (re-insert)
            // Add the existing relayed_by to the new record
            for (uint8_t i = 0; i < NUM_RELAYERS - 1; i++) {
                if (found->relayed_by[i])
                    r.relayed_by[i + 1] = found->relayed_by[i];
            }
            r.next_hop = found->next_hop; // keep the original next_hop (such that we check whether we were originally asked)
            recentPackets.erase(found);   // as unsorted_set::iterator is const (can't update - so re-insert..)
        }
        recentPackets.insert(r);
        LOG_DEBUG("Add packet record fr=0x%x, id=0x%x", p->from, p->id);
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
    LOG_DEBUG("recentPackets size=%ld", recentPackets.size());

    for (auto it = recentPackets.begin(); it != recentPackets.end();) {
        if (!Throttle::isWithinTimespanMs(it->rxTimeMsec, FLOOD_EXPIRE_TIME)) {
            it = recentPackets.erase(it); // erase returns iterator pointing to element immediately following the one erased
        } else {
            ++it;
        }
    }

    LOG_DEBUG("recentPackets size=%ld (after clearing expired packets)", recentPackets.size());
}

/* Check if a certain node was a relayer of a packet in the history given an ID and sender
 * @return true if node was indeed a relayer, false if not */
bool PacketHistory::wasRelayer(const uint8_t relayer, const uint32_t id, const NodeNum sender)
{
    if (relayer == 0)
        return false;

    PacketRecord r = {.sender = sender, .id = id, .rxTimeMsec = 0, .next_hop = 0};
    auto found = recentPackets.find(r);

    if (found == recentPackets.end()) {
        return false;
    }

    return wasRelayer(relayer, found);
}

/* Check if a certain node was a relayer of a packet in the history given iterator
 * @return true if node was indeed a relayer, false if not */
bool PacketHistory::wasRelayer(const uint8_t relayer, std::unordered_set<PacketRecord, PacketRecordHashFunction>::iterator r)
{
    for (uint8_t i = 0; i < NUM_RELAYERS; i++) {
        if (r->relayed_by[i] == relayer) {
            return true;
        }
    }
    return false;
}

// Remove a relayer from the list of relayers of a packet in the history given an ID and sender
void PacketHistory::removeRelayer(const uint8_t relayer, const uint32_t id, const NodeNum sender)
{
    PacketRecord r = {.sender = sender, .id = id, .rxTimeMsec = 0, .next_hop = 0};
    auto found = recentPackets.find(r);

    if (found == recentPackets.end()) {
        return;
    }
    // Make a copy of the found record
    r.next_hop = found->next_hop;
    r.rxTimeMsec = found->rxTimeMsec;

    // Only add the relayers that are not the one we want to remove
    uint8_t j = 0;
    for (uint8_t i = 0; i < NUM_RELAYERS; i++) {
        if (found->relayed_by[i] != relayer) {
            r.relayed_by[j] = found->relayed_by[i];
            j++;
        }
    }

    recentPackets.erase(found);
    recentPackets.insert(r);
}