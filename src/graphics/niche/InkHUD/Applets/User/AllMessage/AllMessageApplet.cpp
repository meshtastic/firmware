#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./AllMessageApplet.h"

using namespace NicheGraphics;

void InkHUD::AllMessageApplet::onActivate()
{
    textMessageObserver.observe(textMessageModule);
}

void InkHUD::AllMessageApplet::onDeactivate()
{
    textMessageObserver.unobserve(textMessageModule);
}

// We're not consuming the data passed to this method;
// we're just just using it to trigger a render
int InkHUD::AllMessageApplet::onReceiveTextMessage(const meshtastic_MeshPacket *p)
{
    // Abort if applet fully deactivated
    // Already handled by onActivate and onDeactivate, but good practice for all applets
    if (!isActive())
        return 0;

    // Abort if this is an outgoing message
    if (getFrom(p) == nodeDB->getNodeNum())
        return 0;

    requestAutoshow(); // Want to become foreground, if permitted
    requestUpdate();   // Want to update display, if applet is foreground

    // Return zero: no issues here, carry on notifying other observers!
    return 0;
}

void InkHUD::AllMessageApplet::onRender()
{
    // Find newest message, regardless of whether DM or broadcast
    MessageStore::Message *message;
    if (latestMessage->wasBroadcast)
        message = &latestMessage->broadcast;
    else
        message = &latestMessage->dm;

    // Short circuit: no text message
    if (!message->sender) {
        printAt(X(0.5), Y(0.5), "No Message", CENTER, MIDDLE);
        return;
    }

    // ===========================
    // Header (sender, timestamp)
    // ===========================

    // Y position for divider
    // - between header text and messages

    std::string header;

    // RX Time
    // - if valid
    std::string timeString = getTimeString(message->timestamp);
    if (timeString.length() > 0) {
        header += timeString;
        header += ": ";
    }

    // Sender's id
    // - short name and long name, if available, or
    // - node id
    meshtastic_NodeInfoLite *sender = nodeDB->getMeshNode(message->sender);
    if (sender && sender->has_user) {
        header += parseShortName(sender); // May be last-four of node if unprintable (emoji, etc)
        header += " (";
        header += parse(sender->user.long_name);
        header += ")";
    } else
        header += hexifyNodeNum(message->sender);

    // Draw a "standard" applet header
    drawHeader(header);

    // Fade the right edge of the header, if text spills over edge
    uint8_t wF = getFont().lineHeight() / 2; // Width of fade effect
    uint8_t hF = getHeaderHeight();          // Height of fade effect
    if (getCursorX() > width())
        hatchRegion(width() - wF - 1, 1, wF, hF, 2, WHITE);

    // Dimensions of the header
    constexpr int16_t padDivH = 2;
    const int16_t headerDivY = Applet::getHeaderHeight() - 1;

    // ===================
    // Print message text
    // ===================

    // Parse any non-ascii chars in the message
    std::string text = parse(message->text);

    // Extra gap below the header
    int16_t textTop = headerDivY + padDivH;

    // Attempt to print with fontLarge
    uint32_t textHeight;
    setFont(fontLarge);
    textHeight = getWrappedTextHeight(0, width(), text);
    if (textHeight <= (uint32_t)height()) {
        printWrapped(0, textTop, width(), text);
        return;
    }

    // Fallback (too large): attempt to print with fontMedium
    setFont(fontMedium);
    textHeight = getWrappedTextHeight(0, width(), text);
    if (textHeight <= (uint32_t)height()) {
        printWrapped(0, textTop, width(), text);
        return;
    }

    // Fallback (too large): print with fontSmall
    setFont(fontSmall);
    printWrapped(0, textTop, width(), text);
}

// Don't show notifications for text messages when our applet is displayed
bool InkHUD::AllMessageApplet::approveNotification(Notification &n)
{
    if (n.type == Notification::Type::NOTIFICATION_MESSAGE_BROADCAST)
        return false;

    else if (n.type == Notification::Type::NOTIFICATION_MESSAGE_DIRECT)
        return false;

    else
        return true;
}

#endif