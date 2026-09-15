#if defined(T5_S3_EPAPER_PRO) && defined(MESHTASTIC_INCLUDE_INKHUD)

#include "./T5NodeDetailApplet.h"
#include "./T5NodesApplet.h"

#include "mesh/NodeDB.h"

#include <cmath>

using namespace NicheGraphics;

namespace
{
constexpr int16_t MARGIN = 16;
constexpr const char *DASH = "\x97";  // Win-1253 em dash
constexpr const char *BACK = "\x8B "; // Win-1253 single left angle quote: parse() has no remap for U+2039
constexpr int16_t PLOT_RADIUS = 64;   // One node gives direction, not scale: the marker sits at a fixed distance
} // namespace

bool InkHUD::T5NodeDetailApplet::open(NodeNum num)
{
    InkHUD *hud = InkHUD::getInstance();
    const int8_t i = indexOf("Node Detail");
    if (i < 0 || !nodeDB->getMeshNode(num))
        return false;
    static_cast<T5NodeDetailApplet *>(hud->userApplets[i])->node = num;
    return hud->showApplet(i);
}

// Live while shown: the node's own packets change its last heard, signal and SNR / RSSI facts
bool InkHUD::T5NodeDetailApplet::wantPacket(const meshtastic_MeshPacket *p)
{
    return isForeground() && p->from == node;
}

ProcessMessage InkHUD::T5NodeDetailApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    (void)mp;
    requestUpdate();
    return ProcessMessage::CONTINUE;
}

