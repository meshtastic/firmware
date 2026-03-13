#pragma once
#include "configuration.h"

#if HAS_SCREEN
#include "graphics/emotes.h"
#include <Arduino.h>
#include <OLEDDisplay.h>
#include <string>
#include <vector>

namespace graphics
{
namespace EmoteRenderer
{

struct LineMetrics
{
    int width;
    int tallestHeight;
    bool hasEmote;
};

size_t utf8CharLen(uint8_t c);

const Emote *findEmoteByLabel(const char *label, const Emote *emoteSet = emotes, int emoteCount = numEmotes);
const Emote *findEmoteAt(const char *text, size_t textLen, size_t pos, size_t &matchLen, const Emote *emoteSet = emotes,
                         int emoteCount = numEmotes);
inline const Emote *findEmoteAt(const std::string &text, size_t pos, size_t &matchLen, const Emote *emoteSet = emotes,
                                int emoteCount = numEmotes)
{
    return findEmoteAt(text.c_str(), text.length(), pos, matchLen, emoteSet, emoteCount);
}

LineMetrics analyzeLine(OLEDDisplay *display, const char *line, int fallbackHeight = 0, const Emote *emoteSet = emotes,
                        int emoteCount = numEmotes, int emoteSpacing = 1);
inline LineMetrics analyzeLine(OLEDDisplay *display, const std::string &line, int fallbackHeight = 0,
                               const Emote *emoteSet = emotes, int emoteCount = numEmotes, int emoteSpacing = 1)
{
    return analyzeLine(display, line.c_str(), fallbackHeight, emoteSet, emoteCount, emoteSpacing);
}
int maxEmoteHeight(const Emote *emoteSet = emotes, int emoteCount = numEmotes);

int measureStringWithEmotes(OLEDDisplay *display, const char *line, const Emote *emoteSet = emotes,
                            int emoteCount = numEmotes, int emoteSpacing = 1);
inline int measureStringWithEmotes(OLEDDisplay *display, const std::string &line, const Emote *emoteSet = emotes,
                                   int emoteCount = numEmotes, int emoteSpacing = 1)
{
    return measureStringWithEmotes(display, line.c_str(), emoteSet, emoteCount, emoteSpacing);
}
size_t truncateToWidth(OLEDDisplay *display, const char *line, char *out, size_t outSize, int maxWidth,
                       const char *ellipsis = "...", const Emote *emoteSet = emotes, int emoteCount = numEmotes,
                       int emoteSpacing = 1);
inline std::string truncateToWidth(OLEDDisplay *display, const std::string &line, int maxWidth,
                                   const std::string &ellipsis = "...", const Emote *emoteSet = emotes,
                                   int emoteCount = numEmotes, int emoteSpacing = 1)
{
    if (!display || maxWidth <= 0)
        return "";
    if (measureStringWithEmotes(display, line.c_str(), emoteSet, emoteCount, emoteSpacing) <= maxWidth)
        return line;

    std::vector<char> out(line.length() + ellipsis.length() + 1, '\0');
    truncateToWidth(display, line.c_str(), out.data(), out.size(), maxWidth, ellipsis.c_str(), emoteSet, emoteCount, emoteSpacing);
    return std::string(out.data());
}

void drawStringWithEmotes(OLEDDisplay *display, int x, int y, const char *line, int fontHeight, const Emote *emoteSet = emotes,
                          int emoteCount = numEmotes, int emoteSpacing = 1, bool fauxBold = true);
inline void drawStringWithEmotes(OLEDDisplay *display, int x, int y, const std::string &line, int fontHeight,
                                 const Emote *emoteSet = emotes, int emoteCount = numEmotes, int emoteSpacing = 1,
                                 bool fauxBold = true)
{
    drawStringWithEmotes(display, x, y, line.c_str(), fontHeight, emoteSet, emoteCount, emoteSpacing, fauxBold);
}

} // namespace EmoteRenderer
} // namespace graphics

#endif // HAS_SCREEN
