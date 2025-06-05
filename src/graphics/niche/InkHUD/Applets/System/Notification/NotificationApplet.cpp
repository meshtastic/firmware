#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./NotificationApplet.h"

#include "./Notification.h"
#include "graphics/niche/InkHUD/Persistence.h"

#include "meshUtils.h"
#include "modules/TextMessageModule.h"

#include "RTC.h"

using namespace NicheGraphics;

InkHUD::NotificationApplet::NotificationApplet()
{
    textMessageObserver.observe(textMessageModule);
}

// Collect meta-info about the text message, and ask for approval for the notification
// No need to save the message itself; we can use the cached InkHUD::latestMessage data during render()
int InkHUD::NotificationApplet::onReceiveTextMessage(const meshtastic_MeshPacket *p)
{
    // System applets are always active
    assert(isActive());

    // Abort if feature disabled
    // This is a bit clumsy, but avoids complicated handling when the feature is enabled / disabled
    if (!settings->optionalFeatures.notifications)
        return 0;

    // Abort if this is an outgoing message
    if (getFrom(p) == nodeDB->getNodeNum())
        return 0;

    Notification n;
    n.timestamp = getValidTime(RTCQuality::RTCQualityDevice, true); // Current RTC time

    // Gather info: in-channel message
    if (isBroadcast(p->to)) {
        n.type = Notification::Type::NOTIFICATION_MESSAGE_BROADCAST;
        n.channel = p->channel;
    }

    // Gather info: DM
    else {
        n.type = Notification::Type::NOTIFICATION_MESSAGE_DIRECT;
        n.sender = p->from;
    }

    // Close an old notification, if shown
    dismiss();

    // Check if we should display the notification
    // A foreground applet might already be displaying this info
    hasNotification = true;
    currentNotification = n;
    if (isApproved()) {
        bringToForeground();
        inkhud->forceUpdate();
    } else
        hasNotification = false; // Clear the pending notification: it was rejected

    // Return zero: no issues here, carry on notifying other observers!
    return 0;
}

void InkHUD::NotificationApplet::onRender()
{
    // Clear the region beneath the tile
    // Most applets are drawing onto an empty frame buffer and don't need to do this
    // We do need to do this with the battery though, as it is an "overlay"
    fillRect(0, 0, width(), height(), WHITE);

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
    // - don't overlap border, or divider
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

void InkHUD::NotificationApplet::onForeground()
{
    handleInput = true; // Intercept the button input for our applet, so we can dismiss the notification
}

void InkHUD::NotificationApplet::onBackground()
{
    handleInput = false;
}

void InkHUD::NotificationApplet::onButtonShortPress()
{
    dismiss();
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}

void InkHUD::NotificationApplet::onButtonLongPress()
{
    dismiss();
    inkhud->forceUpdate(EInk::UpdateTypes::FULL);
}

// Ask the WindowManager to check whether any displayed applets are already displaying the info from this notification
// Called internally when we first get a "notifiable event", and then again before render,
// in case autoshow swapped which applet was displayed
bool InkHUD::NotificationApplet::isApproved()
{
    // Instead of an assert
    if (!hasNotification) {
        LOG_WARN("No notif to approve");
        return false;
    }

    // Ask all visible user applets for approval
    for (Applet *ua : inkhud->userApplets) {
        if (ua->isForeground() && !ua->approveNotification(currentNotification))
            return false;
    }

    return true;
}

// Mark that the notification should no-longer be rendered
// In addition to calling thing method, code needs to request a re-render of all applets
void InkHUD::NotificationApplet::dismiss()
{
    sendToBackground();
    hasNotification = false;
    // Not requesting update directly from this method,
    // as it is used to dismiss notifications which have been made redundant by autoshow settings, before they are ever drawn
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

        // Although we are handling DM and broadcast notifications together, we do need to treat them slightly differently
        bool isBroadcast = currentNotification.type == Notification::Type::NOTIFICATION_MESSAGE_BROADCAST;

        // Pick source of message
        MessageStore::Message *message =
            isBroadcast ? &inkhud->persistence->latestMessage.broadcast : &inkhud->persistence->latestMessage.dm;

        // Find info about the sender
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(message->sender);

        // Leading tag (channel vs. DM)
        text += isBroadcast ? "From:" : "DM: ";

        // Sender id
        if (node && node->has_user)
            text += parseShortName(node);
        else
            text += hexifyNodeNum(message->sender);

        // Check if text fits
        // - use a longer string, if we have the space
        if (getTextWidth(text) < widthAvailable * 0.5) {
            text.clear();

            // Leading tag (channel vs. DM)
            text += isBroadcast ? "Msg from " : "DM from ";

            // Sender id
            if (node && node->has_user)
                text += parseShortName(node);
            else
                text += hexifyNodeNum(message->sender);

            text += ": ";
            text += message->text;
        }
    }

    // Parse any non-ascii characters and return
    return parse(text);
}

#endif