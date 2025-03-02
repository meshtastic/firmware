#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./BasicExampleApplet.h"

using namespace NicheGraphics;

// All drawing happens here
// Our basic example doesn't do anything useful. It just passively prints some text.
void InkHUD::BasicExampleApplet::onRender()
{
    print("Hello, World!");
}

#endif