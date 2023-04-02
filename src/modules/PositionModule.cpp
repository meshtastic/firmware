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
    : ProtobufModule("position", meshtastic_PortNum_POSITION_APP, &meshtastic_Position_msg), concurrency::OSThread(
                                                                                                 "PositionModule")
{
    isPromiscuous = true;          // We always want to update our nodedb, even if we are sniffing on others
    setIntervalFromNow(60 * 1000); // Send our initial position 60 seconds after we start (to give GPS time to setup)
}

bool PositionModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Position *pptr)
{
    auto p = *pptr;

    // If inbound message is a replay (or spoof!) of our own messages, we shouldn't process
    // (why use second-hand sources for our own data?)

    // FIXME this can in fact happen with packets sent from EUD (src=RX_SRC_USER)
    // to set fixed location, EUD-GPS location or just the time (see also issue #900)
    if (nodeDB.getNodeNum() == getFrom(&mp)) {
        LOG_DEBUG("Incoming update from MYSELF\n");
        // LOG_DEBUG("Ignored an incoming update from MYSELF\n");
        // return false;
    }

    // Log packet size and list of fields
    LOG_INFO("POSITION node=%08x l=%d %s%s%s%s%s%s%s%s%s%s%s%s%s\n", getFrom(&mp), mp.decoded.payload.size,
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

meshtastic_MeshPacket *PositionModule::allocReply()
{
    meshtastic_NodeInfo *node = service.refreshMyNodeInfo(); // should guarantee there is now a position
    assert(node->has_position);

    node->position.seq_number++;

    // configuration of POSITION packet
    //   consider making this a function argument?
    uint32_t pos_flags = config.position.position_flags;

    // Populate a Position struct with ONLY the requested fields
    meshtastic_Position p = meshtastic_Position_init_default; //   Start with an empty structure

    // lat/lon are unconditionally included - IF AVAILABLE!
    p.latitude_i = node->position.latitude_i;
    p.longitude_i = node->position.longitude_i;
    p.time = node->position.time;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE) {
        if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE_MSL)
            p.altitude = node->position.altitude;
        else
            p.altitude_hae = node->position.altitude_hae;

        if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_GEOIDAL_SEPARATION)
            p.altitude_geoidal_separation = node->position.altitude_geoidal_separation;
    }

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_DOP) {
        if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_HVDOP) {
            p.HDOP = node->position.HDOP;
            p.VDOP = node->position.VDOP;
        } else
            p.PDOP = node->position.PDOP;
    }

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_SATINVIEW)
        p.sats_in_view = node->position.sats_in_view;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_TIMESTAMP)
        p.timestamp = node->position.timestamp;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_SEQ_NO)
        p.seq_number = node->position.seq_number;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_HEADING)
        p.ground_track = node->position.ground_track;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_SPEED)
        p.ground_speed = node->position.ground_speed;

    // Strip out any time information before sending packets to other nodes - to keep the wire size small (and because other
    // nodes shouldn't trust it anyways) Note: we allow a device with a local GPS to include the time, so that gpsless
    // devices can get time.
    if (getRTCQuality() < RTCQualityDevice) {
        LOG_INFO("Stripping time %u from position send\n", p.time);
        p.time = 0;
    } else
        LOG_INFO("Providing time to mesh %u\n", p.time);

    LOG_INFO("Position reply: time=%i, latI=%i, lonI=-%i\n", p.time, p.latitude_i, p.longitude_i);

    return allocDataProtobuf(p);
}

void PositionModule::sendOurPosition(NodeNum dest, bool wantReplies, uint8_t channel)
{
    // cancel any not yet sent (now stale) position packets
    if (prevPacketId) // if we wrap around to zero, we'll simply fail to cancel in that rare case (no big deal)
        service.cancelSending(prevPacketId);

    meshtastic_MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER)
        p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    else
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    prevPacketId = p->id;

    if (channel > 0)
        p->channel = channel;

    service.sendToMesh(p, RX_SRC_LOCAL, true);
}

int32_t PositionModule::runOnce()
{
    meshtastic_NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());

    // We limit our GPS broadcasts to a max rate
    uint32_t now = millis();
    uint32_t intervalMs = getConfiguredOrDefaultMs(config.position.position_broadcast_secs, default_broadcast_interval_secs);
    uint32_t msSinceLastSend = now - lastGpsSend;

    if (lastGpsSend == 0 || msSinceLastSend >= intervalMs) {
        // Only send packets if the channel is less than 40% utilized.
        if (airTime->isTxAllowedChannelUtil()) {
            if (node->has_position && (node->position.latitude_i != 0 || node->position.longitude_i != 0)) {
                lastGpsSend = now;

                lastGpsLatitude = node->position.latitude_i;
                lastGpsLongitude = node->position.longitude_i;

                // If we changed channels, ask everyone else for their latest info
                bool requestReplies = currentGeneration != radioGeneration;
                currentGeneration = radioGeneration;

                LOG_INFO("Sending pos@%x:6 to mesh (wantReplies=%d)\n", node->position.timestamp, requestReplies);
                sendOurPosition(NODENUM_BROADCAST, requestReplies);
            }
        }
    } else if (config.position.position_broadcast_smart_enabled) {
        // Only send packets if the channel is less than 25% utilized or we're a tracker.
        if (airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_TRACKER)) {
            meshtastic_NodeInfo *node2 = service.refreshMyNodeInfo(); // should guarantee there is now a position

            if (node2->has_position && (node2->position.latitude_i != 0 || node2->position.longitude_i != 0)) {
                // The minimum distance to travel before we are able to send a new position packet.
                const uint32_t distanceTravelThreshold =
                    config.position.broadcast_smart_minimum_distance > 0 ? config.position.broadcast_smart_minimum_distance : 100;

                // The minimum time (in seconds) that would pass before we are able to send a new position packet.
                const uint32_t minimumTimeThreshold =
                    getConfiguredOrDefaultMs(config.position.broadcast_smart_minimum_interval_secs, 30);

                // Determine the distance in meters between two points on the globe
                float distanceTraveledSinceLastSend =
                    GeoCoord::latLongToMeter(lastGpsLatitude * 1e-7, lastGpsLongitude * 1e-7, node->position.latitude_i * 1e-7,
                                             node->position.longitude_i * 1e-7);

                if ((abs(distanceTraveledSinceLastSend) >= distanceTravelThreshold) && msSinceLastSend >= minimumTimeThreshold) {
                    bool requestReplies = currentGeneration != radioGeneration;
                    currentGeneration = radioGeneration;

                    LOG_INFO("Sending smart pos@%x:6 to mesh (distanceTraveled=%fm, minDistanceThreshold=%im, timeElapsed=%ims, "
                             "minTimeInterval=%ims)\n",
                             node2->position.timestamp, abs(distanceTraveledSinceLastSend), distanceTravelThreshold,
                             msSinceLastSend, minimumTimeThreshold);
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
        }
    }

    return 5000; // to save power only wake for our callback occasionally
}
