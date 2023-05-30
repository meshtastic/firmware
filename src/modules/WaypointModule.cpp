#include "WaypointModule.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"

WaypointModule *waypointModule;

ProcessMessage WaypointModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#ifdef DEBUG_PORT
    auto &p = mp.decoded;
    LOG_INFO("Received waypoint msg from=0x%0x, id=0x%x, msg=%.*s\n", mp.from, mp.id, p.payload.size, p.payload.bytes);
#endif

    // We only store/display messages destined for us.
    // Keep a copy of the most recent text message.
    devicestate.rx_waypoint = mp;
    devicestate.has_rx_waypoint = true;

    powerFSM.trigger(EVENT_RECEIVED_MSG);
    notifyObservers(&mp);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
