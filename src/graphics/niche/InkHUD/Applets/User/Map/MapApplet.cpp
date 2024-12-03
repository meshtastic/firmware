#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./MapApplet.h"

#include "GeoCoord.h"
#include "NodeDB.h"

using namespace NicheGraphics;

InkHUD::MapApplet::MapApplet() : MeshModule("Map Applet"), concurrency::OSThread("Map Applet")
{
    // No timer activity at boot
    OSThread::disable();
}

// Part of MeshModule class. Which packets to we want to handle?
bool InkHUD::MapApplet::wantPacket(const meshtastic_MeshPacket *p)
{
    // Handle position packets, no matter who they come from
    if (p->decoded.portnum == meshtastic_PortNum_POSITION_APP)
        return true;

    // Handle other packets too, if they're not from us
    // We want this info to update hops away
    if (getFrom(p) != myNodeInfo.my_node_num)
        return true;

    return false;
}

// Part of MeshModule class. Packets we selected with wantPacket() arrive here.
ProcessMessage InkHUD::MapApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    // =============
    // Extract info
    // =============

    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(mp.from);
    uint8_t hopsAway = node ? node->hops_away : 0;

    // Try decode a position
    bool hasPosition = false;
    uint32_t newLat = 0;
    uint32_t newLong = 0;
    uint32_t travelMeters = 0;
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.decoded.portnum == meshtastic_PortNum_POSITION_APP) {
        meshtastic_Position position = meshtastic_Position_init_default;
        if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Position_msg, &position)) {
            if (position.has_latitude_i && position.has_longitude_i) {
                hasPosition = true;
                newLat = position.latitude_i;
                newLong = position.longitude_i;
            }

            // Distance from previous position
            // - if same node, or
            // - if our node
            if (isFromUs(&mp))
                travelMeters = GeoCoord::latLongToMeter(newLat * 1e-7, newLong * 1e-7, ourLastLat * 1e-7, ourLastLong * 1e-7);
            else if (mp.from == lastHeardNodeNum)
                travelMeters = GeoCoord::latLongToMeter(newLat * 1e-7, newLong * 1e-7, lastHeardLat * 1e-7, lastHeardLong * 1e-7);
        }
    }

    // =======================================
    // Decide whether to calculate and render
    // =======================================

    bool shouldRender = false;

    // If our own position
    if (hasPosition && isFromUs(&mp)) {
        // Render if changed
        if (travelMeters > 50)
            shouldRender = true;
    }

    // If other node's position
    // - same node as last render
    else if (hasPosition && !isFromUs(&mp) && mp.from == lastHeardNodeNum) {
        // Render if changed
        if (travelMeters > 50)
            shouldRender = true;
    }

    // If other node's position
    // - different node than last render
    else if (hasPosition && !isFromUs(&mp) && mp.from != lastHeardNodeNum) {
        shouldRender = true;
    }

    // =================================
    // Store info, to compare next time
    // =================================

    if (hasPosition) {
        if (isFromUs(&mp)) {
            ourLastLat = newLat;
            ourLastLong = newLong;
        } else {
            lastHeardNodeNum = mp.from;
            lastHeardLat = newLat;
            lastHeardLong = newLong;
            lastHeardHopsAway = hopsAway;
        }
    }

    // We *are* listening for lastHeardNodeNum in the background
    // But we will only render if:
    // - in foreground
    // - position info has probably changed

    if (isForeground() && shouldRender) {
        // Begin the process of updating the map
        // - precalculate as much as possible with our OSThread, yielding occasionally
        // - render the new image
        beforeRender();
    }

    return ProcessMessage::CONTINUE;
}

