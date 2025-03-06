#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./RecentsListApplet.h"

#include "RTC.h"

using namespace NicheGraphics;

InkHUD::RecentsListApplet::RecentsListApplet() : NodeListApplet("RecentsListApplet"), concurrency::OSThread("RecentsListApplet")
{
    // No scheduled tasks initially
    OSThread::disable();
}

void InkHUD::RecentsListApplet::onActivate()
{
    // When the applet is activated, begin scheduled purging of any nodes which are no longer "active"
    OSThread::enabled = true;
    OSThread::setIntervalFromNow(60 * 1000UL); // Every minute
}

void InkHUD::RecentsListApplet::onDeactivate()
{
    // Halt scheduled purging
    OSThread::disable();
}

int32_t InkHUD::RecentsListApplet::runOnce()
{
    prune(); // Remove CardInfo and Age record for nodes which we haven't heard recently
    return OSThread::interval;
}

// When base applet hears a new packet, it extracts the info and passes it to us as CardInfo
// We need to store it (at front to sort recent), and request display update if our list has visibly changed as a result
// We also need to record the current time against the nodenum, so we know when it becomes inactive
void InkHUD::RecentsListApplet::handleParsed(CardInfo c)
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

    cards.push_front(c);                                  // Store this CardInfo
    cards.resize(min(maxCards(), (uint8_t)cards.size())); // Don't keep more cards than we could *ever* fit on screen

    // Record the time of this observation
    // Used to count active nodes, and to know when to prune inactive nodes
    seenNow(c.nodeNum);

    // Our rendered image needs to change if:
    if (previous.nodeNum != c.nodeNum                  // Different node
        || previous.signal != c.signal                 // or different signal strength
        || previous.distanceMeters != c.distanceMeters // or different position
        || previous.hopsAway != c.hopsAway)            // or different hops away
    {
        prune(); // Take the opportunity now to remove inactive nodes
        requestAutoshow();
        requestUpdate();
    }
}

// Record the time (millis, right now) that we hear a node
// If we do not hear from a node for a while, its card and age info will be removed by the purge method, which runs regularly
void InkHUD::RecentsListApplet::seenNow(NodeNum nodeNum)
{
    // If we're updating an existing entry, remove the old one. Will reinsert at front
    for (auto it = ages.begin(); it != ages.end(); ++it) {
        if (it->nodeNum == nodeNum) {
            ages.erase(it);
            break;
        }
    }

    Age a;
    a.nodeNum = nodeNum;
    a.seenAtMs = millis();

    ages.push_front(a);
}

// Remove Card and Age info for any nodes which are now inactive
// Determined by when a node was last heard, in our internal record (not from nodeDB)
void InkHUD::RecentsListApplet::prune()
{
    // Iterate age records from newest to oldest
    for (uint16_t i = 0; i < ages.size(); i++) {
        // Found the first record which is too old
        if (!isActive(ages.at(i).seenAtMs)) {
            // Drop this item, and all others behind it
            ages.resize(i);
            cards.resize(i);

            // Request an update, if pruning did modify our data
            // Required if pruning was scheduled. Redundent if pruning was prior to rendering.
            requestAutoshow();
            requestUpdate();

            break;
        }
    }

    // Push next scheduled pruning back
    // Pruning may be called from by handleParsed, immediately prior to rendering
    // In that case, we can slightly delay our scheduled pruning
    OSThread::setIntervalFromNow(60 * 1000UL);
}

// Is a timestamp old enough that it would make a node inactive, and in need of purging?
bool InkHUD::RecentsListApplet::isActive(uint32_t seenAtMs)
{
    uint32_t now = millis();
    uint32_t secsAgo = (now - seenAtMs) / 1000UL; // millis() overflow safe

    return (secsAgo < settings->recentlyActiveSeconds);
}

// Text to be shown at top of applet
// ChronoListApplet base class allows us to set this dynamically
// Might want to adjust depending on node count, RTC status, etc
std::string InkHUD::RecentsListApplet::getHeaderText()
{
    std::string text;

    // Print the length of our "Recents" time-window
    text += "Last ";
    text += to_string(settings->recentlyActiveSeconds / 60);
    text += " mins";

    // Print the node count
    const uint16_t nodeCount = ages.size();
    text += ": ";
    text += to_string(nodeCount);
    text += " ";
    text += (nodeCount == 1) ? "node" : "nodes";

    return text;
}

#endif