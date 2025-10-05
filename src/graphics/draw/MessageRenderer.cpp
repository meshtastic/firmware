#include "configuration.h"
#if HAS_SCREEN
#include "MessageRenderer.h"

// Core includes
#include "MessageStore.h"
#include "NodeDB.h"
#include "UIRenderer.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/TimeFormatters.h"
#include "graphics/emotes.h"
#include "main.h"
#include "meshUtils.h"
#include <string>
#include <vector>

// External declarations
extern bool hasUnreadMessage;
extern meshtastic_DeviceState devicestate;
extern graphics::Screen *screen;

using graphics::Emote;
using graphics::emotes;
using graphics::numEmotes;

namespace graphics
{
namespace MessageRenderer
{

static std::vector<std::string> cachedLines;
static std::vector<int> cachedHeights;

void drawStringWithEmotes(OLEDDisplay *display, int x, int y, const std::string &line, const Emote *emotes, int emoteCount)
{
    int cursorX = x;
    const int fontHeight = FONT_HEIGHT_SMALL;

    // Step 1: Find tallest emote in the line
    int maxIconHeight = fontHeight;
    for (size_t i = 0; i < line.length();) {
        bool matched = false;
        for (int e = 0; e < emoteCount; ++e) {
            size_t emojiLen = strlen(emotes[e].label);
            if (line.compare(i, emojiLen, emotes[e].label) == 0) {
                if (emotes[e].height > maxIconHeight)
                    maxIconHeight = emotes[e].height;
                i += emojiLen;
                matched = true;
                break;
            }
        }
        if (!matched) {
            uint8_t c = static_cast<uint8_t>(line[i]);
            if ((c & 0xE0) == 0xC0)
                i += 2;
            else if ((c & 0xF0) == 0xE0)
                i += 3;
            else if ((c & 0xF8) == 0xF0)
                i += 4;
            else
                i += 1;
        }
    }

    // Step 2: Baseline alignment
    int lineHeight = std::max(fontHeight, maxIconHeight);
    int baselineOffset = (lineHeight - fontHeight) / 2;
    int fontY = y + baselineOffset;
    int fontMidline = fontY + fontHeight / 2;

    // Step 3: Render line in segments
    size_t i = 0;
    bool inBold = false;

    while (i < line.length()) {
        // Check for ** start/end for faux bold
        if (line.compare(i, 2, "**") == 0) {
            inBold = !inBold;
            i += 2;
            continue;
        }

        // Look ahead for the next emote match
        size_t nextEmotePos = std::string::npos;
        const Emote *matchedEmote = nullptr;
        size_t emojiLen = 0;

        for (int e = 0; e < emoteCount; ++e) {
            size_t pos = line.find(emotes[e].label, i);
            if (pos != std::string::npos && (nextEmotePos == std::string::npos || pos < nextEmotePos)) {
                nextEmotePos = pos;
                matchedEmote = &emotes[e];
                emojiLen = strlen(emotes[e].label);
            }
        }

        // Render normal text segment up to the emote or bold toggle
        size_t nextControl = std::min(nextEmotePos, line.find("**", i));
        if (nextControl == std::string::npos)
            nextControl = line.length();

        if (nextControl > i) {
            std::string textChunk = line.substr(i, nextControl - i);
            if (inBold) {
                // Faux bold: draw twice, offset by 1px
                display->drawString(cursorX + 1, fontY, textChunk.c_str());
            }
            display->drawString(cursorX, fontY, textChunk.c_str());
#if defined(OLED_UA) || defined(OLED_RU)
            cursorX += display->getStringWidth(textChunk.c_str(), textChunk.length(), true);
#else
            cursorX += display->getStringWidth(textChunk.c_str());
#endif
            i = nextControl;
            continue;
        }

        // Render the emote (if found)
        if (matchedEmote && i == nextEmotePos) {
            // Center vertically — padding handled in calculateLineHeights
            int iconY = fontMidline - matchedEmote->height / 2;
            display->drawXbm(cursorX, iconY, matchedEmote->width, matchedEmote->height, matchedEmote->bitmap);
            cursorX += matchedEmote->width + 1;
            i += emojiLen;
        } else {
            // No more emotes — render the rest of the line
            std::string remaining = line.substr(i);
            if (inBold) {
                display->drawString(cursorX + 1, fontY, remaining.c_str());
            }
            display->drawString(cursorX, fontY, remaining.c_str());
#if defined(OLED_UA) || defined(OLED_RU)
            cursorX += display->getStringWidth(remaining.c_str(), remaining.length(), true);
#else
            cursorX += display->getStringWidth(remaining.c_str());
#endif

            break;
        }
    }
}

// Scroll state (file scope so we can reset on new message)
float scrollY = 0.0f;
uint32_t lastTime = 0;
uint32_t scrollStartDelay = 0;
uint32_t pauseStart = 0;
bool waitingToReset = false;
bool scrollStarted = false;
static bool didReset = false; // <-- add here

// Reset scroll state when new messages arrive
void resetScrollState()
{
    scrollY = 0.0f;
    scrollStarted = false;
    waitingToReset = false;
    scrollStartDelay = millis();
    lastTime = millis();

    didReset = false; // <-- now valid
}
// Current thread state
static ThreadMode currentMode = ThreadMode::ALL;
static int currentChannel = -1;
static uint32_t currentPeer = 0;

// Registry of seen threads for manual toggle
static std::vector<int> seenChannels;
static std::vector<uint32_t> seenPeers;

// Public helper so menus / store can clear stale registries
void clearThreadRegistries()
{
    LOG_DEBUG("[MessageRenderer] Clearing thread registries (seenChannels/seenPeers)");
    seenChannels.clear();
    seenPeers.clear();
}

// Setter so other code can switch threads
void setThreadMode(ThreadMode mode, int channel /* = -1 */, uint32_t peer /* = 0 */)
{
    LOG_DEBUG("[MessageRenderer] setThreadMode(mode=%d, ch=%d, peer=0x%08x)", (int)mode, channel, (unsigned int)peer);
    currentMode = mode;
    currentChannel = channel;
    currentPeer = peer;
    didReset = false; // force reset when mode changes

    // Track channels we’ve seen
    if (mode == ThreadMode::CHANNEL && channel >= 0) {
        if (std::find(seenChannels.begin(), seenChannels.end(), channel) == seenChannels.end()) {
            LOG_DEBUG("[MessageRenderer] Track seen channel: %d", channel);
            seenChannels.push_back(channel);
        }
    }

    // Track DMs we’ve seen
    if (mode == ThreadMode::DIRECT && peer != 0) {
        if (std::find(seenPeers.begin(), seenPeers.end(), peer) == seenPeers.end()) {
            LOG_DEBUG("[MessageRenderer] Track seen peer: 0x%08x", (unsigned int)peer);
            seenPeers.push_back(peer);
        }
    }
}

ThreadMode getThreadMode()
{
    return currentMode;
}

int getThreadChannel()
{
    return currentChannel;
}

uint32_t getThreadPeer()
{
    return currentPeer;
}

// Accessors for menuHandler
const std::vector<int> &getSeenChannels()
{
    return seenChannels;
}
const std::vector<uint32_t> &getSeenPeers()
{
    return seenPeers;
}

// Helpers for drawing status marks (thickened strokes)
void drawCheckMark(OLEDDisplay *display, int x, int y, int size = 8)
{
    int h = size;
    int w = size;

    // Center mark vertically with the text row
    int midY = y + (FONT_HEIGHT_SMALL / 2);
    int topY = midY - (h / 2);

    display->setColor(WHITE); // ensure we use current fg

    // Draw thicker checkmark by overdrawing lines with 1px offset
    // arm 1
    display->drawLine(x, topY + h / 2, x + w / 3, topY + h);
    display->drawLine(x, topY + h / 2 + 1, x + w / 3, topY + h + 1);
    // arm 2
    display->drawLine(x + w / 3, topY + h, x + w, topY);
    display->drawLine(x + w / 3, topY + h + 1, x + w, topY + 1);
}

void drawXMark(OLEDDisplay *display, int x, int y, int size = 8)
{
    int h = size;
    int w = size;

    // Center mark vertically with the text row
    int midY = y + (FONT_HEIGHT_SMALL / 2);
    int topY = midY - (h / 2);

    display->setColor(WHITE);

    // Draw thicker X with 1px offset
    display->drawLine(x, topY, x + w, topY + h);
    display->drawLine(x, topY + 1, x + w, topY + h + 1);
    display->drawLine(x + w, topY, x, topY + h);
    display->drawLine(x + w, topY + 1, x, topY + h + 1);
}

void drawRelayMark(OLEDDisplay *display, int x, int y, int size = 8)
{
    int r = size / 2;
    int midY = y + (FONT_HEIGHT_SMALL / 2);
    int centerY = midY;
    int centerX = x + r;

    display->setColor(WHITE);

    // Draw circle outline (relay = uncertain status)
    display->drawCircle(centerX, centerY, r);

    // Draw "?" inside (approx, 3px wide)
    display->drawLine(centerX, centerY - 2, centerX, centerY); // stem
    display->setPixel(centerX, centerY + 2);                   // dot
    display->drawLine(centerX - 1, centerY - 4, centerX + 1, centerY - 4);
}

void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Ensure any boot-relative timestamps are upgraded if RTC is valid
    messageStore.upgradeBootRelativeTimestamps();

