#include "MeshPacketQueue.h"

#include <algorithm>

/// @return the priority of the specified packet
inline uint32_t getPriority(MeshPacket *p)
{
    auto pri = p->priority;
    return pri;
}

/// @return "true" if "p1" is ordered before "p2"
bool CompareMeshPacket::operator()(MeshPacket *p1, MeshPacket *p2)
{
    assert(p1 && p2);
    auto p1p = getPriority(p1), p2p = getPriority(p2);

    // If priorities differ, use that
    // for equal priorities, order by id (older packets have higher priority - this will briefly be wrong when IDs roll over but
    // no big deal)
    return (p1p != p2p) ? (p1p < p2p)         // prefer bigger priorities
                        : (p1->id >= p2->id); // prefer smaller packet ids
}

MeshPacketQueue::MeshPacketQueue(size_t _maxLen) : maxLen(_maxLen) {}

/** Some clients might not properly set priority, therefore we fix it here.
 */
void fixPriority(MeshPacket *p)
{
    // We might receive acks from other nodes (and since generated remotely, they won't have priority assigned.  Check for that
    // and fix it
    if (p->priority == MeshPacket_Priority_UNSET) {
        // if acks give high priority
        // if a reliable message give a bit higher default priority
        p->priority = (p->decoded.portnum == PortNum_ROUTING_APP) ? MeshPacket_Priority_ACK :                
                          (p->want_ack ? MeshPacket_Priority_RELIABLE : MeshPacket_Priority_DEFAULT);
    }
}

/** enqueue a packet, return false if full */
bool MeshPacketQueue::enqueue(MeshPacket *p)
{

    fixPriority(p);

    // fixme if there is something lower priority in the queue that can be deleted to make space, delete that instead
    if (size() >= maxLen)
        return false;
    else {
        push(p);
        return true;
    }
}

MeshPacket *MeshPacketQueue::dequeue()
{
    if (empty())
        return NULL;
    else {
        auto p = top();
        pop(); // remove the first item
        return p;
    }
}

// this is kinda yucky, but I'm not sure if all arduino c++ compilers support closuers.  And we only have one
// thread that can run at a time - so safe
static NodeNum findFrom;
static PacketId findId;

static bool isMyPacket(MeshPacket *p)
{
    return p->id == findId && getFrom(p) == findFrom;
}

/** Attempt to find and remove a packet from this queue.  Returns true the packet which was removed from the queue */
MeshPacket *MeshPacketQueue::remove(NodeNum from, PacketId id)
{
    findFrom = from;
    findId = id;
    auto it = std::find_if(this->c.begin(), this->c.end(), isMyPacket);
    if (it != this->c.end()) {
        auto p = *it;
        this->c.erase(it);
        std::make_heap(this->c.begin(), this->c.end(), this->comp);
        return p;
    } else {
        return NULL;
    }
}
