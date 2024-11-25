#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./NotificationApplet.h"

#include "./Notification.h"

#include "rtc.h"

using namespace NicheGraphics;

InkHUD::NotificationApplet::NotificationApplet()
{
    // Testing only: trigger notification 30 seconds after boot
    // OSThread::setIntervalFromNow(30 * 1000UL);
}

void InkHUD::NotificationApplet::onActivate()
{
    textMessageObserver.observe(textMessageModule);
}

// Note: This applet probably won't ever be deactivated
void InkHUD::NotificationApplet::onDeactivate()
{
    textMessageObserver.unobserve(textMessageModule);
}

// Collect meta-info about the text message, and ask for approval for the notification
// No need to save the message itself; we can use the cached settings.lastMessage data during render()
int InkHUD::NotificationApplet::onReceiveTextMessage(const meshtastic_MeshPacket *p)
{
    Notification n;
    n.timestamp = getValidTime(RTCQuality::RTCQualityDevice, true); // Current RTC time

    // Gather info: in-channel message
    if (p->to == NODENUM_BROADCAST) {
        n.type = Notification::Type::NOTIFICATION_MESSAGE_BROADCAST;
        n.channel = p->channel;
    }

    // Gather info: DM
    else {
        n.type = Notification::Type::NOTIFICATION_MESSAGE_DIRECT;
        n.sender = p->from;
    }

    // If all currently displayed applets approve, show the notification
    if (WindowManager::getInstance()->approveNotification(n)) {
        hasNotification = true;
        currentNotification = n;
        bringToForeground();
        requestUpdate();
    }

    // Return zero: no issues here, carry on notifying other observers!
    return 0;
}

void InkHUD::NotificationApplet::render()
{
    setFont(fontSmall);

    // Padding (horizontal)
    const uint16_t padW = 4;

    // Main border
    drawRect(0, 0, width(), height(), BLACK);
    // drawRect(1, 1, width() - 2, height() - 2, BLACK);

    // Timestamp (potentially)
    // ====================
    std::string ts = getTimeString(currentNotification.timestamp);
    uint16_t tsW = 0;
    int16_t divX = 0;

    // Timestamp available
    if (ts.length() > 0) {
        tsW = getTextWidth(ts);
        divX = padW + tsW + padW;

        hatchRegion(0, 0, divX, height(), 2, BLACK);  // Fill with a dark background
        drawLine(divX, 0, divX, height() - 1, BLACK); // Draw divider between timestamp and main text

        setCrop(1, 1, divX - 1, height() - 2);

        // Drop shadow
        setTextColor(WHITE);
        printThick(padW + (tsW / 2), height() / 2, ts, 4, 4);

        // Bold text
        setTextColor(BLACK);
        printThick(padW + (tsW / 2), height() / 2, ts, 2, 1);
    }

    // Main text
    // =====================

    // Background fill
    // - medium dark (1/3)
    hatchRegion(divX, 0, width() - divX - 1, height(), 3, BLACK);

    uint16_t availableWidth = width() - divX - padW;
    std::string text = getNotificationText(availableWidth);

    int16_t textM = divX + padW + (getTextWidth(text) / 2);

    // Restrict area for printing
    // - don't overlap border, or diveder
    setCrop(divX + 1, 1, (width() - (divX + 1) - 1), height() - 2);

    // Drop shadow
    // - thick white text
    setTextColor(WHITE);
    printThick(textM, height() / 2, text, 4, 4);

    // Main text
    // - faux bold: double width
    setTextColor(BLACK);
    printThick(textM, height() / 2, text, 2, 1);
}

void InkHUD::NotificationApplet::dismiss()
{
    sendToBackground();
    hasNotification = false;
    requestUpdate(NicheGraphics::Drivers::EInk::FAST, true, true);
}

// Get a string for the main body text of a notification
// Formatted to suit screen width
// Takes info from InkHUD::currentNotification
std::string InkHUD::NotificationApplet::getNotificationText(uint16_t widthAvailable)
{
    assert(hasNotification);

    std::string text;

    // Text message
    // ==============

    if (IS_ONE_OF(currentNotification.type, Notification::Type::NOTIFICATION_MESSAGE_DIRECT,
                  Notification::Type::NOTIFICATION_MESSAGE_BROADCAST)) {

        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(settings.lastMessage.nodeNum);

        // Leading tag (channel vs. DM)
        if (currentNotification.type == Notification::Type::NOTIFICATION_MESSAGE_DIRECT)
            text += "DM: ";
        else
            text += "From: ";

        // Sender id
        if (node && node->has_user)
            text += node->user.short_name;
        else
            text += hexifyNodeNum(settings.lastMessage.nodeNum);

        // Check if text fits
        // - use a longer string, if we have the space
        if (getTextWidth(text) < widthAvailable * 0.5) {
            text.clear();

            // Leading tag (channel vs. DM)
            if (currentNotification.type == Notification::Type::NOTIFICATION_MESSAGE_DIRECT)
                text += "DM from ";
            else
                text += "Msg from ";

            // Sender id
            if (node && node->has_user)
                text += node->user.short_name;
            else
                text += hexifyNodeNum(settings.lastMessage.nodeNum);

            text += ": ";
            text += settings.lastMessage.text;
        }
    }

    return text;
}

#endif