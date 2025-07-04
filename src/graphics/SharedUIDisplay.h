#pragma once

#include <OLEDDisplay.h>
#include <string>

namespace graphics
{

// =======================
// Shared UI Helpers
// =======================

#define textZeroLine 0
// Consistent Line Spacing - this is standard for all display and the fall-back spacing
#define textFirstLine (FONT_HEIGHT_SMALL - 1)
#define textSecondLine (textFirstLine + (FONT_HEIGHT_SMALL - 5))
#define textThirdLine (textSecondLine + (FONT_HEIGHT_SMALL - 5))
#define textFourthLine (textThirdLine + (FONT_HEIGHT_SMALL - 5))
#define textFifthLine (textFourthLine + (FONT_HEIGHT_SMALL - 5))
#define textSixthLine (textFifthLine + (FONT_HEIGHT_SMALL - 5))

// Consistent Line Spacing for devices like T114 and TEcho/ThinkNode M1 of devices
#define textFirstLine_medium (FONT_HEIGHT_SMALL + 1)
#define textSecondLine_medium (textFirstLine_medium + FONT_HEIGHT_SMALL)
#define textThirdLine_medium (textSecondLine_medium + FONT_HEIGHT_SMALL)
#define textFourthLine_medium (textThirdLine_medium + FONT_HEIGHT_SMALL)
#define textFifthLine_medium (textFourthLine_medium + FONT_HEIGHT_SMALL)
#define textSixthLine_medium (textFifthLine_medium + FONT_HEIGHT_SMALL)

// Consistent Line Spacing for devices like VisionMaster T190
#define textFirstLine_large (FONT_HEIGHT_SMALL + 1)
#define textSecondLine_large (textFirstLine_large + (FONT_HEIGHT_SMALL + 5))
#define textThirdLine_large (textSecondLine_large + (FONT_HEIGHT_SMALL + 5))
#define textFourthLine_large (textThirdLine_large + (FONT_HEIGHT_SMALL + 5))
#define textFifthLine_large (textFourthLine_large + (FONT_HEIGHT_SMALL + 5))
#define textSixthLine_large (textFifthLine_large + (FONT_HEIGHT_SMALL + 5))

// Quick screen access
#define SCREEN_WIDTH display->getWidth()
#define SCREEN_HEIGHT display->getHeight()

// Shared state (declare inside namespace)
extern bool hasUnreadMessage;
extern bool isMuted;
extern bool isHighResolution;
void determineResolution(int16_t screenheight, int16_t screenwidth);

// Rounded highlight (used for inverted headers)
void drawRoundedHighlight(OLEDDisplay *display, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r);

// Shared battery/time/mail header
void drawCommonHeader(OLEDDisplay *display, int16_t x, int16_t y, const char *titleStr = "", bool battery_only = false);

const int *getTextPositions(OLEDDisplay *display);

bool isAllowedPunctuation(char c);

std::string sanitizeString(const std::string &input);

} // namespace graphics
