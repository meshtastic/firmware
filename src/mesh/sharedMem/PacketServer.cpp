#include "sharedMem/PacketServer.h"
#include "sharedMem/SharedQueue.h"
#include <assert.h>

const uint32_t max_packet_queue_size = 50;

PacketServer::PacketServer() : queue(nullptr) {}

void PacketServer::begin(SharedQueue *_queue)
{
    queue = _queue;
}

#if 1
Packet::PacketPtr PacketServer::receivePacket(void)
{
    assert(queue);
    if (queue->clientQueueSize() == 0)
        return {nullptr};
    return queue->serverReceive();
}
#else // template variant with typed return values
template <> Packet::PacketPtr PacketServer::receivePacket<Packet::PacketPtr>()
{
    assert(queue);
    if (queue->clientQueueSize() == 0)
        return {nullptr};
    return queue->serverReceive();
}
#endif

bool PacketServer::sendPacket(Packet &&p)
{
    assert(queue);
    if (queue->serverQueueSize() >= max_packet_queue_size)
        return false;
    queue->serverSend(std::move(p));
    return true;
}

bool PacketServer::hasData() const
{
    assert(queue);
    return queue->clientQueueSize() > 0;
}

bool PacketServer::available() const
{
    assert(queue);
    return queue->serverQueueSize() < max_packet_queue_size;
}
