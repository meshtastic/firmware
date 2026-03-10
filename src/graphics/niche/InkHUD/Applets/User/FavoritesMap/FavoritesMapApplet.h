#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Plots position of favorited nodes from DB, with North facing up.
Scaled to fit the most distant node.
Size of marker represents hops away.
The favorite node which most recently sent a position will be labeled.

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applets/Bases/Map/MapApplet.h"

#include "SinglePortModule.h"

namespace NicheGraphics::InkHUD
{

class FavoritesMapApplet : public MapApplet, public SinglePortModule
{
  public:
    FavoritesMapApplet() : SinglePortModule("FavoritesMapApplet", meshtastic_PortNum_POSITION_APP) {}
    void onRender(bool full) override;

  protected:
    bool shouldDrawNode(meshtastic_NodeInfoLite *node) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    NodeNum lastFrom = 0; // Sender of most recent favorited (non-local) position packet
    float lastLat = 0.0;
    float lastLng = 0.0;
    float lastHopsAway = 0;

    float ourLastLat = 0.0; // Info about most recent local position
    float ourLastLng = 0.0;
};

} // namespace NicheGraphics::InkHUD

#endif
