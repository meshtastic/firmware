#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./T5NodesApplet.h"
#include "./T5NodeDetailApplet.h"

#include "gps/GeoCoord.h"
#include "mesh/NodeDB.h"

#include <algorithm>

using namespace NicheGraphics;

namespace
{
constexpr int16_t MARGIN = 16;
constexpr const char *DASH = "\x97";    // Win-1253 em dash
constexpr const char *CHEVRON = "\x9B"; // Win-1253 single right angle quote: parse() has no remap for U+203A
} // namespace

void InkHUD::T5NodesApplet::onActivate()
{
    setInputsSubscribed(NAV_UP | NAV_DOWN, true); // Vertical swipes page the list
}

void InkHUD::T5NodesApplet::onDeactivate()
{
    setInputsSubscribed(NAV_UP | NAV_DOWN, false);
    signals.clear();
}

bool InkHUD::T5NodesApplet::wantPacket(const meshtastic_MeshPacket *p)
{
    return isActive() && !isFromUs(p);
}

// nodeDB keeps SNR but not RSSI, and the shared bars need both: record them from packets heard directly by our radio
ProcessMessage InkHUD::T5NodesApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    bool signalChanged = false;
    if (mp.transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA && mp.has_rx_rssi &&
        getHopsAway(mp) == 0) {
        const SignalStrength signal = getSignalStrength(mp.rx_snr, mp.rx_rssi);
        auto it = std::find_if(signals.begin(), signals.end(), [&mp](const Reception &s) { return s.from == mp.from; });
        if (it == signals.end()) {
            if (signals.size() >= MAX_NUM_NODES)
                signals.erase(signals.begin()); // Sender ids are unauthenticated: stay bounded
            signals.push_back({mp.from, signal, mp.rx_rssi});
            signalChanged = true;
        } else {
            signalChanged = it->signal != signal;
            *it = {mp.from, signal, mp.rx_rssi};
        }
    }

    // The sender is now the most recently heard node. Only the first page shows that; a later page isn't reshuffled
    // under the user's finger
    if (firstRow == 0 && (shown.empty() || shown[0] != mp.from || signalChanged))
        requestUpdate();

    return ProcessMessage::CONTINUE;
}

// All known nodes but ours, newest last_heard first. Never heard (0) sorts last. Stable, so equal timestamps
// keep nodeDB order and pages don't reshuffle between renders
std::vector<meshtastic_NodeInfoLite *> InkHUD::T5NodesApplet::sortedNodes()
{
    std::vector<meshtastic_NodeInfoLite *> nodes;
    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (node->num != nodeDB->getNodeNum())
            nodes.push_back(node);
    }
    std::stable_sort(nodes.begin(), nodes.end(), [](const meshtastic_NodeInfoLite *a, const meshtastic_NodeInfoLite *b) {
        return a->last_heard > b->last_heard;
    });
    return nodes;
}

InkHUD::T5NodesApplet::Row InkHUD::T5NodesApplet::readRow(meshtastic_NodeInfoLite *node)
{
    Row row;
    row.shortName = parseShortName(node);
    row.longName = nodeInfoLiteHasUser(node) ? parse(node->long_name) : hexifyNodeNum(node->num);
    if (node->last_heard)
        row.heardSecs = sinceLastSeen(node);
    if (node->has_hops_away)
        row.hops = node->hops_away;

    // Last SNR nodeDB recorded from our own radio. Bars also need RSSI, so only a direct reception this session has them
    if (nodeInfoLiteHasSnr(node)) {
        char snr[8];
        snprintf(snr, sizeof(snr), "%.1f", node->snr);
        row.snr = snr;
    }
    if (row.hops == 0) {
        for (const Reception &s : signals) {
            if (s.from == node->num) {
                row.signal = s.signal;
                row.rssi = to_string(s.rssi);
            }
        }
    }

    meshtastic_PositionLite ourPos, theirPos;
    const NodeNum ourNum = nodeDB->getNodeNum();
    if (nodeDB->hasValidPosition(nodeDB->getMeshNode(ourNum)) && nodeDB->hasValidPosition(node) &&
        nodeDB->copyNodePosition(ourNum, ourPos) && nodeDB->copyNodePosition(node->num, theirPos)) {
        const double ourLat = ourPos.latitude_i * 1e-7, ourLon = ourPos.longitude_i * 1e-7;
        const double theirLat = theirPos.latitude_i * 1e-7, theirLon = theirPos.longitude_i * 1e-7;
        row.distance = localizeDistance(GeoCoord::latLongToMeter(theirLat, theirLon, ourLat, ourLon));
        row.degrees = lround(GeoCoord::toDegrees(GeoCoord::bearing(ourLat, ourLon, theirLat, theirLon)) + 360) % 360;
        row.bearing =
            std::string(GeoCoord::degreesToBearing(row.degrees)) + " " + to_string(row.degrees) + "\xB0"; // Win-1253 degree
    }
    return row;
}

