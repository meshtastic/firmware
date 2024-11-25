#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Shows a list of all nodes (recently heard or not), sorted by time last heard.
Most of the work is done by the InkHUD::NodeListApplet base class

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applets/Bases/NodeList/NodeListApplet.h"

namespace NicheGraphics::InkHUD
{

class Applet;

class LastHeardNodesApplet : public NodeListApplet
{
  public:
    LastHeardNodesApplet();
    void onActivate() override;

  protected:
    bool shouldListNode(NodeListItem item) override;
    std::string getHeaderText() override;
    void updateActivityInfo() override;

    uint16_t activeNodeCount = 0;
};

} // namespace NicheGraphics::InkHUD

#endif