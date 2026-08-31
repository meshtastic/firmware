#pragma once
#if defined(TTGO_T_ECHO_PLUS) && defined(USE_EINK)
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
namespace graphics::SatellitesRenderer
{
void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
}
#endif
