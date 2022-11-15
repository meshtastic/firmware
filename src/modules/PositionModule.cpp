#include "PositionModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "airtime.h"
#include "configuration.h"
#include "gps/GeoCoord.h"

PositionModule *positionModule;

PositionModule::PositionModule()
    : ProtobufModule("position", PortNum_POSITION_APP, Position_fields), concurrency::OSThread("PositionModule")
{
    isPromiscuous = true;          // We always want to update our nodedb, even if we are sniffing on others
    setIntervalFromNow(60 * 1000); // Send our initial position 60 seconds after we start (to give GPS time to setup)
}

bool PositionModule::handleReceivedProtobuf(const MeshPacket &mp, Position *pptr)
{
    auto p = *pptr;

    // If inbound message is a replay (or spoof!) of our own messages, we shouldn't process
    // (why use second-hand sources for our own data?)

    // FIXME this can in fact happen with packets sent from EUD (src=RX_SRC_USER)
    // to set fixed location, EUD-GPS location or just the time (see also issue #900)
    if (nodeDB.getNodeNum() == getFrom(&mp)) {
        DEBUG_MSG("Incoming update from MYSELF\n");
        // DEBUG_MSG("Ignored an incoming update from MYSELF\n");
        // return false;
    }

    // Log packet size and list of fields
    DEBUG_MSG("POSITION node=%08x l=%d %s%s%s%s%s%s%s%s%s%s%s%s%s\n", getFrom(&mp), mp.decoded.payload.size,
              p.latitude_i ? "LAT " : "", p.longitude_i ? "LON " : "", p.altitude ? "MSL " : "", p.altitude_hae ? "HAE " : "",
              p.altitude_geoidal_separation ? "GEO " : "", p.PDOP ? "PDOP " : "", p.HDOP ? "HDOP " : "", p.VDOP ? "VDOP " : "",
              p.sats_in_view ? "SIV " : "", p.fix_quality ? "FXQ " : "", p.fix_type ? "FXT " : "", p.timestamp ? "PTS " : "",
              p.time ? "TIME " : "");

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

MeshPacket *PositionModule::allocReply()
{
    NodeInfo *node = service.refreshMyNodeInfo(); // should guarantee there is now a position
    assert(node->has_position);

    node->position.seq_number++;

    // configuration of POSITION packet
    //   consider making this a function argument?
    uint32_t pos_flags = config.position.position_flags;

    // Populate a Position struct with ONLY the requested fields
    Position p = Position_init_default; //   Start with an empty structure

    // lat/lon are unconditionally included - IF AVAILABLE!
    p.latitude_i = node->position.latitude_i;
    p.longitude_i = node->position.longitude_i;
    p.time = node->position.time;

    if (pos_flags & Config_PositionConfig_PositionFlags_ALTITUDE) {
        if (pos_flags & Config_PositionConfig_PositionFlags_ALTITUDE_MSL)
            p.altitude = node->position.altitude;
        else
            p.altitude_hae = node->position.altitude_hae;

        if (pos_flags & Config_PositionConfig_PositionFlags_GEOIDAL_SEPARATION)
            p.altitude_geoidal_separation = node->position.altitude_geoidal_separation;
    }

    if (pos_flags & Config_PositionConfig_PositionFlags_DOP) {
        if (pos_flags & Config_PositionConfig_PositionFlags_HVDOP) {
            p.HDOP = node->position.HDOP;
            p.VDOP = node->position.VDOP;
        } else
            p.PDOP = node->position.PDOP;
    }

    if (pos_flags & Config_PositionConfig_PositionFlags_SATINVIEW)
        p.sats_in_view = node->position.sats_in_view;

    if (pos_flags & Config_PositionConfig_PositionFlags_TIMESTAMP)
        p.timestamp = node->position.timestamp;

    if (pos_flags & Config_PositionConfig_PositionFlags_SEQ_NO)
        p.seq_number = node->position.seq_number;

    if (pos_flags & Config_PositionConfig_PositionFlags_HEADING)
        p.ground_track = node->position.ground_track;

    if (pos_flags & Config_PositionConfig_PositionFlags_SPEED)
        p.ground_speed = node->position.ground_speed;

    // Strip out any time information before sending packets to other nodes - to keep the wire size small (and because other
    // nodes shouldn't trust it anyways) Note: we allow a device with a local GPS to include the time, so that gpsless
    // devices can get time.
    if (getRTCQuality() < RTCQualityDevice) {
        DEBUG_MSG("Stripping time %u from position send\n", p.time);
        p.time = 0;
    } else
        DEBUG_MSG("Providing time to mesh %u\n", p.time);

    DEBUG_MSG("Position reply: time=%i, latI=%i, lonI=-%i\n", p.time, p.latitude_i, p.longitude_i);

    return allocDataProtobuf(p);
}

void PositionModule::sendOurPosition(NodeNum dest, bool wantReplies)
{
    // cancel any not yet sent (now stale) position packets
    if (prevPacketId) // if we wrap around to zero, we'll simply fail to cancel in that rare case (no big deal)
        service.cancelSending(prevPacketId);

    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;
    p->priority = MeshPacket_Priority_BACKGROUND;
    prevPacketId = p->id;

    service.sendToMesh(p, RX_SRC_LOCAL, true);
}

int32_t PositionModule::runOnce()
{
    NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());

    // We limit our GPS broadcasts to a max rate
    uint32_t now = millis();
    uint32_t intervalMs = config.position.position_broadcast_secs > 0 ? config.position.position_broadcast_secs * 1000 : default_broadcast_interval_secs * 1000;
    if (lastGpsSend == 0 || (now - lastGpsSend) >= intervalMs) {

        // Only send packets if the channel is less than 40% utilized.
        if (airTime->channelUtilizationPercent() < max_channel_util_percent) {
            if (node->has_position && (node->position.latitude_i != 0 || node->position.longitude_i != 0)) {
                lastGpsSend = now;

                lastGpsLatitude = node->position.latitude_i;
                lastGpsLongitude = node->position.longitude_i;

                // If we changed channels, ask everyone else for their latest info
                bool requestReplies = currentGeneration != radioGeneration;
                currentGeneration = radioGeneration;

                DEBUG_MSG("Sending pos@%x:6 to mesh (wantReplies=%d)\n", node->position.timestamp, requestReplies);
                sendOurPosition(NODENUM_BROADCAST, requestReplies);
            }
        } else {
            DEBUG_MSG("Channel utilization is >40 percent. Skipping this opportunity to send.\n");
        }

    } else if (config.position.position_broadcast_smart_enabled) {

        // Only send packets if the channel is less than 25% utilized.
        if (airTime->channelUtilizationPercent() < polite_channel_util_percent) {

            NodeInfo *node2 = service.refreshMyNodeInfo(); // should guarantee there is now a position

            if (node2->has_position && (node2->position.latitude_i != 0 || node2->position.longitude_i != 0)) {
                // The minimum distance to travel before we are able to send a new position packet.
                const uint32_t distanceTravelMinimum = 30;

                // The minimum time that would pass before we are able to send a new position packet.
                const uint32_t timeTravelMinimum = 30;

                // Determine the distance in meters between two points on the globe
                float distance = GeoCoord::latLongToMeter(lastGpsLatitude * 1e-7, lastGpsLongitude * 1e-7,
                                                          node->position.latitude_i * 1e-7, node->position.longitude_i * 1e-7);

                // Yes, this has a bunch of magic numbers. Sorry. This is to make the scale non-linear.
                const float distanceTravelMath = 1203 / (sqrt(pow(myNodeInfo.bitrate, 1.5) / 1.1));
                uint32_t distanceTravelThreshold =
                    (distanceTravelMath >= distanceTravelMinimum) ? distanceTravelMath : distanceTravelMinimum;

                // Yes, this has a bunch of magic numbers. Sorry.
                uint32_t timeTravel =
                    ((1500 / myNodeInfo.bitrate) >= timeTravelMinimum) ? (1500 / myNodeInfo.bitrate) : timeTravelMinimum;
                // If the distance traveled since the last update is greater than distanceTravelMinimum meters
                //   and it's been at least timeTravelMinimum seconds since the last update
                if ((abs(distance) >= distanceTravelThreshold) && (now - lastGpsSend) >= (timeTravel * 1000)) {
                    bool requestReplies = currentGeneration != radioGeneration;
                    currentGeneration = radioGeneration;

                    DEBUG_MSG("Sending smart pos@%x:6 to mesh (wantReplies=%d, d=%d, dtt=%d, tt=%d)\n", node2->position.timestamp,
                              requestReplies, distance, distanceTravelThreshold, timeTravel);
                    sendOurPosition(NODENUM_BROADCAST, requestReplies);

                    // Set the current coords as our last ones, after we've compared distance with current and decided to send
                    lastGpsLatitude = node->position.latitude_i;
                    lastGpsLongitude = node->position.longitude_i;

                    /* Update lastGpsSend to now. This means if the device is stationary, then
                       getPref_position_broadcast_secs will still apply.
                    */
                    lastGpsSend = now;
                }
            }
        } else {
            DEBUG_MSG("Channel utilization is >25 percent. Skipping this opportunity to send.\n");
        }
    }

    return 5000; // to save power only wake for our callback occasionally
}
