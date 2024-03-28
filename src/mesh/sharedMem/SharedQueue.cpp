#include "sharedMem/SharedQueue.h"

SharedQueue::SharedQueue() {}

SharedQueue::~SharedQueue() {}

bool SharedQueue::serverSend(Packet &&p)
{
    serverQueue.push(std::move(p));
    return true;
}

Packet::PacketPtr SharedQueue::serverReceive()
{
    return clientQueue.try_pop();
}

size_t SharedQueue::serverQueueSize() const
{
    return serverQueue.size();
}

bool SharedQueue::clientSend(Packet &&p)
{
    clientQueue.push(std::move(p));
    return true;
}

Packet::PacketPtr SharedQueue::clientReceive()
{
    return serverQueue.try_pop();
}

size_t SharedQueue::clientQueueSize() const
{
    return clientQueue.size();
}
