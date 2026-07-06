#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./MapApplet.h"
#include "./MapTile.h"

#include <math.h>
#include <string.h>

using namespace NicheGraphics;

bool InkHUD::MapApplet::s_zoomLocked = false;
int InkHUD::MapApplet::s_lockedZoom = -1;
int InkHUD::MapApplet::s_lastRenderedZoom = -1;
int InkHUD::MapApplet::s_autoFitZoom = -1;

static bool usesGridTileLayout();
static int gridTilesPerBlock();
static int tileZoomAt(int tileIndex);
static int tileTxAt(int tileIndex);
static int tileTyAt(int tileIndex);
static int tileMetadataZoomCount();
static int tileMetadataZoomAt(int index);

// Observe GPS position updates so the map redraws whenever a new location arrives.
InkHUD::MapApplet::MapApplet()
{
    if (gpsStatus)
        gpsStatusObserver.observe(&gpsStatus->onNewStatus);
}

int InkHUD::MapApplet::onGpsStatusUpdate(const meshtastic::Status *status)
{
    if (status->getStatusType() != STATUS_TYPE_GPS)
        return 0;
    if (!isActive() || !gpsStatus->getHasLock())
        return 0;

    requestUpdate();
    return 0;
}

// Zoom in one step from the current display zoom.
void InkHUD::MapApplet::zoomIn()
{
    int baseZoom = s_zoomLocked ? s_lockedZoom : s_lastRenderedZoom;
    if (baseZoom < 0)
        return;

    if (map_tile_count == 0) {
        if (baseZoom < ZOOM_MAX_NO_TILES) {
            s_lockedZoom = baseZoom + 1;
            s_zoomLocked = true;
        }
        return;
    }

    // Jump to the next tile zoom strictly above current, not just +1
    int next = -1;
    for (int i = 0; i < tileMetadataZoomCount(); i++) {
        int z = tileMetadataZoomAt(i);
        if (z > baseZoom && (next < 0 || z < next))
            next = z;
    }
    if (next < 0)
        return;

    s_lockedZoom = next;
    s_zoomLocked = true;
}

void InkHUD::MapApplet::resetZoom()
{
    s_zoomLocked = false;
    s_lockedZoom = -1;
}

bool InkHUD::MapApplet::canZoomIn() const
{
    if (s_lastRenderedZoom < 0)
        return false;
    int ref = s_zoomLocked ? s_lockedZoom : s_lastRenderedZoom;
    if (map_tile_count == 0)
        return ref < ZOOM_MAX_NO_TILES;
    for (int i = 0; i < tileMetadataZoomCount(); i++) {
        if (tileMetadataZoomAt(i) > ref)
            return true;
    }
    return false;
}

void InkHUD::MapApplet::zoomOut()
{
    int baseZoom = s_zoomLocked ? s_lockedZoom : s_lastRenderedZoom;
    if (baseZoom < 0) {
        s_zoomLocked = false;
        s_lockedZoom = -1;
        return;
    }

    if (map_tile_count == 0) {
        int floor = (s_autoFitZoom >= 0) ? s_autoFitZoom : baseZoom;
        if (baseZoom > floor) {
            s_lockedZoom = baseZoom - 1;
            s_zoomLocked = true;
        } else {
            s_zoomLocked = false;
            s_lockedZoom = -1;
        }
        return;
    }

    // Jump to the next tile zoom strictly below current, not just -1
    int next = -1;
    for (int i = 0; i < tileMetadataZoomCount(); i++) {
        int z = tileMetadataZoomAt(i);
        if (z < baseZoom && (next < 0 || z > next))
            next = z;
    }
    if (next < 0) {
        s_zoomLocked = false;
        s_lockedZoom = -1;
        return;
    }

    s_lockedZoom = next;
    s_zoomLocked = true;
}

bool InkHUD::MapApplet::canZoomOut() const
{
    if (s_lastRenderedZoom < 0)
        return false;
    int ref = s_zoomLocked ? s_lockedZoom : s_lastRenderedZoom;
    if (map_tile_count == 0)
        return s_autoFitZoom >= 0 ? ref > s_autoFitZoom : false;
    for (int i = 0; i < tileMetadataZoomCount(); i++) {
        if (tileMetadataZoomAt(i) < ref)
            return true;
    }
    return false;
}

