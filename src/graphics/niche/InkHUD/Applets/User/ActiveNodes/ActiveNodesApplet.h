#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Shows a list of nodes which have been recently active
For testing, this duration is 2 minutes(?)
In future, this duration will be configurable in the onscreen menu

Most of the work is done by the shared InkHUD::NodeListApplet base class

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applets/Bases/NodeList/NodeListApplet.h"

namespace NicheGraphics::InkHUD
{

class Applet;

class ActiveNodesApplet : public NodeListApplet
{
  public:
    ActiveNodesApplet();

  protected:
    bool shouldListNode(NodeListItem item) override;
    std::string getHeaderText() override;

    void updateActivityInfo() override;
};

} // namespace NicheGraphics::InkHUD

#endif