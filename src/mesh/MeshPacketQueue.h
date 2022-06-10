#pragma once

#include "MeshTypes.h"

#include <assert.h>
#include <queue>


/**
 * A priority queue of packets
 */
class MeshPacketQueue
{
    size_t maxLen;
    std::vector<MeshPacket *> queue;

    /** Replace a lower priority package in the queue with 'mp' (provided there are lower pri packages). Return true if replaced. */
    bool replaceLowerPriorityPacket(MeshPacket *mp);

  public:
    explicit MeshPacketQueue(size_t _maxLen);

    /** enqueue a packet, return false if full */
    bool enqueue(MeshPacket *p);

    /** return true if the queue is empty */
    bool empty();

    MeshPacket *dequeue();

    MeshPacket *getFront();

    /** Attempt to find and remove a packet from this queue.  Returns the packet which was removed from the queue */
    MeshPacket *remove(NodeNum from, PacketId id);
};
