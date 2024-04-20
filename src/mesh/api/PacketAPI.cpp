#ifdef USE_PACKET_API

#include "api/PacketAPI.h"
#include "MeshService.h"
#include "PowerFSM.h"
#include "RadioInterface.h"

PacketAPI *packetAPI = nullptr;

PacketAPI *PacketAPI::create(PacketServer *_server)
{
    if (!packetAPI) {
        packetAPI = new PacketAPI(_server);
    }
    return packetAPI;
}

PacketAPI::PacketAPI(PacketServer *_server) : concurrency::OSThread("PacketAPI"), isConnected(false), server(_server) {}

int32_t PacketAPI::runOnce()
{
    bool success = sendPacket();
    success |= receivePacket();
    return success ? 10 : 50;
}

bool PacketAPI::receivePacket(void)
{
    bool data_received = false;
    while (server->hasData()) {
        isConnected = true;
        data_received = true;

        powerFSM.trigger(EVENT_CONTACT_FROM_PHONE);
        lastContactMsec = millis();

        meshtastic_ToRadio *mr;
        auto p = server->receivePacket()->move();
        int id = p->getPacketId();
        LOG_DEBUG("Received packet id=%u\n", id);
        mr = (meshtastic_ToRadio *)&static_cast<DataPacket<meshtastic_ToRadio> *>(p.get())->getData();

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
        case meshtastic_ToRadio_heartbeat_tag:
            LOG_DEBUG("Got client heartbeat\n");
            break;
        default:
            LOG_ERROR("Error: unhandled meshtastic_ToRadio variant: %d\n", mr->which_payload_variant);
            break;
        }
    }
    return data_received;
}

bool PacketAPI::sendPacket(void)
{
    // fill dummy buffer; we don't use it, we directly send the fromRadio structure
    uint32_t len = getFromRadio(txBuf);
    if (len != 0) {
        static uint32_t id = 0;
        fromRadioScratch.id = ++id;
        // TODO: think about redesign or drop class MeshPacketServer
        // if (typeid(*server) == typeid(MeshPacketServer))
        //    return dynamic_cast<MeshPacketServer*>(server)->sendPacket(fromRadioScratch);
        // else
        return server->sendPacket(DataPacket<meshtastic_FromRadio>(id, fromRadioScratch));
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

#endif