#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./NodeListApplet.h"

#include "rtc.h"

using namespace NicheGraphics;

InkHUD::NodeListApplet::NodeListApplet(const char *name) : MeshModule(name), OSThread(name)
{
    // No scheduled tasks initially
    OSThread::disable();
}

// When applet fully stops, due to disabled via on-screen menu
// This does *not* happen user cycles through applets with the user button
void InkHUD::NodeListApplet::onDeactivate()
{
    // Free some memory; we'll have to recalculate this when we reactivate anyway
    ordered.clear();
    ordered.shrink_to_fit();
}

void InkHUD::NodeListApplet::render()
{
    /*
    +-------------------------------+
    |                            |  |
    |  SHRT                  . | |  |
    |                               |
    |  Long name              50km  |
    |                               |
    |                               |
    |  ABCD                 2 Hops  |
    |                               |
    |  abcdedfghijk           30km  |
    |                               |
    +-------------------------------+
    */

    // Perhaps update info about which nodes are active
    // - optional, virtual
    // - implemented by derived applet class, as required
    // - suitable for: pruning the list of nodes, updating active node counts, etc
    updateActivityInfo();

    // ================================
    // Draw the standard applet header
    // ================================

    drawHeader(getHeaderText()); // Todo: refactor the getHeaderText() method; relic from before the "standard applet header"

    // Dimensions of the header
    int16_t headerDivY = getHeaderHeight() - 1;
    constexpr uint16_t padDivH = 2;

    // ========================
    // Draw the main node list
    // ========================

    const uint8_t cardMarginH = fontSmall.lineHeight() / 2; // Gap between cards
    const uint16_t cardH = fontLarge.lineHeight() + fontSmall.lineHeight() + cardMarginH;

    // Imaginary vertical line dividing left-side and right-side info
    // Long-name will crop here
    const uint16_t dividerX = (width() - 1) - getTextWidth("X Hops");

    // Y value (top) of the current card. Increases as we draw.
    uint16_t cardTopY = headerDivY + padDivH;

    // -- Each node in list --
    for (uint16_t i = 0; i < ordered.size(); i++) {
        NodeListItem record = ordered.at(i);

        // Gather info
        // ========================================
        NodeNum &nodeNum = record.nodeNum;
        SignalStrength &strength = record.strength;
        std::string longName;  // handled below
        std::string shortName; // handled below
        std::string distance;  // handled below
        uint8_t hopsAway;      // handled below

        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeNum);

        // -- Shortname --
        // use "?" if unknown
        if (node && node->has_user)
            shortName = node->user.short_name;
        else
            shortName = "?";

        // -- Longname --
        // use node id if unknown
        if (node && node->has_user)
            longName = node->user.long_name; // Found in nodeDB
        else {
            // Not found in nodeDB, show a hex nodeid instead
            longName = hexifyNodeNum(nodeNum);
        }

        // -- Distance --
        // use "? km" if unknown
        // Todo: miles
        meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
        if (node && nodeDB->hasValidPosition(node) && nodeDB->hasValidPosition(ourNode)) {
            // Get lat and long as float
            // Meshtastic stores these as integers internally
            float ourLat = ourNode->position.latitude_i * 1e-7;
            float ourLong = ourNode->position.longitude_i * 1e-7;
            float theirLat = node->position.latitude_i * 1e-7;
            float theirLong = node->position.longitude_i * 1e-7;

            float metersApart = GeoCoord::latLongToMeter(theirLat, theirLong, ourLat, ourLong);
            int16_t kmApart = ((metersApart / 1000.0) + 0.5); // 0.5 for rounding
            distance = to_string(kmApart);
        } else {
            distance = "? ";
        }
        distance += "km";

        // -- Hops Away --
        // Not drawn if unknown
        if (node)
            hopsAway = node->hops_away;
        else
            hopsAway = 0xFF; // Mark as unknown: no NodeDB entry available

        // Draw the info
        // ====================================

        // Define two lines of text for the card
        // We will center our text on these lines
        uint16_t lineAY = cardTopY + (fontLarge.lineHeight() / 2);
        uint16_t lineBY = cardTopY + fontLarge.lineHeight() + (fontSmall.lineHeight() / 2);

        // Print the short name
        setFont(fontLarge);
        printAt(0, lineAY, shortName, LEFT, MIDDLE);

        // Print the distance
        setFont(fontSmall);
        printAt(width() - 1, lineBY, distance, RIGHT, MIDDLE);

        // If we have a direct connection to the node, draw the signal indicator
        // Todo: figure out how to implement "hops unknown"
        if (hopsAway == 0 && strength != SIGNAL_UNKNOWN) {
            uint16_t signalW = getTextWidth("Xkm");
            uint16_t signalH = fontLarge.lineHeight() * 0.75;
            int16_t signalY = lineAY + (fontLarge.lineHeight() / 2) - (fontLarge.lineHeight() * 0.75);
            int16_t signalX = width() - signalW;
            drawSignalIndicator(signalX, signalY, signalW, signalH, strength);
        }
        // Otherwise, print "hops away" info, if available
        else if (hopsAway != 0xFF) {
            std::string hopString = to_string(node->hops_away);
            hopString += " Hop";
            if (node->hops_away != 1)
                hopString += "s"; // Append s for "Hops", rather than "Hop"

            printAt(width() - 1, lineAY, hopString, RIGHT, MIDDLE);
        }

        // Print the long name, cropping to prevent overflow onto the right-side info
        setCrop(0, 0, dividerX - 1, height());
        printAt(0, lineBY, longName, LEFT, MIDDLE);

        // GFX effect: "hatch" the right edge of longName area
        // If a longName has been cropped, it will appear to fade out,
        // creating a soft barrier with the right-side info
        const int16_t hatchLeft = dividerX - 1 - (fontSmall.lineHeight());
        const int16_t hatchWidth = fontSmall.lineHeight();
        hatchRegion(hatchLeft, headerDivY + 1, hatchWidth, height(), 2, WHITE);

        // Prepare to draw the next card
        resetCrop();
        cardTopY += cardH;

        // Once we've run out of screen:
        // - stop drawing cards
        // - drop any unused HeardRecords from the vector
        //     This frees a significant amount of memory after a populateNodeList() call
        //     It also prevents the vector growing slowly during normal use
        if (cardTopY > height()) {
            ordered.resize(i + 1);
            ordered.shrink_to_fit();
            break;
        }
    }
}

