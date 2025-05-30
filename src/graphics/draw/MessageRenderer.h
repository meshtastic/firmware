#pragma once
#include "OLEDDisplay.h"
#include "OLEDDisplayUi.h"

namespace graphics
{
namespace MessageRenderer
{

/// Draws the text message frame for displaying received messages
void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

} // namespace MessageRenderer
} // namespace graphics
