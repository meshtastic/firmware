#include "MeshPacketClient.h"

void MeshPacketClient::init(void)
{
    PacketClient::init();
}

MeshPacketClient::MeshPacketClient() {}

bool MeshPacketClient::connect(void)
{
    return PacketClient::connect();
}

bool MeshPacketClient::disconnect(void)
{
    return PacketClient::disconnect();
}

bool MeshPacketClient::isConnected(void)
{
    return PacketClient::isConnected();
}

bool MeshPacketClient::send(meshtastic_ToRadio &&to)
{
    static uint32_t id = 0;
    return PacketClient::sendPacket(DataPacket<meshtastic_ToRadio>(++id, to));
}

meshtastic_FromRadio MeshPacketClient::receive(void)
{
    if (hasData()) {
        auto p = receivePacket();
        if (p) {
            return static_cast<DataPacket<meshtastic_FromRadio> *>(p->move().get())->getData();
        }
    }
    return meshtastic_FromRadio();
}
