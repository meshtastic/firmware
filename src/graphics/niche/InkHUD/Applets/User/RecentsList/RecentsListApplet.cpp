#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./RecentsListApplet.h"

#include "gps/RTC.h"
#include "mesh/NodeDB.h"

using namespace NicheGraphics;

InkHUD::RecentsListApplet::RecentsListApplet() : ChronoListApplet("RecentsListApplet")
{
    // No timer activity at boot
    OSThread::disable();
}

// Tell base class which nodes should be drawn
bool InkHUD::RecentsListApplet::shouldListNode(ChronoListItem item)
{
    // Only draw if heard within our time limit
    return heardRecently(item);
}

// Text to be shown at top of applet
// ChronoListApplet base class allows us to set this dynamically
// Might want to adjust depending on node count, RTC status, etc
std::string InkHUD::RecentsListApplet::getHeaderText()
{
    std::string text;

    // Print the length of our "Recents" time-window
    text += "Last ";
    text += to_string(settings.recentlyActiveSeconds / 60);
    text += " mins";

    // Print the node count
    // - only if our RTC is set, to avoid weird things, if "last heard" is in the future

    if (getRTCQuality() != RTCQualityNone) {
        const uint16_t nodeCount = getActiveNodeCount();
        text += ": ";
        text += to_string(nodeCount);
        text += " ";
        text += (nodeCount == 1) ? "node" : "nodes";
    }

    return text;
}

// Prune our applets list of active nodes, in case any are now too old
// Runs at regular intervals
void InkHUD::RecentsListApplet::updateActivityInfo()
{
    bool modified = false;

    for (auto i = ordered.begin(); i < ordered.end();) {
        if (!heardRecently(*i)) {
            i = ordered.erase(i);
            modified = true;
        } else
            i++;
    }

    // Note: not requesting autoshow, because we're purging old data, not displaying new
    // In this situtaion, the display will only update if our applet is already foreground, even if autoshow is permitted
    if (modified)
        requestUpdate();
}

#endif