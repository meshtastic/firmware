#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./ThreadedMessageApplet.h"

#include "RTC.h"
#include "mesh/NodeDB.h"

using namespace NicheGraphics;

// Hard limits on how much message data to write to flash
// Avoid filling the storage if something goes wrong
// Normal usage should be well below this size
constexpr uint8_t MAX_MESSAGES_SAVED = 10;
constexpr uint32_t MAX_MESSAGE_SIZE = 250;

InkHUD::ThreadedMessageApplet::ThreadedMessageApplet(uint8_t channelIndex)
    : SinglePortModule("ThreadedMessageApplet", meshtastic_PortNum_TEXT_MESSAGE_APP), channelIndex(channelIndex)
{
    // Create the message store
    // Will shortly attempt to load messages from RAM, if applet is active
    // Label (filename in flash) is set from channel index
    store = new MessageStore("ch" + to_string(channelIndex));
}

void InkHUD::ThreadedMessageApplet::onRender()
{
    // =============
    // Draw a header
    // =============

    // Header text
    std::string headerText;
    headerText += "Channel ";
    headerText += to_string(channelIndex);
    headerText += ": ";
    if (channels.isDefaultChannel(channelIndex))
        headerText += "Public";
    else
        headerText += channels.getByIndex(channelIndex).settings.name;

    // Draw a "standard" applet header
    drawHeader(headerText);

    // Y position for divider
    const int16_t dividerY = Applet::getHeaderHeight() - 1;

    // ==================
    // Draw each message
    // ==================

    // Restrict drawing area
    // - don't overdraw the header
    // - small gap below divider
    setCrop(0, dividerY + 2, width(), height() - (dividerY + 2));

    // Set padding
    // - separates text from the vertical line which marks its edge
    constexpr uint16_t padW = 2;
    constexpr int16_t msgL = padW;
    const int16_t msgR = (width() - 1) - padW;
    const uint16_t msgW = (msgR - msgL) + 1;

    int16_t msgB = height() - 1; // Vertical cursor for drawing. Messages are bottom-aligned to this value.
    uint8_t i = 0;               // Index of stored message

    // Loop over messages
    // - until no messages left, or
    // - until no part of message fits on screen
    while (msgB >= (0 - fontSmall.lineHeight()) && i < store->messages.size()) {

        // Grab data for message
        MessageStore::Message &m = store->messages.at(i);
        bool outgoing = (m.sender == 0) || (m.sender == myNodeInfo.my_node_num); // Own NodeNum if canned message
        std::string bodyText = parse(m.text);                                    // Parse any non-ascii chars in the message

        // Cache bottom Y of message text
        // - Used when drawing vertical line alongside
        const int16_t dotsB = msgB;

        // Get dimensions for message text
        uint16_t bodyH = getWrappedTextHeight(msgL, msgW, bodyText);
        int16_t bodyT = msgB - bodyH;

        // Print message
        // - if incoming
        if (!outgoing)
            printWrapped(msgL, bodyT, msgW, bodyText);

        // Print message
        // - if outgoing
        else {
            if (getTextWidth(bodyText) < width())          // If short,
                printAt(msgR, bodyT, bodyText, RIGHT);     // print right align
            else                                           // If long,
                printWrapped(msgL, bodyT, msgW, bodyText); // need printWrapped(), which doesn't support right align
        }

        // Move cursor up
        // - above message text
        msgB -= bodyH;
        msgB -= getFont().lineHeight() * 0.2; // Padding between message and header

        // Compose info string
        // - shortname, if possible, or "me"
        // - time received, if possible
        std::string info;
        if (outgoing)
            info += "Me";
        else {
            // Check if sender is node db
            meshtastic_NodeInfoLite *sender = nodeDB->getMeshNode(m.sender);
            if (sender)
                info += parseShortName(sender); // Handle any unprintable chars in short name
            else
                info += hexifyNodeNum(m.sender); // No node info at all. Print the node num
        }

        std::string timeString = getTimeString(m.timestamp);
        if (timeString.length() > 0) {
            info += " - ";
            info += timeString;
        }

        // Print the info string
        // - Faux bold: printed twice, shifted horizontally by one px
        printAt(outgoing ? msgR : msgL, msgB, info, outgoing ? RIGHT : LEFT, BOTTOM);
        printAt(outgoing ? msgR - 1 : msgL + 1, msgB, info, outgoing ? RIGHT : LEFT, BOTTOM);

        // Underline the info string
        const int16_t divY = msgB;
        int16_t divL;
        int16_t divR;
        if (!outgoing) {
            // Left side - incoming
            divL = msgL;
            divR = getTextWidth(info) + getFont().lineHeight() / 2;
        } else {
            // Right side - outgoing
            divR = msgR;
            divL = divR - getTextWidth(info) - getFont().lineHeight() / 2;
        }
        for (int16_t x = divL; x <= divR; x += 2)
            drawPixel(x, divY, BLACK);

        // Move cursor up: above info string
        msgB -= fontSmall.lineHeight();

        // Vertical line alongside message
        for (int16_t y = msgB; y < dotsB; y += 1)
            drawPixel(outgoing ? width() - 1 : 0, y, BLACK);

        // Move cursor up: padding before next message
        msgB -= fontSmall.lineHeight() * 0.5;

        i++;
    } // End of loop: drawing each message

    // Fade effect:
    // Area immediately below the divider. Overdraw with sparse white lines.
    // Make text appear to pass behind the header
    hatchRegion(0, dividerY + 1, width(), fontSmall.lineHeight() / 3, 2, WHITE);

    // If we've run out of screen to draw messages, we can drop any leftover data from the queue
    // Those messages have been pushed off the screen-top by newer ones
    while (i < store->messages.size())
        store->messages.pop_back();
}

