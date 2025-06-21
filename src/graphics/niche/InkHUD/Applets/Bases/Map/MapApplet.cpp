#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./MapApplet.h"

using namespace NicheGraphics;

void InkHUD::MapApplet::onRender()
{
    // Abort if no markers to render
    if (!enoughMarkers()) {
        printAt(X(0.5), Y(0.5) - (getFont().lineHeight() / 2), "Node positions", CENTER, MIDDLE);
        printAt(X(0.5), Y(0.5) + (getFont().lineHeight() / 2), "will appear here", CENTER, MIDDLE);
        return;
    }

    // Find center of map
    // - latitude and longitude
    // - will be placed at X(0.5), Y(0.5)
    getMapCenter(&latCenter, &lngCenter);

    // Calculate North+East distance of each node to map center
    // - which nodes to use controlled by virtual shouldDrawNode method
    calculateAllMarkers();

    // Set the region shown on the map
    // - default: fit all nodes, plus padding
    // - maybe overriden by derived applet
    // - getMapSize *sets* passed parameters (C-style)
    getMapSize(&widthMeters, &heightMeters);

    // Set the metersToPx conversion value
    calculateMapScale();

    // Special marker for own node
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (ourNode && nodeDB->hasValidPosition(ourNode))
        drawLabeledMarker(ourNode);

    // Draw all markers
    for (Marker m : markers) {
        int16_t x = X(0.5) + (m.eastMeters * metersToPx);
        int16_t y = Y(0.5) - (m.northMeters * metersToPx);

        // Cross Size
        constexpr uint16_t csMin = 5;
        constexpr uint16_t csMax = 12;

        // Too many hops away
        if (m.hasHopsAway && m.hopsAway > config.lora.hop_limit) // Too many mops
            printAt(x, y, "!", CENTER, MIDDLE);
        else if (!m.hasHopsAway) // Unknown hops
            drawCross(x, y, csMin);
        else // The fewer hops, the larger the cross
            drawCross(x, y, map(m.hopsAway, 0, config.lora.hop_limit, csMax, csMin));
    }
}

// Find the center point, in the middle of all node positions
// Calculated values are written to the *lat and *long pointer args
// - Finds the "mean lat long"
// - Calculates furthest nodes from "mean lat long"
// - Place map center directly between these furthest nodes

