#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

This applet floats top-left, giving a graphical representation of battery remaining
It should be optional, enabled by the on-screen menu

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

#include "PowerStatus.h"

namespace NicheGraphics::InkHUD
{

class BatteryIconApplet : public Applet
{
  public:
    void onRender() override;

    void onActivate() override;
    void onDeactivate() override;

    int onPowerStatusUpdate(const meshtastic::Status *status); // Called when new info about battery is available

  protected:
    // Get informed when new information about the battery is available (via onPowerStatusUpdate method)
    CallbackObserver<BatteryIconApplet, const meshtastic::Status *> powerStatusObserver =
        CallbackObserver<BatteryIconApplet, const meshtastic::Status *>(this, &BatteryIconApplet::onPowerStatusUpdate);

    uint8_t socRounded = 0; // Battery state of charge, rounded to nearest 10%
};

} // namespace NicheGraphics::InkHUD

#endif