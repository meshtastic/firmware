#include "configuration.h"
#if HAS_SCREEN
#include "MessageRenderer.h"

// Core includes
#include "MessageStore.h"
#include "NodeDB.h"
#include "UIRenderer.h"
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
static bool manualScrolling = false;

// UTF-8 skip helper
static inline size_t utf8CharLen(uint8_t c)
{
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1;
}

// Remove variation selectors (FE0F) and skin tone modifiers from emoji so they match your labels
static std::string normalizeEmoji(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size();) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        size_t len = utf8CharLen(c);

        if (c == 0xEF && i + 2 < s.size() && (uint8_t)s[i + 1] == 0xB8 && (uint8_t)s[i + 2] == 0x8F) {
            i += 3;
            continue;
        }

        // Skip skin tone modifiers
        if (c == 0xF0 && i + 3 < s.size() && (uint8_t)s[i + 1] == 0x9F && (uint8_t)s[i + 2] == 0x8F &&
            ((uint8_t)s[i + 3] >= 0xBB && (uint8_t)s[i + 3] <= 0xBF)) {
            i += 4;
            continue;
        }

        out.append(s, i, len);
        i += len;
    }
    return out;
}

// Scroll state (file scope so we can reset on new message)
float scrollY = 0.0f;
uint32_t lastTime = 0;
uint32_t scrollStartDelay = 0;
uint32_t pauseStart = 0;
bool waitingToReset = false;
bool scrollStarted = false;
static bool didReset = false;
static constexpr int MESSAGE_BLOCK_GAP = 6;

void scrollUp()
{
    manualScrolling = true;
    scrollY -= 12;
    if (scrollY < 0)
        scrollY = 0;
}

void scrollDown()
{
    manualScrolling = true;

    int totalHeight = 0;
    for (int h : cachedHeights)
        totalHeight += h;

    int visibleHeight = screen->getHeight() - (FONT_HEIGHT_SMALL * 2);
    int maxScroll = totalHeight - visibleHeight;
    if (maxScroll < 0)
        maxScroll = 0;

    scrollY += 12;
    if (scrollY > maxScroll)
        scrollY = maxScroll;
}

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
            i += utf8CharLen(static_cast<uint8_t>(line[i]));
        }
    }

    // Step 2: Baseline alignment
    int lineHeight = std::max(fontHeight, maxIconHeight);
    int baselineOffset = (lineHeight - fontHeight) / 2;
    int fontY = y + baselineOffset;

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
            int iconY = y + (lineHeight - matchedEmote->height) / 2;
            display->drawXbm(cursorX, iconY, matchedEmote->width, matchedEmote->height, matchedEmote->bitmap);
            cursorX += matchedEmote->width + 1;
            i += emojiLen;
            continue;
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

// Reset scroll state when new messages arrive
void resetScrollState()
{
    scrollY = 0.0f;
    scrollStarted = false;
    waitingToReset = false;
    scrollStartDelay = millis();
    lastTime = millis();
    manualScrolling = false;
    didReset = false;
}

void nudgeScroll(int8_t direction)
{
    if (direction == 0)
        return;

    if (cachedHeights.empty()) {
        scrollY = 0.0f;
        return;
    }

    OLEDDisplay *display = (screen != nullptr) ? screen->getDisplayDevice() : nullptr;
    const int displayHeight = display ? display->getHeight() : 64;
    const int navHeight = FONT_HEIGHT_SMALL;
    const int usableHeight = std::max(0, displayHeight - navHeight);

    int totalHeight = 0;
    for (int h : cachedHeights)
        totalHeight += h;

    if (totalHeight <= usableHeight) {
        scrollY = 0.0f;
        return;
    }

    const int scrollStop = std::max(0, totalHeight - usableHeight + cachedHeights.back());
    const int step = std::max(FONT_HEIGHT_SMALL, usableHeight / 3);

    float newScroll = scrollY + static_cast<float>(direction) * static_cast<float>(step);
    if (newScroll < 0.0f)
        newScroll = 0.0f;
    if (newScroll > scrollStop)
        newScroll = static_cast<float>(scrollStop);

    if (newScroll != scrollY) {
        scrollY = newScroll;
        waitingToReset = false;
        scrollStarted = false;
        scrollStartDelay = millis();
        lastTime = millis();
    }
}

