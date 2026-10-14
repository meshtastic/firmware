#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Radar minimap, north-up: our node at the centre, the closest online nodes
plotted at their true bearing and proportional distance. Each plotted node gets
a unique marker, repeated beside its name in the list.

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

#include "SinglePortModule.h"

namespace NicheGraphics::InkHUD
{

class RadarApplet : public Applet, public SinglePortModule
{
  public:
    RadarApplet() : SinglePortModule("RadarApplet", meshtastic_PortNum_POSITION_APP) {}
    void onRender(bool full) override;

  protected:
    struct Entry {
        meshtastic_NodeInfoLite *node;
        float distM;
        float bearingRad;
    };

    void onActivate() override { loopbackOk = true; }
    void onDeactivate() override { loopbackOk = false; }
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    void drawMarker(int16_t px, int16_t py, uint8_t sym, int16_t r);
    void drawListRow(int16_t left, int16_t right, int16_t centerY, const Entry &e, uint8_t sym, int16_t r);

    float ourLastLat = 0.0; // Our position at the last redraw, to rate-limit local updates
    float ourLastLng = 0.0;
};

} // namespace NicheGraphics::InkHUD

#endif