// Raw LZ4 block decompressor. Returns bytes written, or -1 on error.
static int lz4_decompress(const uint8_t *src, int src_len, uint8_t *dst, int dst_cap)
{
    const uint8_t *s = src;
    const uint8_t *s_end = src + src_len;
    uint8_t *d = dst;
    const uint8_t *d_end = dst + dst_cap;
    while (s < s_end) {
        uint8_t token = *s++;
        int lit_len = (token >> 4) & 0xF;
        if (lit_len == 15) {
            uint8_t x;
            do {
                x = *s++;
                lit_len += x;
            } while (x == 255 && s < s_end);
        }
        if (d + lit_len > d_end || s + lit_len > s_end)
            return -1;
        memcpy(d, s, lit_len);
        d += lit_len;
        s += lit_len;
        if (s >= s_end)
            break;
        if (s + 2 > s_end)
            return -1;
        int offset = (int)s[0] | ((int)s[1] << 8);
        s += 2;
        if (offset == 0 || d - offset < dst)
            return -1;
        int mat_len = (token & 0xF) + 4;
        if (mat_len == 4 + 15) {
            uint8_t x;
            do {
                x = *s++;
                mat_len += x;
            } while (x == 255 && s < s_end);
        }
        if (d + mat_len > d_end)
            return -1;
        const uint8_t *m = d - offset;
        for (int i = 0; i < mat_len; i++)
            *d++ = m[i];
    }
    return (int)(d - dst);
}

// Tiles are 1 bit/pixel, column-major: [bx=0..31][y=0..255], 8 pixels per byte.
static uint8_t s_tileCacheBuffer[8192];
static constexpr uint8_t MAP_TILE_LAYOUT_SPARSE = 0;
static constexpr uint8_t MAP_TILE_LAYOUT_GRID = 1;
static constexpr uint8_t MAP_TILE_KIND_LZ4 = 0;
static constexpr uint8_t MAP_TILE_KIND_WHITE = 1;
static constexpr uint8_t MAP_TILE_KIND_BLACK = 2;

static bool usesGridTileLayout()
{
    return map_tile_layout == MAP_TILE_LAYOUT_GRID && map_tile_grid_cols > 0 && map_tile_grid_rows > 0 &&
           map_tile_block_count > 0;
}

static int gridTilesPerBlock()
{
    return (int)map_tile_grid_cols * (int)map_tile_grid_rows;
}

static int tileZoomAt(int tileIndex)
{
    if (!usesGridTileLayout())
        return map_tile_zooms[tileIndex];
    int tilesPerBlock = gridTilesPerBlock();
    int blockIndex = tilesPerBlock > 0 ? (tileIndex / tilesPerBlock) : 0;
    return map_tile_block_zooms[blockIndex];
}

static int tileTxAt(int tileIndex)
{
    if (!usesGridTileLayout())
        return map_tile_tx[tileIndex];
    int rows = map_tile_grid_rows;
    int tilesPerBlock = gridTilesPerBlock();
    int blockIndex = tilesPerBlock > 0 ? (tileIndex / tilesPerBlock) : 0;
    int localIndex = tilesPerBlock > 0 ? (tileIndex % tilesPerBlock) : 0;
    return map_tile_block_tx[blockIndex] + (rows > 0 ? (localIndex / rows) : 0);
}

static int tileTyAt(int tileIndex)
{
    if (!usesGridTileLayout())
        return map_tile_ty[tileIndex];
    int rows = map_tile_grid_rows;
    int tilesPerBlock = gridTilesPerBlock();
    int blockIndex = tilesPerBlock > 0 ? (tileIndex / tilesPerBlock) : 0;
    int localIndex = tilesPerBlock > 0 ? (tileIndex % tilesPerBlock) : 0;
    return map_tile_block_ty[blockIndex] + (rows > 0 ? (localIndex % rows) : 0);
}

static int tileMetadataZoomCount()
{
    if (usesGridTileLayout())
        return map_tile_block_count;
    return map_tile_count;
}

static int tileMetadataZoomAt(int index)
{
    return usesGridTileLayout() ? map_tile_block_zooms[index] : map_tile_zooms[index];
}

static const uint8_t *decodeSparseTile(int tileIndex)
{
    const uint8_t kind = map_tile_kinds[tileIndex];
    if (kind == MAP_TILE_KIND_WHITE) {
        memset(s_tileCacheBuffer, 0x00, sizeof(s_tileCacheBuffer));
        return s_tileCacheBuffer;
    }
    if (kind == MAP_TILE_KIND_BLACK) {
        memset(s_tileCacheBuffer, 0xFF, sizeof(s_tileCacheBuffer));
        return s_tileCacheBuffer;
    }
    const uint8_t *compressed = map_tile_data + map_tile_offsets[tileIndex];
    int n = lz4_decompress(compressed, map_tile_sizes[tileIndex], s_tileCacheBuffer, sizeof(s_tileCacheBuffer));
    return n == sizeof(s_tileCacheBuffer) ? s_tileCacheBuffer : nullptr;
}

