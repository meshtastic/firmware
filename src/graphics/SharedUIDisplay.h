#pragma once

#include <OLEDDisplay.h>

namespace graphics
{

// =======================
// Shared UI Helpers
// =======================

// Consistent Line Spacing
#define textZeroLine 0
#define textFirstLine ((FONT_HEIGHT_SMALL - 1) * 1)
#define textSecondLine (textFirstLine + (FONT_HEIGHT_SMALL - 5))
#define textThirdLine (textSecondLine + (FONT_HEIGHT_SMALL - 5))
#define textFourthLine (textThirdLine + (FONT_HEIGHT_SMALL - 5))
#define textFifthLine (textFourthLine + (FONT_HEIGHT_SMALL - 5))
#define textSixthLine (textFifthLine + (FONT_HEIGHT_SMALL - 5))

// Quick screen access
#define SCREEN_WIDTH display->getWidth()
#define SCREEN_HEIGHT display->getHeight()

// Shared state (declare inside namespace)
extern bool hasUnreadMessage;
extern bool isMuted;

// Rounded highlight (used for inverted headers)
void drawRoundedHighlight(OLEDDisplay *display, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r);

// Shared battery/time/mail header
void drawCommonHeader(OLEDDisplay *display, int16_t x, int16_t y, const char *titleStr = "");

} // namespace graphics