// Code which runs when the applet begins running
// This might happen at boot, or if user enables the applet at run-time, via the menu
void InkHUD::ThreadedMessageApplet::onActivate()
{
    loadMessagesFromFlash();
    loopbackOk = true; // Allow us to handle messages generated on the node (canned messages)
}

// Code which runs when the applet stop running
// This might be at shutdown, or if the user disables the applet at run-time, via the menu
void InkHUD::ThreadedMessageApplet::onDeactivate()
{
    loopbackOk = false; // Slightly reduce our impact if the applet is disabled
}

// Handle new text messages
// These might be incoming, from the mesh, or outgoing from phone
// Each instance of the ThreadMessageApplet will only listen on one specific channel
ProcessMessage InkHUD::ThreadedMessageApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Abort if applet fully deactivated
    if (!isActive())
        return ProcessMessage::CONTINUE;

    // Abort if wrong channel
    if (mp.channel != this->channelIndex)
        return ProcessMessage::CONTINUE;

    // Abort if message was a DM
    if (mp.to != NODENUM_BROADCAST)
        return ProcessMessage::CONTINUE;

    // Extract info into our slimmed-down "StoredMessage" type
    MessageStore::Message newMessage;
    newMessage.timestamp = getValidTime(RTCQuality::RTCQualityDevice, true); // Current RTC time
    newMessage.sender = mp.from;
    newMessage.channelIndex = mp.channel;
    newMessage.text = std::string((const char *)mp.decoded.payload.bytes, mp.decoded.payload.size);

    // Store newest message at front
    // These records are used when rendering, and also stored in flash at shutdown
    store->messages.push_front(newMessage);

    // If this was an incoming message, suggest that our applet becomes foreground, if permitted
    if (getFrom(&mp) != nodeDB->getNodeNum())
        requestAutoshow();

    // Redraw the applet, perhaps.
    requestUpdate(); // Want to update display, if applet is foreground

    // Tell Module API to continue informing other firmware components about this message
    // We're not the only component which is interested in new text messages
    return ProcessMessage::CONTINUE;
}

// Don't show notifications for text messages broadcast to our channel, when the applet is displayed
bool InkHUD::ThreadedMessageApplet::approveNotification(Notification &n)
{
    if (n.type == Notification::Type::NOTIFICATION_MESSAGE_BROADCAST && n.getChannel() == channelIndex)
        return false;

    // None of our business. Allow the notification.
    else
        return true;
}

// Save several recent messages to flash
// Stores the contents of ThreadedMessageApplet::messages
// Just enough messages to fill the display
// Messages are packed "back-to-back", to minimize blocks of flash used
void InkHUD::ThreadedMessageApplet::saveMessagesToFlash()
{
    // Create a label (will become the filename in flash)
    std::string label = "ch" + to_string(channelIndex);

    store->saveToFlash();
}

// Load recent messages to flash
// Fills ThreadedMessageApplet::messages with previous messages
// Just enough messages have been stored to cover the display
void InkHUD::ThreadedMessageApplet::loadMessagesFromFlash()
{
    // Create a label (will become the filename in flash)
    std::string label = "ch" + to_string(channelIndex);

    store->loadFromFlash();
}

// Code to run when device is shutting down
// This is in addition to any onDeactivate() code, which will also run
// Todo: implement before a reboot also
void InkHUD::ThreadedMessageApplet::onShutdown()
{
    // Save our current set of messages to flash, provided the applet isn't disabled
    if (isActive())
        saveMessagesToFlash();
}

#endif