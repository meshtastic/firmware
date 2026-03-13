#include "configuration.h"
#if HAS_SCREEN
#include "MessageRenderer.h"

// Core includes
#include "MessageStore.h"
#include "NodeDB.h"
#include "UIRenderer.h"
#include "gps/RTC.h"
#include "graphics/EmoteRenderer.h"
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
    graphics::EmoteRenderer::drawStringWithEmotes(display, x, y, line, FONT_HEIGHT_SMALL, emotes, emoteCount);
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
    return graphics::EmoteRenderer::analyzeLine(display, line, 0, emotes, emoteCount).width;
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

    const int tallest = graphics::EmoteRenderer::analyzeLine(nullptr, line, FONT_HEIGHT_SMALL, emotes, numEmotes).tallestHeight;

    const int lineHeight = std::max(FONT_HEIGHT_SMALL, tallest);
    const int iconTop = lineTopY + (lineHeight - tallest) / 2;

    return iconTop + tallest - 1;
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

    // Check if bubbles are enabled
    const bool showBubbles = config.display.enable_message_bubbles;
    const int textIndent = showBubbles ? (BUBBLE_PAD_X + BUBBLE_TEXT_INDENT) : LEFT_MARGIN;

    // Derived widths
    const int leftTextWidth = SCREEN_WIDTH - LEFT_MARGIN - RIGHT_MARGIN - (showBubbles ? (BUBBLE_PAD_X * 2) : 0);
    const int rightTextWidth = SCREEN_WIDTH - LEFT_MARGIN - RIGHT_MARGIN - SCROLLBAR_WIDTH;

    // Title string depending on mode
    char titleStr[48];
    snprintf(titleStr, sizeof(titleStr), "Messages");
    switch (currentMode) {
    case ThreadMode::ALL:
        snprintf(titleStr, sizeof(titleStr), "Messages");
        break;
    case ThreadMode::CHANNEL: {
        const char *cname = channels.getName(currentChannel);
        if (cname && cname[0]) {
            snprintf(titleStr, sizeof(titleStr), "#%s", cname);
        } else {
            snprintf(titleStr, sizeof(titleStr), "Ch%d", currentChannel);
        }
        break;
    }
    case ThreadMode::DIRECT: {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(currentPeer);
        if (node && node->has_user && node->user.short_name[0]) {
            snprintf(titleStr, sizeof(titleStr), "@%s", node->user.short_name);
        } else {
            snprintf(titleStr, sizeof(titleStr), "@%08x", currentPeer);
        }
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

        char senderName[64] = "";
        if (node && node->has_user) {
            if (node->user.long_name[0]) {
                strncpy(senderName, node->user.long_name, sizeof(senderName) - 1);
            } else if (node->user.short_name[0]) {
                strncpy(senderName, node->user.short_name, sizeof(senderName) - 1);
            }
            senderName[sizeof(senderName) - 1] = '\0';
        }
        if (!senderName[0]) {
            snprintf(senderName, sizeof(senderName), "(%08x)", m.sender);
        }

        // If this is *our own* message, override senderName to who the recipient was
        bool mine = (m.sender == nodeDB->getNodeNum());
        if (mine && node_recipient && node_recipient->has_user) {
            if (node_recipient->user.long_name[0]) {
                strncpy(senderName, node_recipient->user.long_name, sizeof(senderName) - 1);
                senderName[sizeof(senderName) - 1] = '\0';
            } else if (node_recipient->user.short_name[0]) {
                strncpy(senderName, node_recipient->user.short_name, sizeof(senderName) - 1);
                senderName[sizeof(senderName) - 1] = '\0';
            }
        }

        // Shrink Sender name if needed
        int availWidth = (mine ? rightTextWidth : leftTextWidth) - display->getStringWidth(timeBuf) -
                         display->getStringWidth(chanType) - graphics::UIRenderer::measureStringWithEmotes(display, "   @...");
        if (availWidth < 0)
            availWidth = 0;
        char truncatedSender[64];
        graphics::UIRenderer::truncateStringWithEmotes(display, senderName, truncatedSender, sizeof(truncatedSender), availWidth);

        // Final header line
        char headerStr[128];
        if (mine) {
            if (currentMode == ThreadMode::ALL) {
                if (strcmp(chanType, "(DM)") == 0) {
                    snprintf(headerStr, sizeof(headerStr), "%s to %s", timeBuf, truncatedSender);
                } else {
                    snprintf(headerStr, sizeof(headerStr), "%s to %s", timeBuf, chanType);
                }
            } else {
                snprintf(headerStr, sizeof(headerStr), "%s", timeBuf);
            }
        } else {
            snprintf(headerStr, sizeof(headerStr), chanType[0] ? "%s @%s %s" : "%s @%s", timeBuf, truncatedSender, chanType);
        }

        // Push header line
        allLines.push_back(headerStr);
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

    // Draw bubbles (only if enabled)
    if (showBubbles) {
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
                const bool thisLineHasEmote =
                    graphics::EmoteRenderer::analyzeLine(nullptr, cachedLines[b.start].c_str(), 0, emotes, numEmotes).hasEmote;
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
                    w = graphics::UIRenderer::measureStringWithEmotes(display, cachedLines[i].c_str());
                    if (b.mine)
                        w += 12; // room for ACK/NACK/relay mark
                } else {
                    w = getRenderedLineWidth(display, cachedLines[i], emotes, numEmotes);
                }
                if (w > maxLineW)
                    maxLineW = w;
            }

            int bubbleW = std::max(BUBBLE_MIN_W, maxLineW + (textIndent * 2));
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

            // Draw rounded rectangle bubble
            if (bubbleW > BUBBLE_RADIUS * 2 && bubbleH > BUBBLE_RADIUS * 2) {
                const int r = BUBBLE_RADIUS;
                const int bx = bubbleX;
                const int by = topY;
                const int bw = bubbleW;
                const int bh = bubbleH;

                // Draw the 4 corner arcs using drawCircleQuads
                display->drawCircleQuads(bx + r, by + r, r, 0x2);                   // Top-left
                display->drawCircleQuads(bx + bw - r - 1, by + r, r, 0x1);          // Top-right
                display->drawCircleQuads(bx + r, by + bh - r - 1, r, 0x4);          // Bottom-left
                display->drawCircleQuads(bx + bw - r - 1, by + bh - r - 1, r, 0x8); // Bottom-right

                // Draw the 4 edges between corners
                display->drawHorizontalLine(bx + r, by, bw - 2 * r);          // Top edge
                display->drawHorizontalLine(bx + r, by + bh - 1, bw - 2 * r); // Bottom edge
                display->drawVerticalLine(bx, by + r, bh - 2 * r);            // Left edge
                display->drawVerticalLine(bx + bw - 1, by + r, bh - 2 * r);   // Right edge
            } else if (bubbleW > 1 && bubbleH > 1) {
                // Fallback to simple rectangle for very small bubbles
                display->drawRect(bubbleX, topY, bubbleW, bubbleH);
            }
        }
    } // end if (showBubbles)

    // Render visible lines
    int lineY = yOffset;
    for (size_t i = 0; i < cachedLines.size(); ++i) {

        if (lineY > -cachedHeights[i] && lineY < scrollBottom) {
            if (isHeader[i]) {

                int w = graphics::UIRenderer::measureStringWithEmotes(display, cachedLines[i].c_str());
                int headerX;
                if (isMine[i]) {
                    // push header left to avoid overlap with scrollbar
                    headerX = (SCREEN_WIDTH - SCROLLBAR_WIDTH - RIGHT_MARGIN) - w - (showBubbles ? textIndent : 0);
                    if (headerX < LEFT_MARGIN)
                        headerX = LEFT_MARGIN;
                } else {
                    headerX = x + textIndent;
                }
                graphics::UIRenderer::drawStringWithEmotes(display, headerX, lineY, cachedLines[i].c_str(), FONT_HEIGHT_SMALL, 1,
                                                           false);

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
                    int rightX = (SCREEN_WIDTH - SCROLLBAR_WIDTH - RIGHT_MARGIN) - renderedWidth - (showBubbles ? textIndent : 0);
                    if (rightX < LEFT_MARGIN)
                        rightX = LEFT_MARGIN;

                    drawStringWithEmotes(display, rightX, lineY, cachedLines[i], emotes, numEmotes);
                } else {
                    drawStringWithEmotes(display, x + textIndent, lineY, cachedLines[i], emotes, numEmotes);
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
            uint16_t strWidth = graphics::UIRenderer::measureStringWithEmotes(display, test.c_str());
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
    std::vector<graphics::EmoteRenderer::LineMetrics> lineMetrics;
    lineMetrics.reserve(lines.size());

    for (const auto &line : lines) {
        lineMetrics.push_back(graphics::EmoteRenderer::analyzeLine(nullptr, line, FONT_HEIGHT_SMALL, emotes, numEmotes));
    }

    for (size_t idx = 0; idx < lines.size(); ++idx) {
        const int baseHeight = FONT_HEIGHT_SMALL;
        int lineHeight = baseHeight;

        const int tallestEmote = lineMetrics[idx].tallestHeight;
        const bool hasEmote = lineMetrics[idx].hasEmote;
        const bool nextHasEmote = (idx + 1 < lines.size()) && lineMetrics[idx + 1].hasEmote;

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
        char longName[64] = "?";
        if (node && node->has_user) {
            if (node->user.long_name[0]) {
                strncpy(longName, node->user.long_name, sizeof(longName) - 1);
                longName[sizeof(longName) - 1] = '\0';
            } else if (node->user.short_name[0]) {
                strncpy(longName, node->user.short_name, sizeof(longName) - 1);
                longName[sizeof(longName) - 1] = '\0';
            }
        }
        int availWidth = display->getWidth() - ((currentResolution == ScreenResolution::High) ? 40 : 20);
        if (availWidth < 0)
            availWidth = 0;
        char truncatedLongName[64];
        graphics::UIRenderer::truncateStringWithEmotes(display, longName, truncatedLongName, sizeof(truncatedLongName), availWidth);
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
            if (truncatedLongName[0])
                snprintf(banner, sizeof(banner), "Alert Received from\n%s", truncatedLongName);
            else
                strcpy(banner, "Alert Received");
        } else {
            // Skip muted channels unless it's an alert
            if (isChannelMuted)
                return;

            if (truncatedLongName[0]) {
                if (currentResolution == ScreenResolution::UltraLow) {
                    strcpy(banner, "New Message");
                } else {
                    snprintf(banner, sizeof(banner), "New Message from\n%s", truncatedLongName);
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
