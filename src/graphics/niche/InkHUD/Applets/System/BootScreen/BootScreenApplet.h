#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Shown at startup.
Initial proof of concept only.
May merge with other system applets in future (deep sleep screen?)

*/

#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h"
#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

class BootScreenApplet : public Applet, public concurrency::OSThread
{
  public:
    BootScreenApplet();
    void render() override;

    void onForeground() override;
    void onBackground() override;

  protected:
    int32_t runOnce() override;
};

} // namespace NicheGraphics::InkHUD

#endif