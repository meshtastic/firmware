#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./ThreadedMessageApplet.h"

#include "mesh/NodeDB.h"
#include "rtc.h"

using namespace NicheGraphics;

// Hard limits on how much message data to write to flash
// Avoid filling the storage if something goes wrong
// Normal usage should be well below this size
constexpr uint8_t MAX_MESSAGES_SAVED = 10;
constexpr uint32_t MAX_MESSAGE_SIZE = 250;

// Nested subdirectories where the messages are stored in flash at shutdown
const char *dirs[3] = {"NicheGraphics", "InkHUD", "thrMsg"};

InkHUD::ThreadedMessageApplet::ThreadedMessageApplet(uint8_t channelIndex) : channelIndex(channelIndex) {}

void InkHUD::ThreadedMessageApplet::render()
{
    setFont(fontSmall);

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
    const uint16_t msgR = (width() - 1) - padW;

    int16_t msgB = height() - 1; // Vertical cursor for drawing. Messages are bottom-aligned to this value.
    uint8_t i = 0;               // Index of stored message

    // Loop over messages
    // - until no messages left, or
    // - until no part of message fits on screen
    while (msgB >= (0 - fontSmall.lineHeight()) && i < messages.size()) {

        // Grab data for message
        StoredMessage &m = messages.at(i);
        bool outgoing = (m.sender == 0);
        meshtastic_NodeInfoLite *sender = nodeDB->getMeshNode(m.sender);

        // Cache bottom Y of message text
        // - Used when drawing vertical line alongside
        const int16_t dotsB = msgB;

        // Get dimensions for message text
        uint16_t bodyH = getWrappedTextHeight(0, width(), m.text);
        int16_t bodyT = msgB - bodyH;

        // Print message
        // - if incoming
        if (!outgoing)
            printWrapped(msgL, bodyT, msgR, m.text);

        // Print message
        // - if outgoing
        else {
            if (getTextWidth(m.text) < width())          // If short,
                printAt(msgR, bodyT, m.text, RIGHT);     // print right align
            else                                         // If long,
                printWrapped(msgL, bodyT, msgR, m.text); // need printWrapped(), which doesn't support right align
        }

        // Move cursor up
        // - above message text
        msgB -= bodyH;
        msgB -= getFont().lineHeight() * 0.2; // Padding between message and header

        // Compose info string
        // - shortname, if possible, or "me"
        // - time received, if possible
        std::string info;
        if (sender && sender->has_user)
            info += sender->user.short_name;
        else if (outgoing)
            info += "Me";
        else
            info += hexifyNodeNum(m.sender);

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
    while (i < messages.size())
        messages.pop_back();
}

// Code which runs when the applet begins running
// This might happen at boot, or if user enables the applet at run-time, via the menu
void InkHUD::ThreadedMessageApplet::onActivate()
{
    loadMessagesFromFlash();
    textMessageObserver.observe(textMessageModule); // Begin handling any new text messages with onReceiveTextMessage
}

// Code which runs when the applet stop running
// This might be happen at shutdown, or if user disables the applet at run-time
void InkHUD::ThreadedMessageApplet::onDeactivate()
{
    textMessageObserver.unobserve(textMessageModule); // Stop handling any new text messages with onReceiveTextMessage
}

// Handle new text messages
// These might be incoming, from the mesh, or outgoing from phone
// Each instance of the ThreadMessageApplet will only listen on one specific channel
// Method should return 0, to indicate general success to TextMessageModule
int InkHUD::ThreadedMessageApplet::onReceiveTextMessage(const meshtastic_MeshPacket *p)
{
    // Short circuit: applet is fully disabled
    if (!isActive())
        return 0;

    // Short circuit: wrong channel
    if (p->channel != this->channelIndex)
        return 0;

    // Short circuit: don't show DMs
    if (p->to != NODENUM_BROADCAST)
        return 0;

    // Extract info into our slimmed-down "StoredMessage" type
    StoredMessage newMessage;
    newMessage.timestamp = getValidTime(RTCQuality::RTCQualityDevice, true); // Current RTC time
    newMessage.sender = p->from;
    newMessage.text = (const char *)p->decoded.payload.bytes;

    // Store newest message at front
    // These records are used when rendering, and also stored in flash at shutdown
    messages.push_front(newMessage);

    // If Applet is currently displayed, redraw it
    if (isForeground())
        requestUpdate();

    return 0;
}

// Save several recent messages to flash
// Stores the contents of ThreadedMessageApplet::messages
// Just enough messages to fill the display
// Messages are packed "back-to-back", to minimize blocks of flash used
void InkHUD::ThreadedMessageApplet::saveMessagesToFlash()
{

#ifdef FSCom
    // Create the nested directories, if they don't already exist
    std::string dir;
    for (uint8_t i = 0; i < (sizeof(dirs) / sizeof(dirs[0])); i++) {
        dir += '/';
        dir += dirs[i];
        FSCom.mkdir(dir.c_str());
    }

    // Get a filename based on the channel index
    std::string filename = dir;
    filename += "/ch" + to_string(channelIndex) + ".dat";

    // Open or create the file
    // No "full atomic": don't save then rename
    auto f = SafeFile(filename.c_str(), false);

    LOG_INFO("Saving ch%u messages in %s", (uint32_t)channelIndex, filename.c_str());

    // 1st byte: how many messages will be written to store
    f.write(messages.size());

    // For each message
    for (uint8_t i = 0; i < messages.size() && i < MAX_MESSAGES_SAVED; i++) {
        StoredMessage &m = messages.at(i);
        f.write((uint8_t *)&m.timestamp, sizeof(m.timestamp));                    // Write timestamp. 4 bytes
        f.write((uint8_t *)&m.sender, sizeof(m.sender));                          // Write sender NodeId. 4 Bytes
        f.write((uint8_t *)m.text.c_str(), min(MAX_MESSAGE_SIZE, m.text.size())); // Write message text. Variable length
        f.write('\0');                                                            // Append null term
        LOG_DEBUG("Wrote message %u, length %u, text \"%s\"", (uint32_t)i, min(MAX_MESSAGE_SIZE, m.text.size()), m.text.c_str());
    }

    bool writeSucceeded = f.close();

    if (!writeSucceeded) {
        LOG_ERROR("Can't write data!");
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented\n");
#endif
}

// Load recent messages to flash
// Fills ThreadedMessageApplet::messages with previous messages
// Just enough messages have been stored to cover the display
void InkHUD::ThreadedMessageApplet::loadMessagesFromFlash()
{
#ifdef FSCom
    // Get a filename based on the channel index
    std::string filename;
    for (uint8_t i = 0; i < (sizeof(dirs) / sizeof(dirs[0])); i++) {
        filename += '/';
        filename += dirs[i];
    }
    filename += "/ch" + to_string(channelIndex) + ".dat";

    // Check that the file *does* actually exist
    if (!FSCom.exists(filename.c_str())) {
        LOG_INFO("'%s' not found.", filename.c_str());
        return;
    }

    // Open the file
    auto f = FSCom.open(filename.c_str(), FILE_O_READ);

    if (f.size() == 0) {
        LOG_INFO("%s is empty", filename.c_str());
        f.close();
        return;
    }

    // If opened, start reading
    if (f) {
        LOG_INFO("Loading threaded messages '%s'", filename.c_str());

        // First byte: how many messages are in the flash store
        uint8_t flashMessageCount = 0;
        f.readBytes((char *)&flashMessageCount, 1);
        LOG_DEBUG("Messages available: %u", (uint32_t)flashMessageCount);

        // For each message
        for (uint8_t i = 0; i < flashMessageCount && i < MAX_MESSAGES_SAVED; i++) {
            StoredMessage m;

            // Read meta data (fixed width)
            f.readBytes((char *)&m.timestamp, sizeof(m.timestamp));
            f.readBytes((char *)&m.sender, sizeof(m.sender));

            // Read characters until we find a null term
            char c;
            while (m.text.size() < MAX_MESSAGE_SIZE) {
                f.readBytes(&c, 1);
                if (c != '\0')
                    m.text += c;
                else
                    break;
            }

            // Store in RAM
            messages.push_back(m);

            LOG_DEBUG("#%u, timestamp=%u, sender(num)=%u, sender(id)=%s, text=\"%s\"", (uint32_t)i, m.timestamp, m.sender,
                      hexifyNodeNum(m.sender).c_str(), m.text.c_str());
        }

        f.close();
    } else {
        LOG_ERROR("Could not open / read %s", filename.c_str());
    }
#else
    LOG_ERROR("Filesystem not implemented");
    state = LoadFileState::NO_FILESYSTEM;
#endif
    return;
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