#include "PositionPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"

PositionPlugin *positionPlugin;

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

    Position p = node->position;

    // Strip out any time information before sending packets to other nodes - to keep the wire size small (and because other
    // nodes shouldn't trust it anyways) Note: we allow a device with a local GPS to include the time, so that gpsless
    // devices can get time.
    if (getRTCQuality() < RTCQualityGPS) {
        DEBUG_MSG("Stripping time %u from position send\n", p.time);
        p.time = 0;
    } else
        DEBUG_MSG("Providing time to mesh %u\n", p.time);

    return allocDataProtobuf(p);
}

void PositionPlugin::sendOurPosition(NodeNum dest, bool wantReplies)
{
    // cancel any not yet sent (now stale) position packets
    if (prevPacketId) // if we wrap around to zero, we'll simply fail to cancel in that rare case (no big deal)
        service.cancelSending(prevPacketId);

    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;
    p->priority = MeshPacket_Priority_BACKGROUND;
    prevPacketId = p->id;

    service.sendToMesh(p);
}