void InkHUD::MapApplet::render()
{
    switch (calcState) {

    // If calculation has not yet been given an opportunity to run
    // - at boot
    // - when enabling applet via menu
    // - ?
    case CALC_NOT_STARTED:
        printAt(X(0.5), Y(0.5) - (getFont().lineHeight() / 2), "Map not yet", CENTER, MIDDLE);
        printAt(X(0.5), Y(0.5) + (getFont().lineHeight() / 2), "calculated", CENTER, MIDDLE);
        beforeRender();
        break;

    // Our own node has no position
    case CALC_FAILED_NO_POSITION:
        printAt(X(0.5), Y(0.5), "Position unavailable", CENTER, MIDDLE);
        break;

    // NodeDB has no nodes with positions
    case CALC_FAILED_NO_NODES:
        printAt(X(0.5), Y(0.5) - (getFont().lineHeight() / 2), "Node positions", CENTER, MIDDLE);
        printAt(X(0.5), Y(0.5) + (getFont().lineHeight() / 2), "will appear here", CENTER, MIDDLE);
        break;

    // Valid pre-calculated info is available, render the markers now
    case CALC_SUCCEDED:

        constexpr uint8_t padding = 10;

        // Determine how much we need to squash the map to maintain its aspect ratio
        // --------------------------------------------------------------------------
        // Marker x and positions are stored as floats between 0 and 1
        // This section handles scaling them to draw appropriately in whatever region we've been allocated

        float aspectRatioMap = rangeEastWestMeters / rangeNorthSouthMeters; // What shape is our map
        float aspectRatioTile = (float)width() / (float)height();           // What shape is our tile

        float scaleX = 1;
        float scaleY = 1;

        // If our map is too wide for the tile
        if (aspectRatioTile < aspectRatioMap)
            // Will use full tile width, and squash height to give a square aspect ratio
            scaleY = aspectRatioTile / aspectRatioMap;

        // If our map is too tall for the tile
        else if (aspectRatioTile > aspectRatioMap)
            // Will use full tile height, and squash width to give a square aspect ratio
            scaleX = aspectRatioMap / aspectRatioTile;

        // Draw the markers
        // ------------------

        // Draw a label for our own node
        renderMarker(ourMarker, scaleX, scaleY, padding, nodeDB->getMeshNode(nodeDB->getNodeNum()));

        // Draw all the normal markers
        for (MapMarker marker : markers)
            renderMarker(marker, scaleX, scaleY, padding);

        // Draw a special marker for the most recently heard node
        // This node might not yet have a nodeDB entry
        meshtastic_NodeInfoLite *lastHeardNode = nodeDB->getMeshNode(lastHeardNodeNum);
        if (lastHeardNode && nodeDB->hasValidPosition(lastHeardNode))
            renderMarker(lastHeardMarker, scaleX, scaleY, padding, lastHeardNode);

        // Tidy up
        freeCalculationResources();

        break;
    }
}

// Start up our OSThread to process position data from NodeDB
// This step could potentially take a second or two, so we'll do it gradually,
// giving other threads a chance to run in-between
void InkHUD::MapApplet::beforeRender()
{
    // Mark that the window manager should wait for us
    // Most applets should be ready to render immediately, and not need to set this flag..
    // Remember to clear before requestUpdate()
    // (Non-static member of base clase)
    Applet::preparedToRender = false;

    calcStep = STEP_INIT;

    OSThread::setInterval(0);
    OSThread::enabled = true;
    runASAP = true;
}

// Part of OSThread class. This is our timer method.
// We're using it to time-share the task of scanning NodeDB for position data,
// giving other Meshtastic threads a chance to run
int32_t InkHUD::MapApplet::runOnce()
{
    // We'll track how long this one run takes, and use it to determine how long we should yield for
    uint32_t startExecutionMs = millis();

    // Perform another piece of our calculation
    bool inProgress = serviceCalculationThread();

    // If our calculation job is not yet complete
    if (inProgress) {
        // Stand-down briefly, to yield to other threads.
        // Won't exceed 50% of processor time.
        uint32_t executionTime = millis() - startExecutionMs;
        runASAP = true;
        return executionTime;
    }

    // If our calculation job is now done
    else {
        return OSThread::disable(); // Stop this thread running
    }
}