// Draw tiles centered on latCenter/lngCenter. Falls back to the nearest available zoom if
// no tiles exist at exactly zoom (upsamples), enabling smooth zoom steps.
void InkHUD::MapApplet::drawMapTileBackground(int zoom)
{
    if (map_tile_count == 0 || metersToPx <= 0.0f)
        return;

    const float R = 6378137.0f;
    const float latRad = latCenter * DEG_TO_RAD;
    const float mpp = (2.0f * M_PI * R / (256.0f * (float)(1 << zoom))) * cosf(latRad);
    const float worldPxPerScreenPx = 1.0f / (metersToPx * mpp);

    // Find best tile zoom: highest available <= zoom, or lowest available if none below.
    int tileZoom = -1;
    for (int i = 0; i < tileMetadataZoomCount(); i++) {
        int z = tileMetadataZoomAt(i);
        if (z <= zoom && (tileZoom < 0 || z > tileZoom))
            tileZoom = z;
    }
    if (tileZoom < 0) {
        for (int i = 0; i < tileMetadataZoomCount(); i++) {
            int z = tileMetadataZoomAt(i);
            if (tileZoom < 0 || z < tileZoom)
                tileZoom = z;
        }
    }
    if (tileZoom < 0)
        return;

    // Convert screen-pixel movement into tileZoom coordinate space.
    // When tileZoom < zoom, tile pixels are upsampled (each tile pixel covers >1 screen px).
    const float tileWorldPx = worldPxPerScreenPx * ((float)(1 << tileZoom) / (float)(1 << zoom));

    const float sinLat = sinf(latRad);
    const float gpxX = ((lngCenter + 180.0f) / 360.0f) * (float)(1 << tileZoom) * 256.0f;
    const float gpxY = (0.5f - logf((1.0f + sinLat) / (1.0f - sinLat)) / (4.0f * M_PI)) * (float)(1 << tileZoom) * 256.0f;

    const float minWx = gpxX - width() * 0.5f * tileWorldPx;
    const float maxWx = gpxX + width() * 0.5f * tileWorldPx;
    const float minWy = gpxY - height() * 0.5f * tileWorldPx;
    const float maxWy = gpxY + height() * 0.5f * tileWorldPx;

    for (int i = 0; i < map_tile_count; i++) {
        if (tileZoomAt(i) != tileZoom)
            continue;

        const int tx = tileTxAt(i);
        const int ty = tileTyAt(i);
        const float tileMinWx = tx * 256.0f;
        const float tileMaxWx = tileMinWx + 256.0f;
        const float tileMinWy = ty * 256.0f;
        const float tileMaxWy = tileMinWy + 256.0f;
        if (tileMaxWx < minWx || tileMinWx > maxWx || tileMaxWy < minWy || tileMinWy > maxWy)
            continue;

        const uint8_t *tile = decodeSparseTile(i);
        if (!tile)
            continue;

        const int sxStart = max(0, (int)floorf(((tileMinWx - gpxX) / tileWorldPx) + width() * 0.5f));
        const int sxEnd = min(width() - 1, (int)ceilf(((tileMaxWx - gpxX) / tileWorldPx) + width() * 0.5f) - 1);
        const int syStart = max(0, (int)floorf(((tileMinWy - gpxY) / tileWorldPx) + height() * 0.5f));
        const int syEnd = min(height() - 1, (int)ceilf(((tileMaxWy - gpxY) / tileWorldPx) + height() * 0.5f) - 1);

        for (int sy = syStart; sy <= syEnd; sy++) {
            const float wy = gpxY + (sy - height() * 0.5f) * tileWorldPx;
            const int py = (int)(wy - tileMinWy);
            if (py < 0 || py > 255)
                continue;

            for (int sx = sxStart; sx <= sxEnd; sx++) {
                const float wx = gpxX + (sx - width() * 0.5f) * tileWorldPx;
                const int px = (int)(wx - tileMinWx);
                if (px < 0 || px > 255)
                    continue;

                if (!(tile[(px / 8) * 256 + py] & (1 << (px % 8))))
                    continue;

                drawPixel(sx, sy, BLACK);
            }
        }
    }
}

