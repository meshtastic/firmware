#include "MeshPacketQueue.h"
#include "NodeDB.h"
#include "configuration.h"
#include <assert.h>

#include <algorithm>

/// @return the priority of the specified packet
inline uint32_t getPriority(const meshtastic_MeshPacket *p)
{
    auto pri = p->priority;
    return pri;
}

/// @return "true" if "p1" is ordered before "p2"
bool CompareMeshPacketFunc(const meshtastic_MeshPacket *p1, const meshtastic_MeshPacket *p2)
{
    assert(p1 && p2);

    // If one packet is in the late transmit window, prefer the other one
    if ((bool)p1->tx_after != (bool)p2->tx_after) {
        return !p1->tx_after;
    }

    auto p1p = getPriority(p1), p2p = getPriority(p2);
    // If priorities differ, use that
    // for equal priorities, prefer packets already on mesh.
    return (p1p != p2p) ? (p1p > p2p) : (!isFromUs(p1) && isFromUs(p2));
}

MeshPacketQueue::MeshPacketQueue(size_t _maxLen) : maxLen(_maxLen) {}

bool MeshPacketQueue::empty()
{
    return queue.empty();
}

/**
 * Some clients might not properly set priority, therefore we fix it here.
 */
void fixPriority(meshtastic_MeshPacket *p)
{
    // We might receive acks from other nodes (and since generated remotely, they won't have priority assigned.  Check for that
    // and fix it
    if (p->priority == meshtastic_MeshPacket_Priority_UNSET) {
        // if a reliable message give a bit higher default priority
        p->priority = (p->want_ack ? meshtastic_MeshPacket_Priority_RELIABLE : meshtastic_MeshPacket_Priority_DEFAULT);
        if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
            // if acks/naks give very high priority
            if (p->decoded.portnum == meshtastic_PortNum_ROUTING_APP) {
                p->priority = meshtastic_MeshPacket_Priority_ACK;
                // if text or admin, give high priority
            } else if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
                       p->decoded.portnum == meshtastic_PortNum_ADMIN_APP) {
                p->priority = meshtastic_MeshPacket_Priority_HIGH;
                // if it is a response, give higher priority to let it arrive early and stop the request being relayed
            } else if (p->decoded.request_id != 0) {
                p->priority = meshtastic_MeshPacket_Priority_RESPONSE;
                // Also if we want a response, give a bit higher priority
            } else if (p->decoded.want_response) {
                p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
            }
        }
    }
}

/** enqueue a packet, return false if full */
bool MeshPacketQueue::enqueue(meshtastic_MeshPacket *p)
{
    // no space - try to replace a lower priority packet in the queue
    if (queue.size() >= maxLen) {
        bool replaced = replaceLowerPriorityPacket(p);
        if (!replaced) {
            LOG_WARN("TX queue is full, and there is no lower-priority packet available to evict in favour of 0x%08x", p->id);
        }
        return replaced;
    }

    // Find the correct position using upper_bound to maintain a stable order
    auto it = std::upper_bound(queue.begin(), queue.end(), p, CompareMeshPacketFunc);
    queue.insert(it, p); // Insert packet at the found position
    return true;
}

meshtastic_MeshPacket *MeshPacketQueue::dequeue()
{
    if (empty()) {
        return NULL;
    }

    auto *p = queue.front();
    queue.erase(queue.begin()); // Remove the highest-priority packet
    return p;
}

meshtastic_MeshPacket *MeshPacketQueue::getFront()
{
    if (empty()) {
        return NULL;
    }

    auto *p = queue.front();
    return p;
}

/** Attempt to find and remove a packet from this queue.  Returns a pointer to the removed packet, or NULL if not found */
meshtastic_MeshPacket *MeshPacketQueue::remove(NodeNum from, PacketId id, bool tx_normal, bool tx_late)
{
    for (auto it = queue.begin(); it != queue.end(); it++) {
        auto p = (*it);
        if (getFrom(p) == from && p->id == id && ((tx_normal && !p->tx_after) || (tx_late && p->tx_after))) {
            queue.erase(it);
            return p;
        }
    }

    return NULL;
}

/* Attempt to find a packet from this queue. Return true if it was found. */
bool MeshPacketQueue::find(NodeNum from, PacketId id)
{
    for (auto it = queue.begin(); it != queue.end(); it++) {
        auto p = (*it);
        if (getFrom(p) == from && p->id == id) {
            return true;
        }
    }

    return false;
}

/**
 * Attempt to find a lower-priority packet in the queue and replace it with the provided one.
 * @return True if the replacement succeeded, false otherwise
 */
bool MeshPacketQueue::replaceLowerPriorityPacket(meshtastic_MeshPacket *p)
{

    if (queue.empty()) {
        return false; // No packets to replace
    }

    // Check if the packet at the back has a lower priority than the new packet
    auto *backPacket = queue.back();
    if (!backPacket->tx_after && backPacket->priority < p->priority) {
        LOG_WARN("Dropping packet 0x%08x to make room in the TX queue for higher-priority packet 0x%08x", backPacket->id, p->id);
        // Remove the back packet
        queue.pop_back();
        packetPool.release(backPacket);
        // Insert the new packet in the correct order
        enqueue(p);
        return true;
    }

    if (backPacket->tx_after) {
        // Check if there's a non-late packet with lower priority
        auto it = queue.end();
        auto refPacket = *--it;
        for (; refPacket->tx_after && it != queue.begin(); refPacket = *--it)
            ;
        if (!refPacket->tx_after && refPacket->priority < p->priority) {
            LOG_WARN("Dropping non-late packet 0x%08x to make room in the TX queue for higher-priority packet 0x%08x",
                     refPacket->id, p->id);
            queue.erase(it);
            packetPool.release(refPacket);
            // Insert the new packet in the correct order
            enqueue(p);
            return true;
        }
    }

    // If the back packet's priority is not lower, no replacement occurs
    return false;
}