void InkHUD::MapApplet::getMapCenter(float *lat, float *lng)
{
    // Find mean lat long coords
    // ============================
    // - assigning X, Y and Z values to position on Earth's surface in 3D space, relative to center of planet
    // - averages the x, y and z coords
    // - uses tan to find angles for lat / long degrees
    //   - longitude: triangle formed by x and y (on plane of the equator)
    //   - latitude: triangle formed by z (north south),
    //     and the line along plane of equator which stretches from earth's axis to where point xyz intersects planet's surface

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

        // Latitude and Longitude of node, in radians
        float latRad = node->position.latitude_i * (1e-7) * DEG_TO_RAD;
        float lngRad = node->position.longitude_i * (1e-7) * DEG_TO_RAD;

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

    // ----------------------------------------------
    // This has given us the "mean position"
    // This will be a position *somewhere* near the center of our nodes.
    // What we actually want is to place our center so that our outermost nodes end up on the border of our map.
    // The only real use of our "mean position" is to give us a reference frame:
    // which direction is east, and which is west.
    //------------------------------------------------

    // Find furthest nodes from "mean lat long"
    // ========================================

    float northernmost = latCenter;
    float southernmost = latCenter;
    float easternmost = lngCenter;
    float westernmost = lngCenter;

    for (uint8_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);

        // Skip if no position
        if (!nodeDB->hasValidPosition(node))
            continue;

        // Skip if derived applet doesn't want to show this node on the map
        if (!shouldDrawNode(node))
            continue;

        // Check for a new top or bottom latitude
        float lat = node->position.latitude_i * 1e-7;
        northernmost = max(northernmost, lat);
        southernmost = min(southernmost, lat);

        // Longitude is trickier
        float lng = node->position.longitude_i * 1e-7;
        float degEastward = fmod(((lng - lngCenter) + 360), 360);      // Degrees traveled east from lngCenter to reach node
        float degWestward = abs(fmod(((lng - lngCenter) - 360), 360)); // Degrees traveled west from lngCenter to reach node
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
InkHUD::MapApplet::Marker InkHUD::MapApplet::calculateMarker(float lat, float lng, bool hasHopsAway, uint8_t hopsAway)
{
    assert(lat != 0 || lng != 0); // Not null island. Applets should check this before calling.

    // Bearing and distance from map center to node
    float distanceFromCenter = GeoCoord::latLongToMeter(latCenter, lngCenter, lat, lng);
    float bearingFromCenter = GeoCoord::bearing(latCenter, lngCenter, lat, lng); // in radians

    // Split into meters north and meters east components (signed)
    // - signedness of cos / sin automatically sets negative if south or west
    float northMeters = cos(bearingFromCenter) * distanceFromCenter;
    float eastMeters = sin(bearingFromCenter) * distanceFromCenter;

    // Store this as a new marker
    Marker m;
    m.eastMeters = eastMeters;
    m.northMeters = northMeters;
    m.hasHopsAway = hasHopsAway;
    m.hopsAway = hopsAway;
    return m;
}

// Draw a marker on the map for a node, with a shortname label, and backing box
void InkHUD::MapApplet::drawLabeledMarker(meshtastic_NodeInfoLite *node)
{
    // Find x and y position based on node's position in nodeDB
    assert(nodeDB->hasValidPosition(node));
    Marker m = calculateMarker(node->position.latitude_i * 1e-7,  // Lat, converted from Meshtastic's internal int32 style
                               node->position.longitude_i * 1e-7, // Long, converted from Meshtastic's internal int32 style
                               node->has_hops_away,               // Is the hopsAway number valid
                               node->hops_away                    // Hops away
    );

    // Convert to pixel coords
    int16_t markerX = X(0.5) + (m.eastMeters * metersToPx);
    int16_t markerY = Y(0.5) - (m.northMeters * metersToPx);

    constexpr uint16_t paddingH = 2;
    constexpr uint16_t paddingW = 4;
    uint16_t paddingInnerW = 2;            // Zero'd out if no text
    constexpr uint16_t markerSizeMax = 12; // Size of cross (if marker uses a cross)
    constexpr uint16_t markerSizeMin = 5;

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
    bool isOurNode = node->num == nodeDB->getNodeNum();
    bool unknownHops = !node->has_hops_away && !isOurNode;

    // Parse any non-ascii chars in the short name,
    // and use last 4 instead if unknown / can't render
    std::string shortName = parseShortName(node);

    // We will draw a left or right hand variant, to place text towards screen center
    // Hopefully avoid text spilling off screen
    // Most values are the same, regardless of left-right handedness

    // Pick emblem style
    if (tooManyHops)
        markerSize = getTextWidth("!");
    else if (unknownHops)
        markerSize = markerSizeMin;
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
        drawCross(markerX, markerY, markerSize); // The fewer the hops, the larger the marker. Also handles unknownHops
}

// Check if we actually have enough nodes which would be shown on the map
// Need at least two, to draw a sensible map
bool InkHUD::MapApplet::enoughMarkers()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);

        // Count nodes
        if (nodeDB->hasValidPosition(node) && shouldDrawNode(node))
            count++;

        // We need to find two
        if (count == 2)
            return true; // Two nodes is enough for a sensible map
    }

    return false; // No nodes would be drawn (or just the one, uselessly at 0,0)
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

        // Calculate marker and store it
        markers.push_back(
            calculateMarker(node->position.latitude_i * 1e-7,  // Lat, converted from Meshtastic's internal int32 style
                            node->position.longitude_i * 1e-7, // Long, converted from Meshtastic's internal int32 style
                            node->has_hops_away,               // Is the hopsAway number valid
                            node->hops_away                    // Hops away
                            ));
    }
}

// Determine the conversion factor between metres, and pixels on screen
// May be overriden by derived applet, if custom scale required (fixed map size?)
void InkHUD::MapApplet::calculateMapScale()
{
    // Aspect ratio of map and screen
    // - larger = wide, smaller = tall
    // - used to set scale, so that widest map dimension fits in applet
    float mapAspectRatio = (float)widthMeters / heightMeters;
    float appletAspectRatio = (float)width() / height();

    // "Shrink to fit"
    // Scale the map so that the largest dimension is fully displayed
    // Because aspect ratio will be maintained, the other dimension will appear "padded"
    if (mapAspectRatio > appletAspectRatio)
        metersToPx = (float)width() / widthMeters; // Too wide for applet. Constrain to fit width.
    else
        metersToPx = (float)height() / heightMeters; // Too tall for applet. Constrain to fit height.
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