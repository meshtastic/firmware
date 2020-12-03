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
