#include "RoutingModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

RoutingModule *routingModule;

bool RoutingModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Routing *r)
{
    printPacket("Routing sniffing", &mp);
    router->sniffReceived(&mp, r);

    // FIXME - move this to a non promsicious PhoneAPI module?
    // Note: we are careful not to send back packets that started with the phone back to the phone
    if ((mp.to == NODENUM_BROADCAST || mp.to == nodeDB.getNodeNum()) && (mp.from != 0)) {
        printPacket("Delivering rx packet", &mp);
        service.handleFromRadio(&mp);
    }

    return false; // Let others look at this message also if they want
}

meshtastic_MeshPacket *RoutingModule::allocReply()
{
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER)
        return NULL;
    assert(currentRequest);

    // We only consider making replies if the request was a legit routing packet (not just something we were sniffing)
    if (currentRequest->decoded.portnum == meshtastic_PortNum_ROUTING_APP) {
        assert(0); // 1.2 refactoring fixme, Not sure if anything needs this yet?
        // return allocDataProtobuf(u);
    }
    return NULL;
}

void RoutingModule::sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex)
{
    auto p = allocAckNak(err, to, idFrom, chIndex);

    router->sendLocal(p); // we sometimes send directly to the local node
}

RoutingModule::RoutingModule() : ProtobufModule("routing", meshtastic_PortNum_ROUTING_APP, &meshtastic_Routing_msg)
{
    isPromiscuous = true;
    encryptedOk = config.device.rebroadcast_mode != meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY &&
                  config.device.rebroadcast_mode != meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY;
}
