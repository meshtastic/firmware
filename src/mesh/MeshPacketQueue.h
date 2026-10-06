#pragma once

#include "MeshTypes.h"

#include <queue>

/**
 * A priority queue of packets
 */
class MeshPacketQueue
{
    size_t maxLen;
    std::vector<meshtastic_MeshPacket *> queue;

    /// Remove and return the packet mp may displace from a full queue, or nullptr if none may go. Frees nothing.
    meshtastic_MeshPacket *evictLowerPriorityPacket(const meshtastic_MeshPacket *mp);

    /// Insert p in CompareMeshPacketFunc order, after every packet that ranks equal, so equals keep arrival order.
    void insertSorted(meshtastic_MeshPacket *p);

  public:
    explicit MeshPacketQueue(size_t _maxLen);

    /** Enqueue p, displacing a lower-priority packet if the queue is full. Returns false if p was refused.
     * @param evicted Set to the displaced packet, or nullptr. The queue never frees: the caller releases both. */
    bool enqueue(meshtastic_MeshPacket *p, meshtastic_MeshPacket **evicted);

    /** return true if the queue is empty */
    bool empty();

    /** return amount of free packets in Queue */
    size_t getFree() { return maxLen - queue.size(); }

    /** return total size of the Queue */
    size_t getMaxLen() { return maxLen; }

    meshtastic_MeshPacket *dequeue();

    meshtastic_MeshPacket *getFront();

    /** Get a packet from this queue. Returns a pointer to the packet, or NULL if not found. */
    meshtastic_MeshPacket *getPacketFromQueue(NodeNum from, PacketId id);

    /** Attempt to find and remove a packet from this queue.  Returns the packet which was removed from the queue */
    meshtastic_MeshPacket *remove(NodeNum from, PacketId id, bool tx_normal = true, bool tx_late = true,
                                  uint8_t hop_limit_lt = 0);

    /* Attempt to find a packet from this queue. Return true if it was found. */
    bool find(const NodeNum from, const PacketId id);
};