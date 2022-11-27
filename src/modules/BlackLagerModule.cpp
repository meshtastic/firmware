#include "BlackLagerModule.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"

BlackLagerModule *blackLagerModule;

/**
 * Text messaging module with digital signatures.
 */
ProcessMessage BlackLagerModule::handleReceived(const MeshPacket &mp)
{
    auto &p = mp.decoded;
    DEBUG_MSG("Received black lager msg from=0x%0x, id=0x%x, msg=%.*s\n", mp.from, mp.id, p.payload.size, p.payload.bytes);

    // Only store/display messages destined for us.
    // Keep a copy of the most recent black lager message.
    devicestate.rx_text_message = mp;
    devicestate.has_rx_text_message = true;

    powerFSM.trigger(EVENT_RECEIVED_TEXT_MSG);
    notifyObservers(&mp);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
