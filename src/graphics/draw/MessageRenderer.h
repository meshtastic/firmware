#pragma once
#include "OLEDDisplay.h"
#include "OLEDDisplayUi.h"
#include "graphics/emotes.h"
#include <string>
#include <vector>

namespace graphics
{
namespace MessageRenderer
{

// Text and emote rendering
void drawStringWithEmotes(OLEDDisplay *display, int x, int y, const std::string &line, const Emote *emotes, int emoteCount);

/// Draws the text message frame for displaying received messages
void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

// Function to generate lines with word wrapping
std::vector<std::string> generateLines(OLEDDisplay *display, const char *headerStr, const char *messageBuf, int textWidth);

// Function to calculate heights for each line
std::vector<int> calculateLineHeights(const std::vector<std::string> &lines, const Emote *emotes);

// Function to render the message content
void renderMessageContent(OLEDDisplay *display, const std::vector<std::string> &lines, const std::vector<int> &rowHeights, int x,
                          int yOffset, int scrollBottom, const Emote *emotes, int numEmotes, bool isInverted, bool isBold);

} // namespace MessageRenderer
} // namespace graphics
