#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

System Applet for aligning a physical keyboard's arrow keys with the screen

Shown the first time a keyboard is connected, separately from the joystick's AlignStick applet,
as a device can have both. The alignment itself is stored by the input source (see CardKbInput)

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applets/System/AlignStick/AlignStickApplet.h"

namespace NicheGraphics::InkHUD
{

class AlignKeyboardApplet : public AlignStickApplet
{
  public:
    AlignKeyboardApplet();

    void onNavUp() override;
    void onNavDown() override;
    void onNavLeft() override;
    void onNavRight() override;
};

} // namespace NicheGraphics::InkHUD

#endif