// Perform one step of the calculations which process position info from NodeDB,
// generating x and y values which are mostly ready from drawing.
// Called repeatedly from runOnce()
bool InkHUD::MapApplet::serviceCalculationThread()
{
    static uint32_t nodeIndex;
    static float ourLatitude;
    static float ourLongitude;

    static float maxNorth; // Northern extent, from our node, in meters
    static float minNorth; // Southern extent, from our node, in meters
    static float maxEast;  // Eastern extent, from our node, in meters
    static float minEast;  // Western extent, from our node, in meters

    switch (calcStep) {

    // ##############################
    // ###  Prepare to calculate  ###
    // ##############################
    case STEP_INIT: {

        // Re-init working variables
        nodeIndex = 0;
        maxNorth = 0;
        minNorth = 0;
        maxEast = 0;
        minEast = 0;
        markers.clear();

        // Next step
        [[fallthrough]];
    }

    // ############################################################
    // ###  Check whether nodeDB has any nodes with a position  ###
    // ############################################################
    case STEP_CHECK_FOR_NODES: {
        // Scan NodeDB to confirm that we have at least one node with a position
        bool anyNodes = false;
        uint32_t nodeCount = nodeDB->getNumMeshNodes();
        for (uint32_t i = 0; i < nodeCount; i++) {
            meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
            if (node && nodeDB->hasValidPosition(node)) {
                anyNodes = true;
                break;
            }
        }

        // If we didn't find any nodes, register the error and abort
        if (!anyNodes) {
            calcState = CALC_FAILED_NO_NODES;
            calcStep = STEP_RENDER;
            return true; // run again, moving directly to the final step
        }

        // Otherwise, carry on to next step
        [[fallthrough]];
    }

    // #######################################
    // ###  Get our own node's lat / long  ###
    // #######################################
    case STEP_OUR_POSITION: {
        calcStep = STEP_OUR_POSITION;

        // Get our own node info
        meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(myNodeInfo.my_node_num);

        // Fail if we don't have a position
        // Todo: handle in render()
        if (!ourNode || !nodeDB->hasValidPosition(ourNode)) {
            calcState = CALC_FAILED_NO_POSITION;
            calcStep = STEP_RENDER;
            return true; // run again, moving directly to the final step
        }

        // Get lat and long as float
        // Meshtastic stores these as integers internally
        ourLatitude = ourNode->position.latitude_i * 1e-7;
        ourLongitude = ourNode->position.longitude_i * 1e-7;

        // Next step
        [[fallthrough]];
    }

    // #################################
    // ###  Find most distant nodes  ###
    // #################################
    case STEP_FIND_EXTENTS: {
        calcStep = STEP_FIND_EXTENTS;

        // This step runs repeatedly
        // We're iterating NodeDB here, using runOnce to avoid blocking

        // Grab next node in sequence
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(nodeIndex);

        // Analyze only if has a valid position (not 0,0 "null island")
        if (node && nodeDB->hasValidPosition(node)) {

            // Convert from Meshtastic's internal "integer" format for position info
            float latitude = node->position.latitude_i * 1e-7;
            float longitude = node->position.longitude_i * 1e-7;

            float metersAway = GeoCoord::latLongToMeter(ourLatitude, ourLongitude, latitude, longitude);
            float bearing = GeoCoord::bearing(ourLatitude, ourLongitude, latitude, longitude);

            // Split distance away from us into north and east components
            // South or west are negative
            float metersNorth = cos(bearing) * metersAway;
            float metersEast = sin(bearing) * metersAway;

            // Update the position of our map edge, if it has grown
            maxNorth = max(maxNorth, metersNorth);
            minNorth = min(minNorth, metersNorth);
            maxEast = max(maxEast, metersEast);
            minEast = min(minEast, metersEast);
        }

        // If we've got more nodes to analyze,
        // iterate and repeat this calculation step
        if (nodeIndex < nodeDB->getNumMeshNodes() - 1) {
            nodeIndex++;
            return true;
            // --- loop: repeat this step when runOnce() fires ---
        }

        // Once we've scanned the whole NodeDB
        nodeIndex = 0;   // Reset iterator
        [[fallthrough]]; // Next step
    }

    // ########################################################
    // ###  Set the map dimensions (meters)                 ###
    // ###  Place our own node relative to these dimensions ###
    // #######################################################
    case STEP_RANGE:
        calcStep = STEP_RANGE;

        // - We know the extreme north, south, east and west points of the map
        // Determine how wide / tall the map must be to display these
        rangeNorthSouthMeters = maxNorth - minNorth;
        rangeEastWestMeters = maxEast - minEast;

        // - We know the extreme north, south, east and west points of the map
        // - We know how wide / tall the map must be to display these
        // Assign our own node a position relative to this info (between 0 and 1)
        ourMarker.x = remapFloat(abs(minEast), 0, rangeEastWestMeters, 0, 1);
        ourMarker.y = remapFloat(maxNorth, 0, rangeNorthSouthMeters, 0, 1);

        // Next step
        [[fallthrough]];

    // #####################################################
    // ###  Calculate positions relative to map range    ###
    // ###  Calculate marker sizes relative to hops away ###
    // #####################################################
    case STEP_MARKERS: {
        calcStep = STEP_MARKERS;

        // This step runs repeatedly

        // We're iterating through NodeDB *again*, now that we know the map edges and size

        // Node position info is given to us as "meters away from our node, and a bearing"
        // We want to convert this to x and y values between 0 and 1, relative to our map size and edges
        // x of 0.25 means node's distance from map's western edge is 25% of the map's total east-west size

        // Marker size relates to the hop count. Fewer hops: bigger marker.
        // We need to calculate marker size now, because our Marker struct doesn't record which node
        // a marker belongs to, so we can't access this info later.

        // Get next node from DB (iterating)
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(nodeIndex);

        // Process only if node has valid position
        if (node && nodeDB->hasValidPosition(node)) {
            // Convert from Meshtastic's internal "integer" format for position info
            float latitude = node->position.latitude_i * 1e-7;
            float longitude = node->position.longitude_i * 1e-7;

            // Get distance, (relative to our own node)
            float metersAway = GeoCoord::latLongToMeter(ourLatitude, ourLongitude, latitude, longitude);
            float bearing = GeoCoord::bearing(ourLatitude, ourLongitude, latitude, longitude);

            // Split distance from us into north and east components
            // South or west are negative
            float metersNorth = cos(bearing) * metersAway;
            float metersEast = sin(bearing) * metersAway;

            // De-reference the position info
            // Make relative to map side and edges, instead of our own node
            // Value between 0 and 1
            MapMarker m;
            m.x = remapFloat(metersEast, minEast, maxEast, 0, 1);
            m.y = remapFloat(metersNorth, minNorth, maxNorth, 1, 0); // Note: inverted. Y increases as we move south.

            // How big the marker should be (0 to 1) based on hop count
            // Fewers hops means a bigger marker
            m.size = remapFloat(node->hops_away, config.lora.hop_limit, 0, 0, 1);

            // Store our marker, for use by render()
            markers.push_back(m);

            // If the node we're currently processing was the node we heard from most recently,
            // store this marker specially.
            // We can highlight it when render()ing
            if (node->num == lastHeardNodeNum)
                lastHeardMarker = m;
        }

        // If we've got more nodes to analyze,
        // iterate through nodeDB and repeat this calculation step
        if (nodeIndex < nodeDB->getNumMeshNodes() - 1) {
            nodeIndex++;
            return true;
            // --- loop: repeat this step when runOnce() fires ---
        }

        // Once we've reached the end of NodeDB
        calcState = CALC_SUCCEDED;
        [[fallthrough]]; // Next step
    }

    // ##########################################
    // ###  Ask the window manager to render  ###
    // ##########################################
    case STEP_RENDER:
        calcStep = STEP_RENDER;
        assert(calcState != CALC_NOT_STARTED);
        Applet::preparedToRender = true; // Mark that we're ready to render
        requestUpdate();
        return false; // Indicate that thread's job is done
    };

    // We shouldn't be able to reach here
    // All paths through the switch should return
    // Supressing a compiler warning only
    assert(false);
    return false;
}

