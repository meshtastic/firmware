#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    Low-profile bottom-edge touch status indicator.
    Shown only while touch input is disabled.

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/SystemApplet.h"

namespace NicheGraphics::InkHUD
{

class TouchStatusApplet : public SystemApplet
{
  public:
    TouchStatusApplet();

    void onRender(bool full) override;
};

} // namespace NicheGraphics::InkHUD

#endif
