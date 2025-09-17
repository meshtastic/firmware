#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "RTC.h"

#include "GeoCoord.h"
#include "NodeDB.h"

#include "./NodeListApplet.h"

using namespace NicheGraphics;

InkHUD::NodeListApplet::NodeListApplet(const char *name) : MeshModule(name)
{
    // We only need to be promiscuous in order to hear NodeInfo, apparently. See NodeInfoModule
    // For all other packets, we manually act as if isPromiscuous=false, in wantPacket
    MeshModule::isPromiscuous = true;
}

// Do we want to process this packet with handleReceived()?
bool InkHUD::NodeListApplet::wantPacket(const meshtastic_MeshPacket *p)
{
    // Only interested if:
    return isActive()                                                  // Applet is active
           && !isFromUs(p)                                             // Packet is incoming (not outgoing)
           && (isToUs(p) || isBroadcast(p->to) ||                      // Either: intended for us,
               p->decoded.portnum == meshtastic_PortNum_NODEINFO_APP); // or nodeinfo

    // To match the behavior seen in the client apps:
    // - NodeInfoModule's ProtoBufModule base is "promiscuous"
    // - All other activity is *not* promiscuous

    // To achieve this, our MeshModule *is* promiscuous, and we're manually reimplementing non-promiscuous behavior here,
    // to match the code in MeshModule::callModules
}

// MeshModule packets arrive here
// Extract the info and pass it to the derived applet
// Derived applet will store the CardInfo, and perform any required sorting of the CardInfo collection
// Derived applet might also need to keep other tallies (active nodes count?)
ProcessMessage InkHUD::NodeListApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Abort if applet fully deactivated
    // Already handled by wantPacket in this case, but good practice for all applets, as some *do* require this early return
    if (!isActive())
        return ProcessMessage::CONTINUE;

    // Assemble info: from this event
    CardInfo c;
    c.nodeNum = mp.from;
    c.signal = getSignalStrength(mp.rx_snr, mp.rx_rssi);

    // Assemble info: from nodeDB (needed to detect changes)
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(c.nodeNum);
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (node) {
        if (node->has_hops_away)
            c.hopsAway = node->hops_away;

        if (nodeDB->hasValidPosition(node) && nodeDB->hasValidPosition(ourNode)) {
            // Get lat and long as float
            // Meshtastic stores these as integers internally
            float ourLat = ourNode->position.latitude_i * 1e-7;
            float ourLong = ourNode->position.longitude_i * 1e-7;
            float theirLat = node->position.latitude_i * 1e-7;
            float theirLong = node->position.longitude_i * 1e-7;

            c.distanceMeters = (int32_t)GeoCoord::latLongToMeter(theirLat, theirLong, ourLat, ourLong);
        }
    }

    // Pass to the derived applet
    // Derived applet is responsible for requesting update, if justified
    // That request will eventually trigger our class' onRender method
    handleParsed(c);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

// Calculate maximum number of cards we may ever need to render, in our tallest layout config
// Number might be slightly in excess of the true value: applet header text not accounted for
uint8_t InkHUD::NodeListApplet::maxCards()
{
    // Cache result. Shouldn't change during execution
    static uint8_t cards = 0;

    if (!cards) {
        const uint16_t height = Tile::maxDisplayDimension();

        // Use a loop instead of arithmetic, because it's easier for my brain to follow
        // Add cards one by one, until the latest card extends below screen

        uint16_t y = cardH; // First card: no margin above
        cards = 1;

        while (y < height) {
            y += cardMarginH;
            y += cardH;
            cards++;
        }
    }

    return cards;
}

