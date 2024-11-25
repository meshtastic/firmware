#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./PlaceholderApplet.h"

using namespace NicheGraphics;

InkHUD::PlaceholderApplet::PlaceholderApplet()
{
    Applet::name = "Placeholder";
}

void InkHUD::PlaceholderApplet::render()
{
    // This placeholder applet fills its area with sparse diagonal lines
    hatchRegion(0, 0, width(), height(), 8, BLACK);
}

#endif