#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./PlaceholderApplet.h"

using namespace NicheGraphics;

void InkHUD::PlaceholderApplet::onRender()
{
    // This placeholder applet fills its area with sparse diagonal lines
    hatchRegion(0, 0, width(), height(), 8, BLACK);
}

#endif