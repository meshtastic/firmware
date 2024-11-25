#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./LastHeardNodesApplet.h"

using namespace NicheGraphics;

InkHUD::LastHeardNodesApplet::LastHeardNodesApplet() : NodeListApplet() {}

// When applet starts
// This happens at boot, or from config changes via menu
// This does *not* happen when user cycles through applets with the user button
void InkHUD::LastHeardNodesApplet::onActivate()
{
    // Initially, we fill from NodeDB
    // The rest of the time, we update manually with mesh packets from handleReceived
    populateNodeList();
}

// The "Last Heard" variant of the Node List applet will include all applets
// They will still be sorted by time last seen
bool InkHUD::LastHeardNodesApplet::shouldListNode(NodeListItem item)
{
    return true;
}

// Text drawn in the usual applet header
// Handled by base class: NodeListApplet
std::string InkHUD::LastHeardNodesApplet::getHeaderText()
{
    std::string text = "Nodes: ";
    text += to_string(nodeDB->getNumMeshNodes() - 1);

    return text;
}

void InkHUD::LastHeardNodesApplet::updateActivityInfo()
{
    // uint16_t currentCount = Applet::getActiveNodeCount();
    // if (LastHeardNodesApplet::activeNodeCount != currentCount)
    //     requestUpdate();

    // LastHeardNodesApplet::activeNodeCount = currentCount;
}

#endif