#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./NotificationApplet.h"

#include "./Notification.h"
#include "MessageStore.h"
#include "graphics/niche/InkHUD/Persistence.h"
#if !MESHTASTIC_EXCLUDE_WAYPOINT
#include "modules/GeofenceModule.h"
#endif

#include <algorithm>

#include "meshUtils.h"
#include "modules/TextMessageModule.h"

#include "RTC.h"

using namespace NicheGraphics;

InkHUD::NotificationApplet::NotificationApplet()
{
    textMessageObserver.observe(textMessageModule);
#if !MESHTASTIC_EXCLUDE_WAYPOINT
    if (geofenceModule)
        geofenceObserver.observe(geofenceModule);
#endif
}

void InkHUD::NotificationApplet::clearPreparedLines()
{
    preparedWrapped = false;
    preparedLineCount = 0;
    preparedTextHeight = 0;
    for (PreparedLine &line : preparedLines) {
        line.text.clear();
        line.width = 0;
    }
}

void InkHUD::NotificationApplet::resetTileHeight()
{
    if (Tile *tile = getTile())
        tile->setRegion(tile->getLeft(), tile->getTop(), inkhud->width(), DEFAULT_HEIGHT);
}

void InkHUD::NotificationApplet::prepareCurrentNotificationLayout()
{
    Tile *tile = getTile();
    if (!tile)
        return;

    clearPreparedLines();
    updateDimensions();
    resetDrawingSpace();
    setFont(fontSmall);

    const uint16_t padW = 4;
    std::string ts = getTimeString(currentNotification.timestamp);
    const uint16_t tsW = ts.length() > 0 ? getTextWidth(ts) : 0;
    const int16_t divX = ts.length() > 0 ? (padW + tsW + padW) : 0;
    const int16_t textLeft = divX + padW;
    const uint16_t availableWidth = (textLeft + 2 < width()) ? (width() - textLeft - 2) : 1;

    std::string text = getNotificationText(availableWidth);
    if (text.empty()) {
        resetTileHeight();
        return;
    }

    preparedWrapped = getTextWidth(text) > availableWidth;
    if (!preparedWrapped) {
        preparedLines[0].text = std::move(text);
        preparedLines[0].width = getTextWidth(preparedLines[0].text);
        preparedLineCount = 1;
        preparedTextHeight = fontSmall.lineHeight();
        resetTileHeight();
        return;
    }

    std::string current;
    std::string word;
    const auto commitCurrentLine = [&]() {
        if (!current.empty() && preparedLineCount < MAX_WRAPPED_LINES) {
            preparedLines[preparedLineCount].text = current;
            preparedLines[preparedLineCount].width = getTextWidth(current);
            preparedLineCount++;
            current.clear();
        }
    };

    const auto appendWord = [&](bool forceLineBreak) {
        if (!word.empty()) {
            std::string candidate = current.empty() ? word : (current + " " + word);
            if (!current.empty() && getTextWidth(candidate) > availableWidth && preparedLineCount < (MAX_WRAPPED_LINES - 1)) {
                commitCurrentLine();
                current = word;
            } else {
                current = candidate;
            }
            word.clear();
        }

        if (forceLineBreak && preparedLineCount < MAX_WRAPPED_LINES)
            commitCurrentLine();
    };

    for (char c : text) {
        if (c == ' ') {
            appendWord(false);
        } else if (c == '\n') {
            appendWord(true);
        } else {
            word += c;
        }
    }
    appendWord(false);
    commitCurrentLine();

    if (preparedLineCount == 0) {
        preparedLines[0].text = std::move(text);
        preparedLines[0].width = getTextWidth(preparedLines[0].text);
        preparedLineCount = 1;
        preparedWrapped = false;
        preparedTextHeight = fontSmall.lineHeight();
        resetTileHeight();
        return;
    }

    const uint16_t lineGap = 2;
    preparedTextHeight = (fontSmall.lineHeight() * preparedLineCount) + (lineGap * (preparedLineCount - 1));
    const uint16_t desiredHeight = std::max<uint16_t>(DEFAULT_HEIGHT, preparedTextHeight + 4);
    tile->setRegion(tile->getLeft(), tile->getTop(), inkhud->width(), desiredHeight);
}

void InkHUD::NotificationApplet::showNotification(const Notification &n)
{
    assert(isActive());

    if (!settings->optionalFeatures.notifications)
        return;

    dismiss();

    hasNotification = true;
    currentNotification = n;
    prepareCurrentNotificationLayout();
    if (isApproved()) {
        bringToForeground();
        inkhud->forceUpdate();
    } else {
        hasNotification = false;
        clearPreparedLines();
        resetTileHeight();
    }
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

    showNotification(n);

    // Return zero: no issues here, carry on notifying other observers!
    return 0;
}

