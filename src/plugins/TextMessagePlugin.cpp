#include "configuration.h"
#include "TextMessagePlugin.h"
#include "NodeDB.h"
#include "PowerFSM.h"

TextMessagePlugin textMessagePlugin;

bool TextMessagePlugin::handleReceived(const MeshPacket &mp)
{
    auto &p = mp.decoded.data;
    DEBUG_MSG("Received text msg from=0x%0x, id=%d, msg=%.*s\n", mp.from, mp.id, p.payload.size, p.payload.bytes);

    // We only store/display messages destined for us.
    // Keep a copy of the most recent text message.
    devicestate.rx_text_message = mp;
    devicestate.has_rx_text_message = true;

    powerFSM.trigger(EVENT_RECEIVED_TEXT_MSG);
    notifyObservers(&mp);

    // This is going into the wifidev feature branch
    // Only update the WebUI if WiFi is enabled
    //#if WiFi_MODE != 0
    //  notifyWebUI();
    //#endif

    return false; // Let others look at this message also if they want
}