// Draw, using info which derived applet placed into NodeListApplet::cards for us
void InkHUD::NodeListApplet::onRender()
{

    // ================================
    // Draw the standard applet header
    // ================================

    drawHeader(getHeaderText()); // Ask derived applet for the title

    // Dimensions of the header
    int16_t headerDivY = getHeaderHeight() - 1;
    constexpr uint16_t padDivH = 2;

    // ========================
    // Draw the main node list
    // ========================

    // Imaginary vertical line dividing left-side and right-side info
    // Long-name will crop here
    const uint16_t dividerX = (width() - 1) - getTextWidth("X Hops");

    // Y value (top) of the current card. Increases as we draw.
    uint16_t cardTopY = headerDivY + padDivH;

    // -- Each node in list --
    for (auto card = cards.begin(); card != cards.end(); ++card) {

        // Gather info
        // ========================================
        NodeNum &nodeNum = card->nodeNum;
        SignalStrength &signal = card->signal;
        std::string longName;  // handled below
        std::string shortName; // handled below
        std::string distance;  // handled below;
        uint8_t &hopsAway = card->hopsAway;

        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeNum);

        // -- Shortname --
        // Parse special chars in the short name
        // Use "?" if unknown
        if (node)
            shortName = parseShortName(node);
        else
            shortName = "?";

        // -- Longname --
        // Parse special chars in long name
        // Use node id if unknown
        if (node && node->has_user)
            longName = parse(node->user.long_name); // Found in nodeDB
        else {
            // Not found in nodeDB, show a hex nodeid instead
            longName = hexifyNodeNum(nodeNum);
        }

        // -- Distance --
        if (card->distanceMeters != CardInfo::DISTANCE_UNKNOWN)
            distance = localizeDistance(card->distanceMeters);

        // Draw the info
        // ====================================

        // Define two lines of text for the card
        // We will center our text on these lines
        uint16_t lineAY = cardTopY + (fontMedium.lineHeight() / 2);
        uint16_t lineBY = cardTopY + fontMedium.lineHeight() + (fontSmall.lineHeight() / 2);

        // Print the short name
        setFont(fontMedium);
        printAt(0, lineAY, shortName, LEFT, MIDDLE);

        // Print the distance
        setFont(fontSmall);
        printAt(width() - 1, lineBY, distance, RIGHT, MIDDLE);

        // If we have a direct connection to the node, draw the signal indicator
        if (hopsAway == 0 && signal != SIGNAL_UNKNOWN) {
            uint16_t signalW = getTextWidth("Xkm"); // Indicator should be similar width to distance label
            uint16_t signalH = fontMedium.lineHeight() * 0.75;
            int16_t signalY = lineAY + (fontMedium.lineHeight() / 2) - (fontMedium.lineHeight() * 0.75);
            int16_t signalX = width() - signalW;
            drawSignalIndicator(signalX, signalY, signalW, signalH, signal);
        }
        // Otherwise, print "hops away" info, if available
        else if (hopsAway != CardInfo::HOPS_UNKNOWN) {
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
        hatchRegion(hatchLeft, cardTopY, hatchWidth, cardH, 2, WHITE);

        // Prepare to draw the next card
        resetCrop();
        cardTopY += cardH;

        // Once we've run out of screen, stop drawing cards
        // Depending on tiles / rotation, this may be before we hit maxCards
        if (cardTopY > height())
            break;
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
    constexpr float gutterW = 0.1;  // Between bars

    constexpr float barHRel[] = {0.3, 0.5, 0.7, 1.0}; // Heights of the signal bars, relative to the tallest
    constexpr uint8_t barCount = 4; // How many bars we draw. Reference only: changing value won't change the count.

    // Dynamically calculate the width of the bars, and height of the rightmost, relative to other dimensions
    float barW = (1.0 - (paddingW + ((barCount - 1) * gutterW) + paddingW)) / barCount;
    float barHMax = 1.0 - (paddingH + paddingH);

    // Draw signal bar rectangles, then placeholder lines once strength reached
    for (uint8_t i = 0; i < barCount; i++) {
        // Coords for this specific bar
        float barH = barHMax * barHRel[i];
        float barX = paddingW + (i * (gutterW + barW));
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

#endif