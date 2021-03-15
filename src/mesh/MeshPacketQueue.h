#pragma once

#include "MeshTypes.h"

#include <assert.h>
#include <queue>

// this is an strucure which implements the
// operator overloading
struct CompareMeshPacket {
    bool operator()(MeshPacket *p1, MeshPacket *p2);
};

/**
 * A priority queue of packets.
 *
 */
class MeshPacketQueue : public std::priority_queue<MeshPacket *, std::vector<MeshPacket *>, CompareMeshPacket>
{
    size_t maxLen;
  public:
    MeshPacketQueue(size_t _maxLen);

    /** enqueue a packet, return false if full */
    bool enqueue(MeshPacket *p);

    // bool isEmpty();

    MeshPacket *dequeue();

    /** Attempt to find and remove a packet from this queue.  Returns true the packet which was removed from the queue */
    MeshPacket *remove(NodeNum from, PacketId id);
};