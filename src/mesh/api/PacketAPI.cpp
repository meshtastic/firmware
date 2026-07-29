#ifdef USE_PACKET_API

#include "api/PacketAPI.h"
#include "MeshService.h"
#include "PowerFSM.h"
#include "RadioInterface.h"
#include "modules/NodeInfoModule.h"

PacketAPI *packetAPI = nullptr;

PacketAPI *PacketAPI::create(PacketServer *_server)
{
    if (!packetAPI) {
        packetAPI = new PacketAPI(_server);
    }
    return packetAPI;
}

PacketAPI::PacketAPI(PacketServer *_server)
    : concurrency::OSThread("PacketAPI"), isConnected(false), server(_server)
{
    api_type = TYPE_PACKET;
}

int32_t PacketAPI::runOnce()
{
    // Always run the full config sync regardless of bluetooth state. The device-ui (MUI) must
    // receive deviceuiConfig first so its state advances past eBootScreenDone before the
    // bluetooth config arrives; only then does updateBluetoothConfig() skip enterProgrammingMode().
    // Sending only the bluetooth config via notifyProgrammingMode() when BT is enabled caused the
    // TFT screen to be permanently stuck on the ">> Programming mode <<" screen.
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

        powerFSM.trigger(EVENT_INPUT);
        lastContactMsec = millis();

        meshtastic_ToRadio *mr;
        auto p = server->receivePacket()->move();
        int id = p->getPacketId();
        LOG_DEBUG("Received packet id=%u", id);
        mr = (meshtastic_ToRadio *)&static_cast<DataPacket<meshtastic_ToRadio> *>(p.get())->getData();

        switch (mr->which_payload_variant) {
        case meshtastic_ToRadio_packet_tag: {
            meshtastic_MeshPacket *mp = &mr->packet;
            mp->transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_API;
            printPacket("PACKET FROM QUEUE", mp);
            service->handleToRadio(*mp);
            break;
        }
        case meshtastic_ToRadio_want_config_id_tag: {
            uint32_t config_nonce = mr->want_config_id;
            LOG_INFO("Screen wants config, nonce=%u", config_nonce);
            handleStartConfig();
            break;
        }
        case meshtastic_ToRadio_heartbeat_tag:
            if (mr->heartbeat.nonce == 1) {
                if (nodeInfoModule) {
                    LOG_INFO("Broadcasting nodeinfo ping");
                    nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST, true, 0, true);
                }
            } else {
                LOG_DEBUG("Got client heartbeat");
            }
            break;
        default:
            LOG_ERROR("Error: unhandled meshtastic_ToRadio variant: %d", mr->which_payload_variant);
            break;
        }
    }
    return data_received;
}

bool PacketAPI::sendPacket(void)
{
    if (server->available()) {
        // fill dummy buffer; we don't use it, we directly send the fromRadio structure
        uint32_t len = getFromRadio(txBuf);
        if (len != 0) {
            static uint32_t id = 0;
            fromRadioScratch.id = ++id;
            bool result = server->sendPacket(DataPacket<meshtastic_FromRadio>(id, fromRadioScratch));
            if (!result) {
                LOG_ERROR("send queue full");
            }
            return result;
        }
    }
    return false;
}

bool PacketAPI::checkIsConnected()
{
    isConnected |= server->hasData();
    return isConnected && server->available();
}

#endif