// Fully free cached message data from heap
void clearMessageCache()
{
    std::vector<std::string>().swap(cachedLines);
    std::vector<int>().swap(cachedHeights);

    // Reset scroll so we rebuild cleanly next time we enter the screen
    resetScrollState();
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
    seenChannels.clear();
    seenPeers.clear();
}

// Setter so other code can switch threads
void setThreadMode(ThreadMode mode, int channel /* = -1 */, uint32_t peer /* = 0 */)
{
    currentMode = mode;
    currentChannel = channel;
    currentPeer = peer;
    didReset = false; // force reset when mode changes

    // Track channels we’ve seen
    if (mode == ThreadMode::CHANNEL && channel >= 0) {
        if (std::find(seenChannels.begin(), seenChannels.end(), channel) == seenChannels.end()) {
            seenChannels.push_back(channel);
        }
    }

    // Track DMs we’ve seen
    if (mode == ThreadMode::DIRECT && peer != 0) {
        if (std::find(seenPeers.begin(), seenPeers.end(), peer) == seenPeers.end()) {
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

static int centerYForRow(int y, int size)
{
    int midY = y + (FONT_HEIGHT_SMALL / 2);
    return midY - (size / 2);
}

// Helpers for drawing status marks (thickened strokes)
static void drawCheckMark(OLEDDisplay *display, int x, int y, int size)
{
    int topY = centerYForRow(y, size);
    display->setColor(WHITE);
    display->drawLine(x, topY + size / 2, x + size / 3, topY + size);
    display->drawLine(x, topY + size / 2 + 1, x + size / 3, topY + size + 1);
    display->drawLine(x + size / 3, topY + size, x + size, topY);
    display->drawLine(x + size / 3, topY + size + 1, x + size, topY + 1);
}

static void drawXMark(OLEDDisplay *display, int x, int y, int size = 8)
{
    int topY = centerYForRow(y, size);
    display->setColor(WHITE);
    display->drawLine(x, topY, x + size, topY + size);
    display->drawLine(x, topY + 1, x + size, topY + size + 1);
    display->drawLine(x + size, topY, x, topY + size);
    display->drawLine(x + size, topY + 1, x, topY + size + 1);
}

static void drawRelayMark(OLEDDisplay *display, int x, int y, int size = 8)
{
    int r = size / 2;
    int centerY = centerYForRow(y, size) + r;
    int centerX = x + r;
    display->setColor(WHITE);
    display->drawCircle(centerX, centerY, r);
    display->drawLine(centerX, centerY - 2, centerX, centerY);
    display->setPixel(centerX, centerY + 2);
    display->drawLine(centerX - 1, centerY - 4, centerX + 1, centerY - 4);
}

static inline int getRenderedLineWidth(OLEDDisplay *display, const std::string &line, const Emote *emotes, int emoteCount)
{
    std::string normalized = normalizeEmoji(line);
    int totalWidth = 0;

    size_t i = 0;
    while (i < normalized.length()) {
        bool matched = false;
        for (int e = 0; e < emoteCount; ++e) {
            size_t emojiLen = strlen(emotes[e].label);
            if (normalized.compare(i, emojiLen, emotes[e].label) == 0) {
                totalWidth += emotes[e].width + 1; // +1 spacing
                i += emojiLen;
                matched = true;
                break;
            }
        }
        if (!matched) {
            size_t charLen = utf8CharLen(static_cast<uint8_t>(normalized[i]));
#if defined(OLED_UA) || defined(OLED_RU)
            totalWidth += display->getStringWidth(normalized.substr(i, charLen).c_str(), charLen, true);
#else
            totalWidth += display->getStringWidth(normalized.substr(i, charLen).c_str());
#endif
            i += charLen;
        }
    }
    return totalWidth;
}

struct MessageBlock {
    size_t start;
    size_t end;
    bool mine;
};

static int getDrawnLinePixelBottom(int lineTopY, const std::string &line, bool isHeaderLine)
{
    if (isHeaderLine) {
        return lineTopY + (FONT_HEIGHT_SMALL - 1);
    }

    int tallest = FONT_HEIGHT_SMALL;
    for (int e = 0; e < numEmotes; ++e) {
        if (line.find(emotes[e].label) != std::string::npos) {
            if (emotes[e].height > tallest)
                tallest = emotes[e].height;
        }
    }

    const int lineHeight = std::max(FONT_HEIGHT_SMALL, tallest);
    const int iconTop = lineTopY + (lineHeight - tallest) / 2;

    return iconTop + tallest - 1;
}

static void drawRoundedRectOutline(OLEDDisplay *display, int x, int y, int w, int h, int r)
{
    if (w <= 1 || h <= 1)
        return;

    if (r < 0)
        r = 0;

    int maxR = (std::min(w, h) / 2) - 1;
    if (r > maxR)
        r = maxR;

    if (r == 0) {
        display->drawRect(x, y, w, h);
        return;
    }

    const int x0 = x;
    const int y0 = y;
    const int x1 = x + w - 1;
    const int y1 = y + h - 1;

    // sides
    if (x0 + r <= x1 - r) {
        display->drawLine(x0 + r, y0, x1 - r, y0); // top
        display->drawLine(x0 + r, y1, x1 - r, y1); // bottom
    }
    if (y0 + r <= y1 - r) {
        display->drawLine(x0, y0 + r, x0, y1 - r); // left
        display->drawLine(x1, y0 + r, x1, y1 - r); // right
    }

    // corner arcs
    display->drawCircleQuads(x0 + r, y0 + r, r, 2); // top left
    display->drawCircleQuads(x1 - r, y0 + r, r, 1); // top right
    display->drawCircleQuads(x1 - r, y1 - r, r, 8); // bottom right
    display->drawCircleQuads(x0 + r, y1 - r, r, 4); // bottom left
}

static std::vector<MessageBlock> buildMessageBlocks(const std::vector<bool> &isHeaderVec, const std::vector<bool> &isMineVec)
{
    std::vector<MessageBlock> blocks;
    if (isHeaderVec.empty())
        return blocks;

    size_t start = 0;
    bool mine = isMineVec[0];

    for (size_t i = 1; i < isHeaderVec.size(); ++i) {
        if (isHeaderVec[i]) {
            MessageBlock b;
            b.start = start;
            b.end = i - 1;
            b.mine = mine;
            blocks.push_back(b);

            start = i;
            mine = isMineVec[i];
        }
    }

    MessageBlock last;
    last.start = start;
    last.end = isHeaderVec.size() - 1;
    last.mine = mine;
    blocks.push_back(last);

    return blocks;
}

static void drawMessageScrollbar(OLEDDisplay *display, int visibleHeight, int totalHeight, int scrollOffset, int startY)
{
    if (totalHeight <= visibleHeight)
        return; // no scrollbar needed

    int scrollbarX = display->getWidth() - 2;
    int scrollbarHeight = visibleHeight;
    int thumbHeight = std::max(6, (scrollbarHeight * visibleHeight) / totalHeight);
    int maxScroll = std::max(1, totalHeight - visibleHeight);
    int thumbY = startY + (scrollbarHeight - thumbHeight) * scrollOffset / maxScroll;

    for (int i = 0; i < thumbHeight; i++) {
        display->setPixel(scrollbarX, thumbY + i);
    }
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
    for (const auto &m : messageStore.getLiveMessages()) {
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
            if (m.dest != NODENUM_BROADCAST && (m.sender == currentPeer || m.dest == currentPeer))
                include = true;
            break;
        }
        if (include)
            filtered.push_back(m);
    }

    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    const int navHeight = FONT_HEIGHT_SMALL;
    const int scrollBottom = SCREEN_HEIGHT - navHeight;
    const int usableHeight = scrollBottom;
    constexpr int LEFT_MARGIN = 2;
    constexpr int RIGHT_MARGIN = 2;
    constexpr int SCROLLBAR_WIDTH = 3;
    constexpr int BUBBLE_PAD_X = 3;
    constexpr int BUBBLE_PAD_Y = 4;
    constexpr int BUBBLE_RADIUS = 4;
    constexpr int BUBBLE_MIN_W = 24;
    constexpr int BUBBLE_TEXT_INDENT = 2;

    // Derived widths
    const int leftTextWidth = SCREEN_WIDTH - LEFT_MARGIN - RIGHT_MARGIN - (BUBBLE_PAD_X * 2);
    const int rightTextWidth = SCREEN_WIDTH - LEFT_MARGIN - RIGHT_MARGIN - SCROLLBAR_WIDTH;

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
        // If current conversation is empty go back to ALL view
        if (currentMode != ThreadMode::ALL) {
            setThreadMode(ThreadMode::ALL);
            resetScrollState();
            return; // Next draw will rerun in ALL mode
        }

        // Still in ALL mode and no messages at all → show placeholder
        graphics::drawCommonHeader(display, x, y, titleStr);
        didReset = false;
        const char *messageString = "No messages";
        int center_text = (SCREEN_WIDTH / 2) - (display->getStringWidth(messageString) / 2);
        display->drawString(center_text, getTextPositions(display)[2], messageString);
        graphics::drawCommonFooter(display, x, y);
        return;
    }

    // Build lines for filtered messages (newest first)
    std::vector<std::string> allLines;
    std::vector<bool> isMine;   // track alignment
    std::vector<bool> isHeader; // track header lines
    std::vector<AckStatus> ackForLine;

    for (auto it = filtered.rbegin(); it != filtered.rend(); ++it) {
        const auto &m = *it;

        // Channel / destination labeling
        char chanType[32] = "";
        if (currentMode == ThreadMode::ALL) {
            if (m.dest == NODENUM_BROADCAST) {
                const char *name = channels.getName(m.channelIndex);
                if (currentResolution == ScreenResolution::Low || currentResolution == ScreenResolution::UltraLow) {
                    if (strcmp(name, "ShortTurbo") == 0)
                        name = "ShortT";
                    else if (strcmp(name, "ShortSlow") == 0)
                        name = "ShortS";
                    else if (strcmp(name, "ShortFast") == 0)
                        name = "ShortF";
                    else if (strcmp(name, "MediumSlow") == 0)
                        name = "MedS";
                    else if (strcmp(name, "MediumFast") == 0)
                        name = "MedF";
                    else if (strcmp(name, "LongSlow") == 0)
                        name = "LongS";
                    else if (strcmp(name, "LongFast") == 0)
                        name = "LongF";
                    else if (strcmp(name, "LongTurbo") == 0)
                        name = "LongT";
                    else if (strcmp(name, "LongMod") == 0)
                        name = "LongM";
                }
                snprintf(chanType, sizeof(chanType), "#%s", name);
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
            snprintf(timeBuf, sizeof(timeBuf), "%us", seconds);
        } else if (seconds < 3600) {
            snprintf(timeBuf, sizeof(timeBuf), "%um", seconds / 60);
        } else if (seconds < 86400) {
            snprintf(timeBuf, sizeof(timeBuf), "%uh", seconds / 3600);
        } else {
            snprintf(timeBuf, sizeof(timeBuf), "%ud", seconds / 86400);
        }

        // Build header line for this message
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(m.sender);
        meshtastic_NodeInfoLite *node_recipient = nodeDB->getMeshNode(m.dest);

        char senderBuf[48] = "";
        if (node && node->has_user) {
            // Use long name if present
            strncpy(senderBuf, node->user.long_name, sizeof(senderBuf) - 1);
            senderBuf[sizeof(senderBuf) - 1] = '\0';
        } else {
            // No long/short name → show NodeID in parentheses
            snprintf(senderBuf, sizeof(senderBuf), "(%08x)", m.sender);
        }

        // If this is *our own* message, override senderBuf to who the recipient was
        bool mine = (m.sender == nodeDB->getNodeNum());
        if (mine && node_recipient && node_recipient->has_user) {
            strcpy(senderBuf, node_recipient->user.long_name);
        }

        // Shrink Sender name if needed
        int availWidth = (mine ? rightTextWidth : leftTextWidth) - display->getStringWidth(timeBuf) -
                         display->getStringWidth(chanType) - display->getStringWidth("   @...");
        if (availWidth < 0)
            availWidth = 0;

        size_t origLen = strlen(senderBuf);
        while (senderBuf[0] && display->getStringWidth(senderBuf) > availWidth) {
            senderBuf[strlen(senderBuf) - 1] = '\0';
        }

        // If we actually truncated, append "..."
        if (strlen(senderBuf) < origLen) {
            strcat(senderBuf, "...");
        }

        // Final header line
        char headerStr[96];
        if (mine) {
            if (currentMode == ThreadMode::ALL) {
                if (strcmp(chanType, "(DM)") == 0) {
                    snprintf(headerStr, sizeof(headerStr), "%s to %s", timeBuf, senderBuf);
                } else {
                    snprintf(headerStr, sizeof(headerStr), "%s to %s", timeBuf, chanType);
                }
            } else {
                snprintf(headerStr, sizeof(headerStr), "%s", timeBuf);
            }
        } else {
            snprintf(headerStr, sizeof(headerStr), "%s @%s %s", timeBuf, senderBuf, chanType);
        }

        // Push header line
        allLines.push_back(std::string(headerStr));
        isMine.push_back(mine);
        isHeader.push_back(true);
        ackForLine.push_back(m.ackStatus);

        const char *msgText = MessageStore::getText(m);

        int wrapWidth = mine ? rightTextWidth : leftTextWidth;
        std::vector<std::string> wrapped = generateLines(display, "", msgText, wrapWidth);
        for (auto &ln : wrapped) {
            allLines.push_back(ln);
            isMine.push_back(mine);
            isHeader.push_back(false);
            ackForLine.push_back(AckStatus::NONE);
        }
    }

    // Cache lines and heights
    cachedLines = allLines;
    cachedHeights = calculateLineHeights(cachedLines, emotes, isHeader);

    std::vector<MessageBlock> blocks = buildMessageBlocks(isHeader, isMine);

    // Scrolling logic (unchanged)
    int totalHeight = 0;
    for (size_t i = 0; i < cachedHeights.size(); ++i)
        totalHeight += cachedHeights[i];
    int usableScrollHeight = usableHeight;
    int scrollStop = std::max(0, totalHeight - usableScrollHeight + cachedHeights.back());

#ifndef USE_EINK
    uint32_t now = millis();
    float delta = (now - lastTime) / 400.0f;
    lastTime = now;
    const float scrollSpeed = 2.0f;

    if (scrollStartDelay == 0)
        scrollStartDelay = now;
    if (!scrollStarted && now - scrollStartDelay > 2000)
        scrollStarted = true;

    if (!manualScrolling && totalHeight > usableScrollHeight) {
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
    } else if (!manualScrolling) {
        scrollY = 0;
    }
#else
    // E-Ink: disable autoscroll
    scrollY = 0.0f;
    waitingToReset = false;
    scrollStarted = false;
    lastTime = millis();
#endif

    int finalScroll = (int)scrollY;
    int yOffset = -finalScroll + getTextPositions(display)[1];
    const int contentTop = getTextPositions(display)[1];
    const int contentBottom = scrollBottom; // already excludes nav line
    const int rightEdge = SCREEN_WIDTH - SCROLLBAR_WIDTH - RIGHT_MARGIN;
    const int bubbleGapY = std::max(1, MESSAGE_BLOCK_GAP / 2);

    std::vector<int> lineTop;
    lineTop.resize(cachedLines.size());
    {
        int acc = 0;
        for (size_t i = 0; i < cachedLines.size(); ++i) {
            lineTop[i] = yOffset + acc;
            acc += cachedHeights[i];
        }
    }

    // Draw bubbles
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const auto &b = blocks[bi];
        if (b.start >= cachedLines.size() || b.end >= cachedLines.size() || b.start > b.end)
            continue;

        int visualTop = lineTop[b.start];

        int topY;
        if (isHeader[b.start]) {
            // Header start
            constexpr int BUBBLE_PAD_TOP_HEADER = 1; // try 1 or 2
            topY = visualTop - BUBBLE_PAD_TOP_HEADER;
        } else {
            // Body start
            bool thisLineHasEmote = false;
            for (int e = 0; e < numEmotes; ++e) {
                if (cachedLines[b.start].find(emotes[e].label) != std::string::npos) {
                    thisLineHasEmote = true;
                    break;
                }
            }
            if (thisLineHasEmote) {
                constexpr int EMOTE_PADDING_ABOVE = 4;
                visualTop -= EMOTE_PADDING_ABOVE;
            }
            topY = visualTop - BUBBLE_PAD_Y;
        }
        int visualBottom = getDrawnLinePixelBottom(lineTop[b.end], cachedLines[b.end], isHeader[b.end]);
        int bottomY = visualBottom + BUBBLE_PAD_Y;

        if (bi + 1 < blocks.size()) {
            int nextHeaderIndex = (int)blocks[bi + 1].start;
            int nextTop = lineTop[nextHeaderIndex];
            int maxBottom = nextTop - 1 - bubbleGapY;
            if (bottomY > maxBottom)
                bottomY = maxBottom;
        }

        if (bottomY <= topY + 2)
            continue;

        if (bottomY < contentTop || topY > contentBottom - 1)
            continue;

        int maxLineW = 0;

        for (size_t i = b.start; i <= b.end; ++i) {
            int w = 0;
            if (isHeader[i]) {
                w = display->getStringWidth(cachedLines[i].c_str());
                if (b.mine)
                    w += 12; // room for ACK/NACK/relay mark
            } else {
                w = getRenderedLineWidth(display, cachedLines[i], emotes, numEmotes);
            }
            if (w > maxLineW)
                maxLineW = w;
        }

        int bubbleW = std::max(BUBBLE_MIN_W, maxLineW + (BUBBLE_PAD_X * 2));
        int bubbleH = (bottomY - topY) + 1;
        int bubbleX = 0;
        if (b.mine) {
            bubbleX = rightEdge - bubbleW;
        } else {
            bubbleX = x;
        }
        if (bubbleX < x)
            bubbleX = x;
        if (bubbleX + bubbleW > rightEdge)
            bubbleW = std::max(1, rightEdge - bubbleX);

        if (bubbleW > 1 && bubbleH > 1) {
            int r = BUBBLE_RADIUS;
            int maxR = (std::min(bubbleW, bubbleH) / 2) - 1;
            if (maxR < 0)
                maxR = 0;
            if (r > maxR)
                r = maxR;

            drawRoundedRectOutline(display, bubbleX, topY, bubbleW, bubbleH, r);
            const int extra = 3;
            const int rr = r + extra;
            int x1 = bubbleX + bubbleW - 1;
            int y1 = topY + bubbleH - 1;

            if (!b.mine) {
                // top-left corner square
                display->drawLine(bubbleX, topY, bubbleX + rr, topY);
                display->drawLine(bubbleX, topY, bubbleX, topY + rr);
            } else {
                // bottom-right corner square
                display->drawLine(x1 - rr, y1, x1, y1);
                display->drawLine(x1, y1 - rr, x1, y1);
            }
        }
    }

    // Render visible lines
    int lineY = yOffset;
    for (size_t i = 0; i < cachedLines.size(); ++i) {

        if (lineY > -cachedHeights[i] && lineY < scrollBottom) {
            if (isHeader[i]) {

                int w = display->getStringWidth(cachedLines[i].c_str());
                int headerX;
                if (isMine[i]) {
                    // push header left to avoid overlap with scrollbar
                    headerX = (SCREEN_WIDTH - SCROLLBAR_WIDTH - RIGHT_MARGIN) - w - BUBBLE_TEXT_INDENT;
                    if (headerX < LEFT_MARGIN)
                        headerX = LEFT_MARGIN;
                } else {
                    headerX = x + BUBBLE_PAD_X + BUBBLE_TEXT_INDENT;
                }
                display->drawString(headerX, lineY, cachedLines[i].c_str());

                // Draw underline just under header text
                int underlineY = lineY + FONT_HEIGHT_SMALL;

                int underlineW = w;
                int maxW = rightEdge - headerX;
                if (maxW < 0)
                    maxW = 0;
                if (underlineW > maxW)
                    underlineW = maxW;

                for (int px = 0; px < underlineW; ++px) {
                    display->setPixel(headerX + px, underlineY);
                }

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

            } else {
                // Render message line
                if (isMine[i]) {
                    // Calculate actual rendered width including emotes
                    int renderedWidth = getRenderedLineWidth(display, cachedLines[i], emotes, numEmotes);
                    int rightX = (SCREEN_WIDTH - SCROLLBAR_WIDTH - RIGHT_MARGIN) - renderedWidth - BUBBLE_TEXT_INDENT;
                    if (rightX < LEFT_MARGIN)
                        rightX = LEFT_MARGIN;

                    drawStringWithEmotes(display, rightX, lineY, cachedLines[i], emotes, numEmotes);
                } else {
                    drawStringWithEmotes(display, x + BUBBLE_PAD_X + BUBBLE_TEXT_INDENT, lineY, cachedLines[i], emotes,
                                         numEmotes);
                }
            }
        }

        lineY += cachedHeights[i];
    }

    // Draw scrollbar
    drawMessageScrollbar(display, usableHeight, totalHeight, finalScroll, getTextPositions(display)[1]);
    graphics::drawCommonHeader(display, x, y, titleStr);
    graphics::drawCommonFooter(display, x, y);
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
std::vector<int> calculateLineHeights(const std::vector<std::string> &lines, const Emote *emotes,
                                      const std::vector<bool> &isHeaderVec)
{
    // Tunables for layout control
    constexpr int HEADER_UNDERLINE_GAP = 0; // space between underline and first body line
    constexpr int HEADER_UNDERLINE_PIX = 1; // underline thickness (1px row drawn)
    constexpr int BODY_LINE_LEADING = -4;   // default vertical leading for normal body lines
    constexpr int EMOTE_PADDING_ABOVE = 4;  // space above emote line (added to line above)
    constexpr int EMOTE_PADDING_BELOW = 3;  // space below emote line (added to emote line)

    std::vector<int> rowHeights;
    rowHeights.reserve(lines.size());

    for (size_t idx = 0; idx < lines.size(); ++idx) {
        const auto &line = lines[idx];
        const int baseHeight = FONT_HEIGHT_SMALL;
        int lineHeight = baseHeight;

        // Detect if THIS line or NEXT line contains an emote
        bool hasEmote = false;
        int tallestEmote = baseHeight;
        for (int i = 0; i < numEmotes; ++i) {
            if (line.find(emotes[i].label) != std::string::npos) {
                hasEmote = true;
                tallestEmote = std::max(tallestEmote, emotes[i].height);
            }
        }

        bool nextHasEmote = false;
        if (idx + 1 < lines.size()) {
            for (int i = 0; i < numEmotes; ++i) {
                if (lines[idx + 1].find(emotes[i].label) != std::string::npos) {
                    nextHasEmote = true;
                    break;
                }
            }
        }

        if (isHeaderVec[idx]) {
            // Header line spacing
            lineHeight = baseHeight + HEADER_UNDERLINE_PIX + HEADER_UNDERLINE_GAP;
        } else {
            // Base spacing for normal lines
            int desiredBody = baseHeight + BODY_LINE_LEADING;

            if (hasEmote) {
                // Emote line: add overshoot + bottom padding
                int overshoot = std::max(0, tallestEmote - baseHeight);
                lineHeight = desiredBody + overshoot + EMOTE_PADDING_BELOW;
            } else {
                // Regular line: no emote → standard spacing
                lineHeight = desiredBody;

                // If next line has an emote → add top padding *here*
                if (nextHasEmote) {
                    lineHeight += EMOTE_PADDING_ABOVE;
                }
            }

            // Add block gap if next is a header
            if (idx + 1 < lines.size() && isHeaderVec[idx + 1]) {
                lineHeight += MESSAGE_BLOCK_GAP;
            }
        }

        rowHeights.push_back(lineHeight);
    }

    return rowHeights;
}

void handleNewMessage(OLEDDisplay *display, const StoredMessage &sm, const meshtastic_MeshPacket &packet)
{
    if (packet.from != 0) {
        hasUnreadMessage = true;

        // Determine if message belongs to a muted channel
        bool isChannelMuted = false;
        if (sm.type == MessageType::BROADCAST) {
            const meshtastic_Channel channel = channels.getByIndex(packet.channel ? packet.channel : channels.getPrimaryIndex());
            if (channel.settings.has_module_settings && channel.settings.module_settings.is_muted)
                isChannelMuted = true;
        }

        // Banner logic
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(packet.from);
        char longName[48] = "?";
        if (node && node->user.long_name) {
            strncpy(longName, node->user.long_name, sizeof(longName) - 1);
            longName[sizeof(longName) - 1] = '\0';
        }
        int availWidth = display->getWidth() - ((currentResolution == ScreenResolution::High) ? 40 : 20);
        if (availWidth < 0)
            availWidth = 0;

        size_t origLen = strlen(longName);
        while (longName[0] && display->getStringWidth(longName) > availWidth) {
            longName[strlen(longName) - 1] = '\0';
        }
        if (strlen(longName) < origLen) {
            strcat(longName, "...");
        }
        const char *msgRaw = reinterpret_cast<const char *>(packet.decoded.payload.bytes);

        char banner[256];
        bool isAlert = false;

        // Check if alert detection is enabled via external notification module
        if (moduleConfig.external_notification.alert_bell || moduleConfig.external_notification.alert_bell_vibra ||
            moduleConfig.external_notification.alert_bell_buzzer) {
            for (size_t i = 0; i < packet.decoded.payload.size && i < 100; i++) {
                if (msgRaw[i] == '\x07') {
                    isAlert = true;
                    break;
                }
            }
        }

        if (isAlert) {
            if (longName && longName[0])
                snprintf(banner, sizeof(banner), "Alert Received from\n%s", longName);
            else
                strcpy(banner, "Alert Received");
        } else {
            // Skip muted channels unless it's an alert
            if (isChannelMuted)
                return;

            if (longName && longName[0]) {
                if (currentResolution == ScreenResolution::UltraLow) {
                    strcpy(banner, "New Message");
                } else {
                    snprintf(banner, sizeof(banner), "New Message from\n%s", longName);
                }
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

        if (shouldWakeOnReceivedMessage()) {
            screen->setOn(true);
        }

        screen->showSimpleBanner(banner, inThread ? 1000 : 3000);
    }

    // Always focus into the correct conversation thread when a message with real text arrives
    const char *msgText = MessageStore::getText(sm);
    if (msgText && msgText[0] != '\0') {
        setThreadFor(sm, packet);
    }

    // Reset scroll for a clean start
    resetScrollState();
}

void setThreadFor(const StoredMessage &sm, const meshtastic_MeshPacket &packet)
{
    if (packet.to == 0 || packet.to == NODENUM_BROADCAST) {
        setThreadMode(ThreadMode::CHANNEL, sm.channelIndex);
    } else {
        uint32_t localNode = nodeDB->getNodeNum();
        uint32_t peer = (sm.sender == localNode) ? packet.to : sm.sender;
        setThreadMode(ThreadMode::DIRECT, -1, peer);
    }
}

} // namespace MessageRenderer
} // namespace graphics
#endif