    if (!didReset) {
        resetScrollState();
        didReset = true;
    }

    // Clear the unread message indicator when viewing the message
    hasUnreadMessage = false;

    // Filter messages based on thread mode
    std::deque<StoredMessage> filtered;
    for (const auto &m : messageStore.getMessages()) {
        bool include = false;
        switch (currentMode) {
        case ThreadMode::ALL:
            include = true;
            break;
        case ThreadMode::CHANNEL:
            if (m.type == MessageType::BROADCAST && (int)m.channelIndex == currentChannel)
                include = true;
            break;
        case ThreadMode::DIRECT:
            if (m.type == MessageType::DM_TO_US && (m.sender == currentPeer || m.dest == currentPeer))
                include = true;
            break;
        }
        if (include)
            filtered.push_back(m);
    }

    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
#if defined(M5STACK_UNITC6L)
    const int fixedTopHeight = 24;
    const int windowX = 0;
    const int windowY = fixedTopHeight;
    const int windowWidth = 64;
    const int windowHeight = SCREEN_HEIGHT - fixedTopHeight;
#else
    const int navHeight = FONT_HEIGHT_SMALL;
    const int scrollBottom = SCREEN_HEIGHT - navHeight;
    const int usableHeight = scrollBottom;
    const int textWidth = SCREEN_WIDTH;
#endif

