#include "TextMessageModule.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"

TextMessageModule *textMessageModule;

ProcessMessage TextMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#ifdef DEBUG_PORT
    auto &p = mp.decoded;
    LOG_INFO("Received text msg from=0x%0x, id=0x%x, msg=%.*s\n", mp.from, mp.id, p.payload.size, p.payload.bytes);
#endif

    // We only store/display messages destined for us.
    // Keep a copy of the most recent text message.
    devicestate.rx_text_message = mp;
    devicestate.has_rx_text_message = true;

    powerFSM.trigger(EVENT_RECEIVED_MSG);
    notifyObservers(&mp);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool TextMessageModule::wantPacket(const meshtastic_MeshPacket *p)
{
    if (moduleConfig.range_test.enabled && p->decoded.portnum == meshtastic_PortNum_RANGE_TEST_APP) {
        return true;
    }
    return p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
           p->decoded.portnum == meshtastic_PortNum_DETECTION_SENSOR_APP;
}