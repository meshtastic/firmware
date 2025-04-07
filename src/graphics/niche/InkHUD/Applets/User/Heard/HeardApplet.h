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

class HeardApplet : public NodeListApplet
{
  public:
    HeardApplet() : NodeListApplet("HeardApplet") {}
    void onActivate() override;
    void onDeactivate() override;

  protected:
    void handleParsed(CardInfo c) override; // Store new info, and update display if needed
    std::string getHeaderText() override;   // Set title for this applet

    void populateFromNodeDB(); // Pre-fill the CardInfo collection from NodeDB
};

} // namespace NicheGraphics::InkHUD

#endif