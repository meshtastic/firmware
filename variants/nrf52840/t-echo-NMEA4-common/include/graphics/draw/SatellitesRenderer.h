#pragma once
#if defined(T_ECHO_NMEA4_FAMILY) && defined(USE_EINK) && !MESHTASTIC_EXCLUDE_GPS
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
namespace graphics::SatellitesRenderer
{
void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
}
#endif
