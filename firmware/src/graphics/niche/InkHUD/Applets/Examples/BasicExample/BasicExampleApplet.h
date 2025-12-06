#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

A bare-minimum example of an InkHUD applet.
Only prints Hello World.

In variants/<your device>/nicheGraphics.h:

    - include this .h file
    - add the following line of code:
        windowManager->addApplet("Basic", new InkHUD::BasicExampleApplet);

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

class BasicExampleApplet : public Applet
{
  public:
    // You must have an onRender() method
    // All drawing happens here

    void onRender() override;
};

} // namespace NicheGraphics::InkHUD

#endif