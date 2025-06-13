#include "PacketHistory.h"
#include "configuration.h"
#include "mesh-pb-constants.h"

#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif
#include "Throttle.h"

/** PacketHistory additional debug level selection */
#define mx_PH_DEBUGLEVEL 0 // Set to 2 for debug messages, 1 for info messages, 0 for no debug messages

/////
#define mx_LOG_INFO(...)                                                                                                         \
    if (mx_PH_DEBUGLEVEL >= 1) {                                                                                                 \
        LOG_INFO(__VA_ARGS__);                                                                                                   \
    }
#define mx_LOG_DEBUG(...)                                                                                                        \
    if (mx_PH_DEBUGLEVEL >= 2) {                                                                                                 \
        LOG_DEBUG(__VA_ARGS__);                                                                                                  \
    }

PacketHistory::PacketHistory(uint32_t size) : recentPacketsCapacity(0), mx_recentPackets(NULL) // Initialize members
{
    if (size < 4 || size > PACKETHISTORY_MAX) { // Copilot suggested - makes sense
        // LOG_ERROR("PH:Invalid size %d, using default %d", size, PACKETHISTORY_MAX);
        size = PACKETHISTORY_MAX; // Use default size if invalid
    }

    // Allocate memory for the recent packets array
    recentPacketsCapacity = size;
    mx_recentPackets = new PacketRecord[recentPacketsCapacity];
    if (!mx_recentPackets) {       // No logging here, console/log probably uninitialized yet.
        recentPacketsCapacity = 0; // mark allocation fail
        return;                    // return early
    }

    // Initialize the recent packets array to zero
    memset(mx_recentPackets, 0, sizeof(PacketRecord) * recentPacketsCapacity);
}

PacketHistory::~PacketHistory()
{
    recentPacketsCapacity = 0;
    delete[] mx_recentPackets;
    mx_recentPackets = NULL;
}

/** Update recentPackets and return true if we have already seen this packet */
bool PacketHistory::wasSeenRecently(const meshtastic_MeshPacket *p, bool withUpdate, bool *wasFallback, bool *weWereNextHop)
{
    if (!initOk()) {
        LOG_ERROR("PH:wSR NOT INITIALIZED, memory allocation failed?");
        return false;
    }

    if (p->id == 0) {
        LOG_INFO("PH:wSR id=0,Ignore");
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

    mx_LOG_INFO("PH:wSR s=%08x id=%08x nh=%02x rn=%02x wU=%d wFb?%d wWNH?%d", r.sender, r.id, r.next_hop, p->relay_node,
                withUpdate, wasFallback ? *wasFallback : -1, weWereNextHop ? *weWereNextHop : -1);

    PacketRecord *found = PRfind(r.sender, r.id); // Find the packet record in the recentPackets array
    bool seenRecently = (found != NULL);          // If found -> the packet was seen recently

    // Expiring packets can be disabled, we have static allocation for history.
#if FLOOD_EXPIRE_TIME > 0
    if (seenRecently) { // Check whether found packet has already expired
        if (!Throttle::isWithinTimespanMs(found->rxTimeMsec,
                                          FLOOD_EXPIRE_TIME)) { // Check whether found packet has already expired
            mx_LOG_INFO("PH:wSR s=%08x id=%08x nh=%02x rby=%02x %02x %02x age=%d>%d slot=%d/%d CLEAR", found->sender, found->id,
                        found->next_hop, found->relayed_by[0], found->relayed_by[1], found->relayed_by[2],
                        millis() - found->rxTimeMsec, FLOOD_EXPIRE_TIME, found - mx_recentPackets, recentPacketsCapacity);

            memset(found, 0, sizeof(PacketRecord)); // Clear the record
            found = NULL;
            seenRecently = false;
        } else {
            mx_LOG_INFO("PH:wSR s=%08x id=%08x nh=%02x rby=%02x %02x %02x age=%d<%d slot=%d/%d KEEP", found->sender, found->id,
                        found->next_hop, found->relayed_by[0], found->relayed_by[1], found->relayed_by[2],
                        millis() - found->rxTimeMsec, FLOOD_EXPIRE_TIME, found - mx_recentPackets, recentPacketsCapacity);
            // only debug log here
        }
    }
#endif

    // LOG_DEBUG("PH:wSR Found record for fr=%08x id=%08x to=%08x",
    //     p->from, p->id, p->to);     // But we don't save p->to. Keeping it as it was.

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
                mx_LOG_DEBUG("PH:wSR f=%08x id=%08x nh=%02x rn=%02x oID=%02x, wasFbk=%d-set TRUE", p->from, p->id, p->next_hop,
                             p->relay_node, ourRelayID, wasFallback ? *wasFallback : -1);
                *wasFallback = true;
            } else {
                mx_LOG_DEBUG("PH:wSR f=%08x id=%08x nh=%02x rn=%02x oID=%02x, wasFbk=%d-no change", p->from, p->id, p->next_hop,
                             p->relay_node, ourRelayID, wasFallback ? *wasFallback : -1);
                // debug log only
            }
        }

        // Check if we were the next hop for this packet
        if (weWereNextHop) {
            *weWereNextHop = (found->next_hop == ourRelayID);
            mx_LOG_DEBUG("PH:wSR f=%08x id=%08x nh=%02x rn=%02x foundnh=%02x oID=%02x -> wWNH=%d", p->from, p->id, p->next_hop,
                         p->relay_node, found->next_hop, ourRelayID, *weWereNextHop);
        }
    }

    if (withUpdate) {
        if (found != NULL) {
            mx_LOG_DEBUG("PH:wSR s=%08x id=%08x nh=%02x rby=%02x %02x %02x age=%d wUpd BEFORE", found->sender, found->id,
                         found->next_hop, found->relayed_by[0], found->relayed_by[1], found->relayed_by[2],
                         millis() - found->rxTimeMsec);

            // Add the existing relayed_by to the new record
            for (uint8_t i = 0; i < (NUM_RELAYERS - 1); i++) {
                if (found->relayed_by[i] != 0)
                    r.relayed_by[i + 1] = found->relayed_by[i];
            }
            r.next_hop = found->next_hop; // keep the original next_hop (such that we check whether we were originally asked)

            mx_LOG_DEBUG("PH:wSR s=%08x id=%08x nh=%02x rby=%02x %02x %02x age=%d wUpd AFTER", r.sender, r.id, r.next_hop,
                         r.relayed_by[0], r.relayed_by[1], r.relayed_by[2], millis() - r.rxTimeMsec);
        }
        PRinsert(r); // Insert or update the packet record in the history
    }

    LOG_INFO("PH:wSR src=%08x id=%08x to=%08x relby=%02x %02x %02x nxthop=%02x seenRecently?%d ageMs=%d", r.sender, r.id, p->to,
             r.relayed_by[0], r.relayed_by[1], r.relayed_by[2], r.next_hop, seenRecently,
             found ? (millis() - found->rxTimeMsec) : -1);

    return seenRecently;
}