void InkHUD::MapApplet::onRender(bool full)
{
    // Map center is always the node centroid - tiles are background only.
    getMapCenter(&latCenter, &lngCenter);
    calculateAllMarkers();

    // Show placeholder only if we have no position at all - no tiles, no own node
    if (!enoughMarkers() && !centerIsOurNode) {
        printAt(X(0.5), Y(0.5) - (getFont().lineHeight() / 2), "Node positions", CENTER, MIDDLE);
        printAt(X(0.5), Y(0.5) + (getFont().lineHeight() / 2), "will appear here", CENTER, MIDDLE);
        return;
    }

    // Determine the metersToPx needed to fit all nodes on screen.
    getMapSize(&widthMeters, &heightMeters);
    calculateMapScale(); // metersToPx = fit-all-nodes scale
    const float metersToPxFit = metersToPx;

    // Pick the highest zoom whose native scale fits all nodes (no downsampling, no dither noise).
    {
        const float R = 6378137.0f;
        const float latRad = latCenter * DEG_TO_RAD;

        // Collect unique zooms, sort descending (highest detail first)
        int zooms[16] = {};
        int nzooms = 0;
        for (int i = 0; i < tileMetadataZoomCount() && nzooms < 16; i++) {
            bool found = false;
            for (int j = 0; j < nzooms; j++) {
                if (zooms[j] == tileMetadataZoomAt(i)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                zooms[nzooms++] = tileMetadataZoomAt(i);
        }
        for (int i = 0; i < nzooms - 1; i++) {
            for (int j = i + 1; j < nzooms; j++) {
                if (zooms[j] > zooms[i]) {
                    int t = zooms[i];
                    zooms[i] = zooms[j];
                    zooms[j] = t;
                }
            }
        }

        int chosenZoom = (nzooms > 0) ? zooms[nzooms - 1] : 13; // fallback: widest zoom
        float chosenMetersToPx = metersToPxFit;                 // fallback: fit-scale (may downsample)

        if (s_zoomLocked && s_lockedZoom >= 0) {
            // Use locked zoom at native 1:1 scale - never zoom out for new nodes
            chosenZoom = s_lockedZoom;
            float mpp = (2.0f * M_PI * R / (256.0f * (float)(1 << chosenZoom))) * cosf(latRad);
            chosenMetersToPx = 1.0f / mpp;
        } else if ((markers.empty() || metersToPxFit <= 0.0f) && nzooms > 0) {
            // No spread to fit (own node only, or single remote node at map center). Use highest zoom at native scale.
            chosenZoom = zooms[0];
            float mpp = (2.0f * M_PI * R / (256.0f * (float)(1 << chosenZoom))) * cosf(latRad);
            chosenMetersToPx = 1.0f / mpp;
        } else {
            for (int zi = 0; zi < nzooms; zi++) {
                float mpp = (2.0f * M_PI * R / (256.0f * (float)(1 << zooms[zi]))) * cosf(latRad);
                float nativeMetersToPx = 1.0f / mpp;
                if (nativeMetersToPx <= metersToPxFit) {
                    // This zoom at native scale shows all nodes - use it (highest detail that fits)
                    chosenZoom = zooms[zi];
                    chosenMetersToPx = nativeMetersToPx;
                    break;
                }
            }
        }

        if (!s_zoomLocked)
            s_autoFitZoom = chosenZoom;
        metersToPx = chosenMetersToPx;
        s_lastRenderedZoom = chosenZoom;
        drawMapTileBackground(chosenZoom);

        char zoomLabel[8];
        snprintf(zoomLabel, sizeof(zoomLabel), "z%d", chosenZoom);
        int16_t zoomLabelW = getTextWidth(zoomLabel);
        int16_t zoomLabelH = getFont().lineHeight();
        int16_t zoomLabelX = width() - zoomLabelW - 3;
        int16_t zoomLabelY = 2;
        fillRect(zoomLabelX - 2, zoomLabelY - 1, zoomLabelW + 4, zoomLabelH + 2, WHITE);
        printAt(zoomLabelX, zoomLabelY, zoomLabel, LEFT, TOP);
    }

    // Helper: draw rounded rectangle centered at x,y
    auto fillRoundedRect = [&](int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t r, uint16_t color) {
        int16_t x = cx - (w / 2);
        int16_t y = cy - (h / 2);

        // center rects
        fillRect(x + r, y, w - 2 * r, h, color);
        fillRect(x, y + r, r, h - 2 * r, color);
        fillRect(x + w - r, y + r, r, h - 2 * r, color);

        // corners
        fillCircle(x + r, y + r, r, color);
        fillCircle(x + w - r - 1, y + r, r, color);
        fillCircle(x + r, y + h - r - 1, r, color);
        fillCircle(x + w - r - 1, y + h - r - 1, r, color);
    };

    // Draw all markers first
    for (Marker m : markers) {
        int16_t x = X(0.5) + (int16_t)(m.eastMeters * metersToPx);
        int16_t y = Y(0.5) - (int16_t)(m.northMeters * metersToPx);

        // Add white halo outline first
        constexpr int outlinePad = 1;
        int boxSize = fontSmall.lineHeight() + 2; // scale with font so digit fits
        int radius = max(2, boxSize / 6);

        // White halo background
        fillRoundedRect(x, y, boxSize + (outlinePad * 2), boxSize + (outlinePad * 2), radius + 1, WHITE);

        // Draw inner box
        fillRoundedRect(x, y, boxSize, boxSize, radius, BLACK);

        // Text inside
        setFont(fontSmall);
        setTextColor(WHITE);

        // Draw actual marker on top
        if (m.hopsAway > config.lora.hop_limit) {
            printAt(x + 1, y + 1, "X", CENTER, MIDDLE);
        } else {
            char hopStr[4];
            snprintf(hopStr, sizeof(hopStr), "%d", m.hopsAway);
            printAt(x, y + 1, hopStr, CENTER, MIDDLE);
        }

        // Restore default font and color
        setFont(fontSmall);
        setTextColor(BLACK);
    }

    // Dual map scale bars
    if (metersToPx <= 0.0f)
        return;
    int16_t horizPx = width() * 0.25f;
    int16_t vertPx = height() * 0.25f;
    float horizMeters = horizPx / metersToPx;
    float vertMeters = vertPx / metersToPx;

    auto formatDistance = [&](float meters, char *out, size_t len) {
        if (config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL) {
            float feet = meters * 3.28084f;
            if (feet < 528)
                snprintf(out, len, "%.0f ft", feet);
            else {
                float miles = feet / 5280.0f;
                snprintf(out, len, miles < 10 ? "%.1f mi" : "%.0f mi", miles);
            }
        } else {
            if (meters >= 1000)
                snprintf(out, len, "%.1f km", meters / 1000.0f);
            else
                snprintf(out, len, "%.0f m", meters);
        }
    };

    // Horizontal scale bar
    int16_t horizBarY = height() - 2;
    int16_t horizBarX = 1;
    drawLine(horizBarX, horizBarY, horizBarX + horizPx, horizBarY, BLACK);
    drawLine(horizBarX, horizBarY - 3, horizBarX, horizBarY + 3, BLACK);
    drawLine(horizBarX + horizPx, horizBarY - 3, horizBarX + horizPx, horizBarY + 3, BLACK);

    char horizLabel[32];
    formatDistance(horizMeters, horizLabel, sizeof(horizLabel));
    int16_t horizLabelW = getTextWidth(horizLabel);
    int16_t horizLabelH = getFont().lineHeight();
    int16_t horizLabelX = horizBarX + horizPx + 4;
    int16_t horizLabelY = horizBarY - horizLabelH + 1;
    fillRect(horizLabelX - 2, horizLabelY - 1, horizLabelW + 4, horizLabelH + 2, WHITE);
    printAt(horizLabelX, horizBarY, horizLabel, LEFT, BOTTOM);

    // Vertical scale bar
    int16_t vertBarX = 1;
    int16_t vertBarBottom = horizBarY;
    int16_t vertBarTop = vertBarBottom - vertPx;
    drawLine(vertBarX, vertBarBottom, vertBarX, vertBarTop, BLACK);
    drawLine(vertBarX - 3, vertBarBottom, vertBarX + 3, vertBarBottom, BLACK);
    drawLine(vertBarX - 3, vertBarTop, vertBarX + 3, vertBarTop, BLACK);

    char vertTopLabel[32];
    formatDistance(vertMeters, vertTopLabel, sizeof(vertTopLabel));
    int16_t topLabelY = vertBarTop - getFont().lineHeight() - 2;
    int16_t topLabelW = getTextWidth(vertTopLabel);
    int16_t topLabelH = getFont().lineHeight();
    fillRect(vertBarX - 2, topLabelY - 1, topLabelW + 6, topLabelH + 2, WHITE);
    printAt(vertBarX + (topLabelW / 2) + 1, topLabelY + (topLabelH / 2), vertTopLabel, CENTER, MIDDLE);

    char vertBottomLabel[32];
    formatDistance(vertMeters, vertBottomLabel, sizeof(vertBottomLabel));
    int16_t bottomLabelY = vertBarBottom + 4;
    int16_t bottomLabelW = getTextWidth(vertBottomLabel);
    int16_t bottomLabelH = getFont().lineHeight();
    fillRect(vertBarX - 2, bottomLabelY - 1, bottomLabelW + 6, bottomLabelH + 2, WHITE);
    printAt(vertBarX + (bottomLabelW / 2) + 1, bottomLabelY + (bottomLabelH / 2), vertBottomLabel, CENTER, MIDDLE);

    // Draw our node LAST with full white fill + outline
    if (centerIsOurNode) {
        const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
        meshtastic_PositionLite ourSelfPos;
        nodeDB->copyNodePosition(ourNode->num, ourSelfPos);
        Marker self = calculateMarker(ourSelfPos.latitude_i * 1e-7, ourSelfPos.longitude_i * 1e-7, 0);
        int16_t centerX = X(0.5) + (self.eastMeters * metersToPx);
        int16_t centerY = Y(0.5) - (self.northMeters * metersToPx);

        int16_t r = fontSmall.lineHeight() / 2; // scale marker with font

        // White fill background + halo
        fillCircle(centerX, centerY, r + 2, WHITE);
        drawCircle(centerX, centerY, r + 2, WHITE);

        // Black bullseye on top
        drawCircle(centerX, centerY, r, BLACK);
        fillCircle(centerX, centerY, max(2, r / 4), BLACK);

        // Crosshairs
        drawLine(centerX - r - 2, centerY, centerX + r + 2, centerY, BLACK);
        drawLine(centerX, centerY - r - 2, centerX, centerY + r + 2, BLACK);
    }
}

// Find the center point, in the middle of all node positions
// Calculated values are written to the *lat and *long pointer args
// - Finds the "mean lat long"
// - Calculates furthest nodes from "mean lat long"
// - Place map center directly between these furthest nodes

void InkHUD::MapApplet::getMapCenter(float *lat, float *lng)
{
    // If we have a valid position for our own node, use that as the anchor
    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    meshtastic_PositionLite ourSelfPos;
    if (ourNode && nodeDB->hasValidPosition(ourNode) && nodeDB->copyNodePosition(ourNode->num, ourSelfPos)) {
        *lat = ourSelfPos.latitude_i * 1e-7;
        *lng = ourSelfPos.longitude_i * 1e-7;
        centerIsOurNode = true;
    } else {
        centerIsOurNode = false;
        // Find mean lat long coords
        // ============================
        // - assigning X, Y and Z values to position on Earth's surface in 3D space, relative to center of planet
        // - averages the x, y and z coords
        // - uses tan to find angles for lat / long degrees
        //   - longitude: triangle formed by x and y (on plane of the equator)
        //   - latitude: triangle formed by z (north south),
        //     and the line along plane of equator which stretches from earth's axis to where point xyz intersects planet's
        //     surface

        // Working totals, averaged after nodeDB processed
        uint32_t positionCount = 0;
        float xAvg = 0;
        float yAvg = 0;
        float zAvg = 0;

        // For each node in db
        for (uint32_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
            meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);

            // Skip if no position
            if (!nodeDB->hasValidPosition(node))
                continue;

            // Skip if derived applet doesn't want to show this node on the map
            if (!shouldDrawNode(node))
                continue;

            meshtastic_PositionLite pos;
            if (!nodeDB->copyNodePosition(node->num, pos))
                continue;

            // Latitude and Longitude of node, in radians
            float latRad = pos.latitude_i * (1e-7) * DEG_TO_RAD;
            float lngRad = pos.longitude_i * (1e-7) * DEG_TO_RAD;

            // Convert to cartesian points, with center of earth at 0, 0, 0
            // Exact distance from center is irrelevant, as we're only interested in the vector
            float x = cos(latRad) * cos(lngRad);
            float y = cos(latRad) * sin(lngRad);
            float z = sin(latRad);

            // To find mean values shortly
            xAvg += x;
            yAvg += y;
            zAvg += z;
            positionCount++;
        }

        // All NodeDB processed, find mean values
        if (positionCount == 0)
            return;
        xAvg /= positionCount;
        yAvg /= positionCount;
        zAvg /= positionCount;

        // Longitude from cartesian coords
        // (Angle from 3D coords describing a point of globe's surface)
        /*
                          UK
                       /-------\
        (Top View)   /-         -\
                   /-      (You)  -\
                 /-           .     -\
               /-             . X     -\
         Asia -             ...         - USA
               \-           Y         -/
                 \-                 -/
                   \-             -/
                     \-         -/
                       \- -----/
                       Pacific

        */

        *lng = atan2(yAvg, xAvg) * RAD_TO_DEG;

        // Latitude from cartesian coords
        // (Angle from 3D coords describing a point on the globe's surface)
        // As latitude increases, distance from the Earth's north-south axis out to our surface point decreases.
        // Means we need to first find the hypotenuse which becomes base of our triangle in the second step
        /*
                           UK                                         North
                        /-------\                 (Front View)      /-------\
         (Top View)   /-         -\                               /-         -\
                    /-       (You) -\                           /-(You)        -\
                  /-         /.      -\                       /-   .             -\
                /-    √X²+Y²/ . X      -\                   /-   Z .               -\
        Asia   -           /...          - USA             -       .....             -
                \-           Y         -/                   \-     √X²+Y²          -/
                  \-                 -/                       \-                 -/
                    \-             -/                           \-             -/
                      \-         -/                               \-         -/
                        \- -----/                                   \- -----/
                         Pacific                                      South
        */

        float hypotenuse = sqrt((xAvg * xAvg) + (yAvg * yAvg)); // Distance from globe's north-south axis to surface intersect
        *lat = atan2(zAvg, hypotenuse) * RAD_TO_DEG;
    }

    // Use either our node position, or the mean fallback as the center
    latCenter = *lat;
    lngCenter = *lng;

    // When zoom is locked, keep center exactly on own node / zero-hop centroid.
    // Skip bounding-box shift so new distant nodes don't move the zoomed view.
    if (s_zoomLocked) {
        // Own node has no position - re-center on zero-hop centroid instead.
        if (!centerIsOurNode) {
            uint32_t count = 0;
            float xAvg = 0, yAvg = 0, zAvg = 0;
            for (uint32_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
                meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
                if (!nodeDB->hasValidPosition(node) || !shouldDrawNode(node))
                    continue;
                if (!node->has_hops_away || node->hops_away != 0)
                    continue;
                meshtastic_PositionLite pos;
                if (!nodeDB->copyNodePosition(node->num, pos))
                    continue;
                float latRad2 = pos.latitude_i * 1e-7 * DEG_TO_RAD;
                float lngRad2 = pos.longitude_i * 1e-7 * DEG_TO_RAD;
                xAvg += cosf(latRad2) * cosf(lngRad2);
                yAvg += cosf(latRad2) * sinf(lngRad2);
                zAvg += sinf(latRad2);
                count++;
            }
            if (count > 0) {
                xAvg /= count;
                yAvg /= count;
                zAvg /= count;
                *lng = atan2f(yAvg, xAvg) * RAD_TO_DEG;
                *lat = atan2f(zAvg, sqrtf(xAvg * xAvg + yAvg * yAvg)) * RAD_TO_DEG;
                latCenter = *lat;
                lngCenter = *lng;
            }
        }
        return; // Do not shift center based on bounding box
    }

    // Find furthest nodes from our center, shift center to midpoint of bounding box
    float northernmost = latCenter;
    float southernmost = latCenter;
    float easternmost = lngCenter;
    float westernmost = lngCenter;

    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);

        if (!nodeDB->hasValidPosition(node))
            continue;
        if (!shouldDrawNode(node))
            continue;

        meshtastic_PositionLite pos;
        if (!nodeDB->copyNodePosition(node->num, pos))
            continue;

        float latNode = pos.latitude_i * 1e-7;
        float lngNode = pos.longitude_i * 1e-7;

        northernmost = max(northernmost, latNode);
        southernmost = min(southernmost, latNode);

        float degEastward = fmod(((lngNode - lngCenter) + 360), 360);      // Degrees east from center to node
        float degWestward = abs(fmod(((lngNode - lngCenter) - 360), 360)); // Degrees west from center to node
        if (degEastward < degWestward)
            easternmost = max(easternmost, lngCenter + degEastward);
        else
            westernmost = min(westernmost, lngCenter - degWestward);
    }

    // Todo: check for issues with map spans >180 deg. MQTT only..
    latCenter = (northernmost + southernmost) / 2;
    lngCenter = (westernmost + easternmost) / 2;

    // In case our new center is west of -180, or east of +180, for some reason
    lngCenter = fmod(lngCenter, 180);
}

