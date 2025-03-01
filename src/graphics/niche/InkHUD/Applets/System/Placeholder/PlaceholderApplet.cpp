#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./PlaceholderApplet.h"

using namespace NicheGraphics;

InkHUD::PlaceholderApplet::PlaceholderApplet()
{
    // Because this applet sometimes gets processed as if it were a bonafide user applet,
    // it's probably better that we do give it a human readable name, just in case it comes up later.
    // For genuine user applets, this is set by WindowManager::addApplet
    Applet::name = "Placeholder";
}

void InkHUD::PlaceholderApplet::onRender()
{
    // This placeholder applet fills its area with sparse diagonal lines
    hatchRegion(0, 0, width(), height(), 8, BLACK);
}

#endif