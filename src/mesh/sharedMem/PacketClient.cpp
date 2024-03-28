#include "sharedMem/PacketClient.h"
#include "configuration.h"
#include "sharedMem/SharedQueue.h"
#include <assert.h>

const uint32_t max_packet_queue_size = 10;

void PacketClient::init(void)
{
    // sharedQueue is currently defined external, it is not shared between processes
    connect(sharedQueue);
}

PacketClient::PacketClient() : queue(nullptr) {}

bool PacketClient::connect(void)
{
    is_connected = true;
    return is_connected;
}

bool PacketClient::disconnect(void)
{
    is_connected = false;
    return is_connected;
}

bool PacketClient::isConnected(void)
{
    return is_connected;
}

int PacketClient::connect(SharedQueue *_queue)
{
    if (!queue) {
        queue = _queue;
    } else if (_queue != queue) {
        LOG_WARN("Client already connected.");
    }
    is_connected = true;
    return queue->serverQueueSize();
}

Packet::PacketPtr PacketClient::receivePacket()
{
    assert(queue);
    if (queue->serverQueueSize() == 0)
        return {nullptr};
    return queue->clientReceive();
}

bool PacketClient::sendPacket(Packet &&p)
{
    assert(queue);
    if (queue->clientQueueSize() >= max_packet_queue_size)
        return false;
    queue->clientSend(std::move(p));
    return true;
}

bool PacketClient::hasData() const
{
    assert(queue);
    return queue->serverQueueSize() > 0;
}

bool PacketClient::available() const
{
    assert(queue);
    return queue->clientQueueSize() < max_packet_queue_size;
}
