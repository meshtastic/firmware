#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./BasicExampleApplet.h"

using namespace NicheGraphics;

// All drawing happens here
// Our basic example doesn't do anything useful. It just passively prints some text.
void InkHUD::BasicExampleApplet::onRender()
{
    printAt(0, 0, "Hello, World!");

    // If text might contain "special characters", is needs parsing first
    // This applies to data such as text-messages and and node names

    // std::string greeting = parse("Grüezi mitenand!");
    // printAt(0, 0, greeting);
}

#endif