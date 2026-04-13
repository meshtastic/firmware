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
    // Increasing breakpoints; each entry is a usable full-scale range in metres.
    static const float scales[] = {50,    100,   250,    500,    1000,   2000,
                                    5000,  10000, 25000,  50000,  100000, 250000,
                                    500000};
    for (float s : scales) {
        if (maxDistM <= s)
            return s;
    }
    return 1000000.0f; // fallback: 1 000 km
}

/** Format a distance (metres) as a short human-readable string. */
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
    // Radar circle: a square area on the left side of the content region.
    // Cap width at 2/3 of screen so the info panel always has room.
    // -----------------------------------------------------------------------
    const int maxRadarDiam = (sw * 2) / 3;
    const int radarDiam = std::min(contentH - 2, maxRadarDiam);
    const int radarRadius = radarDiam / 2;
    const int radarCX = x + radarRadius + 1;
    const int radarCY = y + headerH + 1 + radarRadius;

    // Info panel occupies the space to the right of the radar circle.
    const int infoPanelX = radarCX + radarRadius + 4;

    // -----------------------------------------------------------------------
    // Own position — required; show a message and return early if unavailable.
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
    // Collect other nodes that have a valid position.
    // -----------------------------------------------------------------------
    struct Entry {
        meshtastic_NodeInfoLite *node;
        float distM;
        float bearingRad; // radians, 0 = north
    };

    std::vector<Entry> entries;
    float maxDistM = 1.0f; // 1 m minimum to avoid degenerate scale

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
    // Choose a scale and draw the radar chrome.
    // -----------------------------------------------------------------------
    const float scale = niceScaleMeters(maxDistM);

    // Range rings — three concentric circles at 1/3, 2/3, and full scale.
    for (int ring = 1; ring <= 3; ring++) {
        display->drawCircle(radarCX, radarCY, (radarRadius * ring) / 3);
    }

    // North indicator just inside the outer ring at the top.
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(radarCX, radarCY - radarRadius + 1, "N");

    // Own-node marker: filled 4×4 square at centre.
    display->fillRect(radarCX - 2, radarCY - 2, 4, 4);

    // -----------------------------------------------------------------------
    // Plot each remote node.
    // -----------------------------------------------------------------------
    for (const Entry &e : entries) {
        // Normalise distance; clamp nodes beyond scale to the outer ring edge.
        const float norm = std::min(e.distM / scale, 1.0f);
        const int nx = radarCX + (int)(radarRadius * norm * sinf(e.bearingRad));
        const int ny = radarCY - (int)(radarRadius * norm * cosf(e.bearingRad));

        // 3×3 square marker for each node.
        display->fillRect(nx - 1, ny - 1, 3, 3);
    }

    // -----------------------------------------------------------------------
    // Info panel (right of radar).
    // -----------------------------------------------------------------------
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // Line 1: full-scale range label.
    char scaleStr[12];
    formatDistM(scaleStr, sizeof(scaleStr), scale);
    display->drawString(infoPanelX, y + headerH, scaleStr);

    // Line 2: node count.
    char countStr[10];
    snprintf(countStr, sizeof(countStr), "%d node%s", (int)entries.size(), entries.size() == 1 ? "" : "s");
    display->drawString(infoPanelX, y + headerH + FONT_HEIGHT_SMALL, countStr);

    // Lines 3–4: closest node name + distance.
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

        display->drawString(infoPanelX, y + headerH + FONT_HEIGHT_SMALL * 2, name);
        display->drawString(infoPanelX, y + headerH + FONT_HEIGHT_SMALL * 3, distStr);
    }
}

} // namespace RadarRenderer
} // namespace graphics
#endif // HAS_SCREEN
