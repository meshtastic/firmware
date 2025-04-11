#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Base class for Applets which show nodes on a map

Plots position of for a selection of nodes, with north facing up.
Size of cross represents hops away.
Our own node is identified with a faded label.

The base applet doesn't handle any events; this is left to the derived applets.

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

#include "MeshModule.h"
#include "gps/GeoCoord.h"

namespace NicheGraphics::InkHUD
{

class MapApplet : public Applet
{
  public:
    void onRender() override;

  protected:
    virtual bool shouldDrawNode(meshtastic_NodeInfoLite *node) { return true; } // Allow derived applets to filter the nodes
    virtual void getMapCenter(float *lat, float *lng);
    virtual void getMapSize(uint32_t *widthMeters, uint32_t *heightMeters);

    bool enoughMarkers();                                  // Anything to draw?
    void drawLabeledMarker(meshtastic_NodeInfoLite *node); // Highlight a specific marker

  private:
    // Position and size of a marker to be drawn
    struct Marker {
        float eastMeters = 0;  // Meters east of map center. Negative if west.
        float northMeters = 0; // Meters north of map center. Negative if south.
        bool hasHopsAway = false;
        uint8_t hopsAway = 0; // Determines marker size
    };

    Marker calculateMarker(float lat, float lng, bool hasHopsAway, uint8_t hopsAway);
    void calculateAllMarkers();
    void calculateMapScale();                           // Conversion factor for meters to pixels
    void drawCross(int16_t x, int16_t y, uint8_t size); // Draw the X used for most markers

    float metersToPx = 0; // Conversion factor for meters to pixels
    float latCenter = 0;  // Map center: latitude
    float lngCenter = 0;  // Map center: longitude

    std::list<Marker> markers;
    uint32_t widthMeters = 0;  // Map width: meters
    uint32_t heightMeters = 0; // Map height: meters
};

} // namespace NicheGraphics::InkHUD

#endif