#include "PacketHistory.h"
#include "configuration.h"
#include "mesh-pb-constants.h"

#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif
#include "Throttle.h"

#define PACKETHISTORY_MAX                                                                                                        \
    max((u_int32_t)(MAX_NUM_NODES * 2.0),                                                                                        \
        (u_int32_t)100) // x2..3  Should suffice. Empirical setup. 16B per record malloc'ed, but no less than 100

#define RECENT_WARN_AGE (10 * 60 * 1000L) // Warn if the packet that gets removed was more recent than 10 min

#define VERBOSE_PACKET_HISTORY 0     // Set to 1 for verbose logging, 2 for heavy debugging
#define PACKET_HISTORY_TRACE_AGING 1 // Set to 1 to enable logging of the age of re/used history slots

PacketHistory::PacketHistory(uint32_t size) : recentPacketsCapacity(0), recentPackets(NULL) // Initialize members
{
    if (size < 4 || size > PACKETHISTORY_MAX) { // Copilot suggested - makes sense
        LOG_WARN("Packet History - Invalid size %d, using default %d", size, PACKETHISTORY_MAX);
        size = PACKETHISTORY_MAX; // Use default size if invalid
    }

    // Allocate memory for the recent packets array
    recentPacketsCapacity = size;
    recentPackets = new PacketRecord[recentPacketsCapacity];
    if (!recentPackets) { // No logging here, console/log probably uninitialized yet.
        LOG_ERROR("Packet History - Memory allocation failed for size=%d entries / %d Bytes", size,
                  sizeof(PacketRecord) * recentPacketsCapacity);
        recentPacketsCapacity = 0; // mark allocation fail
        return;                    // return early
    }

    // Initialize the recent packets array to zero
    memset(recentPackets, 0, sizeof(PacketRecord) * recentPacketsCapacity);
}

PacketHistory::~PacketHistory()
{
    recentPacketsCapacity = 0;
    delete[] recentPackets;
    recentPackets = NULL;
}

