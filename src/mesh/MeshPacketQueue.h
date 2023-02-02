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

    /** Replace a lower priority package in the queue with 'mp' (provided there are lower pri packages). Return true if replaced.
     */
    bool replaceLowerPriorityPacket(meshtastic_MeshPacket *mp);

  public:
    explicit MeshPacketQueue(size_t _maxLen);

    /** enqueue a packet, return false if full */
    bool enqueue(meshtastic_MeshPacket *p);

    /** return true if the queue is empty */
    bool empty();

    /** return amount of free packets in Queue */
    size_t getFree() { return maxLen - queue.size(); }

    /** return total size of the Queue */
    size_t getMaxLen() { return maxLen; }

    meshtastic_MeshPacket *dequeue();

    meshtastic_MeshPacket *getFront();

    /** Attempt to find and remove a packet from this queue.  Returns the packet which was removed from the queue */
    meshtastic_MeshPacket *remove(NodeNum from, PacketId id);
};
