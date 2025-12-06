#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Shown when a tile doesn't have any other valid Applets
Fills the area with diagonal lines

*/

#include "configuration.h"

#include "graphics/niche/InkHUD/SystemApplet.h"

namespace NicheGraphics::InkHUD
{

class PlaceholderApplet : public SystemApplet
{
  public:
    void onRender() override;

    // Note: onForeground, onBackground, and wantsToRender are not meaningful for this applet.
    // The window manager decides when and where it should be rendered
    // It may be drawn to several different tiles during an Renderer::render call
};

} // namespace NicheGraphics::InkHUD

#endif