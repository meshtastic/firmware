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

// Screen instance — owns hasHeading()/getHeading()/estimatedHeading()
extern graphics::Screen *screen;

namespace graphics
{
namespace RadarRenderer
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Round maxDistM (metres) up to the nearest "nice" radar range.
 * Returns the chosen scale in metres.
 */
static float niceScaleMeters(float maxDistM)
{
    static const float scales[] = {50,    100,   250,    500,    1000,  2000,
                                    5000,  10000, 25000,  50000,  100000, 250000,
                                    500000};
    for (float s : scales) {
        if (maxDistM <= s)
            return s;
    }
    return 1000000.0f;
}

/** Format a distance (metres) as a compact human-readable string. */
static void formatDistM(char *buf, size_t len, float metres)
{
    bool imperial = (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL);
    if (imperial) {
        float miles = metres / 1609.34f;
        if (miles < 0.1f) {
            snprintf(buf, len, "%dft", (int)(metres * 3.28084f));
        } else if (miles < 10.0f) {
            snprintf(buf, len, "%.1fmi", miles);
        } else {
            snprintf(buf, len, "%dmi", (int)(miles + 0.5f));
        }
    } else {
        if (metres < 1000.0f) {
            snprintf(buf, len, "%dm", (int)metres);
        } else if (metres < 10000.0f) {
            snprintf(buf, len, "%.1fkm", metres / 1000.0f);
        } else {
            snprintf(buf, len, "%dkm", (int)(metres / 1000.0f + 0.5f));
        }
    }
}

/**
 * Plot a point on the radar circle.
 *
 * @param bearingRad   Absolute bearing to the point (radians, 0 = north).
 * @param headingRad   Device heading (radians).  In heading-up mode we subtract
 *                     this from bearingRad so the device's facing direction is
 *                     always rendered at the top of the circle.
 * @param norm         Normalised distance [0..1] from centre to outer ring.
 */
static void plotPoint(OLEDDisplay *display, int cx, int cy, int radius, float bearingRad, float headingRad, float norm)
{
    const float relBrg = bearingRad - headingRad;
    const int px = cx + (int)(radius * norm * sinf(relBrg));
    const int py = cy - (int)(radius * norm * cosf(relBrg));
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
    // Radar circle geometry.
    // Limit diameter to 2/3 of screen width so the info panel always fits.
    // -----------------------------------------------------------------------
    const int radarDiam = std::min(contentH - 2, (sw * 2) / 3);
    const int radarRadius = radarDiam / 2;
    const int radarCX = x + radarRadius + 1;
    const int radarCY = y + headerH + 1 + radarRadius;
    const int infoPanelX = radarCX + radarRadius + 4;

    // -----------------------------------------------------------------------
    // Own position — bail out gracefully if GPS is unavailable.
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
    // Heading — IMU (BMX160 / RAK12034) is preferred; GPS-estimated movement
    // heading is used as fallback.  When neither is available the radar falls
    // back to north-up (headingRad = 0).
    //
    // Screen::setHeading() is called by BMX160Sensor::runOnce() after tilt-
    // compensated compass fusion, so screen->hasHeading() is true whenever the
    // RAK12034 is connected and initialised.
    // -----------------------------------------------------------------------
    const float headingRad = screen->hasHeading()
                                 ? screen->getHeading() * DEG_TO_RAD
                                 : screen->estimatedHeading(myLat, myLon);
    const bool usingIMU = screen->hasHeading();

    // -----------------------------------------------------------------------
    // Collect remote nodes that have valid positions.
    // -----------------------------------------------------------------------
    struct Entry {
        meshtastic_NodeInfoLite *node;
        float distM;
        float bearingRad; // absolute bearing, radians, 0 = north
    };

    std::vector<Entry> entries;
    float maxDistM = 1.0f; // 1 m floor prevents degenerate scale

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

    // -----------------------------------------------------------------------
    // Scale and radar chrome.
    // -----------------------------------------------------------------------
    const float scale = niceScaleMeters(maxDistM);

    // Three concentric range rings.
    for (int ring = 1; ring <= 3; ring++) {
        display->drawCircle(radarCX, radarCY, (radarRadius * ring) / 3);
    }

    // North ("N") indicator.
    // In heading-up mode it rotates to show the true north direction.
    // In north-up mode it sits at the top of the outer ring.
    {
        // North is at absolute bearing 0; relative bearing = 0 - headingRad
        const float northBrg = -headingRad;
        // Place label just inside the outer ring.
        const int inset = FONT_HEIGHT_SMALL / 2 + 1;
        const int nx = radarCX + (int)((radarRadius - inset) * sinf(northBrg));
        const int ny = radarCY - (int)((radarRadius - inset) * cosf(northBrg));
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(nx, ny - FONT_HEIGHT_SMALL / 2, "N");
    }

    // Own-node marker: filled 4×4 square at centre.
    display->fillRect(radarCX - 2, radarCY - 2, 4, 4);

    // -----------------------------------------------------------------------
    // Plot remote nodes.
    // -----------------------------------------------------------------------
    for (const Entry &e : entries) {
        const float norm = std::min(e.distM / scale, 1.0f);
        plotPoint(display, radarCX, radarCY, radarRadius, e.bearingRad, headingRad, norm);
    }

    // -----------------------------------------------------------------------
    // Info panel.
    // -----------------------------------------------------------------------
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    int infoY = y + headerH;

    // Line 1: scale of the outermost ring.
    char scaleStr[12];
    formatDistM(scaleStr, sizeof(scaleStr), scale);
    display->drawString(infoPanelX, infoY, scaleStr);
    infoY += FONT_HEIGHT_SMALL;

    // Line 2: orientation mode.
    display->drawString(infoPanelX, infoY, usingIMU ? "HDG-UP" : "N-UP");
    infoY += FONT_HEIGHT_SMALL;

    // Lines 3–4: closest node name and distance.
    if (!entries.empty()) {
        const Entry &closest = *std::min_element(entries.begin(), entries.end(),
                                                  [](const Entry &a, const Entry &b) { return a.distM < b.distM; });

        char name[16] = "";
        if (closest.node->has_user && closest.node->user.short_name[0]) {
            strncpy(name, closest.node->user.short_name, sizeof(name) - 1);
        } else {
            snprintf(name, sizeof(name), "%04X", (uint16_t)(closest.node->num & 0xFFFF));
        }

        char distStr[12];
        formatDistM(distStr, sizeof(distStr), closest.distM);

        display->drawString(infoPanelX, infoY, name);
        infoY += FONT_HEIGHT_SMALL;
        display->drawString(infoPanelX, infoY, distStr);
    }
}

} // namespace RadarRenderer
} // namespace graphics
#endif // HAS_SCREEN
