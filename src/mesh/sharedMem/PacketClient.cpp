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

bool PacketClient::send(meshtastic_ToRadio &&to)
{
    if (available()) {
        static uint32_t id = 0;
        return queue->clientSend(DataPacket<meshtastic_ToRadio>(++id, to));
    } else {
        return false;
    }
}

meshtastic_FromRadio PacketClient::receive(void)
{
    if (hasData()) {
        auto p = queue->clientReceive();
        if (p) {
            return static_cast<DataPacket<meshtastic_FromRadio> *>(p->move().get())->getData();
        }
    }
    return meshtastic_FromRadio();
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
