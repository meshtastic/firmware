#include "configuration.h"
#if HAS_SCREEN
#include "RadarRenderer.h"
#include "NodeDB.h"
#include "UIRenderer.h"
#include "gps/GeoCoord.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include <algorithm>
#include <cmath>
#include <vector>

// Owns hasHeading() / getHeading() / estimatedHeading()
extern graphics::Screen *screen;

namespace graphics
{
namespace RadarRenderer
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static float niceScaleMeters(float maxDistM)
{
    static const float scales[] = {50,    100,   250,    500,    1000,   2000,
                                    5000,  10000, 25000,  50000,  100000, 250000,
                                    500000};
    for (float s : scales)
        if (maxDistM <= s)
            return s;
    return 1000000.0f;
}

static void formatDistM(char *buf, size_t len, float metres)
{
    const bool imperial = (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL);
    if (imperial) {
        const float miles = metres / 1609.34f;
        if (miles < 0.1f)
            snprintf(buf, len, "%dft", (int)(metres * 3.28084f));
        else if (miles < 10.0f)
            snprintf(buf, len, "%.1fmi", miles);
        else
            snprintf(buf, len, "%dmi", (int)(miles + 0.5f));
    } else {
        if (metres < 1000.0f)
            snprintf(buf, len, "%dm", (int)metres);
        else if (metres < 10000.0f)
            snprintf(buf, len, "%.1fkm", metres / 1000.0f);
        else
            snprintf(buf, len, "%dkm", (int)(metres / 1000.0f + 0.5f));
    }
}

// Plot a 3×3 node marker at the given absolute bearing and normalised distance.
// headingRad rotates the radar so the device's facing direction is always up.
static void plotNode(OLEDDisplay *display, int cx, int cy, int radius, float bearingRad, float headingRad, float norm)
{
    const float rel = bearingRad - headingRad;
    const int px = cx + (int)(radius * norm * sinf(rel));
    const int py = cy - (int)(radius * norm * cosf(rel));
    display->fillRect(px - 1, py - 1, 3, 3);
}

// ---------------------------------------------------------------------------
// Main draw function
// ---------------------------------------------------------------------------

void drawRadarScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    graphics::drawCommonHeader(display, x, y, "Radar");

    const int headerH = FONT_HEIGHT_SMALL - 1;
    const int sw = SCREEN_WIDTH;
    const int sh = SCREEN_HEIGHT;
    const int contentH = sh - headerH;

    // -----------------------------------------------------------------------
    // Radar geometry: circle is centred horizontally, fills the content height.
    // On 128×64 this gives radius=27, leaving ~37 px on each side for labels.
    // On larger displays the circle grows proportionally.
    // -----------------------------------------------------------------------
    const int radarDiam = contentH - 2;          // 1 px margin top + bottom
    const int radarRadius = radarDiam / 2;
    const int radarCX = x + sw / 2;              // horizontally centred
    const int radarCY = y + headerH + 1 + radarRadius;

    // The ring legend sits to the right of the circle.
    const int legendX = radarCX + radarRadius + 3;
    // Three labels, each FONT_HEIGHT_SMALL tall, centred around radarCY.
    const int legendY3 = radarCY - FONT_HEIGHT_SMALL - 3; // outer ring label
    const int legendY2 = radarCY - 3;                     // middle ring label
    const int legendY1 = radarCY + FONT_HEIGHT_SMALL - 3; // inner ring label

