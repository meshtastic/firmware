#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    Shows the Meshtastic logo fullscreen, with accompanying text
    Used for boot and shutdown

*/

#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h"
#include "graphics/niche/InkHUD/SystemApplet.h"

namespace NicheGraphics::InkHUD
{

class LogoApplet : public SystemApplet, public concurrency::OSThread
{
  public:
    LogoApplet();
    void onRender() override;
    void onForeground() override;
    void onBackground() override;
    void onShutdown() override;
    void onReboot() override;

  protected:
    int32_t runOnce() override;

    std::string textLeft;
    std::string textRight;
    std::string textTitle;
    AppletFont fontTitle;
    bool inverted = false; // Invert colors. Used during shutdown, to restore display health.
};

} // namespace NicheGraphics::InkHUD

#endif