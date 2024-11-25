#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./ActiveNodesApplet.h"

#include "gps/RTC.h"
#include "mesh/NodeDB.h"

using namespace NicheGraphics;

InkHUD::ActiveNodesApplet::ActiveNodesApplet() : NodeListApplet()
{
    // No timer activity at boot
    OSThread::disable();
}

// Tell base class which nodes should be drawn
bool InkHUD::ActiveNodesApplet::shouldListNode(NodeListItem item)
{
    // Only draw if heard within our time limit
    return heardRecently(item);
}

// Text to be shown at top of applet
// NodeListApplet base class allows us to set this dynamically
// Might want to adjust depending on node count, RTC status, etc
std::string InkHUD::ActiveNodesApplet::getHeaderText()
{
    std::string text;

    // Print node count, on right side of header
    // - only if our RTC is set, to avoid weird things, if "last heard" is in the future
    const uint16_t nodeCount = getActiveNodeCount();

    if (nodeCount > 0) {
        text += "Active: ";
        text += to_string(getActiveNodeCount());
        text += " of ";
        text += to_string(nodeDB->getNumMeshNodes() - 1); // Minus one: exclude our own node
    } else
        text += "Active Nodes";

    return text;
}

// Prune our applets list of active nodes, in case any are now too old
// Runs at regular intervals
void InkHUD::ActiveNodesApplet::updateActivityInfo()
{
    bool modified = false;

    for (auto i = ordered.begin(); i < ordered.end();) {
        if (!heardRecently(*i)) {
            i = ordered.erase(i);
            modified = true;
        } else
            i++;
    }

    if (modified)
        requestUpdate();
}

#endif