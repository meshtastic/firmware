#include "configuration.h"
#if HAS_SCREEN
#include "RadarRenderer.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "UIRenderer.h"
#include "gps/GeoCoord.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include <algorithm>
#include <cmath>
#include <vector>

extern graphics::Screen *screen;

namespace graphics
{
namespace RadarRenderer
{

// ---------------------------------------------------------------------------
// Runtime state (toggled by radarBearingsMenu)
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
    static const float scales[] = {
        30,    60,    90,    150,   300,   600,   900,
        1500,  3000,  6000,  9000,  15000, 30000, 90000,
        300000
    };
    constexpr int N = sizeof(scales) / sizeof(scales[0]);

    int idx = 0;
    while (idx < N - 1 && maxDistM > scales[idx])
        idx++;

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

// ---------------------------------------------------------------------------
// Node marker shapes
// ---------------------------------------------------------------------------

/**
 * Draw one of five distinct markers centred at (px, py).
 *
 *   0  ■  filled 3×3 square
 *   1  +  axis-aligned cross
 *   2  ×  diagonal cross (X)
 *   3  □  hollow 5×5 square
 *   4  ◆  diamond (rotated square)
 *
 * All shapes fit within a 5×5 pixel bounding box.
 */
static void drawMarker(OLEDDisplay *display, int px, int py, uint8_t sym)
{
    switch (sym) {
    case 0: // ■
        display->fillRect(px - 1, py - 1, 3, 3);
        break;
    case 1: // +
        display->drawLine(px - 2, py, px + 2, py);
        display->drawLine(px, py - 2, px, py + 2);
        break;
    case 2: // ×
        display->drawLine(px - 2, py - 2, px + 2, py + 2);
        display->drawLine(px + 2, py - 2, px - 2, py + 2);
        break;
    case 3: // □
        display->drawLine(px - 2, py - 2, px + 2, py - 2);
        display->drawLine(px + 2, py - 2, px + 2, py + 2);
        display->drawLine(px + 2, py + 2, px - 2, py + 2);
        display->drawLine(px - 2, py + 2, px - 2, py - 2);
        break;
    default: // ◆
        display->drawLine(px, py - 2, px + 2, py);
        display->drawLine(px + 2, py, px, py + 2);
        display->drawLine(px, py + 2, px - 2, py);
        display->drawLine(px - 2, py, px, py - 2);
        break;
    }
}

/** Plot a node on the radar at the correct bearing/distance position. */
static void plotNode(OLEDDisplay *display, int cx, int cy, int radius, float bearingRad, float headingRad, float norm,
                     uint8_t markerIdx)
{
    const float rel = bearingRad - headingRad;
    const int px = cx + (int)(radius * norm * sinf(rel));
    const int py = cy - (int)(radius * norm * cosf(rel));
    drawMarker(display, px, py, markerIdx);
}

// ---------------------------------------------------------------------------
// Overlay renderer
// ---------------------------------------------------------------------------

/**
 * Draw the radar overlay (header + content) for the compass/position screen.
 *
 * Layout (128×64 OLED example):
 *   - Header row: "Radar <scale>" — drawn here so the title can include the
 *     current outer-ring range
 *   - Right side: circular radar with 2 px padding on all sides
 *   - Left side: node list (up to 5 closest nodes, marker + name + distance)
 *
 * Called from NodeListRenderer::drawDynamicListScreen_Location when
 * uiconfig.bearings_view_radar is true.  The caller draws the footer; this
 * function owns the header and content area.
 */
void drawRadarOverlay(OLEDDisplay *display, int16_t x, int16_t y)
{
    const int headerH = FONT_HEIGHT_SMALL - 1;
    const int sw = SCREEN_WIDTH;
    const int sh = SCREEN_HEIGHT;

    // Reserve space at the bottom for the BT/API connection icon footer.
    // drawCommonFooter() paints a black bar across the full width when the API
    // is connected, which would otherwise clip the last list row and the
    // bottom of the radar circle.  Matches the footer height computed in
    // SharedUIDisplay::drawCommonFooter.
    const int footerScale = (currentResolution == ScreenResolution::High) ? 2 : 1;
    const int footerH = isAPIConnected(service ? service->api_state : 0)
                            ? (connection_icon_height * footerScale) + (2 * footerScale)
                            : 0;

    const int contentH = sh - headerH - footerH;
    const int pad = 2; // px padding around the radar circle

    // -----------------------------------------------------------------------
    // Radar circle — right side, 2 px padding on all sides.
    // -----------------------------------------------------------------------
    const int radarDiam = contentH - 2 * pad;
    const int radarRadius = radarDiam / 2;
    const int radarCX = x + sw - pad - radarRadius;
    const int radarCY = y + headerH + pad + radarRadius;

    // Node list panel fills the space to the left of the radar circle.
    const int listRight = radarCX - radarRadius - 4; // 4 px gap between list and circle

    // -----------------------------------------------------------------------
    // GPS — bail gracefully if unavailable.  No fix → no scale to report,
    // so the header stays plain.
    // -----------------------------------------------------------------------
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    meshtastic_PositionLite ourPos;
    if (!ourNode || !nodeDB->copyNodePosition(ourNode->num, ourPos) || (ourPos.latitude_i == 0 && ourPos.longitude_i == 0)) {
        graphics::drawCommonHeader(display, x, y, "Radar");
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + sw / 2, y + sh / 2 - FONT_HEIGHT_SMALL / 2, "No GPS fix");
        return;
    }