/** Update recentPackets and return true if we have already seen this packet */
bool PacketHistory::wasSeenRecently(const meshtastic_MeshPacket *p, bool withUpdate, bool *wasFallback, bool *weWereNextHop)
{
    if (!initOk()) {
        LOG_ERROR("Packet History - Was Seen Recently: NOT INITIALIZED!");
        return false;
    }

    if (p->id == 0) {
#if VERBOSE_PACKET_HISTORY
        LOG_DEBUG("Packet History - Was Seen Recently: ID is 0, not a floodable message");
#endif
        return false; // Not a floodable message ID, so we don't care
    }

    PacketRecord r;
    memset(&r, 0, sizeof(PacketRecord)); // Initialize the record to zero

    // Save basic info from checked packet
    r.id = p->id;
    r.sender = getFrom(p); // If 0 then use our ID
    r.next_hop = p->next_hop;
    r.relayed_by[0] = p->relay_node;

    r.rxTimeMsec = millis(); //
    if (r.rxTimeMsec == 0)   // =0 every 49.7 days? 0 is special
        r.rxTimeMsec = 1;

#if VERBOSE_PACKET_HISTORY
    LOG_DEBUG("Packet History - Was Seen Recently: @start s=%08x id=%08x / to=%08x nh=%02x rn=%02x / wUpd=%s / wasFb?%d wWNH?%d",
              r.sender, r.id, p->to, p->next_hop, p->relay_node, withUpdate ? "YES" : "NO", wasFallback ? *wasFallback : -1,
              weWereNextHop ? *weWereNextHop : -1);
#endif

    PacketRecord *found = find(r.sender, r.id); // Find the packet record in the recentPackets array
    bool seenRecently = (found != NULL);        // If found -> the packet was seen recently

    if (seenRecently) {
        uint8_t ourRelayID = nodeDB->getLastByteOfNodeNum(nodeDB->getNodeNum()); // Get our relay ID from our node number

        if (wasFallback) {
            // If it was seen with a next-hop not set to us and now it's NO_NEXT_HOP_PREFERENCE, and the relayer relayed already
            // before, it's a fallback to flooding. If we didn't already relay and the next-hop neither, we might need to handle
            // it now.
            if (found->sender != nodeDB->getNodeNum() && found->next_hop != NO_NEXT_HOP_PREFERENCE &&
                found->next_hop != ourRelayID && p->next_hop == NO_NEXT_HOP_PREFERENCE && wasRelayer(p->relay_node, *found) &&
                !wasRelayer(ourRelayID, *found) &&
                !wasRelayer(
                    found->next_hop,
                    *found)) { // If we were not the next hop and the next hop is not us, and we are not relaying this packet
#if VERBOSE_PACKET_HISTORY
                LOG_DEBUG("Packet History - Was Seen Recently: f=%08x id=%08x nh=%02x rn=%02x oID=%02x, wasFbk=%d-set TRUE",
                          p->from, p->id, p->next_hop, p->relay_node, ourRelayID, wasFallback ? *wasFallback : -1);
#endif
                *wasFallback = true;
            } else {
                // debug log only
#if VERBOSE_PACKET_HISTORY
                LOG_DEBUG("Packet History - Was Seen Recently: f=%08x id=%08x nh=%02x rn=%02x oID=%02x, wasFbk=%d-no change",
                          p->from, p->id, p->next_hop, p->relay_node, ourRelayID, wasFallback ? *wasFallback : -1);
#endif
            }
        }

        // Check if we were the next hop for this packet
        if (weWereNextHop) {
            *weWereNextHop = (found->next_hop == ourRelayID);
#if VERBOSE_PACKET_HISTORY
            LOG_DEBUG("Packet History - Was Seen Recently: f=%08x id=%08x nh=%02x rn=%02x foundnh=%02x oID=%02x -> wWNH=%s",
                      p->from, p->id, p->next_hop, p->relay_node, found->next_hop, ourRelayID, (*weWereNextHop) ? "YES" : "NO");
#endif
        }
    }

    if (withUpdate) {
        if (found != NULL) {
#if VERBOSE_PACKET_HISTORY
            LOG_DEBUG("Packet History - Was Seen Recently: s=%08x id=%08x nh=%02x rby=%02x %02x %02x age=%d wUpd BEFORE",
                      found->sender, found->id, found->next_hop, found->relayed_by[0], found->relayed_by[1], found->relayed_by[2],
                      millis() - found->rxTimeMsec);
#endif

            // Add the existing relayed_by to the new record
            for (uint8_t i = 0; i < (NUM_RELAYERS - 1); i++) {
                if (found->relayed_by[i] != 0)
                    r.relayed_by[i + 1] = found->relayed_by[i];
            }
            r.next_hop = found->next_hop; // keep the original next_hop (such that we check whether we were originally asked)
#if VERBOSE_PACKET_HISTORY
            LOG_DEBUG("Packet History - Was Seen Recently: s=%08x id=%08x nh=%02x rby=%02x %02x %02x age=%d wUpd AFTER", r.sender,
                      r.id, r.next_hop, r.relayed_by[0], r.relayed_by[1], r.relayed_by[2], millis() - r.rxTimeMsec);
#endif
            // TODO: have direct *found entry - can modify directly without local copy _vs_ not convolute the code by this
        }
        insert(r); // Insert or update the packet record in the history
    }
#if VERBOSE_PACKET_HISTORY
    LOG_DEBUG("Packet History - Was Seen Recently: @exit s=%08x id=%08x (to=%08x) relby=%02x %02x %02x nxthop=%02x rxT=%d "
              "found?%s seenRecently?%s wUpd?%s",
              r.sender, r.id, p->to, r.relayed_by[0], r.relayed_by[1], r.relayed_by[2], r.next_hop, r.rxTimeMsec,
              found ? "YES" : "NO ", seenRecently ? "YES" : "NO ", withUpdate ? "YES" : "NO ");
#endif

    return seenRecently;
}