#if !MESHTASTIC_EXCLUDE_WAYPOINT
int InkHUD::NotificationApplet::onGeofenceEvent(const GeofenceNotificationEvent *event)
{
    assert(isActive());

    if (!event)
        return 0;

    Notification n;
    n.type = Notification::Type::NOTIFICATION_GEOFENCE;
    n.timestamp = getValidTime(RTCQuality::RTCQualityDevice, true);
    n.setGeofenceName(event->geofenceName);
    n.setGeofenceEntered(event->entered);
    n.setGeofenceNodeName(event->nodeName);

    showNotification(n);
    return 0;
}
#endif

void InkHUD::NotificationApplet::onRender(bool full)
{
    updateDimensions();
    setFont(fontSmall);
    if (preparedLineCount == 0)
        prepareCurrentNotificationLayout();
    if (preparedLineCount == 0)
        return;

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

    const int16_t textLeft = divX + padW;

    // Restrict area for printing
    // - don't overlap border, or divider
    setCrop(divX + 1, 1, (width() - (divX + 1) - 1), height() - 2);

    if (preparedWrapped && preparedLineCount > 0) {
        const uint16_t lineGap = 2;
        const int16_t textTop = std::max<int16_t>(1, (height() - preparedTextHeight) / 2);

        for (uint8_t i = 0; i < preparedLineCount; ++i) {
            const int16_t lineY = textTop + (i * (fontSmall.lineHeight() + lineGap)) + (fontSmall.lineHeight() / 2);
            const int16_t lineX = textLeft + (preparedLines[i].width / 2);

            setTextColor(WHITE);
            printThick(lineX, lineY, preparedLines[i].text, 4, 3);

            setTextColor(BLACK);
            printThick(lineX, lineY, preparedLines[i].text, 2, 1);
        }
    } else {
        const int16_t textM = textLeft + (preparedLines[0].width / 2);

        // Drop shadow
        // - thick white text
        setTextColor(WHITE);
        printThick(textM, height() / 2, preparedLines[0].text, 4, 4);

        // Main text
        // - faux bold: double width
        setTextColor(BLACK);
        printThick(textM, height() / 2, preparedLines[0].text, 2, 1);
    }
}

void InkHUD::NotificationApplet::onForeground()
{
    handleInput = true; // Intercept the button input for our applet, so we can dismiss the notification
}

void InkHUD::NotificationApplet::onBackground()
{
    handleInput = false;
    clearPreparedLines();
    resetTileHeight();
    inkhud->forceUpdate(EInk::UpdateTypes::FULL, true);
}

void InkHUD::NotificationApplet::onButtonShortPress()
{
    dismiss();
}

void InkHUD::NotificationApplet::onButtonLongPress()
{
    dismiss();
}

void InkHUD::NotificationApplet::onExitShort()
{
    if (dismissOnAuxInput())
        dismiss();
}

void InkHUD::NotificationApplet::onExitLong()
{
    if (dismissOnAuxInput())
        dismiss();
}

void InkHUD::NotificationApplet::onNavUp()
{
    if (dismissOnAuxInput())
        dismiss();
}

void InkHUD::NotificationApplet::onNavDown()
{
    if (dismissOnAuxInput())
        dismiss();
}

void InkHUD::NotificationApplet::onNavLeft()
{
    if (dismissOnAuxInput())
        dismiss();
}

void InkHUD::NotificationApplet::onNavRight()
{
    if (dismissOnAuxInput())
        dismiss();
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

bool InkHUD::NotificationApplet::dismissOnAuxInput() const
{
    return currentNotification.type != Notification::Type::NOTIFICATION_GEOFENCE;
}

// Mark that the notification should no-longer be rendered
// In addition to calling thing method, code needs to request a re-render of all applets
void InkHUD::NotificationApplet::dismiss()
{
    clearPreparedLines();
    resetTileHeight();
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
        bool msgIsBroadcast = currentNotification.type == Notification::Type::NOTIFICATION_MESSAGE_BROADCAST;

        // Pick source of message
        const StoredMessage *message =
            msgIsBroadcast ? &inkhud->persistence->latestMessage.broadcast : &inkhud->persistence->latestMessage.dm;

        // Find info about the sender
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(message->sender);

        // Leading tag (channel vs. DM)
        text += msgIsBroadcast ? "From:" : "DM: ";

        // Sender id
        if (nodeInfoLiteHasUser(node))
            text += parseShortName(node);
        else
            text += hexifyNodeNum(message->sender);

        // Check if text fits
        // - use a longer string, if we have the space
        if (getTextWidth(text) < widthAvailable * 0.5) {
            text.clear();

            // Leading tag (channel vs. DM)
            text += msgIsBroadcast ? "Msg from " : "DM from ";

            // Sender id
            if (nodeInfoLiteHasUser(node))
                text += parseShortName(node);
            else
                text += hexifyNodeNum(message->sender);

            text += ": ";
            text += MessageStore::getText(*message);
        }
    }

    else if (currentNotification.type == Notification::Type::NOTIFICATION_GEOFENCE) {
        text += currentNotification.getGeofenceNodeName();
        text += currentNotification.getGeofenceEntered() ? " IN " : " OUT ";
        text += currentNotification.getGeofenceName();
    }

    // Parse any non-ascii characters and return
    return parse(text);
}

#endif