void InkHUD::T5NodeDetailApplet::onRender(bool full)
{
    (void)full;
    const bool console = isConsole();
    const int16_t titleTop = console ? 4 : 14;

    meshtastic_NodeInfoLite *info = nodeDB->getMeshNode(node);
    if (!info) {
        // Gone from nodeDB, or reached by cycling applets before any node was opened: only the ways out
        setFont(fontLarge);
        printBold(MARGIN, titleTop, std::string(BACK) + "NODES");
        drawNav(NODES);
        return;
    }

    // Same reading of the node as its T5 Nodes row
    const int8_t nodesIndex = indexOf("Nodes");
    assert(nodesIndex >= 0);
    const T5NodesApplet::Row row = static_cast<T5NodesApplet *>(inkhud->userApplets[nodesIndex])->readRow(info);
    const std::string back = std::string(BACK) + row.shortName, id = hexifyNodeNum(node);

    // Header: the whole band is the Back target. Long name and id share the title's baseline
    setFont(fontLarge);
    printBold(MARGIN, titleTop, back);
    const int16_t nameTop = titleTop + fontLarge.heightAboveCursor() - fontMedium.heightAboveCursor();
    if (console) {
        // The id sits before the favourite chip's slot, which stays reserved when the chip isn't drawn
        setFont(fontSmall);
        const int16_t chipW = getTextWidth("FAVOURITE") + 2 * 14 + 4, chipH = fontSmall.lineHeight() + 2 * 8 + 4;
        const int16_t chipLeft = width() - MARGIN - chipW, chipTop = (63 - chipH) / 2;
        if (nodeInfoLiteIsFavorite(info)) {
            drawRect(chipLeft, chipTop, chipW, chipH, BLACK);
            drawRect(chipLeft + 1, chipTop + 1, chipW - 2, chipH - 2, BLACK);
            printBold(chipLeft + chipW / 2, chipTop + 2 + 8, "FAVOURITE", CENTER);
        }
        const int16_t idLeft = chipLeft - MARGIN - getTextWidth(id);
        printAt(idLeft, titleTop + fontLarge.heightAboveCursor() - fontSmall.heightAboveCursor(), id);
        setFont(fontMedium);
        setCrop(276, 0, idLeft - 12 - 276, 63);
        printAt(276, nameTop, row.longName);
        resetCrop();
        drawLine(0, 63, width() - 1, 63, BLACK);
    } else {
        // Long name clear of the widest short name; at real font widths the id can't share its line, so it sits below
        const int16_t column = std::max<int16_t>(150, MARGIN + getTextWidth(back) + 2 + 12);
        setFont(fontMedium);
        setCrop(column, 0, width() - MARGIN - column, 99);
        printAt(column, nameTop, row.longName);
        resetCrop();
        setFont(fontSmall);
        printAt(width() - MARGIN, 68, id, RIGHT);
        drawLine(0, 99, width() - 1, 99, BLACK);
    }

    // Six facts: 2-up in Carry, 3-up in Console. Console's 192 px cells can't hold "59 min ago" or "-20.0 / -130"
    // at real font widths, so it uses the shorter forms
    static constexpr const char *signalNames[] = {"None", "Bad", "Fair", "Good"};
    auto cell = [](const std::string &text) { return text.empty() ? std::string(DASH) : text; };
    std::string battery;
    meshtastic_DeviceMetrics metrics;
    if (nodeDB->copyNodeTelemetry(node, metrics) && metrics.has_battery_level)
        battery = metrics.battery_level > 100 ? "Plugged In" : to_string(metrics.battery_level) + "%";
    const std::pair<const char *, std::string> facts[] = {
        {"LAST HEARD", cell(row.heardSecs < 0 ? ""
                            : console         ? sinceString(row.heardSecs)
                                              : agoString(row.heardSecs))},
        {"HOPS AWAY", cell(row.hops < 0    ? ""
                           : row.hops == 0 ? "Direct"
                                           : hopsString(row.hops))},
        {"SIGNAL", cell(row.signal == SIGNAL_UNKNOWN ? "" : signalNames[row.signal])},
        {"SNR / RSSI", row.snr.empty() && row.rssi.empty() ? DASH : cell(row.snr) + (console ? "/" : " / ") + cell(row.rssi)},
        {"DISTANCE", cell(row.distance)},
        {"BATTERY", cell(battery)},
    };
    const uint8_t cols = console ? 3 : 2;
    const int16_t gridTop = console ? 76 : 112, cellW = console ? 192 : 238, cellH = console ? 82 : 88;
    const int16_t keyInset = console ? 10 : 12, valueTop = keyInset + fontSmall.lineHeight() + (console ? 4 : 6);
    for (uint8_t i = 0; i < 6; i++) {
        const int16_t left = MARGIN + i % cols * cellW, top = gridTop + i / cols * cellH;
        setFont(fontSmall);
        printAt(left, top + keyInset, facts[i].first);
        setFont(fontMedium);
        printBold(left, top + valueTop, facts[i].second);
        drawLine(left, top + cellH - 1, left + cellW - 1, top + cellH - 1, BLACK);
    }

    // Relative position: under the facts in Carry, beside them in Console. North is up, we are the circle
    const int16_t plotLeft = console ? 624 : MARGIN, plotTop = console ? 76 : 396;
    const int16_t plotW = console ? 288 : 476, plotH = console ? 246 : 240, inset = console ? 10 : 12;
    const int16_t cx = plotLeft + plotW / 2, cy = plotTop + plotH / 2 + 12;
    drawRect(plotLeft, plotTop, plotW, plotH, BLACK);
    drawRect(plotLeft + 1, plotTop + 1, plotW - 2, plotH - 2, BLACK);
    setFont(fontSmall);
    printAt(plotLeft + inset, plotTop + 8, "RELATIVE POSITION");
    for (int16_t y = plotTop + 34; y < plotTop + plotH - 10; y += 4)
        drawPixel(cx, y, BLACK);
    for (int16_t x = plotLeft + inset; x < plotLeft + plotW - inset; x += 4)
        drawPixel(x, cy, BLACK);
    printAt(plotLeft + plotW - inset, plotTop + plotH - 8, cell(join(row.distance, row.bearing)), RIGHT, BOTTOM);
    if (row.degrees >= 0) {
        drawCircle(cx, cy, 8, BLACK);
        drawCircle(cx, cy, 7, BLACK);
        const int16_t mx = cx + lround(PLOT_RADIUS * sin(row.degrees * M_PI / 180));
        const int16_t my = cy - lround(PLOT_RADIUS * cos(row.degrees * M_PI / 180));
        fillRect(mx - 13, my - 1, 26, 2, BLACK);
        fillRect(mx - 1, my - 13, 2, 26, BLACK);
        // Name on the side away from the middle: clear of the title above and the distance label below
        printBold(mx, my > cy ? my - 16 - fontSmall.lineHeight() : my + 16, row.shortName, CENTER);
    }

    // Actions. OPEN MAP is struck through, like a nav cell, when no map applet is active
    const bool mapAvailable = destinationApplet(MAP) >= 0;
    auto action = [this](int16_t left, int16_t top, uint16_t w, uint16_t h, const char *text, bool available) {
        printThick(left + w / 2, top + h / 2, text, 2, 1);
        if (!available)
            fillRect(left + (w - getTextWidth(text)) / 2 - 8, top + h / 2 - 1, getTextWidth(text) + 16, 2, BLACK);
    };
    if (console) {
        drawLine(MARGIN, 334, MARGIN + 575, 334, BLACK);
        drawLine(MARGIN + 288, 334, MARGIN + 288, 403, BLACK);
        setFont(fontSmall);
        action(MARGIN, 334, 288, 70, "COMPOSE MESSAGE", true);
        action(MARGIN + 288, 334, 288, 70, "OPEN MAP", mapAvailable);
    } else {
        drawLine(0, 660, width() - 1, 660, BLACK);
        drawLine(0, 747, width() - 1, 747, BLACK);
        drawLine(0, 835, width() - 1, 835, BLACK);
        setFont(fontMedium);
        action(0, 660, width(), 88, "COMPOSE MESSAGE", true);
        action(0, 748, width(), 88, "OPEN MAP", mapAvailable);
    }

    drawNav(NODES); // Detail is a layer of Nodes, not a destination of its own
}

bool InkHUD::T5NodeDetailApplet::onTouchPoint(uint16_t x, uint16_t y, bool longPress)
{
    if (longPress)
        return false; // Keep the shared long-press fallback (menu)

    // Touch points are in display space; drawing is relative to our tile
    const int16_t tx = x - getTile()->getLeft();
    const int16_t ty = y - getTile()->getTop();

    if (handleNavTap(tx, ty))
        return true;

    const bool console = isConsole();
    int8_t target = -1;
    if (ty < (console ? 64 : 100)) {
        target = destinationApplet(NODES); // Back is T5 Nodes, never Home
    } else if (console ? (ty >= 334 && ty < 404 && tx >= MARGIN && tx < MARGIN + 576) : (ty >= 660 && ty < 836)) {
        if (console ? tx < MARGIN + 288 : ty < 748)
            inkhud->openMenu(); // The shared send flow, at its root: nothing public preselects this node as recipient
        else
            target = destinationApplet(MAP); // Opens as the map composes itself: nothing public centres it on this node
    }
    if (target >= 0)
        inkhud->showApplet(target);

    // Consume every tap: the button fallback would cycle applets
    return true;
}

#endif