/** Find a packet record in history.
 * @return pointer to PacketRecord if found, NULL if not found */
PacketHistory::PacketRecord *PacketHistory::PRfind(NodeNum sender, PacketId id)
{
    if (sender == 0 || id == 0) {
        mx_LOG_DEBUG("PH:PRf s=%08x id=%08x sender/id=0->NOT FOUND", sender, id);
        return NULL;
    }

    PacketRecord *it = NULL;
    for (it = mx_recentPackets; it < (mx_recentPackets + recentPacketsCapacity); ++it) {
        if (it->id == id && it->sender == sender) {
            mx_LOG_DEBUG("PH:PRf s=%08x id=%08x FOUND nh=%02x rby=%02x %02x %02x age=%d slot=%d/%d", it->sender, it->id,
                         it->next_hop, it->relayed_by[0], it->relayed_by[1], it->relayed_by[2], millis() - (it->rxTimeMsec),
                         it - mx_recentPackets, recentPacketsCapacity);
            return it; // Return pointer to the found record
        }
    }
    mx_LOG_DEBUG("PH:PRf s=%08x id=%08x NOT FOUND", sender, id);
    return NULL; // Not found
}

/** Insert/Replace oldest PacketRecord in mx_recentPackets. */
void PacketHistory::PRinsert(PacketRecord &r)
{
    uint32_t OldestrxTimeMsec = 0;
    uint32_t now_millis = millis(); // Should not jump with time changes
    uint32_t rxTimeMsec = 0;
    PacketRecord *tu = NULL; // Will insert here.
    PacketRecord *it = NULL;

    // Find a free or oldest slot in the mx_recentPackets array
    for (it = mx_recentPackets; it < (mx_recentPackets + recentPacketsCapacity); ++it) {
        rxTimeMsec = it->rxTimeMsec;
        if (it->id == 0 && it->sender == 0 && rxTimeMsec == 0) { // Record is empty
            tu = it;                                             // Remember the free slot
            mx_LOG_DEBUG("PH:PRi Free slot@ %d/%d", tu - mx_recentPackets, recentPacketsCapacity);
            // We have that, Exit the loop
            it = (mx_recentPackets + recentPacketsCapacity);
        } else {
            if (rxTimeMsec != 0 && (now_millis - rxTimeMsec) > OldestrxTimeMsec) { // 49.7 days rollover friendly
                OldestrxTimeMsec = now_millis - rxTimeMsec;
                tu = it; // remember the oldest packet
            }
        }
    }
    // Full loop was made - we have oldest used - clear it
    if (it == (mx_recentPackets + recentPacketsCapacity)) {
        if (tu != NULL) {
            mx_LOG_DEBUG("PH:PRi Reuse slot@ %d/%d age=%d", tu - mx_recentPackets, recentPacketsCapacity,
                         millis() - tu->rxTimeMsec); // mx
            memset(tu, 0, sizeof(PacketRecord));     // Clear the record
        }
    }

    if (tu == NULL) {
        LOG_ERROR("PH:PRi No free slot, no old packet to clear?? NOT SAVING PACKET"); // mx
        // assert(false); // This should never happen, we should always have at least one packet to clear
        return; // Return early if we can't update the history
    }

    *tu = r; // store the packet

    // debug info
    if (it == (mx_recentPackets + recentPacketsCapacity)) { // reused
        mx_LOG_INFO("PH:PRi s=%08x id=%08x nh=%02x rby=%02x %02x %02x age=%d Reuse@ %d/%d oldage=%d", tu->sender, tu->id,
                    tu->next_hop, tu->relayed_by[0], tu->relayed_by[1], tu->relayed_by[2], millis() - (tu->rxTimeMsec),
                    tu - mx_recentPackets, recentPacketsCapacity, millis() - OldestrxTimeMsec);
    } else { // empty
        mx_LOG_INFO("PH:PRi s=%08x id=%08x nh=%02x rby=%02x %02x %02x age=%d  save@ %d/%d New", tu->sender, tu->id, tu->next_hop,
                    tu->relayed_by[0], tu->relayed_by[1], tu->relayed_by[2], millis() - (tu->rxTimeMsec), tu - mx_recentPackets,
                    recentPacketsCapacity);
    }
}