// Size of map in meters
// Grown to fit the nodes furthest from map center
// Overridable if derived applet wants a custom map size (fixed size?)
void InkHUD::MapApplet::getMapSize(uint32_t *widthMeters, uint32_t *heightMeters)
{
    // Reset the value
    *widthMeters = 0;
    *heightMeters = 0;

    // Find the greatest distance horizontally and vertically from map center
    for (Marker m : markers) {
        *widthMeters = max(*widthMeters, (uint32_t)abs(m.eastMeters) * 2);
        *heightMeters = max(*heightMeters, (uint32_t)abs(m.northMeters) * 2);
    }

    // Add padding
    *widthMeters *= 1.1;
    *heightMeters *= 1.1;
}

// Convert and store info we need for drawing a marker
// Lat / long to "meters relative to map center", for position on screen
// Info about hopsAway, for marker size
InkHUD::MapApplet::Marker InkHUD::MapApplet::calculateMarker(float lat, float lng, uint8_t hopsAway)
{
    assert(lat != 0 || lng != 0); // Not null island. Applets should check this before calling.

    // Bearing and distance from map center to node
    float distanceFromCenter = GeoCoord::latLongToMeter(latCenter, lngCenter, lat, lng);
    float bearingFromCenter = GeoCoord::bearing(latCenter, lngCenter, lat, lng); // in radians

    // Split into meters north and meters east components (signed)
    // - signedness of cos / sin automatically sets negative if south or west
    float northMeters = cos(bearingFromCenter) * distanceFromCenter;
    float eastMeters = sin(bearingFromCenter) * distanceFromCenter;

    Marker m;
    m.eastMeters = eastMeters;
    m.northMeters = northMeters;
    m.hopsAway = hopsAway;
    return m;
}
// Draw a marker on the map for a node, with a shortname label, and backing box
void InkHUD::MapApplet::drawLabeledMarker(meshtastic_NodeInfoLite *node)
{
    // Find x and y position based on node's position in nodeDB
    assert(nodeDB->hasValidPosition(node));
    meshtastic_PositionLite pos;
    const bool hasPos = nodeDB->copyNodePosition(node->num, pos);
    assert(hasPos);
    Marker m = calculateMarker(pos.latitude_i * 1e-7, pos.longitude_i * 1e-7, node->hops_away);

    // Convert to pixel coords
    int16_t markerX = X(0.5) + (m.eastMeters * metersToPx);
    int16_t markerY = Y(0.5) - (m.northMeters * metersToPx);

    constexpr uint16_t paddingH = 2;
    constexpr uint16_t paddingW = 4;
    uint16_t paddingInnerW = 2;                      // Zero'd out if no text
    uint16_t markerSizeMax = fontSmall.lineHeight(); // Scale cross with font
    uint16_t markerSizeMin = max(5, fontSmall.lineHeight() / 3);

    int16_t textX;
    int16_t textY;
    uint16_t textW;
    uint16_t textH;
    int16_t labelX;
    int16_t labelY;
    uint16_t labelW;
    uint16_t labelH;
    uint8_t markerSize;

    bool tooManyHops = node->hops_away > config.lora.hop_limit;

    // Parse any non-ascii chars in the short name,
    // and use last 4 instead if unknown / can't render
    std::string shortName = parseShortName(node);

    // We will draw a left or right hand variant, to place text towards screen center
    // Hopefully avoid text spilling off screen
    // Most values are the same, regardless of left-right handedness

    // Pick emblem style
    if (tooManyHops)
        markerSize = getTextWidth("!");
    else
        markerSize = map(node->hops_away, 0, config.lora.hop_limit, markerSizeMax, markerSizeMin);

    // Common dimensions (left or right variant)
    textW = getTextWidth(shortName);
    if (textW == 0)
        paddingInnerW = 0; // If no text, no padding for text
    textH = fontSmall.lineHeight();
    labelH = paddingH + max((int16_t)(textH), (int16_t)markerSize) + paddingH;
    labelY = markerY - (labelH / 2);
    textY = markerY;
    labelW = paddingW + markerSize + paddingInnerW + textW + paddingW; // Width is same whether right or left hand variant

    // Left-side variant
    if (markerX < width() / 2) {
        labelX = markerX - (markerSize / 2) - paddingW;
        textX = labelX + paddingW + markerSize + paddingInnerW;
    }

    // Right-side variant
    else {
        labelX = markerX - (markerSize / 2) - paddingInnerW - textW - paddingW;
        textX = labelX + paddingW;
    }

    // Prevent overlap with scale bars and their labels
    // Define a "safe zone" in the bottom-left where the scale bars and text are drawn
    constexpr int16_t safeZoneHeight = 28; // adjust based on your label font height
    constexpr int16_t safeZoneWidth = 60;  // adjust based on horizontal label width zone
    bool overlapsScale = (labelY + labelH > height() - safeZoneHeight) && (labelX < safeZoneWidth);

    // If it overlaps, shift label upward slightly above the safe zone
    if (overlapsScale) {
        labelY = height() - safeZoneHeight - labelH - 2;
        textY = labelY + (labelH / 2);
    }

    // Backing box
    fillRect(labelX, labelY, labelW, labelH, WHITE);
    drawRect(labelX, labelY, labelW, labelH, BLACK);

    // Short name
    printAt(textX, textY, shortName, LEFT, MIDDLE);

    // If the label is for our own node,
    // fade it by overdrawing partially with white
    if (node == nodeDB->getMeshNode(nodeDB->getNodeNum()))
        hatchRegion(labelX, labelY, labelW, labelH, 2, WHITE);

    // Draw the marker emblem
    // - after the fading, because hatching (own node) can align with cross and make it look weird
    if (tooManyHops)
        printAt(markerX, markerY, "!", CENTER, MIDDLE);
    else
        drawCross(markerX, markerY, markerSize);
}