/** Find a packet record in history.
 * @return pointer to PacketRecord if found, NULL if not found */
PacketHistory::PacketRecord *PacketHistory::find(NodeNum sender, PacketId id)
{
    if (sender == 0 || id == 0) {
#if VERBOSE_PACKET_HISTORY
        LOG_DEBUG("Packet History - find: s=%08x id=%08x sender/id=0->NOT FOUND", sender, id);
#endif
        return NULL;
    }

    PacketRecord *it = NULL;
    for (it = recentPackets; it < (recentPackets + recentPacketsCapacity); ++it) {
        if (it->id == id && it->sender == sender) {
#if VERBOSE_PACKET_HISTORY
            LOG_DEBUG("Packet History - find: s=%08x id=%08x FOUND nh=%02x rby=%02x %02x %02x age=%d slot=%d/%d", it->sender,
                      it->id, it->next_hop, it->relayed_by[0], it->relayed_by[1], it->relayed_by[2], millis() - (it->rxTimeMsec),
                      it - recentPackets, recentPacketsCapacity);
#endif
            // only the first match is returned, so be careful not to create duplicate entries
            return it; // Return pointer to the found record
        }
    }

#if VERBOSE_PACKET_HISTORY
    LOG_DEBUG("Packet History - find: s=%08x id=%08x NOT FOUND", sender, id);
#endif
    return NULL; // Not found
}

