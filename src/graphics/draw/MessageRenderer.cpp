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

// Forward declaration from Screen.cpp - this function needs to be accessible
// For now, we'll implement a local version that matches the Screen.cpp functionality
bool deltaToTimestamp(uint32_t secondsAgo, uint8_t *hours, uint8_t *minutes, int32_t *daysAgo)
{
    // Cache the result - avoid frequent recalculation
    static uint8_t hoursCached = 0, minutesCached = 0;
    static uint32_t daysAgoCached = 0;
    static uint32_t secondsAgoCached = 0;
    static bool validCached = false;

    // Abort: if timezone not set
    if (strlen(config.device.tzdef) == 0) {
        validCached = false;
        return validCached;
    }

    // Abort: if invalid pointers passed
    if (hours == nullptr || minutes == nullptr || daysAgo == nullptr) {
        validCached = false;
        return validCached;
    }

    // Abort: if time seems invalid.. (> 6 months ago, probably seen before RTC set)
    if (secondsAgo > SEC_PER_DAY * 30UL * 6) {
        validCached = false;
        return validCached;
    }

    // If repeated request, don't bother recalculating
    if (secondsAgo - secondsAgoCached < 60 && secondsAgoCached != 0) {
        if (validCached) {
            *hours = hoursCached;
            *minutes = minutesCached;
            *daysAgo = daysAgoCached;
        }
        return validCached;
    }

    // Get local time
    uint32_t secondsRTC = getValidTime(RTCQuality::RTCQualityDevice, true); // Get local time

    // Abort: if RTC not set
    if (!secondsRTC) {
        validCached = false;
        return validCached;
    }

    // Get absolute time when last seen
    uint32_t secondsSeenAt = secondsRTC - secondsAgo;

    // Calculate daysAgo
    *daysAgo = (secondsRTC / SEC_PER_DAY) - (secondsSeenAt / SEC_PER_DAY); // How many "midnights" have passed

    // Get seconds since midnight
    uint32_t hms = (secondsRTC - secondsAgo) % SEC_PER_DAY;
    hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

    // Tear apart hms into hours and minutes
    *hours = hms / SEC_PER_HOUR;
    *minutes = (hms % SEC_PER_HOUR) / SEC_PER_MIN;

    // Cache the result
    daysAgoCached = *daysAgo;
    hoursCached = *hours;
    minutesCached = *minutes;
    secondsAgoCached = secondsAgo;

    validCached = true;
    return validCached;
}

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

    // === Step 2: Baseline alignment ===
    int lineHeight = std::max(fontHeight, maxIconHeight);
    int baselineOffset = (lineHeight - fontHeight) / 2;
    int fontY = y + baselineOffset;
    int fontMidline = fontY + fontHeight / 2;

    // === Step 3: Render line in segments ===
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
            cursorX += display->getStringWidth(textChunk.c_str());
            i = nextControl;
            continue;
        }

        // Render the emote (if found)
        if (matchedEmote && i == nextEmotePos) {
            int iconY = fontMidline - matchedEmote->height / 2 - 1;
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
            cursorX += display->getStringWidth(remaining.c_str());
            break;
        }
    }
}

void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Clear the unread message indicator when viewing the message
    hasUnreadMessage = false;

    const meshtastic_MeshPacket &mp = devicestate.rx_text_message;
    const char *msg = reinterpret_cast<const char *>(mp.decoded.payload.bytes);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    const int navHeight = FONT_HEIGHT_SMALL;
    const int scrollBottom = SCREEN_HEIGHT - navHeight;
    const int usableHeight = scrollBottom;
    const int textWidth = SCREEN_WIDTH;
    const int cornerRadius = 2;

    bool isInverted = (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_INVERTED);
    bool isBold = config.display.heading_bold;

    // === Header Construction ===
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(getFrom(&mp));
    char headerStr[80];
    const char *sender = "???";
    if (node && node->has_user) {
        if (SCREEN_WIDTH >= 200 && strlen(node->user.long_name) > 0) {
            sender = node->user.long_name;
        } else {
            sender = node->user.short_name;
        }
    }
    uint32_t seconds = sinceReceived(&mp), minutes = seconds / 60, hours = minutes / 60, days = hours / 24;
    uint8_t timestampHours, timestampMinutes;
    int32_t daysAgo;
    bool useTimestamp = deltaToTimestamp(seconds, &timestampHours, &timestampMinutes, &daysAgo);

    if (useTimestamp && minutes >= 15 && daysAgo == 0) {
        std::string prefix = (daysAgo == 1 && SCREEN_WIDTH >= 200) ? "Yesterday" : "At";
        if (config.display.use_12h_clock) {
            bool isPM = timestampHours >= 12;
            timestampHours = timestampHours % 12;
            if (timestampHours == 0)
                timestampHours = 12;
            snprintf(headerStr, sizeof(headerStr), "%s %d:%02d%s from %s", prefix.c_str(), timestampHours, timestampMinutes,
                     isPM ? "p" : "a", sender);
        } else {
            snprintf(headerStr, sizeof(headerStr), "%s %d:%02d from %s", prefix.c_str(), timestampHours, timestampMinutes,
                     sender);
        }
    } else {
        snprintf(headerStr, sizeof(headerStr), "%s ago from %s", UIRenderer::drawTimeDelta(days, hours, minutes, seconds).c_str(),
                 sender);
    }

