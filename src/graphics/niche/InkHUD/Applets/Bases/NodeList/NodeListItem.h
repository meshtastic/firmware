#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

This struct records some basic info about a node we have seen.

It is used by classes based on InkHUD::NodeListApplet.
These classes keep an ordered vector of the NodeListItem objects.

Initially that vector is populated from NodeDB,
however it is quickly trimmed to hold only enough elements to fit on-screen.
After this, we manually make entries (and delete oldest) as we hear packets.
This removes the need to rescan the entire NodeDB every time we render a NodeListApplet.

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

#include "mesh/MeshTypes.h"

namespace NicheGraphics::InkHUD
{

// Information recorded by InkHUD::NodeListApplet when we hear a node
struct NodeListItem {
    NodeNum nodeNum;
    SignalStrength strength;

    // Used to detect which nodes have been recently active, for NodeList derived applets
    // When node's RTC is evenually set, these values will become decades out of date
    // This is helpful, to invalidate old records shown by ActiveNodesApplet
    uint32_t lastHeardEpoch; // Seconds
};

} // namespace NicheGraphics::InkHUD

#endif