// Check if we actually have enough nodes which would be shown on the map
bool InkHUD::MapApplet::enoughMarkers()
{
    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (nodeDB->hasValidPosition(node) && shouldDrawNode(node))
            return true;
    }
    return false;
}

// Calculate how far north and east of map center each node is
// Derived applets can control which nodes to calculate (and later, draw) by overriding MapApplet::shouldDrawNode
void InkHUD::MapApplet::calculateAllMarkers()
{
    // Clear old markers
    markers.clear();

    // For each node in db
    for (uint32_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);

        // Skip if no position
        if (!nodeDB->hasValidPosition(node))
            continue;

        // Skip if derived applet doesn't want to show this node on the map
        if (!shouldDrawNode(node))
            continue;

        // Skip if our own node
        // - special handling in render()
        if (node->num == nodeDB->getNodeNum())
            continue;

        // Skip nodes with unknown hop count - partial info, not useful to plot
        if (!node->has_hops_away)
            continue;

        meshtastic_PositionLite pos;
        if (!nodeDB->copyNodePosition(node->num, pos))
            continue;

        markers.push_back(calculateMarker(pos.latitude_i * 1e-7, pos.longitude_i * 1e-7, node->hops_away));
    }
}

void InkHUD::MapApplet::calculateMapScale()
{
    if (widthMeters == 0 || heightMeters == 0) {
        metersToPx = 0;
        return;
    }
    float mapAspectRatio = (float)widthMeters / heightMeters;
    float appletAspectRatio = (float)width() / height();
    if (mapAspectRatio > appletAspectRatio)
        metersToPx = (float)width() / widthMeters;
    else
        metersToPx = (float)height() / heightMeters;
}

// Draw an x, centered on a specific point
// Most markers will draw with this method
void InkHUD::MapApplet::drawCross(int16_t x, int16_t y, uint8_t size)
{
    int16_t x0 = x - (size / 2);
    int16_t y0 = y - (size / 2);
    int16_t x1 = x0 + size - 1;
    int16_t y1 = y0 + size - 1;
    drawLine(x0, y0, x1, y1, BLACK);
    drawLine(x0, y1, x1, y0, BLACK);
}

#endif
