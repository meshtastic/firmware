#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    Shows the Meshtastic logo fullscreen, with accompanying text
    Used for boot and shutdown

*/

#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h"
#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

class LogoApplet : public Applet, public concurrency::OSThread
{
  public:
    LogoApplet();
    void onRender() override;
    void onForeground() override;
    void onBackground() override;

    // Note: interacting directly with an applet like this is non-standard
    // Only permitted because this is a "system applet", which has special behavior and interacts directly with WindowManager

    void showBootScreen();
    void showShutdownScreen();

  protected:
    int32_t runOnce() override;

    std::string textLeft;
    std::string textRight;
    std::string textTitle;
    AppletFont fontTitle;

    WindowManager *windowManager = nullptr; // For convenience
};

} // namespace NicheGraphics::InkHUD

#endif