// Draw element: a "mobile phone" style signal indicator
// We will calculate values as floats, then "rasterize" at the last moment, relative to x and w, etc
// This prevents issues with premature rounding when rendering tiny elements
void InkHUD::NodeListApplet::drawSignalIndicator(int16_t x, int16_t y, uint16_t w, uint16_t h, SignalStrength strength)
{

    /*
    +-------------------------------------------+
    |                                           |
    |                                           |
    |                                  barHeightRelative=1.0
    |                                  +--+ ^   |
    |        gutterW          +--+     |  | |   |
    |          <-->  +--+     |  |     |  | |   |
    |     +--+       |  |     |  |     |  | |   |
    |     |  |       |  |     |  |     |  | |   |
    | <-> +--+       +--+     +--+     +--+ v   |
    | paddingW             ^                    |
    |             paddingH |                    |
    |                      v                    |
    +-------------------------------------------+
    */

    constexpr float paddingW = 0.1; // Either side
    constexpr float paddingH = 0.1; // Above and below
    constexpr float gutterX = 0.1;  // Between bars

    constexpr float barHRel[] = {0.3, 0.5, 0.7, 1.0}; // Heights of the signal bars, relative to the talleest
    constexpr uint8_t barCount = 4; // How many bars we draw. Reference only: changing value won't change the count.

    // Dynamically calculate the width of the bars, and height of the rightmost, relative to other dimensions
    float barW = (1.0 - (paddingW + ((barCount - 1) * gutterX) + paddingW)) / barCount;
    float barHMax = 1.0 - (paddingH + paddingH);

    // Draw signal bar rectangles, then placeholder lines once strength reached
    for (uint8_t i = 0; i < barCount; i++) {
        // Co-ords for this specific bar
        float barH = barHMax * barHRel[i];
        float barX = paddingW + (i * (gutterX + barW));
        float barY = paddingH + (barHMax - barH);

        // Rasterize to px coords at the last moment
        int16_t rX = (x + (w * barX)) + 0.5;
        int16_t rY = (y + (h * barY)) + 0.5;
        uint16_t rW = (w * barW) + 0.5;
        uint16_t rH = (h * barH) + 0.5;

        // Draw signal bars, until we are displaying the correct "signal strength", then just draw placeholder lines
        if (i <= strength)
            drawRect(rX, rY, rW, rH, BLACK);
        else {
            // Just draw a placeholder line
            float lineY = barY + barH;
            uint16_t rLineY = (y + (h * lineY)) + 0.5; // Rasterize
            drawLine(rX, rLineY, rX + rW - 1, rLineY, BLACK);
        }
    }
}

