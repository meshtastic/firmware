#include "api/PacketAPI.h"
#include "MeshService.h"
#include "RadioInterface.h"
#include "sharedMem/MeshPacketServer.h"

PacketAPI *packetAPI = nullptr;

void PacketAPI::init(void) {}

PacketAPI::PacketAPI(PacketServer *_server) : concurrency::OSThread("PacketAPI"), isConnected(false), server(_server) {}

int32_t PacketAPI::runOnce()
{
    bool success = sendPacket();
    success |= receivePacket();
    return success ? 10 : 50;
}

bool PacketAPI::receivePacket(void)
{
    if (server->hasData()) {
        isConnected = true;

        // TODO: think about redesign or drop class MeshPacketServer
        meshtastic_ToRadio *mr;
        // if (typeid(*server) == typeid(MeshPacketServer)) {
        //     dynamic_cast<MeshPacketServer*>(server)->receivePacket(*mr);
        // }
        // else {
        auto p = server->receivePacket()->move();
        int id = p->getPacketId();
        LOG_DEBUG("Received packet id=%u\n", id);
        // mr = (meshtastic_ToRadio*)&dynamic_cast<DataPacket<meshtastic_ToRadio>*>(p.get())->getData();
        mr = (meshtastic_ToRadio *)&static_cast<DataPacket<meshtastic_ToRadio> *>(p.get())->getData();
        //}

        switch (mr->which_payload_variant) {
        case meshtastic_ToRadio_packet_tag: {
            meshtastic_MeshPacket *mp = &mr->packet;
            printPacket("PACKET FROM QUEUE", mp);
            service.handleToRadio(*mp);
            break;
        }
        case meshtastic_ToRadio_want_config_id_tag: {
            uint32_t config_nonce = mr->want_config_id;
            LOG_INFO("Screen wants config, nonce=%u\n", config_nonce);
            handleStartConfig();
            break;
        }
        default:
            LOG_ERROR("Error: unhandled meshtastic_ToRadio variant: %d\n", mr->which_payload_variant);
            break;
        }
        return true;
    } else
        return false;
}

bool PacketAPI::sendPacket(void)
{
    // fill dummy buffer; we don't use it, we directly send the fromRadio structure
    uint32_t len = getFromRadio(txBuf);
    if (len != 0) {
        // TODO: think about redesign or drop class MeshPacketServer
        // if (typeid(*server) == typeid(MeshPacketServer))
        //    return dynamic_cast<MeshPacketServer*>(server)->sendPacket(fromRadioScratch);
        // else
        return server->sendPacket(DataPacket<meshtastic_FromRadio>(fromRadioScratch.id, fromRadioScratch));
    } else
        return false;
}

/**
 * return true if we got (once!) contact from our client and the server send queue is not full
 */
bool PacketAPI::checkIsConnected()
{
    isConnected |= server->hasData();
    return isConnected && server->available();
}
