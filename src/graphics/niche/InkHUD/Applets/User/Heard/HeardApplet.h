#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Shows a list of all nodes (recently heard or not), sorted by time last heard.
Most of the work is done by the InkHUD::ChronoListApplet base class

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applets/Bases/ChronoList/ChronoListApplet.h"

namespace NicheGraphics::InkHUD
{

class Applet;

class HeardApplet : public ChronoListApplet
{
  public:
    HeardApplet() : ChronoListApplet("HeardApplet") {}
    void onActivate() override;

  protected:
    bool shouldListNode(ChronoListItem item) override;
    std::string getHeaderText() override;
    void updateActivityInfo() override;

    uint16_t activeNodeCount = 0;
};

} // namespace NicheGraphics::InkHUD

#endif