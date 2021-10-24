#include "configuration.h"
#include "PositionPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"

PositionPlugin *positionPlugin;

PositionPlugin::PositionPlugin()
    : ProtobufPlugin("position", PortNum_POSITION_APP, Position_fields), concurrency::OSThread("PositionPlugin")
{
    isPromiscuous = true;          // We always want to update our nodedb, even if we are sniffing on others
    setIntervalFromNow(60 * 1000); // Send our initial position 60 seconds after we start (to give GPS time to setup)
}

bool PositionPlugin::handleReceivedProtobuf(const MeshPacket &mp, Position *pptr)
{
    auto p = *pptr;

    if (p.time) {
        struct timeval tv;
        uint32_t secs = p.time;

        tv.tv_sec = secs;
        tv.tv_usec = 0;

        perhapsSetRTC(RTCQualityFromNet, &tv);
    }

    nodeDB.updatePosition(getFrom(&mp), p);

    return false; // Let others look at this message also if they want
}

MeshPacket *PositionPlugin::allocReply()
{
    NodeInfo *node = service.refreshMyNodeInfo(); // should guarantee there is now a position
    assert(node->has_position);

    // configuration of POSITION packet
    //   consider making this a function argument?
    uint32_t pos_flags = radioConfig.preferences.position_flags;

    // Populate a Position struct with ONLY the requested fields
    Position p = Position_init_default;  //   Start with an empty structure

    // lat/lon are unconditionally included - IF AVAILABLE!
    p.latitude_i = node->position.latitude_i;
    p.longitude_i = node->position.longitude_i;
    p.time = node->position.time;

    if (pos_flags & PositionFlags_POS_BATTERY)
        p.battery_level = node->position.battery_level;

    if (pos_flags & PositionFlags_POS_ALTITUDE) {
        if (pos_flags & PositionFlags_POS_ALT_MSL)
            p.altitude = node->position.altitude;
        else
            p.altitude_hae = node->position.altitude_hae;

        if (pos_flags & PositionFlags_POS_GEO_SEP)
            p.alt_geoid_sep = node->position.alt_geoid_sep;
    }

    if (pos_flags & PositionFlags_POS_DOP) {
        if (pos_flags & PositionFlags_POS_HVDOP) {
            p.HDOP = node->position.HDOP;
            p.VDOP = node->position.VDOP;
        } else
            p.PDOP = node->position.PDOP;
    }

    if (pos_flags & PositionFlags_POS_SATINVIEW)
        p.sats_in_view = node->position.sats_in_view;

    if (pos_flags & PositionFlags_POS_TIMESTAMP)
        p.pos_timestamp = node->position.pos_timestamp;


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

int32_t PositionPlugin::runOnce()
{

    // We limit our GPS broadcasts to a max rate
    uint32_t now = millis();
    if (lastGpsSend == 0 || now - lastGpsSend >= getPref_position_broadcast_secs() * 1000) {
        lastGpsSend = now;

        // If we changed channels, ask everyone else for their latest info
        bool requestReplies = currentGeneration != radioGeneration;
        currentGeneration = radioGeneration;

        DEBUG_MSG("Sending position to mesh (wantReplies=%d)\n", requestReplies);
        sendOurPosition(NODENUM_BROADCAST, requestReplies);
    }

    return 5000; // to save power only wake for our callback occasionally
}