    // Title string depending on mode
    static char titleBuf[32];
    const char *titleStr = "Messages";
    switch (currentMode) {
    case ThreadMode::ALL:
        titleStr = "Messages";
        break;
    case ThreadMode::CHANNEL: {
        const char *cname = channels.getName(currentChannel);
        if (cname && cname[0]) {
            snprintf(titleBuf, sizeof(titleBuf), "#%s", cname);
        } else {
            snprintf(titleBuf, sizeof(titleBuf), "Ch%d", currentChannel);
        }
        titleStr = titleBuf;
        break;
    }
    case ThreadMode::DIRECT: {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(currentPeer);
        if (node && node->has_user) {
            snprintf(titleBuf, sizeof(titleBuf), "@%s", node->user.short_name);
        } else {
            snprintf(titleBuf, sizeof(titleBuf), "@%08x", currentPeer);
        }
        titleStr = titleBuf;
        break;
    }
    }

    if (filtered.empty()) {
        graphics::drawCommonHeader(display, x, y, titleStr);
        didReset = false;
        const char *messageString = "No messages";
        int center_text = (SCREEN_WIDTH / 2) - (display->getStringWidth(messageString) / 2);
#if defined(M5STACK_UNITC6L)
        display->drawString(center_text, windowY + (windowHeight / 2) - (FONT_HEIGHT_SMALL / 2) - 5, messageString);
#else
        display->drawString(center_text, getTextPositions(display)[2], messageString);
#endif
        return;
    }

