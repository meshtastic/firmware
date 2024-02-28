#include "MeshPacketClient.h"

void MeshPacketClient::init(void)
{
    PacketClient::init();
}

MeshPacketClient::MeshPacketClient() {}

bool MeshPacketClient::connect(void)
{
    PacketClient::connect();
}

bool MeshPacketClient::disconnect(void)
{
    PacketClient::disconnect();
}

bool MeshPacketClient::isConnected(void)
{
    return PacketClient::isConnected();
}

bool MeshPacketClient::send(meshtastic_ToRadio &&to)
{
    static uint32_t id = 0;
    PacketClient::sendPacket(DataPacket<meshtastic_ToRadio>(++id, to));
}

meshtastic_FromRadio MeshPacketClient::receive(void)
{
    auto p = receivePacket()->move();
    if (p) {
        return static_cast<DataPacket<meshtastic_FromRadio> *>(p.get())->getData();
    } else {
        return meshtastic_FromRadio();
    }
}