void InkHUD::NodeListApplet::populateNodeList()
{
    // Erase the old invalid order
    ordered.clear();

    // If no nodes, nothing to do
    if (nodeDB->getNumMeshNodes() == 0)
        return;

    // Fill with nodes from NodeDB
    // Signal strength is unknown, as the RSSI component is not stored
    uint32_t now = getValidTime(RTCQuality::RTCQualityNone, true); // Current RTC time
    for (uint32_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        NodeListItem oldRecord;

        if (node->num != nodeDB->getNodeNum()) { // Don't store our own node
            oldRecord.nodeNum = node->num;
            oldRecord.strength = SIGNAL_UNKNOWN;
            oldRecord.lastHeardEpoch = now;

            // Make derived applets decide whether or not this node should be added to the list
            // Used to distinguish between "active" and "last heard" applet variants
            if (shouldListNode(oldRecord))
                ordered.push_back(oldRecord);
        }
    }

    sortNodeList();

    // The "ordered" vector is truncated at the end of render()
    // At that point, we know how many nodes we can fit on the display
}

void InkHUD::NodeListApplet::sortNodeList()
{
    // Sort from youngest to oldest
    std::sort(ordered.begin(), ordered.end(), NodeListApplet::compareLastHeard);
}

void InkHUD::NodeListApplet::recordHeard(NodeListItem justHeard)
{
    if (!ordered.empty()) {
        for (uint16_t i = 0; i < ordered.size(); i++) {
            // If we had already heard the node, need to erase the element
            // This is inefficient , but we compensated by shrinking ordered,
            // so that it only holds as many nodes as we can fit on the display.
            if (ordered.at(i).nodeNum == justHeard.nodeNum) {
                ordered.erase(ordered.begin() + i);
                break;
            }
        }
    }

    // Insert the node (either new, or updated) at front of vector
    ordered.insert(ordered.begin(), justHeard);

    // The "ordered" vector is truncated at the end of render()
    // At that point, we know how many nodes we can fit on the display
}

// Sort function: youngest to oldest
// True if n1 heard more recently than n2
// When std::sort-ing, true will push r1 towards the front, and r2 towards the back
bool InkHUD::NodeListApplet::compareLastHeard(NodeListItem r1, NodeListItem r2)
{
    meshtastic_NodeInfoLite *n1 = nodeDB->getMeshNode(r1.nodeNum);
    meshtastic_NodeInfoLite *n2 = nodeDB->getMeshNode(r2.nodeNum);

    // r1 *not* more recent: we have no idea when we heard it?
    if (!n1)
        return false;

    // r1 more recent: we have no idea when we heard r2..
    if (!n2)
        return true;

    // Both nodes have a last_heard value
    // true if r1 was heard more recently
    return (n1->last_heard > n2->last_heard);
}

bool InkHUD::NodeListApplet::heardRecently(NodeListItem item)
{
    uint32_t now = getValidTime(RTCQuality::RTCQualityNone, true); // Current RTC time

    // If just booting, these are probably old nodes with invalid times
    // Note: not millis() overflow safe
    // Broken. Todo: fix
    if (item.lastHeardEpoch < 10)
        return false;

    if (now - item.lastHeardEpoch > settings.recentlyActiveSeconds) // Set by user in menu
        return false;                                               // Too old

    // Otherwise, active and found
    return true;
}

// Do we want to process this packet with handleReceived()?
bool InkHUD::NodeListApplet::wantPacket(const meshtastic_MeshPacket *p)
{
    // Only interested if the packet *didn't* come from us, and our applet is active
    return isActive() && !isFromUs(p);
}

// MeshModule packets arrive here. Hand off the appropriate module
ProcessMessage InkHUD::NodeListApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Compile info about this event
    NodeListItem justHeard;
    justHeard.nodeNum = mp.from;
    justHeard.strength = getSignalStrength(mp.rx_snr, mp.rx_rssi);
    justHeard.lastHeardEpoch =
        getValidTime(RTCQuality::RTCQualityNone, true); // Current RTC Time.. or seconds since boot if unset

    // Check if node list has changed
    // Todo: also compare hops away
    bool listChanged = false;
    if (ordered.empty()) // List changed: no nodes yet
        listChanged = true;
    else {
        NodeListItem previouslyHeard = ordered.front();
        if (previouslyHeard.nodeNum != justHeard.nodeNum) // List changed: re-ordered
            listChanged = true;
        else if (previouslyHeard.strength != justHeard.strength) // List changed: signal
            listChanged = true;
    }

    // Put node in list
    recordHeard(justHeard);

    // Redraw the applet, perhaps.
    // Foreground-background and auto-show will be considered by WindowManager
    if (listChanged)
        requestUpdate();

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

void InkHUD::NodeListApplet::onForeground()
{
    // Start applet's thread, running every minute
    OSThread::setIntervalFromNow(60 * 1000UL);
    OSThread::enabled = true;
}

void InkHUD::NodeListApplet::onBackground()
{
    OSThread::disable();
}

int32_t InkHUD::NodeListApplet::runOnce()
{
    updateActivityInfo();
    return interval; // Same as always: once a minute
}

#endif