/** Insert/Replace oldest PacketRecord in recentPackets. */
void PacketHistory::insert(const PacketRecord &r)
{
    uint32_t now_millis = millis(); // Should not jump with time changes
    uint32_t OldtrxTimeMsec = 0;
    PacketRecord *tu = NULL; // Will insert here.
    PacketRecord *it = NULL;

    // Find a free, matching or oldest used slot in the recentPackets array
    for (it = recentPackets; it < (recentPackets + recentPacketsCapacity); ++it) {
        if (it->id == 0 && it->sender == 0 /*&& rxTimeMsec == 0*/) { // Record is empty
            tu = it;                                                 // Remember the free slot
#if VERBOSE_PACKET_HISTORY >= 2
            LOG_DEBUG("Packet History - insert: Free slot@ %d/%d", tu - recentPackets, recentPacketsCapacity);
#endif
            // We have that, Exit the loop
            it = (recentPackets + recentPacketsCapacity);
        } else if (it->id == r.id && it->sender == r.sender) { // Record matches the packet we want to insert
            tu = it;                                           // Remember the matching slot
            OldtrxTimeMsec = now_millis - it->rxTimeMsec;      // ..and save current entry's age
#if VERBOSE_PACKET_HISTORY >= 2
            LOG_DEBUG("Packet History - insert: Matched slot@ %d/%d age=%d", tu - recentPackets, recentPacketsCapacity,
                      OldtrxTimeMsec);
#endif
            // We have that, Exit the loop
            it = (recentPackets + recentPacketsCapacity);
        } else {
            if (it->rxTimeMsec == 0) {
                LOG_WARN(
                    "Packet History - insert: Found packet s=%08x id=%08x with rxTimeMsec = 0, slot %d/%d. Should never happen!",
                    it->sender, it->id, it - recentPackets, recentPacketsCapacity);
            }
            if ((now_millis - it->rxTimeMsec) > OldtrxTimeMsec) { // 49.7 days rollover friendly
                OldtrxTimeMsec = now_millis - it->rxTimeMsec;
                tu = it; // remember the oldest packet
#if VERBOSE_PACKET_HISTORY >= 2
                LOG_DEBUG("Packet History - insert: Older slot@ %d/%d age=%d", tu - recentPackets, recentPacketsCapacity,
                          OldtrxTimeMsec);
#endif
            }
            // keep looking for oldest till entire array is checked
        }
    }

    if (tu == NULL) {
        LOG_ERROR("Packet History - insert: No free slot, no matched packet, no oldest to reuse. Something leaked."); // mx
        // assert(false); // This should never happen, we should always have at least one packet to clear
        return; // Return early if we can't update the history
    }

#if VERBOSE_PACKET_HISTORY
    if (tu->id == 0 && tu->sender == 0) {
        LOG_DEBUG("Packet History - insert: slot@ %d/%d is NEW", tu - recentPackets, recentPacketsCapacity);
    } else if (tu->id == r.id && tu->sender == r.sender) {
        LOG_DEBUG("Packet History - insert: slot@ %d/%d MATCHED, age=%d", tu - recentPackets, recentPacketsCapacity,
                  OldtrxTimeMsec);
    } else {
        LOG_DEBUG("Packet History - insert: slot@ %d/%d REUSE OLDEST, age=%d", tu - recentPackets, recentPacketsCapacity,
                  OldtrxTimeMsec);
    }
#endif

    // If we are reusing a slot, we should warn if the packet is too recent
#if RECENT_WARN_AGE > 0
    if (tu->rxTimeMsec && (OldtrxTimeMsec < RECENT_WARN_AGE)) {
        if (!(tu->id == r.id && tu->sender == r.sender)) {
#if VERBOSE_PACKET_HISTORY
            LOG_WARN("Packet History - insert: Reusing slot aged %ds < %ds RECENT_WARN_AGE", OldtrxTimeMsec / 1000,
                     RECENT_WARN_AGE / 1000);
#endif
        } else {
            // debug only
#if VERBOSE_PACKET_HISTORY
            LOG_WARN("Packet History - insert: Reusing slot aged %.3fs < %ds with MATCHED PACKET - this is normal",
                     OldtrxTimeMsec / 1000., RECENT_WARN_AGE / 1000);
#endif
        }
    }

#if PACKET_HISTORY_TRACE_AGING
    if (tu->rxTimeMsec != 0) {
        LOG_INFO("Packet History - insert: Reusing slot aged %.3fs TRACE %s", OldtrxTimeMsec / 1000.,
                 (tu->id == r.id && tu->sender == r.sender) ? "MATCHED PACKET" : "OLDEST SLOT");
    } else {
        LOG_INFO("Packet History - insert: Using new slot @uptime %.3fs TRACE NEW", millis() / 1000.);
    }
#endif

#endif

#if VERBOSE_PACKET_HISTORY
    LOG_DEBUG("Packet History - insert: Store slot@ %d/%d s=%08x id=%08x nh=%02x rby=%02x %02x %02x rxT=%d BEFORE",
              tu - recentPackets, recentPacketsCapacity, tu->sender, tu->id, tu->next_hop, tu->relayed_by[0], tu->relayed_by[1],
              tu->relayed_by[2], tu->rxTimeMsec);
#endif

    if (r.rxTimeMsec == 0) {
#if VERBOSE_PACKET_HISTORY
        LOG_WARN("Packet History - insert: I will not store packet with rxTimeMsec = 0.");
#endif
        return; // Return early if we can't update the history
    }

    *tu = r; // store the packet

#if VERBOSE_PACKET_HISTORY
    LOG_DEBUG("Packet History - insert: Store slot@ %d/%d s=%08x id=%08x nh=%02x rby=%02x %02x %02x rxT=%d AFTER",
              tu - recentPackets, recentPacketsCapacity, tu->sender, tu->id, tu->next_hop, tu->relayed_by[0], tu->relayed_by[1],
              tu->relayed_by[2], tu->rxTimeMsec);
#endif
}

/* Check if a certain node was a relayer of a packet in the history given an ID and sender
 * @return true if node was indeed a relayer, false if not */
