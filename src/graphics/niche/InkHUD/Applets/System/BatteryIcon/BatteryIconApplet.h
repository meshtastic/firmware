#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

This applet floats top-left, giving a graphical representation of battery remaining
It should be optional, enabled by the on-screen menu

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

class BatteryIconApplet : public Applet
{
  public:
    void render() override;

    void onActivate() override;
    void onDeactivate() override;

    // Todo: observe power status, cache result, periodically request update
};

} // namespace NicheGraphics::InkHUD

#endif