#ifndef EXCLUDE_EMOJI
    // === Bounce animation setup ===
    static uint32_t lastBounceTime = 0;
    static int bounceY = 0;
    const int bounceRange = 2;     // Max pixels to bounce up/down
    const int bounceInterval = 60; // How quickly to change bounce direction (ms)

    uint32_t now = millis();
    if (now - lastBounceTime >= bounceInterval) {
        lastBounceTime = now;
        bounceY = (bounceY + 1) % (bounceRange * 2);
    }
    for (int i = 0; i < numEmotes; ++i) {
        const Emote &e = emotes[i];
        if (strcmp(msg, e.label) == 0) {
            // Draw the header
            if (isInverted) {
                drawRoundedHighlight(display, x, 0, SCREEN_WIDTH, FONT_HEIGHT_SMALL - 1, cornerRadius);
                display->setColor(BLACK);
                display->drawString(x + 3, 0, headerStr);
                if (isBold)
                    display->drawString(x + 4, 0, headerStr);
                display->setColor(WHITE);
            } else {
                display->drawString(x, 0, headerStr);
                if (SCREEN_WIDTH > 128) {
                    display->drawLine(0, 20, SCREEN_WIDTH, 20);
                } else {
                    display->drawLine(0, 14, SCREEN_WIDTH, 14);
                }
            }

            // Center the emote below header + apply bounce
            int remainingHeight = SCREEN_HEIGHT - FONT_HEIGHT_SMALL - navHeight;
            int emoteY = FONT_HEIGHT_SMALL + (remainingHeight - e.height) / 2 + bounceY - bounceRange;
            display->drawXbm((SCREEN_WIDTH - e.width) / 2, emoteY, e.width, e.height, e.bitmap);
            return;
        }
    }
#endif

    // === Word-wrap and build line list ===
    char messageBuf[237];
    snprintf(messageBuf, sizeof(messageBuf), "%s", msg);

    std::vector<std::string> lines;
    lines.push_back(std::string(headerStr)); // Header line is always first

    std::string line, word;
    for (int i = 0; messageBuf[i]; ++i) {
        char ch = messageBuf[i];
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
            if (display->getStringWidth(test.c_str()) > textWidth + 4) {
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

    // === Scrolling logic ===
    std::vector<int> rowHeights;

    for (const auto &_line : lines) {
        int maxHeight = FONT_HEIGHT_SMALL;
        for (int i = 0; i < numEmotes; ++i) {
            const Emote &e = emotes[i];
            if (_line.find(e.label) != std::string::npos) {
                if (e.height > maxHeight)
                    maxHeight = e.height;
            }
        }
        rowHeights.push_back(maxHeight);
    }
    int totalHeight = 0;
    for (size_t i = 1; i < rowHeights.size(); ++i) {
        totalHeight += rowHeights[i];
    }
    int usableScrollHeight = usableHeight - rowHeights[0]; // remove header height
    int scrollStop = std::max(0, totalHeight - usableScrollHeight);

    static float scrollY = 0.0f;
    static uint32_t lastTime = 0, scrollStartDelay = 0, pauseStart = 0;
    static bool waitingToReset = false, scrollStarted = false;

    // === Smooth scrolling adjustment ===
    // You can tweak this divisor to change how smooth it scrolls.
    // Lower = smoother, but can feel slow.
    float delta = (now - lastTime) / 400.0f;
    lastTime = now;

    const float scrollSpeed = 2.0f; // pixels per second

    // Delay scrolling start by 2 seconds
    if (scrollStartDelay == 0)
        scrollStartDelay = now;
    if (!scrollStarted && now - scrollStartDelay > 2000)
        scrollStarted = true;

    if (totalHeight > usableHeight) {
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
    int yOffset = -scrollOffset;
    if (!isInverted) {
        if (SCREEN_WIDTH > 128) {
            display->drawLine(0, yOffset + 20, SCREEN_WIDTH, yOffset + 20);
        } else {
            display->drawLine(0, yOffset + 14, SCREEN_WIDTH, yOffset + 14);
        }
    }

    // === Render visible lines ===
    for (size_t i = 0; i < lines.size(); ++i) {
        int lineY = yOffset;
        for (size_t j = 0; j < i; ++j)
            lineY += rowHeights[j];
        if (lineY > -rowHeights[i] && lineY < scrollBottom) {
            if (i == 0 && isInverted) {
                drawRoundedHighlight(display, x, lineY, SCREEN_WIDTH, FONT_HEIGHT_SMALL - 1, cornerRadius);
                display->setColor(BLACK);
                display->drawString(x + 3, lineY, lines[i].c_str());
                if (isBold)
                    display->drawString(x + 4, lineY, lines[i].c_str());
                display->setColor(WHITE);
            } else {
                drawStringWithEmotes(display, x, lineY, lines[i], emotes, numEmotes);
            }
        }
    }
}

} // namespace MessageRenderer
} // namespace graphics
#endif