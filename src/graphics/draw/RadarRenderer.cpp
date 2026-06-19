#include "configuration.h"
#if HAS_SCREEN
#include "MeshService.h"
#include "NodeDB.h"
#include "RadarRenderer.h"
#include "UIRenderer.h"
#include "gps/GeoCoord.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include <algorithm>
#include <cmath>
#include <vector>

extern graphics::Screen *screen;

namespace graphics {
namespace RadarRenderer {

// ---------------------------------------------------------------------------
// Runtime state (toggled by radarBearingsMenu)
// ---------------------------------------------------------------------------

static bool s_forceNorthUp = false; // override IMU → fixed north-up
static int s_zoomLevel = 0;         // -2..+2, 0 = auto

bool isNorthUp() { return s_forceNorthUp; }

void toggleNorthUp() { s_forceNorthUp = !s_forceNorthUp; }

void zoomIn() {
  if (s_zoomLevel > -2)
    s_zoomLevel--;
}

void zoomOut() {
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
static float niceScaleMeters(float maxDistM, int zoomLevel) {
  static const float scales[] = {30,   60,    90,    150,   300,
                                 600,  900,   1500,  3000,  6000,
                                 9000, 15000, 30000, 90000, 300000};
  constexpr int N = sizeof(scales) / sizeof(scales[0]);

  int idx = 0;
  while (idx < N - 1 && maxDistM > scales[idx])
    idx++;

  idx = std::max(0, std::min(N - 1, idx + zoomLevel));
  return scales[idx];
}

/** Format metres as a compact string (metric or imperial). */
static void formatDistM(char *buf, size_t len, float metres) {
  const bool imperial = (config.display.units ==
                         meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL);
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

/** Format metres as a number only (no unit suffix) — used for radar ring
 * labels. */
static void formatDistNum(char *buf, size_t len, float metres) {
  const bool imperial = (config.display.units ==
                         meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL);
  if (imperial) {
    const float miles = metres / 1609.34f;
    if (miles < 0.1f)
      snprintf(buf, len, "%d", (int)(metres * 3.28084f));
    else if (miles < 10.0f)
      snprintf(buf, len, "%.1f", miles);
    else
      snprintf(buf, len, "%d", (int)(miles + 0.5f));
  } else {
    if (metres < 1000.0f)
      snprintf(buf, len, "%d", (int)metres);
    else if (metres < 10000.0f)
      snprintf(buf, len, "%.1f", metres / 1000.0f);
    else
      snprintf(buf, len, "%d", (int)(metres / 1000.0f + 0.5f));
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
static void drawMarker(OLEDDisplay *display, int px, int py, uint8_t sym) {
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
  case 4: // ◆ diamond
    display->drawLine(px, py - 2, px + 2, py);
    display->drawLine(px + 2, py, px, py + 2);
    display->drawLine(px, py + 2, px - 2, py);
    display->drawLine(px - 2, py, px, py - 2);
    break;
  case 5: // △ triangle up
    display->drawLine(px, py - 2, px + 2, py + 2);
    display->drawLine(px, py - 2, px - 2, py + 2);
    display->drawLine(px - 2, py + 2, px + 2, py + 2);
    break;
  case 6: // ▽ triangle down
    display->drawLine(px, py + 2, px + 2, py - 2);
    display->drawLine(px, py + 2, px - 2, py - 2);
    display->drawLine(px - 2, py - 2, px + 2, py - 2);
    break;
  case 7: // — horizontal bar
    display->drawLine(px - 2, py, px + 2, py);
    break;
  case 8: // ○ hollow circle
    display->drawCircle(px, py, 2);
    break;
  default: // * asterisk (+ and × combined)
    display->drawLine(px - 2, py, px + 2, py);
    display->drawLine(px, py - 2, px, py + 2);
    display->drawLine(px - 2, py - 2, px + 2, py + 2);
    display->drawLine(px + 2, py - 2, px - 2, py + 2);
    break;
  }
}

/** Plot a node on the radar at the correct bearing/distance position. */
static void plotNode(OLEDDisplay *display, int cx, int cy, int radius,
                     float bearingRad, float headingRad, float norm,
                     uint8_t markerIdx) {
  const float rel = bearingRad - headingRad;
  const int px = cx + (int)(radius * norm * sinf(rel));
  const int py = cy - (int)(radius * norm * cosf(rel));
  drawMarker(display, px, py, markerIdx);
}

/**
 * Draw just the BT/API connection icon glyph at the bottom-left, without the
 * full-width black wipe that drawCommonFooter performs.  The wipe was erasing
 * the radar circle's bottom arc and the descender of the last list row even
 * though the icon's actual 5×5 footprint (x=0..4 at scale=1) doesn't overlap
 * the radar (x≈80..126) or the list text (x≥7).
 *
 * Replicates the icon-rendering half of SharedUIDisplay::drawCommonFooter so
 * this overlay can own its own footer behaviour without touching shared UI.
 */
static void drawConnectionIconNoWipe(OLEDDisplay *display) {
  if (!isAPIConnected(service ? service->api_state : 0))
    return;

  const int scale = (currentResolution == ScreenResolution::High) ? 2 : 1;
  const int iconX = 0;
  const int iconY = SCREEN_HEIGHT - (connection_icon_height * scale);

  display->setColor(WHITE);
  if (currentResolution == ScreenResolution::High) {
    const int bytesPerRow = (connection_icon_width + 7) / 8;
    for (int yy = 0; yy < connection_icon_height; ++yy) {
      const uint8_t *rowPtr = connection_icon + yy * bytesPerRow;
      for (int xx = 0; xx < connection_icon_width; ++xx) {
        const uint8_t byteVal = pgm_read_byte(rowPtr + (xx >> 3));
        const uint8_t bitMask = 1U << (xx & 7); // XBM is LSB-first
        if (byteVal & bitMask) {
          display->fillRect(iconX + xx * scale, iconY + yy * scale, scale,
                            scale);
        }
      }
    }
  } else {
    display->drawXbm(iconX, iconY, connection_icon_width,
                     connection_icon_height, connection_icon);
  }
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
void drawRadarOverlay(OLEDDisplay *display, int16_t x, int16_t y) {
  const int headerH = FONT_HEIGHT_SMALL - 1;
  const int sw = SCREEN_WIDTH;
  const int sh = SCREEN_HEIGHT;

  // Single layout — the radar circle always uses the full height below the
  // header (matches the dense layout from before any footer reservation
  // existed) so its size doesn't shift when the BT/API icon appears.  Only
  // the list rows on the left reserve space, since they live in the same
  // column as the icon and would otherwise be clipped at the bottom.  The
  // reservation is icon-height + 1 px (the +1 leaves a single pixel of
  // breathing room above the icon); most font glyphs don't fill the bottom
  // of their bbox, so the last row's visible ink lands flush with the icon
  // instead of leaving the previous 3-4 px of unused descender space.
  const int footerScale = (currentResolution == ScreenResolution::High) ? 2 : 1;
  const int listFooterH = (connection_icon_height * footerScale) + 1;

  const int contentH = sh - headerH; // full-height area for the radar
  const int listContentH = contentH - listFooterH; // shorter area for list rows
  const int pad = (currentResolution == ScreenResolution::High) ? 9 : 4;

  // -----------------------------------------------------------------------
  // Radar circle — right side, 2 px padding on all sides.
  // -----------------------------------------------------------------------
  const int radarDiam = contentH - 2 * pad;
  const int radarRadius = radarDiam / 2;
  const int radarCX = x + sw - pad - radarRadius;
  const int radarCY = y + headerH + pad + radarRadius;

  // Node list panel fills the space to the left of the radar circle.
  const int listRight =
      radarCX - radarRadius - 4; // 4 px gap between list and circle

  // -----------------------------------------------------------------------
  // GPS — bail gracefully if unavailable.  No fix → no scale to report,
  // so the header stays plain.
  // -----------------------------------------------------------------------
  const meshtastic_NodeInfoLite *ourNode =
      nodeDB->getMeshNode(nodeDB->getNodeNum());
  meshtastic_PositionLite ourPos;
  if (!ourNode || !nodeDB->copyNodePosition(ourNode->num, ourPos) ||
      (ourPos.latitude_i == 0 && ourPos.longitude_i == 0)) {
    graphics::drawCommonHeader(display, x, y, "Radar");
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(x + sw / 2, y + sh / 2 - FONT_HEIGHT_SMALL / 2,
                        "No GPS fix");
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
  const float headingRad =
      headingUp
          ? screen->getHeading() * DEG_TO_RAD
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
    // Skip stale nodes — otherwise we plot ghosts at their last-known
    // position long after they have gone offline.  Uses the firmware-wide
    // "online" threshold (NUM_ONLINE_SECS, 2 hrs) so the radar matches what
    // the rest of the UI counts as an online node.
    if (sinceLastSeen(n) >= NUM_ONLINE_SECS)
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
  std::sort(entries.begin(), entries.end(),
            [](const Entry &a, const Entry &b) { return a.distM < b.distM; });

  // Auto-scale from only the nodes we will actually plot, so a single
  // far-away node can't push the scale into a high bucket and squash all
  // the close nodes into an invisible cluster at the centre.
  const int minDim = std::min(sw, sh);
  const int kMaxPlotted = (minDim >= 230) ? 10 : (minDim > 128) ? 8 : 5;
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
  // Ring distance labels — high-res only; numbers only, no unit suffix,
  // smallest available font, right-aligned flush inside the SE arc point.
  // All 3 rings labelled; the outer ring number echoes the header scale.
  // -----------------------------------------------------------------------
  if (currentResolution == ScreenResolution::High) {
    display->setFont(FONT_SMALL_LOCAL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    const int kRingFontH = _fontHeight(FONT_SMALL_LOCAL);
    const float oppNBrg = -headingRad + static_cast<float>(M_PI); // 180° from N
    for (int ring = 1; ring <= 3; ring++) {
      const int ringR = (radarRadius * ring) / 3;
      char ringLabel[12];
      formatDistNum(ringLabel, sizeof(ringLabel), scale * ring / 3.0f);
      // Centred on the ring arc, opposite N — just inside the line.
      const int lx = radarCX + (int)(ringR * sinf(oppNBrg));
      const int ly = radarCY - (int)(ringR * cosf(oppNBrg)) - kRingFontH;
      display->drawString(lx, ly, ringLabel);
    }
  }

  // -----------------------------------------------------------------------
  // North indicator — rotates in heading-up mode.
  // Top edge of the N glyph just touches ring 3 from inside.
  // -----------------------------------------------------------------------
  {
    const float northBrg = -headingRad;
    const int nRadius = radarRadius - FONT_HEIGHT_SMALL / 2;
    const int nx = radarCX + (int)(nRadius * sinf(northBrg));
    const int ny = radarCY - (int)(nRadius * cosf(northBrg));
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(nx, ny - FONT_HEIGHT_SMALL / 2, "N");
  }

  // Own-node marker: single pixel at centre.
  display->setPixel(radarCX, radarCY);

  // -----------------------------------------------------------------------
  // Plot remote nodes — cap at kMaxPlotted to match the list panel.
  //
  // Marker symbol is the sort-position index (0..9) so every plotted node
  // gets a unique shape and matches its row in the list panel.  Using the
  // node number modulo N caused symbol collisions when several plotted
  // nodes shared a residue.
  // -----------------------------------------------------------------------
  for (int i = 0; i < plottedCount; i++) {
    const Entry &e = entries[i];
    plotNode(display, radarCX, radarCY, radarRadius, e.bearingRad, headingRad,
             std::min(e.distM / scale, 1.0f), (uint8_t)i);
  }

  // -----------------------------------------------------------------------
  // Node list (left panel) — up to 10 closest nodes.
  //
  // Each row: marker symbol (matches the radar dot) | short name | distance.
  // -----------------------------------------------------------------------
  display->setFont(FONT_SMALL);

  constexpr int kListTopPad = 5;
  const int rowPitch = (listContentH - kListTopPad) / kMaxPlotted;

  // Marker centred to the visible text height (rowY is the top of the
  // glyph bbox; centring on rowPitch/2 read as "top-aligned" because the
  // font's bbox is taller than its visible ink).
  const int symOffsetY = (FONT_HEIGHT_SMALL - 2) / 2;

  for (int i = 0; i < plottedCount; i++) {
    const Entry &e = entries[i];
    const int rowY = y + headerH + kListTopPad + rowPitch * i;
    const int symCX = x + 6; // 4 px left margin + 2 px to marker centre
    const int symCY = rowY + symOffsetY;

    drawMarker(display, symCX, symCY, (uint8_t)i);

    char name[10] = "";
    if (nodeInfoLiteHasUser(e.node) && e.node->short_name[0])
      strncpy(name, e.node->short_name, sizeof(name) - 1);
    else
      snprintf(name, sizeof(name), "%04X", (uint16_t)(e.node->num & 0xFFFF));

    char dist[10] = "";
    formatDistM(dist, sizeof(dist), e.distM);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x + 11, rowY, name); // 3 px gap after marker right edge
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->drawString(x + listRight, rowY, dist);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
  }

  // BT/API connection icon — drawn here (no surrounding wipe) so the radar
  // circle and the last list row stay intact.  NodeListRenderer's radar
  // branch deliberately skips drawCommonFooter for the same reason.
  drawConnectionIconNoWipe(display);
}

} // namespace RadarRenderer
} // namespace graphics
#endif // HAS_SCREEN