// Free up memory occupied by the vector we filled when calculating
// Mark that our calculation has been consumed, and we need to rerun in future
void InkHUD::MapApplet::freeCalculationResources()
{
    markers.clear();
    markers.shrink_to_fit();
    calcState = CALC_NOT_STARTED;
    Applet::preparedToRender = false;
}

// Rasterize our "relative" marker position and size to pixels values,
// then hand-off to one of the methods which draws the marker
void InkHUD::MapApplet::renderMarker(InkHUD::MapApplet::MapMarker marker, float scaleX, float scaleY, uint16_t padding,
                                     meshtastic_NodeInfoLite *node)
{
    InkHUD::MapApplet::MapMarker m;

    // Apply the scale correction: achieve square aspect ratio in non-square tiles
    m.x = marker.x * scaleX;
    m.x = m.x + ((1 - scaleX) / 2); // Pad to center
    m.y = marker.y * scaleY;
    m.y = m.y + ((1 - scaleY) / 2); // Pad to center
    m.size = marker.size;

    // Convert from relative values to pixel values
    int16_t rasterX = X(m.x);
    int16_t rasterY = Y(m.y);

    // Add a uniform width of padding pixels around map edge
    rasterX = map(rasterX, 0, width(), padding, width() - padding);
    rasterY = map(rasterY, 0, height(), padding, height() - padding);

    // MapMarker::size is set by serviceCalculationThread()
    // Size is determined by how many hops away a node is
    // Size < 0 means the node is further away than our hop limit, and is not reachable
    constexpr uint16_t markerMin = 5;
    constexpr uint16_t markerMax = 12;
    int16_t markerSizePx;
    if (node && node == nodeDB->getMeshNode(nodeDB->getNodeNum())) // Hops away gets encoded weirdly for our own node?
        markerSizePx = markerMin;
    else
        markerSizePx = remapFloat(marker.size, 0, 1, markerMin, markerMax);

    // Normal node, not highighted
    if (!node)
        drawUnlabeledMarker(rasterX, rasterY, markerSizePx);

    // Highlighted node: show short name
    else if (node->has_user)
        drawLabeledMarker(rasterX, rasterY, node, markerSizePx);

    // Hightlighted node: short name unavailable, just highlight with a box
    else
        drawUnlabeledMarker(rasterX, rasterY, markerSizePx, true);
}

