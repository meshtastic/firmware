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

MeshPacket *PositionPlugin::allocReply()
{
    NodeInfo *node = service.refreshMyNodeInfo(); // should guarantee there is now a position
    assert(node->has_position);
    
    return allocDataProtobuf(node->position);
}

void PositionPlugin::sendOurPosition(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    service.sendToMesh(p);
}

