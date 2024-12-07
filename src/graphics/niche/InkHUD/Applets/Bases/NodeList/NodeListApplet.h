#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Base class for Applets which display a list of nodes

Currently, the nodes are always ordered by "most recently seen".
The two current child classes (ActiveNodesApplet, LastHeardNodesApplet) override the shouldListNode method,
to determine whether this base class will render an item for any given node.

This is likely to change if more functionality is added (e.g alternative sorting methods)

*/

#pragma once

#include "configuration.h"

#include "./NodeListItem.h"
#include "graphics/niche/InkHUD/Applet.h"

#include "MeshModule.h"
#include "NodeDB.h"
#include "gps/GeoCoord.h"
#include "mesh/MeshTypes.h"

namespace NicheGraphics::InkHUD
{

class Applet;

class NodeListApplet : public Applet, public MeshModule, public concurrency::OSThread
{
  public:
    NodeListApplet(const char *name);
    void render() override;

    void onForeground() override;
    void onBackground() override;

    // Called when an applet is fully stopped
    // (Not when moved to background)
    void onDeactivate() override;

    // MeshModule overrides
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  protected:
    int32_t runOnce() override;

    virtual void updateActivityInfo() {}                // Prune nodelist before render, in case some items are no longer valid
    virtual bool shouldListNode(NodeListItem item) = 0; // Should derived applet render a given node?
    virtual std::string getHeaderText() = 0;            // Title for the applet's header. Todo: get this info another way?

    // UI element: a "mobile phone" style signal indicator
    void drawSignalIndicator(int16_t x, int16_t y, uint16_t w, uint16_t h, SignalStrength signal);

    void populateNodeList();                                        // Initial fill nodelist with info from NodeDB
    void sortNodeList();                                            // Order the nodelist by epoch time heard
    void recordHeard(NodeListItem justHeard);                       // Add a new entry to the nodelist
    static bool compareLastHeard(NodeListItem r1, NodeListItem r2); // Sort function for ordering nodelist
    bool heardRecently(NodeListItem item);                          // Does the item meet our critera for an "Active Node"?

    std::vector<NodeListItem> ordered; // The main list of nodes
};

} // namespace NicheGraphics::InkHUD

#endif