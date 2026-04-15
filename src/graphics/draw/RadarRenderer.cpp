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

extern graphics::Screen *screen;

namespace graphics
{
namespace RadarRenderer
{

// ---------------------------------------------------------------------------
// Runtime state (toggled by radarMenu)
// ---------------------------------------------------------------------------

static bool s_forceNorthUp = false; // override IMU → fixed north-up
static int s_zoomLevel = 0;         // -2..+2, 0 = auto

bool isNorthUp()
{
    return s_forceNorthUp;
}

void toggleNorthUp()
{
    s_forceNorthUp = !s_forceNorthUp;
}

void zoomIn()
{
    if (s_zoomLevel > -2)
        s_zoomLevel--;
}

void zoomOut()
{
    if (s_zoomLevel < 2)
        s_zoomLevel++;
}

// ---------------------------------------------------------------------------
// Scale helpers
// ---------------------------------------------------------------------------

/**
 * Return the smallest value from the scale table that is >= maxDistM,
 * then apply the zoom offset.  All values are multiples of 3 so that
 * dividing by 3 (for ring labels) always yields whole numbers.
 */
static float niceScaleMeters(float maxDistM, int zoomLevel)
{
    // Every entry is divisible by 3 → ring labels are always integers.
    static const float scales[] = {
        30,    60,    90,    150,   300,   600,   900,
        1500,  3000,  6000,  9000,  15000, 30000, 90000,
        300000
    };
    constexpr int N = sizeof(scales) / sizeof(scales[0]);

    // Find the base auto-scale index.
    int idx = 0;
    while (idx < N - 1 && maxDistM > scales[idx])
        idx++;

    // Apply zoom offset (clamp to valid range).
    idx = std::max(0, std::min(N - 1, idx + zoomLevel));
    return scales[idx];
}

/** Format metres as a compact string (metric or imperial). */
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

/** Plot a 3×3 node marker.  headingRad rotates the view (heading-up). */
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
    // Layout: radar circle on the left, info panel on the right.
    //
    // The radar is a square area equal to the content height, leaving a panel
    // of (sw - radarDiam) pixels on the right for labels.
    //
    // On 128×64 OLED (contentH=57):
    //   radarDiam = 55  radarRadius = 27  infoPanelX = 92 (36 px panel)
    // -----------------------------------------------------------------------
    const int radarDiam = contentH - 2;      // 1 px margin top + bottom
    const int radarRadius = radarDiam / 2;
    const int radarCX = x + radarRadius + 1; // left-aligned with 1px margin
    const int radarCY = y + headerH + 1 + radarRadius;
    const int infoPanelX = x + radarDiam + 4;

    // -----------------------------------------------------------------------
    // GPS — bail gracefully if unavailable.
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
    // Heading.
    //
    // Priority:
    //  1. BMX160/RAK12034 tilt-compensated heading via screen->setHeading()
    //  2. GPS movement track (estimatedHeading)
    //  3. North-up fallback (0)
    //
    // s_forceNorthUp overrides (1) and (2) — set via the long-press menu.
    // -----------------------------------------------------------------------
    const bool imuAvailable = screen->hasHeading();
    const bool headingUp = imuAvailable && !s_forceNorthUp;
    const float headingRad = headingUp ? screen->getHeading() * DEG_TO_RAD
                                       : (s_forceNorthUp ? 0.0f : screen->estimatedHeading(myLat, myLon));

    // -----------------------------------------------------------------------
    // Collect remote nodes with valid positions.
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

    // -----------------------------------------------------------------------
    // Scale (respects zoom level set by long-press menu).
    // -----------------------------------------------------------------------
    const float scale = niceScaleMeters(maxDistM, s_zoomLevel);

    // -----------------------------------------------------------------------
    // Draw radar chrome: three concentric range rings.
    // -----------------------------------------------------------------------
    for (int ring = 1; ring <= 3; ring++)
        display->drawCircle(radarCX, radarCY, (radarRadius * ring) / 3);

    // -----------------------------------------------------------------------
    // North indicator — rotates in heading-up mode.
    // -----------------------------------------------------------------------
    {
        const int inset = FONT_HEIGHT_SMALL / 2 + 1;
        const float northBrg = -headingRad;
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
    for (const Entry &e : entries)
        plotNode(display, radarCX, radarCY, radarRadius, e.bearingRad, headingRad,
                 std::min(e.distM / scale, 1.0f));

    // -----------------------------------------------------------------------
    // Info panel (right of radar).
    //
    // Line 1: outermost ring scale
    // Line 2: middle ring scale
    // Line 3: inner ring scale
    // Line 4: closest node name
    // Line 5: closest node distance + cardinal direction
    // Line 6: node count  +  orientation badge (HDG/N↑)
    // -----------------------------------------------------------------------
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // Ring scale labels — scale is always divisible by 3, so these are ints.
    char buf[12];
    formatDistM(buf, sizeof(buf), scale);
    display->drawString(infoPanelX, y + headerH, buf);

    formatDistM(buf, sizeof(buf), scale * 2.0f / 3.0f);
    display->drawString(infoPanelX, y + headerH + FONT_HEIGHT_SMALL, buf);

    formatDistM(buf, sizeof(buf), scale / 3.0f);
    display->drawString(infoPanelX, y + headerH + FONT_HEIGHT_SMALL * 2, buf);

    // Closest node info.
    if (!entries.empty()) {
        const Entry &closest = *std::min_element(entries.begin(), entries.end(),
                                                  [](const Entry &a, const Entry &b) { return a.distM < b.distM; });

        // Short name or last-4 of node ID.
        char name[16] = "";
        if (closest.node->has_user && closest.node->user.short_name[0])
            strncpy(name, closest.node->user.short_name, sizeof(name) - 1);
        else
            snprintf(name, sizeof(name), "%04X", (uint16_t)(closest.node->num & 0xFFFF));

        display->drawString(infoPanelX, y + headerH + FONT_HEIGHT_SMALL * 3, name);

        // Distance string.
        formatDistM(buf, sizeof(buf), closest.distM);
        display->drawString(infoPanelX, y + headerH + FONT_HEIGHT_SMALL * 4, buf);
    }

    // Bottom row: node count + orientation badge.
    {
        const int bottomY = y + headerH + FONT_HEIGHT_SMALL * 5;
        snprintf(buf, sizeof(buf), "%d", (int)entries.size());
        display->drawString(infoPanelX, bottomY, buf);

        // Orientation badge: "HDG" when heading-up, "N^" when north-up.
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        display->drawString(x + sw - 1, bottomY, headingUp ? "HDG" : "N^");
        display->setTextAlignment(TEXT_ALIGN_LEFT);
    }
}

} // namespace RadarRenderer
} // namespace graphics
#endif // HAS_SCREEN