    // Build lines for filtered messages (newest first)
    std::vector<std::string> allLines;
    std::vector<bool> isMine;   // track alignment
    std::vector<bool> isHeader; // track header lines
    std::vector<AckStatus> ackForLine;

    for (auto it = filtered.rbegin(); it != filtered.rend(); ++it) {
        const auto &m = *it;

        // Build header line for this message
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(m.sender);
        const char *sender = "???";
#if defined(M5STACK_UNITC6L)
        if (node && node->has_user)
            sender = node->user.short_name;
#else
        if (node && node->has_user) {
            if (SCREEN_WIDTH >= 200 && strlen(node->user.long_name) > 0) {
                sender = node->user.long_name;
            } else {
                sender = node->user.short_name;
            }
        }
#endif

        // If this is *our own* message, override sender to "Me"
        bool mine = (m.sender == nodeDB->getNodeNum());
        if (mine) {
            sender = "Me";
        }

        // Channel / destination labeling
        char chanType[32] = "";
        if (currentMode == ThreadMode::ALL) {
            if (m.dest == NODENUM_BROADCAST) {
                snprintf(chanType, sizeof(chanType), "(Ch%d)", m.channelIndex);
            } else {
                snprintf(chanType, sizeof(chanType), "(DM)");
            }
        }

        // Calculate how long ago
        uint32_t nowSecs = getValidTime(RTCQuality::RTCQualityDevice, true);
        uint32_t seconds = 0;
        bool invalidTime = true;

        if (m.timestamp > 0 && nowSecs > 0) {
            if (nowSecs >= m.timestamp) {
                seconds = nowSecs - m.timestamp;
                invalidTime = (seconds > 315360000); // >10 years
            } else {
                uint32_t ahead = m.timestamp - nowSecs;
                if (ahead <= 600) { // allow small skew
                    seconds = 0;
                    invalidTime = false;
                }
            }
        } else if (m.timestamp > 0 && nowSecs == 0) {
            // RTC not valid: only trust boot-relative if same boot
            uint32_t bootNow = millis() / 1000;
            if (m.isBootRelative && m.timestamp <= bootNow) {
                seconds = bootNow - m.timestamp;
                invalidTime = false;
            } else {
                invalidTime = true; // old persisted boot-relative, ignore until healed
            }
        }

        char timeBuf[16];
        if (invalidTime) {
            snprintf(timeBuf, sizeof(timeBuf), "???");
        } else if (seconds < 60) {
            snprintf(timeBuf, sizeof(timeBuf), "%us ago", seconds);
        } else if (seconds < 3600) {
            snprintf(timeBuf, sizeof(timeBuf), "%um ago", seconds / 60);
        } else if (seconds < 86400) {
            snprintf(timeBuf, sizeof(timeBuf), "%uh ago", seconds / 3600);
        } else {
            snprintf(timeBuf, sizeof(timeBuf), "%ud ago", seconds / 86400);
        }

        // Final header line
        char headerStr[96];
        if (mine) {
            if (m.ackStatus == AckStatus::ACKED) {
                // Destination ACK
                snprintf(headerStr, sizeof(headerStr), "Sent %s %s", timeBuf, chanType);
            } else if (m.ackStatus == AckStatus::NACKED || m.ackStatus == AckStatus::TIMEOUT) {
                // Failure or timeout
                snprintf(headerStr, sizeof(headerStr), "Failed %s %s", timeBuf, chanType);
            } else if (m.ackStatus == AckStatus::RELAYED) {
                // Relay ACK
                snprintf(headerStr, sizeof(headerStr), "Relayed %s %s", timeBuf, chanType);
            }
        } else {
            snprintf(headerStr, sizeof(headerStr), "%s @%s %s", timeBuf, sender, chanType);
        }

        // Push header line
        allLines.push_back(std::string(headerStr));
        isMine.push_back(mine);
        isHeader.push_back(true);
        ackForLine.push_back(m.ackStatus);

        // Split message text into wrapped lines
        std::vector<std::string> wrapped = generateLines(display, "", m.text.c_str(), textWidth);
        for (auto &ln : wrapped) {
            allLines.push_back(ln);
            isMine.push_back(mine);
            isHeader.push_back(false);
            ackForLine.push_back(AckStatus::NONE);
        }
    }