bool PacketHistory::wasRelayer(const uint8_t relayer, const uint32_t id, const NodeNum sender)
{
    if (!initOk()) {
        LOG_ERROR("PacketHistory - wasRelayer: NOT INITIALIZED!");
        return false;
    }

    if (relayer == 0) {
#if VERBOSE_PACKET_HISTORY
        LOG_DEBUG("Packet History - was relayer: s=%08x id=%08x / rl=%02x=zero. NO", sender, id, relayer);
#endif
        return false;
    }

    const PacketRecord *found = find(sender, id);

    if (found == NULL) {
#if VERBOSE_PACKET_HISTORY
        LOG_DEBUG("Packet History - was relayer: s=%08x id=%08x / rl=%02x / PR not found. NO", sender, id, relayer);
#endif
        return false;
    }

#if VERBOSE_PACKET_HISTORY >= 2
    LOG_DEBUG("Packet History - was relayer: s=%08x id=%08x nh=%02x age=%d rls=%02x %02x %02x InHistory,check:%02x",
              found->sender, found->id, found->next_hop, millis() - found->rxTimeMsec, found->relayed_by[0], found->relayed_by[1],
              found->relayed_by[2], relayer);
#endif
    return wasRelayer(relayer, *found);
}

/* Check if a certain node was a relayer of a packet in the history given iterator
 * @return true if node was indeed a relayer, false if not */
bool PacketHistory::wasRelayer(const uint8_t relayer, const PacketRecord &r)
{
    for (uint8_t i = 0; i < NUM_RELAYERS; i++) {
        if (r.relayed_by[i] == relayer) {
#if VERBOSE_PACKET_HISTORY
            LOG_DEBUG("Packet History - was rel.PR.: s=%08x id=%08x rls=%02x %02x %02x / rl=%02x? YES", r.sender, r.id,
                      r.relayed_by[0], r.relayed_by[1], r.relayed_by[2], relayer);
#endif
            return true;
        }
    }
#if VERBOSE_PACKET_HISTORY
    LOG_DEBUG("Packet History - was rel.PR.: s=%08x id=%08x rls=%02x %02x %02x / rl=%02x? NO", r.sender, r.id, r.relayed_by[0],
              r.relayed_by[1], r.relayed_by[2], relayer);
#endif
    return false;
}

// Remove a relayer from the list of relayers of a packet in the history given an ID and sender
void PacketHistory::removeRelayer(const uint8_t relayer, const uint32_t id, const NodeNum sender)
{
    if (!initOk()) {
        LOG_ERROR("Packet History - remove Relayer: NOT INITIALIZED!");
        return;
    }

    PacketRecord *found = find(sender, id);
    if (found == NULL) {
#if VERBOSE_PACKET_HISTORY
        LOG_DEBUG("Packet History - remove Relayer s=%08x id=%08x (rl=%02x) NOT FOUND", sender, id, relayer);
#endif
        return; // Nothing to remove
    }

#if VERBOSE_PACKET_HISTORY
    LOG_DEBUG("Packet History - remove Relayer s=%08x id=%08x rby=%02x %02x %02x, rl:%02x BEFORE", found->sender, found->id,
              found->relayed_by[0], found->relayed_by[1], found->relayed_by[2], relayer);
#endif

    // nexthop and rxTimeMsec too stay in found entry

    uint8_t j = 0;
    uint8_t i = 0;
    for (; i < NUM_RELAYERS; i++) {
        if (found->relayed_by[i] != relayer) {
            found->relayed_by[j] = found->relayed_by[i];
            j++;
        } else
            found->relayed_by[i] = 0;
    }
    for (; j < NUM_RELAYERS; j++) { // Clear the rest of the relayed_by array
        found->relayed_by[j] = 0;
    }

#if VERBOSE_PACKET_HISTORY
    LOG_DEBUG("Packet History - remove Relayer s=%08x id=%08x rby=%02x %02x %02x  rl:%02x AFTER - removed?%d", found->sender,
              found->id, found->relayed_by[0], found->relayed_by[1], found->relayed_by[2], relayer, i != j);
#endif
}
