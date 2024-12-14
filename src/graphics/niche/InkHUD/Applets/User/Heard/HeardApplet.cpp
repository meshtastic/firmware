#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./HeardApplet.h"

using namespace NicheGraphics;

// When applet starts
// This happens at boot, or from config changes via menu
// This does *not* happen when user cycles through applets with the user button
void InkHUD::HeardApplet::onActivate()
{
    // Initially, we fill from NodeDB
    // The rest of the time, we update manually with mesh packets from handleReceived
    populateChronoList();
}

// The "Heard" variant of the Chrono List applet will include all applets
// They will still be sorted by time last seen
bool InkHUD::HeardApplet::shouldListNode(ChronoListItem item)
{
    return true;
}

// Text drawn in the usual applet header
// Handled by base class: ChronoListApplet
std::string InkHUD::HeardApplet::getHeaderText()
{
    uint16_t nodeCount = nodeDB->getNumMeshNodes() - 1; // Don't count our own node

    std::string text = "Heard: ";

    // Print node count, if nodeDB not yet nearing full
    if (nodeCount < MAX_NUM_NODES) {
        text += to_string(nodeCount); // Max nodes
        text += " ";
        text += (nodeCount == 1) ? "node" : "nodes";
    }

    return text;
}

void InkHUD::HeardApplet::updateActivityInfo()
{
    // uint16_t currentCount = Applet::getActiveNodeCount();
    // if (HeardApplet::activeNodeCount != currentCount)
    //     requestUpdate();

    // HeardApplet::activeNodeCount = currentCount;
}

#endif