// Draw a marker on the map for a node, without a shortname label
// The marker *may* be highlighted with a surrounding box
void InkHUD::MapApplet::drawUnlabeledMarker(int16_t x, int16_t y, int16_t markerSize, bool highlighted)
{
    constexpr uint16_t padding = 2;

    // Draw a box behind a marker
    // Indicates that this was the most recently heard node, if no node info available
    if (highlighted) {
        int16_t boxX = x - padding - (markerSize / 2); // x and y are centered on the cross
        int16_t boxY = y - padding - (markerSize / 2);
        int16_t boxW = padding + markerSize + padding;
        int16_t boxH = padding + markerSize + padding;
        fillRect(boxX, boxY, boxW, boxH, WHITE);
        drawRect(boxX, boxY, boxW, boxH, BLACK);
    }

    // Marker width is expected to be > 0 for nodes within our hop limit
    if (markerSize > 0)
        drawCross(x, y, markerSize, markerSize, BLACK);

    // A marker width <= 0 indicates that the node is further away than our hop limit
    // Draw it with an exclamation point, to indicate the potential issue
    else
        printAt(x, y, "!", CENTER, MIDDLE);
}

// Draw a marker on the map for a node, with a shortname label, and backing box
void InkHUD::MapApplet::drawLabeledMarker(int16_t markerX, int16_t markerY, meshtastic_NodeInfoLite *node, int16_t markerSize)
{
    setFont(fontSmall);

    constexpr uint16_t paddingH = 2;
    constexpr uint16_t paddingW = 4;
    constexpr uint16_t paddingInnerW = 2;

    // Draw a special marker for the most recently heard node
    // We did already draw a marker for this node from MapApplet::markers, but we'll just draw overtop it

    int16_t textX;
    int16_t textY;
    uint16_t textW;
    uint16_t textH;
    int16_t labelX;
    int16_t labelY;
    uint16_t labelW;
    uint16_t labelH;

    const char *text = node->user.short_name;

    // Marker size comes through weird if we're drawing our own node
    // Probably the hops away value is strange?
    bool isOurNode = (node == nodeDB->getMeshNode(nodeDB->getNodeNum()));

    // markerSize of -1 indicates a node which is more further away than our max hops
    // We'll draw this with an exclamation point instead of a cross
    // Checking this now, so that we can change markerSize to pad for the exclamation point
    bool reachable = markerSize > 0;
    if (!reachable) {
        markerSize = getTextWidth("!");
    }

    // We will draw a left or right hand variant, to place text towards screen center
    // Hopfully avoid text spilling off screen

    // Most values are the same, regardless of left-right handedness

    textW = getTextWidth(text);
    textH = fontSmall.lineHeight();

    labelH = paddingH + max((int16_t)(textH), (int16_t)markerSize) + paddingH;
    labelW = paddingW + markerSize + paddingInnerW + textW + paddingW; // Order is reversed for right-hand, but width same

    labelY = markerY - (labelH / 2);
    textY = markerY;

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

    // Draw the marker
    // - cross if within hop limit
    // - exclamation point if beyond hop limit
    // - circle if our own node
    if (isOurNode)
        fillCircle(markerX, markerY, markerSize / 2, BLACK);
    else if (reachable)
        drawCross(markerX, markerY, markerSize, markerSize, BLACK);
    else
        printAt(markerX, markerY, "!", CENTER, MIDDLE);

    // Short name
    printAt(textX, textY, text, LEFT, MIDDLE);

    // If the label is for our own node,
    // fade it by overdrawing partially with white
    if (node == nodeDB->getMeshNode(nodeDB->getNodeNum()))
        hatchRegion(labelX, labelY, labelW, labelH, 2, WHITE);
}

// Map a float value from one range to another
// Float implementation of Arduino's map() function
float InkHUD::MapApplet::remapFloat(float val, float low, float high, float newLow, float newHigh)
{
    val -= low;
    val /= high - low;
    val *= newHigh - newLow;
    val += newLow;
    return val;
}

// Draw an x, centered on a specific point
void InkHUD::MapApplet::drawCross(int16_t x, int16_t y, uint16_t width, uint16_t height, Color color)
{
    int16_t x0 = x - (width / 2);
    int16_t y0 = y - (height / 2);
    int16_t x1 = x0 + width - 1;
    int16_t y1 = y0 + height - 1;
    drawLine(x0, y0, x1, y1, color);
    drawLine(x0, y1, x1, y0, color);

    // // Double thickness
    // y0--;
    // y1--;
    // drawLine(x0, y0, x1, y1, color);
    // drawLine(x0, y1, x1, y0, color);
}

#endif