#pragma once
#include "OLEDDisplay.h"
#include "OLEDDisplayUi.h"
#include "graphics/emotes.h"

namespace graphics
{
namespace MessageRenderer
{

// Text and emote rendering
void drawStringWithEmotes(OLEDDisplay *display, int x, int y, const std::string &line, const Emote *emotes, int emoteCount);

/// Draws the text message frame for displaying received messages
void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

} // namespace MessageRenderer
} // namespace graphics
