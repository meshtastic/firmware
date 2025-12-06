#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

    Shows info on how to use InkHUD
    - tutorial at first boot
    - additional tips in certain situation (e.g. bad shutdown, region unset)

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/SystemApplet.h"

namespace NicheGraphics::InkHUD
{

class TipsApplet : public SystemApplet
{
  protected:
    enum class Tip {
        WELCOME,
        FINISH_SETUP,
        SAFE_SHUTDOWN,
        CUSTOMIZATION,
        BUTTONS,
        ROTATION,
    };

  public:
    TipsApplet();

    void onRender() override;
    void onForeground() override;
    void onBackground() override;
    void onButtonShortPress() override;

  protected:
    void renderWelcome(); // Very first screen of tutorial

    std::deque<Tip> tipQueue; // List of tips to show, one after another

    WindowManager *windowManager = nullptr; // For convenience. Set in constructor.
};

} // namespace NicheGraphics::InkHUD

#endif