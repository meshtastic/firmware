#include "configuration.h"
#include "WaypointModule.h"
#include "NodeDB.h"
#include "PowerFSM.h"

WaypointModule *waypointModule;

ProcessMessage WaypointModule::handleReceived(const MeshPacket &mp)
{
    auto &p = mp.decoded;
    DEBUG_MSG("Received waypoint msg from=0x%0x, id=0x%x, msg=%.*s\n", mp.from, mp.id, p.payload.size, p.payload.bytes);


    notifyObservers(&mp);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
