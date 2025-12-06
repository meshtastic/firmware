#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./NewMsgExampleApplet.h"

using namespace NicheGraphics;

// We configured the Module API to call this method when we receive a new text message
ProcessMessage InkHUD::NewMsgExampleApplet::handleReceived(const meshtastic_MeshPacket &mp)
{

    // Abort if applet fully deactivated
    // Don't waste time: we wouldn't be rendered anyway
    if (!isActive())
        return ProcessMessage::CONTINUE;

    // Check that this is an incoming message
    // Outgoing messages (sent by us) will also call handleReceived

    if (!isFromUs(&mp)) {
        // Store the sender's nodenum
        // We need to keep this information, so we can re-use it anytime render() is called
        haveMessage = true;
        fromWho = mp.from;

        // Tell InkHUD that we have something new to show on the screen
        requestUpdate();
    }

    // Tell Module API to continue informing other firmware components about this message
    // We're not the only component which is interested in new text messages
    return ProcessMessage::CONTINUE;
}

// All drawing happens here
// We can trigger a render by calling requestUpdate()
// Render might be called by some external source
// We should always be ready to draw
void InkHUD::NewMsgExampleApplet::onRender()
{
    printAt(0, 0, "Example: NewMsg", LEFT, TOP); // Print top-left corner of text at (0,0)

    int16_t centerX = X(0.5); // Same as width() / 2
    int16_t centerY = Y(0.5); // Same as height() / 2

    if (haveMessage) {
        printAt(centerX, centerY, "New Message", CENTER, BOTTOM);
        printAt(centerX, centerY, "From: " + hexifyNodeNum(fromWho), CENTER, TOP);
    } else {
        printAt(centerX, centerY, "No Message", CENTER, MIDDLE); // Place center of string at (centerX, centerY)
    }
}

#endif