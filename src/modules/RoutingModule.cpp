#include "configuration.h"
#include "RoutingModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "main.h"

RoutingModule *routingModule;

bool RoutingModule::handleReceivedProtobuf(const MeshPacket &mp, Routing *r)
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

MeshPacket *RoutingModule::allocReply()
{
    assert(currentRequest);

    // We only consider making replies if the request was a legit routing packet (not just something we were sniffing)
    if (currentRequest->decoded.portnum == PortNum_ROUTING_APP) {
        assert(0); // 1.2 refactoring fixme, Not sure if anything needs this yet?
        // return allocDataProtobuf(u);
    }
    return NULL;
}

void RoutingModule::sendAckNak(Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex)
{
    auto p = allocAckNak(err, to, idFrom, chIndex);

    router->sendLocal(p); // we sometimes send directly to the local node
}

RoutingModule::RoutingModule() : ProtobufModule("routing", PortNum_ROUTING_APP, Routing_fields)
{
    isPromiscuous = true;
    encryptedOk = true;
}
