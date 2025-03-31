#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

An applet with nonstandard behavior, which will require special handling

For features like the menu, and the battery icon.

*/

#pragma once

#include "configuration.h"

#include "./Applet.h"

namespace NicheGraphics::InkHUD
{

class SystemApplet : public Applet
{
  public:
    // System applets have the right to:

    bool handleInput = false;   // - respond to input from the user button
    bool lockRendering = false; // - prevent other applets from being rendered during an update
    bool lockRequests = false;  // - prevent other applets from triggering display updates

    virtual void onReboot() { onShutdown(); } // - handle reboot specially

    // Other system applets may take precedence over our own system applet though
    // The order an applet is passed to WindowManager::addSystemApplet determines this hierarchy (added earlier = higher rank)

  private:
    // System applets are always running (active), but may not be visible (foreground)

    void onActivate() override {}
    void onDeactivate() override {}
};

}; // namespace NicheGraphics::InkHUD

#endif