#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./RadarApplet.h"
#include "NodeDB.h"
#include "gps/GeoCoord.h"

#include <algorithm>
#include <math.h>
#include <vector>

using namespace NicheGraphics;

// Outer-ring ranges; multiples of 3 so the inner rings stay round numbers.
static float niceScaleMeters(float maxDistM)
{
    static const float scales[] = {30, 60, 90, 150, 300, 600, 900, 1500, 3000, 6000, 9000, 15000, 30000, 90000, 300000};
    constexpr int N = sizeof(scales) / sizeof(scales[0]);
    int idx = 0;
    while (idx < N - 1 && maxDistM > scales[idx])
        idx++;
    return scales[idx];
}

// Compact distance with one decimal where it matters, e.g. "350m", "1.5km",
// "0.3mi".
static std::string formatDist(float metres)
{
    char buf[12];
    if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
        const float miles = metres / 1609.34f;
        if (miles < 0.1f)
            snprintf(buf, sizeof(buf), "%dft", (int)(metres * 3.28084f));
        else if (miles < 10.0f)
            snprintf(buf, sizeof(buf), "%.1fmi", miles);
        else
            snprintf(buf, sizeof(buf), "%dmi", (int)(miles + 0.5f));
    } else if (metres < 1000.0f) {
        snprintf(buf, sizeof(buf), "%dm", (int)metres);
    } else if (metres < 10000.0f) {
        snprintf(buf, sizeof(buf), "%.1fkm", metres / 1000.0f);
    } else {
        snprintf(buf, sizeof(buf), "%dkm", (int)(metres / 1000.0f + 0.5f));
    }
    return buf;
}

// One of ten distinct shapes in a (2r+1) box: ■ + × □ ◆ △ ▽ - ○ *
void InkHUD::RadarApplet::drawMarker(int16_t px, int16_t py, uint8_t sym, int16_t r)
{
    switch (sym) {
    case 0:
        fillRect(px - r, py - r, 2 * r + 1, 2 * r + 1, BLACK);
        break;
    case 1:
        drawLine(px - r, py, px + r, py, BLACK);
        drawLine(px, py - r, px, py + r, BLACK);
        break;
    case 2:
        drawLine(px - r, py - r, px + r, py + r, BLACK);
        drawLine(px + r, py - r, px - r, py + r, BLACK);
        break;
    case 3:
        drawRect(px - r, py - r, 2 * r + 1, 2 * r + 1, BLACK);
        break;
    case 4:
        drawLine(px, py - r, px + r, py, BLACK);
        drawLine(px + r, py, px, py + r, BLACK);
        drawLine(px, py + r, px - r, py, BLACK);
        drawLine(px - r, py, px, py - r, BLACK);
        break;
    case 5:
        drawTriangle(px, py - r, px + r, py + r, px - r, py + r, BLACK);
        break;
    case 6:
        drawTriangle(px, py + r, px + r, py - r, px - r, py - r, BLACK);
        break;
    case 7:
        fillRect(px - r, py - 1, 2 * r + 1, 3, BLACK);
        break;
    case 8:
        drawCircle(px, py, r, BLACK);
        break;
    default:
        drawLine(px - r, py, px + r, py, BLACK);
        drawLine(px, py - r, px, py + r, BLACK);
        drawLine(px - r, py - r, px + r, py + r, BLACK);
        drawLine(px + r, py - r, px - r, py + r, BLACK);
        break;
    }
}

void InkHUD::RadarApplet::drawListRow(int16_t left, int16_t right, int16_t centerY, const Entry &e, uint8_t sym, int16_t r)
{
    drawMarker(left + r, centerY, sym, r);
    printAt(left + 2 * r + 4, centerY, parseShortName(e.node), LEFT, MIDDLE);
    printAt(right, centerY, formatDist(e.distM), RIGHT, MIDDLE);
}

