#include "sharedMem/MeshPacketServer.h"
#include "api/PacketAPI.h"
#include "sharedMem/SharedQueue.h"

SharedQueue *sharedQueue = nullptr;

MeshPacketServer *meshPacketServer = nullptr;

void MeshPacketServer::init(void)
{
    meshPacketServer = new MeshPacketServer;
    packetAPI = new PacketAPI(meshPacketServer);
    meshPacketServer->begin();
}

MeshPacketServer::MeshPacketServer() {}

void MeshPacketServer::begin(void)
{
    sharedQueue = new SharedQueue;
    PacketServer::begin(sharedQueue);
}

bool MeshPacketServer::receivePacket(meshtastic_ToRadio &to)
{
    //    auto p = PacketServer::receivePacket<Packet::PacketPtr>()->move();
    auto p = PacketServer::receivePacket()->move();
    if (p) {
        // TODO: avoid data copy :(
        // to = dynamic_cast<DataPacket<meshtastic_ToRadio>*>(p.get())->getData();
        to = static_cast<DataPacket<meshtastic_ToRadio> *>(p.get())->getData();
    }
    return p != nullptr;
}

bool MeshPacketServer::sendPacket(meshtastic_FromRadio &from)
{
    return PacketServer::sendPacket(DataPacket<meshtastic_FromRadio>(from.id, from));
}

bool MeshPacketServer::sendPacket(meshtastic_FromRadio &&from)
{
    return PacketServer::sendPacket(DataPacket<meshtastic_FromRadio>(from.id, from));
}
