#include "PositionPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"

PositionPlugin positionPlugin;

bool PositionPlugin::handleReceivedProtobuf(const MeshPacket &mp, const Position &p)
{
    // FIXME - we currently update position data in the DB only if the message was a broadcast or destined to us
    // it would be better to update even if the message was destined to others.

    DEBUG_MSG("handled incoming position time=%u\n", p.time);
    if (p.time) {
        struct timeval tv;
        uint32_t secs = p.time;

        tv.tv_sec = secs;
        tv.tv_usec = 0;

        perhapsSetRTC(RTCQualityFromNet, &tv);
    }

    nodeDB.updatePosition(mp.from, p);

    return false; // Let others look at this message also if they want
}

void PositionPlugin::sendOurPosition(NodeNum dest, bool wantReplies)
{
    NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());
    assert(node);
    assert(node->has_position);

    // Update our local node info with our position (even if we don't decide to update anyone else)
    auto position = node->position;
    position.time = getValidTime(RTCQualityGPS); // This nodedb timestamp might be stale, so update it if our clock is valid.

    MeshPacket *p = allocForSending(position);
    p->to = dest;
    p->decoded.want_response = wantReplies;

    service.sendToMesh(p);
}

/** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
 * so that subclasses can (optionally) send a response back to the original sender.  Implementing this method
 * is optional
 */
void PositionPlugin::sendResponse(NodeNum to) {
    DEBUG_MSG("Sending posistion reply\n");
    sendOurPosition(to, false);
}