void InkHUD::RadarApplet::onRender(bool full)
{
    setFont(fontSmall);

    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    meshtastic_PositionLite ourPos;
    if (!ourNode || !nodeDB->copyNodePosition(ourNode->num, ourPos) || (ourPos.latitude_i == 0 && ourPos.longitude_i == 0)) {
        drawHeader("Radar");
        printAt(X(0.5), Y(0.5), "No GPS fix", CENTER, MIDDLE);
        return;
    }
    const double myLat = ourPos.latitude_i * 1e-7;
    const double myLon = ourPos.longitude_i * 1e-7;

    // Online nodes with a position, closest first
    std::vector<Entry> entries;
    const bool favoritesOnly = config.display.radar_favorites_only;
    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (!n || n->num == ourNode->num || (favoritesOnly && !nodeInfoLiteIsFavorite(n)))
            continue;
        if (sinceLastSeen(n) >= NUM_ONLINE_SECS) // Drop stale ghosts
            continue;
        meshtastic_PositionLite pos;
        if (!nodeDB->copyNodePosition(n->num, pos) || (pos.latitude_i == 0 && pos.longitude_i == 0))
            continue;
        const double lat = pos.latitude_i * 1e-7;
        const double lon = pos.longitude_i * 1e-7;
        entries.push_back({n, GeoCoord::latLongToMeter(myLat, myLon, lat, lon), GeoCoord::bearing(myLat, myLon, lat, lon)});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.distM < b.distM; });

    // Layout: list beside the radar on wide tiles, below it on square/tall tiles
    const int16_t top = getHeaderHeight();
    const int16_t contentH = height() - top;
    const int16_t lineH = getFont().lineHeight();
    const bool wide = width() >= contentH * 5 / 4;
    const int16_t r = std::max<int16_t>(2, std::min(width(), height()) / 60);

    int16_t diam, cx, cy, listL, listR, listTop, listBottom;
    uint8_t listCols;
    if (wide) {
        diam = std::min<int16_t>(contentH - 4, width() * 11 / 20);
        cx = width() - 2 - diam / 2;
        cy = top + contentH / 2;
        listL = 2;
        listR = cx - diam / 2 - 6;
        listTop = top + 2;
        listBottom = height();
        listCols = 1;
    } else {
        // Reserve at least three list rows under the circle
        diam = std::min<int16_t>(width() - 4, contentH - 3 * lineH - 6);
        cx = width() / 2;
        cy = top + 2 + diam / 2;
        listL = 2;
        listR = width() - 2;
        listTop = top + diam + 6;
        listBottom = height();
        listCols = (width() >= 150) ? 2 : 1;
    }
    const int16_t radius = diam / 2;

    const int listRows = std::max(1, (listBottom - listTop) / lineH);
    const int maxPlotted = std::min(10, listRows * listCols);
    const int plotted = std::min((int)entries.size(), maxPlotted);

    // Scale from plotted nodes only, so one far node can't squash the rest into
    // the centre
    float maxDistM = 1.0f;
    for (int i = 0; i < plotted; i++)
        maxDistM = std::max(maxDistM, entries[i].distM);
    const float scale = niceScaleMeters(maxDistM);

    drawHeader("Radar " + formatDist(scale));

    // Rings, north label and our own node
    for (int ring = 1; ring <= 3; ring++)
        drawCircle(cx, cy, (radius * ring) / 3, BLACK);
    const int16_t nY = cy - radius + lineH / 2 + 1;
    fillRect(cx - lineH / 2, nY - lineH / 2, lineH, lineH, WHITE);
    printAt(cx, nY, "N", CENTER, MIDDLE);
    fillCircle(cx, cy, 1, BLACK);

    for (int i = 0; i < plotted; i++) {
        const float norm = std::min(entries[i].distM / scale, 1.0f);
        const int16_t px = cx + (int16_t)(radius * norm * sinf(entries[i].bearingRad));
        const int16_t py = cy - (int16_t)(radius * norm * cosf(entries[i].bearingRad));
        drawMarker(px, py, (uint8_t)i, r);
    }

    if (entries.empty()) {
        const int16_t emptyX = wide ? (listL + listR) / 2 : X(0.5);
        printAt(emptyX, (listTop + listBottom) / 2, "No active nodes", CENTER, MIDDLE);
        return;
    }

    // List, filled column by column
    const int16_t colW = (listR - listL) / listCols;
    for (int i = 0; i < plotted; i++) {
        const int col = i / listRows;
        const int row = i % listRows;
        const int16_t left = listL + col * colW;
        const int16_t right = left + colW - (col + 1 < listCols ? 6 : 0);
        drawListRow(left, right, listTop + row * lineH + lineH / 2, entries[i], (uint8_t)i, r);
    }
}

// Redraw when someone's position arrives, or ours moves enough to matter
ProcessMessage InkHUD::RadarApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (!isActive())
        return ProcessMessage::CONTINUE;

    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag || mp.decoded.portnum != meshtastic_PortNum_POSITION_APP)
        return ProcessMessage::CONTINUE;

    meshtastic_Position position = meshtastic_Position_init_default;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Position_msg, &position))
        return ProcessMessage::CONTINUE;
    if (!position.has_latitude_i || !position.has_longitude_i || (position.latitude_i == 0 && position.longitude_i == 0))
        return ProcessMessage::CONTINUE;

    if (isFromUs(&mp)) {
        const float lat = position.latitude_i * 1e-7;
        const float lng = position.longitude_i * 1e-7;
        if (GeoCoord::latLongToMeter(ourLastLat, ourLastLng, lat, lng) <= 10)
            return ProcessMessage::CONTINUE;
        ourLastLat = lat;
        ourLastLng = lng;
    } else if (config.display.radar_favorites_only && !nodeInfoLiteIsFavorite(nodeDB->getMeshNode(mp.from))) {
        return ProcessMessage::CONTINUE;
    }

    requestUpdate();
    return ProcessMessage::CONTINUE;
}

#endif
