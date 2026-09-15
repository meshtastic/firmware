#include "FavoritesMapRenderer.h"
#if defined(TTGO_T_ECHO_PLUS) && defined(USE_EINK)
#include "NodeDB.h"
#include "gps/GeoCoord.h"
#include "graphics/ScreenFonts.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace graphics::FavoritesMapRenderer
{

static int8_t zoomStep = 0;
void zoomIn()
{
    if (zoomStep < 4)
        ++zoomStep;
}
void zoomOut()
{
    if (zoomStep > 0)
        --zoomStep;
}
void resetZoom()
{
    zoomStep = 0;
}

struct MapNode {
    NodeNum num;
    float lat;
    float lon;
    bool local;
};

static bool loadPosition(NodeNum num, float &lat, float &lon)
{
    if (num == nodeDB->getNodeNum()) {
        if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0)
            return false;
        lat = localPosition.latitude_i * 1e-7f;
        lon = localPosition.longitude_i * 1e-7f;
        return true;
    }

    meshtastic_PositionLite p = meshtastic_PositionLite_init_default;
    if (!nodeDB->copyNodePosition(num, p))
        return false;
    if (p.latitude_i == 0 && p.longitude_i == 0)
        return false;
    lat = p.latitude_i * 1e-7f;
    lon = p.longitude_i * 1e-7f;
    return true;
}

void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *, int16_t x, int16_t y)
{
    const int16_t w = display->getWidth();
    const int16_t h = display->getHeight();
    const int16_t bottom = y + h - 13;
    const int16_t mapTop = y + 16;
    const int16_t mapH = bottom - mapTop;
    const int16_t mapW = w - 6;

    std::vector<MapNode> nodes;
    nodes.reserve(12);

    float lat = 0, lon = 0;
    if (loadPosition(nodeDB->getNodeNum(), lat, lon))
        nodes.push_back({nodeDB->getNodeNum(), lat, lon, true});

    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); ++i) {
        const auto *n = nodeDB->getMeshNodeByIndex(i);
        if (!n || n->num == nodeDB->getNodeNum() || !nodeInfoLiteIsFavorite(n))
            continue;
        if (loadPosition(n->num, lat, lon))
            nodes.push_back({n->num, lat, lon, false});
    }

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    char title[32];
    snprintf(title, sizeof(title), "Favorites Map  x%d", 1 << zoomStep);
    display->drawString(x + 2, y + 1, title);

    if (nodes.empty()) {
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + w / 2, y + h / 2 - 6, "Favorite node position");
        display->drawString(x + w / 2, y + h / 2 + 6, "will appear here");
        return;
    }

    float centerLat = nodes[0].lat;
    float centerLon = nodes[0].lon;
    if (!nodes[0].local) {
        centerLat = centerLon = 0;
        for (const auto &n : nodes) {
            centerLat += n.lat;
            centerLon += n.lon;
        }
        centerLat /= nodes.size();
        centerLon /= nodes.size();
    }

    struct Point {
        const MapNode *node;
        float east;
        float north;
    };
    std::vector<Point> pts;
    pts.reserve(nodes.size());
    float maxE = 1.0f, maxN = 1.0f;

    for (const auto &n : nodes) {
        const float d = GeoCoord::latLongToMeter(centerLat, centerLon, n.lat, n.lon);
        const float b = GeoCoord::bearing(centerLat, centerLon, n.lat, n.lon);
        const float east = sinf(b) * d;
        const float north = cosf(b) * d;
        pts.push_back({&n, east, north});
        maxE = std::max(maxE, fabsf(east));
        maxN = std::max(maxN, fabsf(north));
    }

    float scale = std::min((mapW * 0.45f) / maxE, (mapH * 0.43f) / maxN);
    scale *= (float)(1 << zoomStep);

    const int16_t cx = x + w / 2;
    const int16_t cy = mapTop + mapH / 2;
    display->drawRect(x + 2, mapTop, mapW, mapH);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(cx, mapTop + 1, "N");

    for (const auto &p : pts) {
        const int16_t px = cx + (int16_t)(p.east * scale);
        const int16_t py = cy - (int16_t)(p.north * scale);
        if (px < x + 5 || px > x + mapW - 2 || py < mapTop + 8 || py > bottom - 3)
            continue;

        if (p.node->local)
            display->fillRect(px - 3, py - 3, 7, 7);
        else {
            display->drawLine(px - 4, py, px + 4, py);
            display->drawLine(px, py - 4, px, py + 4);
        }
    }
}
} // namespace graphics::FavoritesMapRenderer
#endif