    // -----------------------------------------------------------------------
    // Own position — bail gracefully if GPS is unavailable.
    // -----------------------------------------------------------------------
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (!ourNode || !nodeDB->hasValidPosition(ourNode)) {
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + sw / 2, y + sh / 2 - FONT_HEIGHT_SMALL / 2, "No GPS fix");
        return;
    }

    const double myLat = ourNode->position.latitude_i * 1e-7;
    const double myLon = ourNode->position.longitude_i * 1e-7;

    // -----------------------------------------------------------------------
    // Heading — BMX160 via screen->setHeading() (tilt-compensated compass
    // fusion).  Falls back to GPS movement track, then north-up (0).
    // -----------------------------------------------------------------------
    const float headingRad = screen->hasHeading() ? screen->getHeading() * DEG_TO_RAD
                                                   : screen->estimatedHeading(myLat, myLon);
    const bool usingIMU = screen->hasHeading();

    // -----------------------------------------------------------------------
    // Collect remote nodes with valid GPS positions.
    // -----------------------------------------------------------------------
    struct Entry {
        meshtastic_NodeInfoLite *node;
        float distM;
        float bearingRad;
    };

    std::vector<Entry> entries;
    float maxDistM = 1.0f;

    const int numNodes = nodeDB->getNumMeshNodes();
    for (int i = 0; i < numNodes; i++) {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (!n || n->num == nodeDB->getNodeNum())
            continue;
        if (!nodeDB->hasValidPosition(n))
            continue;

        const double nodeLat = n->position.latitude_i * 1e-7;
        const double nodeLon = n->position.longitude_i * 1e-7;
        const float dist = GeoCoord::latLongToMeter(myLat, myLon, nodeLat, nodeLon);
        const float brg = GeoCoord::bearing(myLat, myLon, nodeLat, nodeLon);

        entries.push_back({n, dist, brg});
        if (dist > maxDistM)
            maxDistM = dist;
    }

    const float scale = niceScaleMeters(maxDistM);

    // -----------------------------------------------------------------------
    // Draw radar chrome: three concentric range rings.
    // -----------------------------------------------------------------------
    for (int ring = 1; ring <= 3; ring++)
        display->drawCircle(radarCX, radarCY, (radarRadius * ring) / 3);

    // -----------------------------------------------------------------------
    // North indicator — rotates with IMU heading so it always points true north.
    // Placed just inside the outer ring to avoid clipping the header.
    // -----------------------------------------------------------------------
    {
        const int inset = FONT_HEIGHT_SMALL / 2 + 1;
        const float northBrg = -headingRad; // bearing of north relative to "up"
        const int nx = radarCX + (int)((radarRadius - inset) * sinf(northBrg));
        const int ny = radarCY - (int)((radarRadius - inset) * cosf(northBrg));
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(nx, ny - FONT_HEIGHT_SMALL / 2, "N");
    }

    // Own-node marker: filled 4×4 square at the centre.
    display->fillRect(radarCX - 2, radarCY - 2, 4, 4);

    // -----------------------------------------------------------------------
    // Plot remote nodes.
    // -----------------------------------------------------------------------
    for (const Entry &e : entries) {
        const float norm = std::min(e.distM / scale, 1.0f);
        plotNode(display, radarCX, radarCY, radarRadius, e.bearingRad, headingRad, norm);
    }

    // -----------------------------------------------------------------------
    // Ring scale legend (right of radar circle).
    // Three rows aligned with the centre of the radar, showing the distance
    // each ring represents so the user can read off approximate distances.
    // -----------------------------------------------------------------------
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    char buf[10];

    // Outer ring (full scale)
    formatDistM(buf, sizeof(buf), scale);
    display->drawString(legendX, legendY3, buf);

    // Middle ring (2/3 scale)
    formatDistM(buf, sizeof(buf), scale * 2.0f / 3.0f);
    display->drawString(legendX, legendY2, buf);

    // Inner ring (1/3 scale)
    formatDistM(buf, sizeof(buf), scale / 3.0f);
    display->drawString(legendX, legendY1, buf);

    // IMU active indicator below the legend — helps confirm the RAK12034
    // is working during first-time setup.
    if (usingIMU) {
        display->drawString(legendX, legendY1 + FONT_HEIGHT_SMALL, "IMU");
    }
}

} // namespace RadarRenderer
} // namespace graphics
#endif // HAS_SCREEN
