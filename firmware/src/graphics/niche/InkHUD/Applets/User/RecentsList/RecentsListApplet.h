#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Shows a list of nodes which have been recently active
The length of this "recently active" window is configurable using the onscreen menu

Most of the work is done by the shared InkHUD::NodeListApplet base class

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applets/Bases/NodeList/NodeListApplet.h"

namespace NicheGraphics::InkHUD
{

class RecentsListApplet : public NodeListApplet, public concurrency::OSThread
{
  protected:
    // Used internally to count the number of active nodes
    // We count for ourselves, instead of using the value provided by NodeDB,
    // as the values occasionally differ, due to the timing of our Applet's purge method
    struct Age {
        uint32_t nodeNum;
        uint32_t seenAtMs;
    };

  public:
    RecentsListApplet();
    void onActivate() override;
    void onDeactivate() override;

  protected:
    int32_t runOnce() override;

    void handleParsed(CardInfo c) override; // Store new info, update active count, update display if needed
    std::string getHeaderText() override;   // Set title for this applet

    void seenNow(NodeNum nodeNum);        // Record that we have just seen this node, for active node count
    void prune();                         // Remove cards for nodes which we haven't seen recently
    bool isActive(uint32_t seenAtMillis); // Is a node still active, based on when we last heard it?

    std::deque<Age> ages; // Information about when we last heard nodes. Independent of NodeDB
};

} // namespace NicheGraphics::InkHUD

#endif