void InkHUD::T5NodesApplet::onRender(bool full)
{
    (void)full;
    const std::vector<meshtastic_NodeInfoLite *> nodes = sortedNodes();
    const uint8_t perPage = rowsPerPage();

    // Snap to a page boundary: rows per page differ between modes, so a rotation keeps roughly the same nodes
    firstRow -= firstRow % perPage;
    if (firstRow >= nodes.size())
        firstRow = nodes.empty() ? 0 : (nodes.size() - 1) / perPage * perPage;

    uint16_t heard = 0, direct = 0;
    for (const meshtastic_NodeInfoLite *node : nodes) {
        heard += heardRecently(node);
        direct += node->has_hops_away && node->hops_away == 0;
    }

    std::vector<Row> rows;
    shown.clear();
    for (size_t i = firstRow; i < nodes.size() && rows.size() < perPage; i++) {
        rows.push_back(readRow(nodes[i]));
        shown.push_back(nodes[i]->num);
    }

    if (isConsole())
        renderConsole(rows, nodes.size(), heard, direct);
    else
        renderCarry(rows, nodes.size(), heard);
}

// Four bars, filled up to the shared getSignalStrength() rating, as NodeListApplet counts them
void InkHUD::T5NodesApplet::drawSignal(int16_t left, int16_t bottom, SignalStrength signal)
{
    constexpr int16_t heights[] = {8, 13, 17, 22};
    for (uint8_t i = 0; i < 4; i++) {
        const int16_t x = left + i * 11;
        if (i <= signal)
            fillRect(x, bottom - heights[i], 8, heights[i], BLACK);
        else
            drawLine(x, bottom - 1, x + 7, bottom - 1, BLACK);
    }
}

void InkHUD::T5NodesApplet::renderCarry(const std::vector<Row> &rows, uint16_t nodes, uint16_t heard)
{
    const int16_t right = width() - MARGIN;

    // Header: title with both counts inline, sharing its baseline. At real font metrics the LAST HEARD label
    // can't also fit, so Carry leaves it out
    setFont(fontLarge);
    printBold(MARGIN, 14, "NODES");
    int16_t x = MARGIN + getTextWidth("NODES") + 12;
    const int16_t countsTop = 14 + fontLarge.heightAboveCursor() - fontSmall.heightAboveCursor();
    setFont(fontSmall);
    const std::string nodesText = to_string(nodes) + " NODES", heardText = to_string(heard) + " HEARD 10M";
    printBold(x, countsTop, nodesText);
    x += getTextWidth(nodesText) + 12;
    printAt(x, countsTop, heardText);
    drawLine(0, 79, width() - 1, 79, BLACK);

    // Long names and metadata share one column, just clear of the widest short name on this page
    constexpr int16_t barsLeft = 429;
    int16_t column = 112;
    setFont(fontMedium);
    for (const Row &row : rows)
        column = std::max<int16_t>(column, MARGIN + getTextWidth(row.shortName) + 2 + 12);

    for (uint8_t i = 0; i < rows.size(); i++) {
        const Row &row = rows[i];
        const int16_t top = rowsTop() + i * rowHeight();

        setFont(fontMedium);
        printBold(MARGIN, top + 12, row.shortName);
        setCrop(column, top, barsLeft - 12 - column, rowHeight());
        printAt(column, top + 12, row.longName);
        resetCrop();
        printAt(width() - 14, top + 42, CHEVRON, RIGHT, MIDDLE);
        if (row.signal != SIGNAL_UNKNOWN)
            drawSignal(barsLeft, top + 52, row.signal);

        setFont(fontSmall);
        const std::string hops = row.hops < 0 ? "" : hopsString(row.hops);
        const std::string ago = row.heardSecs < 0 ? "" : agoString(row.heardSecs);
        setCrop(column, top, right - column, rowHeight());
        printAt(column, top + 54, join(join(row.distance, hops), ago));
        resetCrop();

        drawLine(0, top + rowHeight() - 1, width() - 1, top + rowHeight() - 1, BLACK);
    }

    drawNav(NODES);
}