/* Check if a certain node was a relayer of a packet in the history given an ID and sender
 * @return true if node was indeed a relayer, false if not */
bool PacketHistory::wasRelayer(const uint8_t relayer, const uint32_t id, const NodeNum sender)
{
    if (!initOk()) {
        LOG_ERROR("PH:wRl NOT INITIALIZED, memory allocation failed?");
        return false;
    }

    if (relayer == 0) {
        mx_LOG_DEBUG("PH:wRl s=%08x id=%08x rl=%02x=zero. NO", sender, id, relayer);
        return false;
    }

    PacketRecord *found = PRfind(sender, id);

    if (found == NULL) {
        mx_LOG_DEBUG("PH:wRl s=%08x id=%08x (rl=%02x) PR not found. NO", sender, id, relayer);
        return false;
    }

    // LOG_DEBUG("PH:wRl s=%08x id=%08x nh=%02x age=%d rls=%02x %02x %02x FOUND check:%02x",
    //         found->sender, found->id, found->next_hop, millis() - found->rxTimeMsec,
    //         found->relayed_by[0], found->relayed_by[1], found->relayed_by[2],
    //         relayer);
    return wasRelayer(relayer, *found);
}

/* Check if a certain node was a relayer of a packet in the history given iterator
 * @return true if node was indeed a relayer, false if not */
bool PacketHistory::wasRelayer(const uint8_t relayer, PacketRecord &r)
{
    for (uint8_t i = 0; i < NUM_RELAYERS; i++) {
        if (r.relayed_by[i] == relayer) {
            mx_LOG_INFO("PH:wRP s=%08x id=%08x rls=%02x %02x %02x rl=%02x? YES", r.sender, r.id, r.relayed_by[0], r.relayed_by[1],
                        r.relayed_by[2], relayer);
            return true;
        }
    }
    mx_LOG_INFO("PH:wRP s=%08x id=%08x rls=%02x %02x %02x rl=%02x? NO", r.sender, r.id, r.relayed_by[0], r.relayed_by[1],
                r.relayed_by[2], relayer);
    return false;
}

// Remove a relayer from the list of relayers of a packet in the history given an ID and sender
void PacketHistory::removeRelayer(const uint8_t relayer, const uint32_t id, const NodeNum sender)
{
    if (!initOk()) {
        LOG_ERROR("PH:rRl NOT INITIALIZED, memory allocation failed?");
        return;
    }
    // LOG_DEBUG("PH:rRl s=%08x id=%08x rlayer=%02x",
    //            sender, id, relayer);

    PacketRecord *found = PRfind(sender, id);
    if (found == NULL) {
        mx_LOG_DEBUG("PH:rRl s=%08x id=%08x (rl=%02x) NOT FOUND", sender, id, relayer);
        return; // Nothing to remove
    }

    mx_LOG_DEBUG("PH:rRl s=%08x id=%08x rby=%02x %02x %02x, rl:%02x BEFORE", found->sender, found->id, found->relayed_by[0],
                 found->relayed_by[1], found->relayed_by[2], relayer);

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

    mx_LOG_DEBUG("PH:rRl s=%08x id=%08x rby=%02x %02x %02x, rl:%02x AFTER", found->sender, found->id, found->relayed_by[0],
                 found->relayed_by[1], found->relayed_by[2], relayer);

    LOG_INFO("PH:rRl src=%08x id=%08x relby=%02x %02x %02x  rl:%02x removed?%d", found->sender, found->id, found->relayed_by[0],
             found->relayed_by[1], found->relayed_by[2], relayer, i != j);
}