    const double myLat = ourPos.latitude_i * 1e-7;
    const double myLon = ourPos.longitude_i * 1e-7;

    // -----------------------------------------------------------------------
    // Heading.
    //
    // Priority:
    //  1. BMX160/RAK12034 tilt-compensated heading (screen->hasHeading())
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

    const bool favoritesOnly = uiconfig.radar_favorites_only;

    const int numNodes = nodeDB->getNumMeshNodes();
    for (int i = 0; i < numNodes; i++) {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (!n || n->num == nodeDB->getNodeNum())
            continue;
        if (favoritesOnly && !nodeInfoLiteIsFavorite(n))
            continue;
        meshtastic_PositionLite nodePos;
        if (!nodeDB->copyNodePosition(n->num, nodePos))
            continue;
        if (nodePos.latitude_i == 0 && nodePos.longitude_i == 0)
            continue;

        const double nodeLat = nodePos.latitude_i * 1e-7;
        const double nodeLon = nodePos.longitude_i * 1e-7;
        const float dist = GeoCoord::latLongToMeter(myLat, myLon, nodeLat, nodeLon);
        const float brg = GeoCoord::bearing(myLat, myLon, nodeLat, nodeLon);

        entries.push_back({n, dist, brg});
    }

    // Sort by distance so entries[0] is always the closest node.
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.distM < b.distM; });

    // Auto-scale from only the nodes we will actually plot, so a single
    // far-away node can't push the scale into a high bucket and squash all
    // the close nodes into an invisible cluster at the centre.
    constexpr int kMaxPlotted = 5;
    float maxDistM = 1.0f;
    const int plottedCount = std::min((int)entries.size(), kMaxPlotted);
    for (int i = 0; i < plottedCount; i++) {
        if (entries[i].distM > maxDistM)
            maxDistM = entries[i].distM;
    }

    const float scale = niceScaleMeters(maxDistM, s_zoomLevel);

    // -----------------------------------------------------------------------
    // Header — "Radar <scale>", drawn now that we know the outer-ring range.
    // Keeps the scale legible in the title bar instead of overlapping the
    // inner ring.
    // -----------------------------------------------------------------------
    {
        char scaleBuf[12] = "";
        formatDistM(scaleBuf, sizeof(scaleBuf), scale);
        char titleBuf[24];
        snprintf(titleBuf, sizeof(titleBuf), "Radar %s", scaleBuf);
        graphics::drawCommonHeader(display, x, y, titleBuf);
    }

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

    // Own-node marker: single pixel at centre.
    display->setPixel(radarCX, radarCY);

    // -----------------------------------------------------------------------
    // Plot remote nodes — cap at kMaxPlotted to match the list panel.
    //
    // Marker symbol is the sort-position index (0..4) so every plotted node
    // gets a unique shape and matches its row in the list panel.  Using the
    // node number modulo 5 caused symbol collisions when several plotted
    // nodes shared a residue.
    // -----------------------------------------------------------------------
    for (int i = 0; i < plottedCount; i++) {
        const Entry &e = entries[i];
        plotNode(display, radarCX, radarCY, radarRadius, e.bearingRad, headingRad,
                 std::min(e.distM / scale, 1.0f), (uint8_t)i);
    }

    // -----------------------------------------------------------------------
    // Node list (left panel) — up to 5 closest nodes.
    //
    // Each row: marker symbol (matches the radar dot) | short name | distance.
    // -----------------------------------------------------------------------
    display->setFont(FONT_SMALL);

    const int rowPitch = contentH / kMaxPlotted;

    for (int i = 0; i < plottedCount; i++) {
        const Entry &e = entries[i];
        const int rowY = y + headerH + rowPitch * i;
        const int symCX = x + 3;
        const int symCY = rowY + rowPitch / 2;

        drawMarker(display, symCX, symCY, (uint8_t)i);

        char name[10] = "";
        if (nodeInfoLiteHasUser(e.node) && e.node->short_name[0])
            strncpy(name, e.node->short_name, sizeof(name) - 1);
        else
            snprintf(name, sizeof(name), "%04X", (uint16_t)(e.node->num & 0xFFFF));

        char dist[10] = "";
        formatDistM(dist, sizeof(dist), e.distM);

        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->drawString(x + 7, rowY, name);
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        display->drawString(x + listRight, rowY, dist);
        display->setTextAlignment(TEXT_ALIGN_LEFT);
    }
}

} // namespace RadarRenderer
} // namespace graphics
#endif // HAS_SCREEN
