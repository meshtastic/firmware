#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./BasicExampleApplet.h"

using namespace NicheGraphics;

// All drawing happens here
// Our basic example doesn't do anything useful. It just passively prints some text.
void InkHUD::BasicExampleApplet::onRender()
{
    printAt(0, 0, "Hello, World!");
}

#endif