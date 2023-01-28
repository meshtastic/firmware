#include "RepeaterModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

RepeaterModule *repeaterModule;

bool RepeaterModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Routing *r)
{
    printPacket("Repeater observed message", &mp);
    router->sniffReceived(&mp, r);

    if ((mp.to == NODENUM_BROADCAST || mp.to == nodeDB.getNodeNum()) && (mp.from != 0)) {
        printPacket("Delivering rx packet", &mp);
        service.handleFromRadio(&mp);
    }

    return false;
}

meshtastic_MeshPacket *RepeaterModule::allocReply()
{
    return NULL;
}

RepeaterModule::RepeaterModule() : ProtobufModule("repeater", meshtastic_PortNum_ROUTING_APP, &meshtastic_Routing_msg)
{
    isPromiscuous = true;
    encryptedOk = true;
}
