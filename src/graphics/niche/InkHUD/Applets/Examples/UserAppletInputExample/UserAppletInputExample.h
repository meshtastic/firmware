#ifdef MESHTASTIC_INCLUDE_INKHUD

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

class UserAppletInputExampleApplet : public Applet
{
  public:
    void onActivate() override;

    void onRender(bool full) override;
    void onButtonShortPress() override;
    void onButtonLongPress() override;
    void onExitShort() override;
    void onExitLong() override;
    void onNavUp() override;
    void onNavDown() override;
    void onNavLeft() override;
    void onNavRight() override;

  private:
    std::string lastInput = "None";
    bool isGrabbed = false;

    void setGrabbed(bool grabbed);
};

} // namespace NicheGraphics::InkHUD

#endif