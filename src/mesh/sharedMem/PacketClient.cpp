#include "sharedMem/PacketClient.h"
#include "DebugConfiguration.h"
#include "sharedMem/SharedQueue.h"
#include <assert.h>

const uint32_t max_packet_queue_size = 10;

PacketClient *packetClient = nullptr;

void PacketClient::init(void)
{
    // for now we hard-code (only) one client task, but in principle one could
    // create as many PacketServer/PacketClient pairs as desired.
    packetClient = new PacketClient();
    packetClient->connect(sharedQueue);
}

PacketClient::PacketClient() : queue(nullptr) {}

int PacketClient::connect(SharedQueue *_queue)
{
    if (!queue) {
        queue = _queue;
    } else if (_queue != queue) {
        LOG_WARN("Client already connected.");
    }
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
