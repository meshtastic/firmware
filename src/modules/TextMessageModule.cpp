#include "TextMessageModule.h"
#include "MeshService.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "buzz.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/MessageRenderer.h"
#include "main.h"
#include "mesh/AckBatcher.h"
TextMessageModule *textMessageModule;

ProcessMessage TextMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    auto &p = mp.decoded;
    LOG_INFO("Received text msg from=0x%0x, id=0x%x, msg=%.*s", mp.from, mp.id, p.payload.size, p.payload.bytes);
#endif


    // Check for !ackbatch command (DM to self)
    if (mp.to == nodeDB->getNodeNum() && mp.from == nodeDB->getNodeNum()) {
        const char *msg = (const char *)mp.decoded.payload.bytes;
        size_t len = mp.decoded.payload.size;
        if (len >= 9 && strncasecmp(msg, "!ackbatch", 9) == 0) {
            if (ackBatcher) {
                bool newState = ackBatcher->isEnabled();
                if (len >= 12 && strncasecmp(msg + 10, "on", 2) == 0) {
                    newState = true;
                    LOG_INFO("ACK batching ENABLED");
                } else if (len >= 13 && strncasecmp(msg + 10, "off", 3) == 0) {
                    newState = false;
                    LOG_INFO("ACK batching DISABLED");
                } else {
                    LOG_INFO("ACK batching is %s", ackBatcher->isEnabled() ? "ON" : "OFF");
                }
                ackBatcher->setEnabled(newState);
            }
            return ProcessMessage::STOP;
        }
    }

    // We only store/display messages destined for us.
    devicestate.rx_text_message = mp;
    devicestate.has_rx_text_message = true;
    IF_SCREEN(
        // Guard against running in MeshtasticUI or with no screen
        if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
            // Store in the central message history
            const StoredMessage &sm = messageStore.addFromPacket(mp);

            // Pass message to renderer (banner + thread switching + scroll reset)
            // Use the global Screen singleton to retrieve the current OLED display
            auto *display = screen ? screen->getDisplayDevice() : nullptr;
            graphics::MessageRenderer::handleNewMessage(display, sm, mp);
        })
    // Only trigger screen wake if configuration allows it
    if (shouldWakeOnReceivedMessage()) {
        powerFSM.trigger(EVENT_RECEIVED_MSG);
    }

    // Notify any observers (e.g. external modules that care about packets)
    notifyObservers(&mp);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool TextMessageModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}
