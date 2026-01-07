#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Plots position of all nodes from DB, with North facing up.
Scaled to fit the most distant node.
Size of cross represents hops away.
The node which has most recently sent a position will be labeled.

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applets/Bases/Map/MapApplet.h"

#include "SinglePortModule.h"

namespace NicheGraphics::InkHUD
{

class PositionsApplet : public MapApplet, public SinglePortModule
{
  public:
    PositionsApplet() : SinglePortModule("PositionsApplet", meshtastic_PortNum_POSITION_APP) {}
    void onRender() override;

  protected:
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    NodeNum lastFrom = 0; // Sender of most recent (non-local) position packet
    float lastLat = 0.0;
    float lastLng = 0.0;
    float lastHopsAway = 0;

    float ourLastLat = 0.0; // Info about the most recent (non-local) position packet
    float ourLastLng = 0.0; // Info about most recent *local* position
};

} // namespace NicheGraphics::InkHUD

#endif