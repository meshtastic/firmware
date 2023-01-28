#include "RepeaterModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

RepeaterModule *repeaterModule;

bool RepeaterModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Routing *r)
{
    printPacket("Repeater rebroadcasting", &mp);
    meshtastic_MeshPacket *p = const_cast<meshtastic_MeshPacket *>(&mp);
    router->send(p);
    return true;
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