    // Cache lines and heights
    cachedLines = allLines;
    cachedHeights = calculateLineHeights(cachedLines, emotes);

    // Scrolling logic (unchanged)
    uint32_t now = millis();
    int totalHeight = 0;
    for (size_t i = 0; i < cachedHeights.size(); ++i)
        totalHeight += cachedHeights[i];
    int usableScrollHeight = usableHeight;
    int scrollStop = std::max(0, totalHeight - usableScrollHeight + cachedHeights.back());

    float delta = (now - lastTime) / 400.0f;
    lastTime = now;
    const float scrollSpeed = 2.0f;

    if (scrollStartDelay == 0)
        scrollStartDelay = now;
    if (!scrollStarted && now - scrollStartDelay > 2000)
        scrollStarted = true;

    if (totalHeight > usableScrollHeight) {
        if (scrollStarted) {
            if (!waitingToReset) {
                scrollY += delta * scrollSpeed;
                if (scrollY >= scrollStop) {
                    scrollY = scrollStop;
                    waitingToReset = true;
                    pauseStart = lastTime;
                }
            } else if (lastTime - pauseStart > 3000) {
                scrollY = 0;
                waitingToReset = false;
                scrollStarted = false;
                scrollStartDelay = lastTime;
            }
        }
    } else {
        scrollY = 0;
    }

    int scrollOffset = static_cast<int>(scrollY);
    int yOffset = -scrollOffset + getTextPositions(display)[1];

    // Render visible lines
    for (size_t i = 0; i < cachedLines.size(); ++i) {
        int lineY = yOffset;
        for (size_t j = 0; j < i; ++j)
            lineY += cachedHeights[j];

        if (lineY > -cachedHeights[i] && lineY < scrollBottom) {
            if (isHeader[i]) {
                // Render header
                int w = display->getStringWidth(cachedLines[i].c_str());
                int headerX = isMine[i] ? (SCREEN_WIDTH - w - 2) : x;
                display->drawString(headerX, lineY, cachedLines[i].c_str());

                // Draw ACK/NACK mark for our own messages
                if (isMine[i]) {
                    int markX = headerX - 10;
                    int markY = lineY;
                    if (ackForLine[i] == AckStatus::ACKED) {
                        // Destination ACK
                        drawCheckMark(display, markX, markY, 8);
                    } else if (ackForLine[i] == AckStatus::NACKED || ackForLine[i] == AckStatus::TIMEOUT) {
                        // Failure or timeout
                        drawXMark(display, markX, markY, 8);
                    } else if (ackForLine[i] == AckStatus::RELAYED) {
                        // Relay ACK
                        drawRelayMark(display, markX, markY, 8);
                    }
                    // AckStatus::NONE → show nothing
                }

                // Draw underline just under header text
                int underlineY = lineY + FONT_HEIGHT_SMALL;
                for (int px = 0; px < w; ++px) {
                    display->setPixel(headerX + px, underlineY);
                }
            } else {
                // Render message line
                if (isMine[i]) {
                    int w = display->getStringWidth(cachedLines[i].c_str());
                    int rightX = SCREEN_WIDTH - w - 2;
                    drawStringWithEmotes(display, rightX, lineY, cachedLines[i], emotes, numEmotes);
                } else {
                    drawStringWithEmotes(display, x, lineY, cachedLines[i], emotes, numEmotes);
                }
            }
        }
    }

