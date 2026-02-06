#ifdef MESHTASTIC_INCLUDE_INKHUD

#pragma once

#include "configuration.h"
#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

class ObdApplet : public Applet, public concurrency::OSThread
{
  public:
    ObdApplet() : concurrency::OSThread("ObdApplet") {}
    void onRender(bool full) override;
    int32_t runOnce() override;
};

} // namespace NicheGraphics::InkHUD

#endif
