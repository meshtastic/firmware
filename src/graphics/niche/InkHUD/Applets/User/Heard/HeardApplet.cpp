#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "RTC.h"

#include "gps/GeoCoord.h"

#include "./HeardApplet.h"

using namespace NicheGraphics;

void InkHUD::HeardApplet::onActivate()
{
    // When applet begins, pre-fill with stale info from NodeDB
    populateFromNodeDB();
}

void InkHUD::HeardApplet::onDeactivate()
{
    // Avoid an unlikely situation where frequent activation / deactivation populates duplicate info from node DB
    cards.clear();
}

// When base applet hears a new packet, it extracts the info and passes it to us as CardInfo
// We need to store it (at front to sort recent), and request display update if our list has visibly changed as a result
void InkHUD::HeardApplet::handleParsed(CardInfo c)
{
    // Grab the previous entry.
    // To check if the new data is different enough to justify re-render
    // Need to cache now, before we manipulate the deque
    CardInfo previous;
    if (!cards.empty())
        previous = cards.at(0);

    // If we're updating an existing entry, remove the old one. Will reinsert at front
    for (auto it = cards.begin(); it != cards.end(); ++it) {
        if (it->nodeNum == c.nodeNum) {
            cards.erase(it);
            break;
        }
    }

    cards.push_front(c);                                  // Insert into base class' card collection
    cards.resize(min(maxCards(), (uint8_t)cards.size())); // Don't keep more cards than we could *ever* fit on screen
    cards.shrink_to_fit();

    // Our rendered image needs to change if:
    if (previous.nodeNum != c.nodeNum                  // Different node
        || previous.signal != c.signal                 // or different signal strength
        || previous.distanceMeters != c.distanceMeters // or different position
        || previous.hopsAway != c.hopsAway)            // or different hops away
    {
        requestAutoshow();
        requestUpdate();
    }
}

// When applet is activated, pre-fill with stale data from NodeDB
// We're sorting using the last_heard value. Susceptible to weirdness if node's RTC changes.
// No SNR is available in node db, so we can't calculate signal either
// These initial cards from node db will be gradually pushed out by new packets which originate from out base applet instead
void InkHUD::HeardApplet::populateFromNodeDB()
{
    // Fill a collection with pointers to each node in db
    std::vector<meshtastic_NodeInfoLite *> ordered;
    for (auto mn = nodeDB->meshNodes->begin(); mn != nodeDB->meshNodes->end(); ++mn) {
        // Only copy if valid, and not our own node
        if (mn->num != 0 && mn->num != nodeDB->getNodeNum())
            ordered.push_back(&*mn);
    }

    // Sort the collection by age
    std::sort(ordered.begin(), ordered.end(), [](meshtastic_NodeInfoLite *top, meshtastic_NodeInfoLite *bottom) -> bool {
        return (top->last_heard > bottom->last_heard);
    });

    // Keep the most recent entries only
    // Just enough to fill the screen
    if (ordered.size() > maxCards())
        ordered.resize(maxCards());

    // Create card info for these (stale) node observations
    meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    for (meshtastic_NodeInfoLite *node : ordered) {
        CardInfo c;
        c.nodeNum = node->num;

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

        // Insert into the card collection (member of base class)
        cards.push_back(c);
    }
}

// Text drawn in the usual applet header
// Handled by base class: ChronoListApplet
std::string InkHUD::HeardApplet::getHeaderText()
{
    uint16_t nodeCount = nodeDB->getNumMeshNodes() - 1; // Don't count our own node

    std::string text = "Heard: ";

    // Print node count, if nodeDB not yet nearing full
    if (nodeCount < MAX_NUM_NODES) {
        text += to_string(nodeCount); // Max nodes
        text += " ";
        text += (nodeCount == 1) ? "node" : "nodes";
    }

    return text;
}

#endif