    graphics::drawCommonHeader(display, x, y, titleStr);
}

std::vector<std::string> generateLines(OLEDDisplay *display, const char *headerStr, const char *messageBuf, int textWidth)
{
    std::vector<std::string> lines;

    // Only push headerStr if it's not empty (prevents extra blank line after headers)
    if (headerStr && headerStr[0] != '\0') {
        lines.push_back(std::string(headerStr));
    }

    std::string line, word;
    for (int i = 0; messageBuf[i]; ++i) {
        char ch = messageBuf[i];
        if ((unsigned char)messageBuf[i] == 0xE2 && (unsigned char)messageBuf[i + 1] == 0x80 &&
            (unsigned char)messageBuf[i + 2] == 0x99) {
            ch = '\''; // plain apostrophe
            i += 2;    // skip over the extra UTF-8 bytes
        }
        if (ch == '\n') {
            if (!word.empty())
                line += word;
            if (!line.empty())
                lines.push_back(line);
            line.clear();
            word.clear();
        } else if (ch == ' ') {
            line += word + ' ';
            word.clear();
        } else {
            word += ch;
            std::string test = line + word;
#if defined(OLED_UA) || defined(OLED_RU)
            uint16_t strWidth = display->getStringWidth(test.c_str(), test.length(), true);
#else
            uint16_t strWidth = display->getStringWidth(test.c_str());
#endif
            if (strWidth > textWidth) {
                if (!line.empty())
                    lines.push_back(line);
                line = word;
                word.clear();
            }
        }
    }

    if (!word.empty())
        line += word;
    if (!line.empty())
        lines.push_back(line);

    return lines;
}

std::vector<int> calculateLineHeights(const std::vector<std::string> &lines, const Emote *emotes)
{
    std::vector<int> rowHeights;

    for (size_t idx = 0; idx < lines.size(); ++idx) {
        const auto &_line = lines[idx];
        int lineHeight = FONT_HEIGHT_SMALL;
        bool hasEmote = false;
        bool isHeader = false;

        // Detect emotes in this line
        for (int i = 0; i < numEmotes; ++i) {
            const Emote &e = emotes[i];
            if (_line.find(e.label) != std::string::npos) {
                lineHeight = std::max(lineHeight, e.height);
                hasEmote = true;
            }
        }

        // Detect header lines (start of a message, or time stamps like "5m ago")
        if (idx == 0 || _line.find("ago") != std::string::npos || _line.rfind("me ", 0) == 0) {
            isHeader = true;
        }

        // Look ahead to see if next line is a header → this is the last line of a message
        bool beforeHeader =
            (idx + 1 < lines.size() && (lines[idx + 1].find("ago") != std::string::npos || lines[idx + 1].rfind("me ", 0) == 0));

        if (isHeader) {
            // Headers always keep full line height
            lineHeight = FONT_HEIGHT_SMALL;
        } else if (beforeHeader) {
            if (hasEmote) {
                // Last line has emote → preserve its height + padding
                lineHeight = std::max(lineHeight, FONT_HEIGHT_SMALL) + 4;
            } else {
                // Plain last line → full spacing only
                lineHeight = FONT_HEIGHT_SMALL;
            }
        } else if (!hasEmote) {
            // Plain body line, tighter spacing
            lineHeight -= 4;
            if (lineHeight < 8)
                lineHeight = 8; // safe minimum
        } else {
            // Line has emotes, don’t compress
            lineHeight += 4; // add breathing room
        }

        rowHeights.push_back(lineHeight);
    }

    return rowHeights;
}

void renderMessageContent(OLEDDisplay *display, const std::vector<std::string> &lines, const std::vector<int> &rowHeights, int x,
                          int yOffset, int scrollBottom, const Emote *emotes, int numEmotes, bool isInverted, bool isBold)
{
    for (size_t i = 0; i < lines.size(); ++i) {
        int lineY = yOffset;
        for (size_t j = 0; j < i; ++j)
            lineY += rowHeights[j];
        if (lineY > -rowHeights[i] && lineY < scrollBottom) {
            if (i == 0 && isInverted) {
                display->drawString(x, lineY, lines[i].c_str());
                if (isBold)
                    display->drawString(x, lineY, lines[i].c_str());
            } else {
                drawStringWithEmotes(display, x, lineY, lines[i], emotes, numEmotes);
            }
        }
    }
}

void handleNewMessage(const StoredMessage &sm, const meshtastic_MeshPacket &packet)
{
    if (packet.from != 0) {
        hasUnreadMessage = true;

        if (shouldWakeOnReceivedMessage()) {
            screen->setOn(true);
            // screen->forceDisplay();  <-- remove, let Screen handle this
        }

        // Banner logic
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(packet.from);
        const char *longName = (node && node->has_user) ? node->user.long_name : nullptr;
        const char *msgRaw = reinterpret_cast<const char *>(packet.decoded.payload.bytes);

        char banner[256];
        bool isAlert = false;
        for (size_t i = 0; i < packet.decoded.payload.size && i < 100; i++) {
            if (msgRaw[i] == '\x07') {
                isAlert = true;
                break;
            }
        }

        if (isAlert) {
            if (longName && longName[0])
                snprintf(banner, sizeof(banner), "Alert Received from\n%s", longName);
            else
                strcpy(banner, "Alert Received");
        } else {
            if (longName && longName[0]) {
#if defined(M5STACK_UNITC6L)
                strcpy(banner, "New Message");
#else
                snprintf(banner, sizeof(banner), "New Message from\n%s", longName);
#endif
            } else
                strcpy(banner, "New Message");
        }

        // Append context (which channel or DM) so the banner shows where the message arrived
        {
            char contextBuf[64] = "";
            if (sm.type == MessageType::BROADCAST) {
                const char *cname = channels.getName(sm.channelIndex);
                if (cname && cname[0])
                    snprintf(contextBuf, sizeof(contextBuf), "in #%s", cname);
                else
                    snprintf(contextBuf, sizeof(contextBuf), "in Ch%d", sm.channelIndex);
            } else if (sm.type == MessageType::DM_TO_US) {
                /* Commenting out to better understand if we need this info in the banner
                uint32_t peer = (packet.from == 0) ? sm.dest : sm.sender;
                const meshtastic_NodeInfoLite *peerNode = nodeDB->getMeshNode(peer);
                if (peerNode && peerNode->has_user && peerNode->user.short_name)
                    snprintf(contextBuf, sizeof(contextBuf), "Direct: @%s", peerNode->user.short_name);
                else
                    snprintf(contextBuf, sizeof(contextBuf), "Direct Message");
                */
            }

            if (contextBuf[0]) {
                size_t cur = strlen(banner);
                if (cur + 1 < sizeof(banner)) {
                    if (cur > 0 && banner[cur - 1] != '\n') {
                        banner[cur] = '\n';
                        banner[cur + 1] = '\0';
                        cur++;
                    }
                    strncat(banner, contextBuf, sizeof(banner) - cur - 1);
                }
            }
        }

        // Shorter banner if already in a conversation (Channel or Direct)
        bool inThread = (getThreadMode() != ThreadMode::ALL);

#if defined(M5STACK_UNITC6L)
        screen->setOn(true);
        screen->showSimpleBanner(banner, inThread ? 1000 : 1500);
        playLongBeep();
#else
        screen->showSimpleBanner(banner, inThread ? 1000 : 3000);
#endif
    }

    // No setFrames() here anymore
    setThreadFor(sm, packet);
    resetScrollState();
}

void setThreadFor(const StoredMessage &sm, const meshtastic_MeshPacket &packet)
{
    if (sm.type == MessageType::BROADCAST) {
        setThreadMode(ThreadMode::CHANNEL, sm.channelIndex);
    } else if (sm.type == MessageType::DM_TO_US) {
        uint32_t peer = (packet.from == 0) ? sm.dest : sm.sender;
        setThreadMode(ThreadMode::DIRECT, -1, peer);
    }
}

} // namespace MessageRenderer
} // namespace graphics
#endif