void InkHUD::T5NodesApplet::renderConsole(const std::vector<Row> &rows, uint16_t nodes, uint16_t heard, uint16_t direct)
{
    // Header: title, three counts, sort label, centered in a 63 px band
    setFont(fontLarge);
    const int16_t titleTop = (63 - (fontLarge.heightAboveCursor() + fontLarge.heightBelowCursor())) / 2;
    printBold(MARGIN, titleTop, "NODES");
    int16_t x = MARGIN + getTextWidth("NODES") + 24;
    const int16_t countsTop = titleTop + fontLarge.heightAboveCursor() - fontSmall.heightAboveCursor();
    setFont(fontSmall);
    for (const std::string &count :
         {to_string(nodes) + " NODES", to_string(heard) + " HEARD 10M", to_string(direct) + " DIRECT"}) {
        printAt(x, countsTop, count);
        x += getTextWidth(count) + 34;
    }
    const int16_t sortW = getTextWidth("LAST HEARD") + 2 * 14 + 4, sortH = fontSmall.lineHeight() + 2 * 8 + 4;
    const int16_t sortLeft = width() - MARGIN - sortW, sortTop = (63 - sortH) / 2;
    drawRect(sortLeft, sortTop, sortW, sortH, BLACK); // The one sort order, boxed at the right edge
    drawRect(sortLeft + 1, sortTop + 1, sortW - 2, sortH - 2, BLACK);
    printBold(sortLeft + sortW / 2, sortTop + 2 + 8, "LAST HEARD", CENTER);
    drawLine(0, 63, width() - 1, 63, BLACK);

    // Column headings
    constexpr int16_t nameX = 16, longX = 120, heardX = 366, hopsX = 476, snrX = 564, distX = 652, bearingX = 750;
    const std::pair<int16_t, const char *> headings[] = {{nameX, "NAME"},      {longX, "LONG NAME"}, {heardX, "HEARD"},
                                                         {hopsX, "HOPS"},      {snrX, "SNR"},        {distX, "DIST"},
                                                         {bearingX, "BEARING"}};
    for (const auto &heading : headings)
        printBold(heading.first, 81 - fontSmall.lineHeight() / 2, heading.second);
    drawLine(0, 97, width() - 1, 97, BLACK);

    for (uint8_t i = 0; i < rows.size(); i++) {
        const Row &row = rows[i];
        const int16_t top = rowsTop() + i * rowHeight();
        const int16_t mid = top + rowHeight() / 2 - 1;
        auto cell = [](const std::string &text) { return text.empty() ? std::string(DASH) : text; };

        setFont(fontMedium);
        setCrop(nameX, top, longX - nameX - 8, rowHeight());
        printBold(nameX, mid - fontMedium.lineHeight() / 2, row.shortName);
        setCrop(longX, top, heardX - longX - 24, rowHeight()); // Wider than a word space: a cut name can't run into HEARD
        printAt(longX, mid, row.longName, LEFT, MIDDLE);
        resetCrop();
        printAt(width() - MARGIN, mid, CHEVRON, RIGHT, MIDDLE);

        setFont(fontSmall);
        printAt(heardX, mid, cell(row.heardSecs < 0 ? "" : sinceString(row.heardSecs)), LEFT, MIDDLE);
        printBold(hopsX, mid - fontSmall.lineHeight() / 2,
                  cell(row.hops < 0    ? ""
                       : row.hops == 0 ? "Direct"
                                       : to_string(row.hops)));
        printAt(snrX, mid, cell(row.snr), LEFT, MIDDLE);
        printAt(distX, mid, cell(row.distance), LEFT, MIDDLE);
        printAt(bearingX, mid, cell(row.bearing), LEFT, MIDDLE);

        drawLine(0, top + rowHeight() - 1, width() - 1, top + rowHeight() - 1, BLACK);
    }

    drawNav(NODES);
}

bool InkHUD::T5NodesApplet::onTouchPoint(uint16_t x, uint16_t y, bool longPress)
{
    if (longPress)
        return false; // Keep the shared long-press fallback (menu)

    // Touch points are in display space; drawing is relative to our tile
    const int16_t tx = x - getTile()->getLeft();
    const int16_t ty = y - getTile()->getTop();

    if (handleNavTap(tx, ty))
        return true;

    if (ty >= rowsTop() && (ty - rowsTop()) / rowHeight() < (int16_t)shown.size()) {
        const NodeNum selected = shown[(ty - rowsTop()) / rowHeight()];
        if (!T5NodeDetailApplet::open(selected))
            LOG_WARN("T5 Nodes: can't open Node detail for 0x%08x", selected); // Stay on Nodes
    }

    // Consume every tap: the button fallback would cycle applets
    return true;
}

// Swipes arrive as nav events, with the shared menu's and waypoint list's direction: down moves forward
void InkHUD::T5NodesApplet::onNavDown()
{
    const uint16_t next = firstRow - firstRow % rowsPerPage() + rowsPerPage();
    if (next < sortedNodes().size()) {
        firstRow = next;
        requestUpdate();
    }
}

void InkHUD::T5NodesApplet::onNavUp()
{
    if (firstRow < rowsPerPage())
        return; // First page already
    firstRow -= firstRow % rowsPerPage() + rowsPerPage();
    requestUpdate();
}

#endif
