#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Plots position of all nodes from DB, with North facing up.
Scaled to fit the most distant node.
Size of cross represents hops away.
The node which has most recently sent a position will be labeled.

This applet takes quite a lot of computation to render.
In order to avoid blocking execution, it calculates its data gradually, using runOnce.
The consequence of this is that the WindowManager does have to await this applet,
which makes the WindowManager::render code a bit messy.

In future, this might before a base class, with various map-base applets extending it.

*/

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

#include "concurrency/OSThread.h"
#include "mesh/MeshModule.h"

namespace NicheGraphics::InkHUD
{

class MapApplet : public Applet, public MeshModule, public concurrency::OSThread
{
  public:
    // Position and size of a node on the map
    struct MapMarker {
        float x;    // Relative to the map width
        float y;    // Relative to the map height
        float size; // Related to hops away
    };

    MapApplet();
    void beforeRender() override;
    void render() override;

  protected:
    // Tracks progress while we gradually calculate the map data
    enum CalculationThreadStep {
        STEP_INIT,
        STEP_CHECK_FOR_NODES,
        STEP_OUR_POSITION,
        STEP_FIND_EXTENTS,
        STEP_RANGE,
        STEP_MARKERS,
        STEP_RENDER,
    } calcStep = STEP_INIT;

    // What data do we have available? Why? What should be do about that?
    enum CalculationState {
        CALC_NOT_STARTED,
        CALC_SUCCEDED,
        CALC_FAILED_NO_POSITION,
        CALC_FAILED_NO_NODES,
    } calcState = CALC_NOT_STARTED;

    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    int32_t runOnce() override;

    bool serviceCalculationThread(); // Gradual calculation, via runOnce
    void freeCalculationResources(); // Clear data, once rendered

    // Draw one marker on the map
    void renderMarker(MapMarker m, float scaleX, float scaleY, uint16_t padding, meshtastic_NodeInfoLite *node = nullptr);

    // UI elements: a node on the map
    void drawCross(int16_t markerX, int16_t markerY, uint16_t width, uint16_t height, Color color);
    void drawUnlabeledMarker(int16_t x, int16_t y, int16_t markerSize, bool highlighted = false);
    void drawLabeledMarker(int16_t x, int16_t y, meshtastic_NodeInfoLite *node, int16_t markerSize);

    // Scale a float from one range to another. Float implementation of Arduino's map()
    float remapFloat(float val, float low, float high, float newLow, float newHigh);

    MapMarker ourMarker;            // Marker for our own device. Gets a special label
    std::vector<MapMarker> markers; // *All* the markers (except ours)

    // Info about the node from which we most recently received position data
    // It gets its own special marker
    MapMarker lastHeardMarker;
    NodeNum lastHeardNodeNum = 0;
    uint32_t lastHeardLat = 0;
    uint32_t lastHeardLong = 0;
    uint8_t lastHeardHopsAway = 0;

    // Previous position for our own node
    // Used to prevent constant re-rendering when our position is updated every few seconds by a connected phone
    uint32_t ourLastLat = 0;
    uint32_t ourLastLong = 0;

    // Horizontal and Vertical span of our map
    // Calculated to fit the most distant nodes
    // Determines map scale
    float rangeNorthSouthMeters;
    float rangeEastWestMeters;
};

} // namespace NicheGraphics::InkHUD

#endif