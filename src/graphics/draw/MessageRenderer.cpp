/*
BaseUI

Developed and Maintained By:
- Ronald Garcia (HarukiToreda) – Lead development and implementation.
- JasonP (Xaositek)  – Screen layout and icon design, UI improvements and testing.
- TonyG (Tropho) – Project management, structural planning, and testing

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "configuration.h"
#if HAS_SCREEN
#include "MessageRenderer.h"

// Core includes
#include "MessageStore.h"
#include "NodeDB.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/emotes.h"
#include "main.h"
#include "meshUtils.h"

// Additional includes for UI rendering
#include "UIRenderer.h"
#include "graphics/TimeFormatters.h"

// Additional includes for dependencies
#include <string>
#include <vector>

// External declarations
extern bool hasUnreadMessage;
extern meshtastic_DeviceState devicestate;

using graphics::Emote;
using graphics::emotes;
using graphics::numEmotes;

namespace graphics
{
namespace MessageRenderer
{

// Simple cache based on text hash
static size_t cachedKey = 0;
static std::vector<std::string> cachedLines;
static std::vector<int> cachedHeights;

void drawStringWithEmotes(OLEDDisplay *display, int x, int y, const std::string &line, const Emote *emotes, int emoteCount)
{
    int cursorX = x;
    const int fontHeight = FONT_HEIGHT_SMALL;

    // === Step 1: Find tallest emote in the line ===
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

// Setter so other code can switch threads
void setThreadMode(ThreadMode mode, int channel /* = -1 */, uint32_t peer /* = 0 */)
{
    currentMode = mode;
    currentChannel = channel;
    currentPeer = peer;
    didReset = false; // force reset when mode changes

    // Track channels we’ve seen
    if (mode == ThreadMode::CHANNEL && channel >= 0) {
        if (std::find(seenChannels.begin(), seenChannels.end(), channel) == seenChannels.end())
            seenChannels.push_back(channel);
    }

    // Track DMs we’ve seen
    if (mode == ThreadMode::DIRECT && peer != 0) {
        if (std::find(seenPeers.begin(), seenPeers.end(), peer) == seenPeers.end())
            seenPeers.push_back(peer);
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

// === Accessors for menuHandler ===
const std::vector<int> &getSeenChannels()
{
    return seenChannels;
}
const std::vector<uint32_t> &getSeenPeers()
{
    return seenPeers;
}

void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
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
            snprintf(titleBuf, sizeof(titleBuf), "DM: %s", node->user.short_name);
        } else {
            snprintf(titleBuf, sizeof(titleBuf), "DM: %08x", currentPeer);
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
        // else: leave empty for thread views

        // Calculate how long ago
        uint32_t nowSecs = millis() / 1000;
        uint32_t seconds = (nowSecs > m.timestamp) ? (nowSecs - m.timestamp) : 0;
        bool invalidTime = (m.timestamp == 0 || seconds > 315360000); // >10 years

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
            snprintf(headerStr, sizeof(headerStr), "me %s %s", timeBuf, chanType);
        } else {
            snprintf(headerStr, sizeof(headerStr), "%s @%s %s", timeBuf, sender, chanType);
        }

        // Push header line
        allLines.push_back(std::string(headerStr));
        isMine.push_back(mine);
        isHeader.push_back(true);

        // Split message text into wrapped lines
        std::vector<std::string> wrapped = generateLines(display, "", m.text.c_str(), textWidth);
        for (auto &ln : wrapped) {
            allLines.push_back(ln);
            isMine.push_back(mine);
            isHeader.push_back(false);
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

    // Draw screen title header last
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

} // namespace MessageRenderer
} // namespace graphics
#endif