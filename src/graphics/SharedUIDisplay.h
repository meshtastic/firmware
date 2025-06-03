#pragma once

#include <OLEDDisplay.h>

namespace graphics
{

// =======================
// Shared UI Helpers
// =======================

// Compact line layout
#define compactFirstLine ((FONT_HEIGHT_SMALL - 1) * 1)
#define compactSecondLine ((FONT_HEIGHT_SMALL - 1) * 2) - 2
#define compactThirdLine ((FONT_HEIGHT_SMALL - 1) * 3) - 4
#define compactFourthLine ((FONT_HEIGHT_SMALL - 1) * 4) - 6
#define compactFifthLine ((FONT_HEIGHT_SMALL - 1) * 5) - 8
#define compactSixthLine ((FONT_HEIGHT_SMALL - 1) * 6) - 10

// Standard line layout
#define standardFirstLine (FONT_HEIGHT_SMALL + 1) * 1
#define standardSecondLine (FONT_HEIGHT_SMALL + 1) * 2
#define standardThirdLine (FONT_HEIGHT_SMALL + 1) * 3
#define standardFourthLine (FONT_HEIGHT_SMALL + 1) * 4

// More Compact line layout
#define moreCompactFirstLine compactFirstLine
#define moreCompactSecondLine (moreCompactFirstLine + (FONT_HEIGHT_SMALL - 5))
#define moreCompactThirdLine (moreCompactSecondLine + (FONT_HEIGHT_SMALL - 5))
#define moreCompactFourthLine (moreCompactThirdLine + (FONT_HEIGHT_SMALL - 5))
#define moreCompactFifthLine (moreCompactFourthLine + (FONT_HEIGHT_SMALL - 5))
#define moreCompactSixthLine (moreCompactFifthLine + (FONT_HEIGHT_SMALL - 5))

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
