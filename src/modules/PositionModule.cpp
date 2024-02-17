#include "PositionModule.h"
#include "GPS.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "TypeConversions.h"
#include "airtime.h"
#include "configuration.h"
#include "gps/GeoCoord.h"
#include "main.h"
#include "meshtastic/atak.pb.h"
#include "sleep.h"
#include "target_specific.h"

extern "C" {
#include "mesh/compression/unishox2.h"
}

PositionModule *positionModule;

PositionModule::PositionModule()
    : ProtobufModule("position", meshtastic_PortNum_POSITION_APP, &meshtastic_Position_msg),
      concurrency::OSThread("PositionModule")
{
    isPromiscuous = true; // We always want to update our nodedb, even if we are sniffing on others
    if (config.device.role != meshtastic_Config_DeviceConfig_Role_TRACKER &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_TAK_TRACKER)
        setIntervalFromNow(60 * 1000);

    // Power saving trackers should clear their position on startup to avoid waking up and sending a stale position
    if ((config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER ||
         config.device.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER) &&
        config.power.is_power_saving) {
        clearPosition();
    }
}

void PositionModule::clearPosition()
{
    LOG_DEBUG("Clearing position on startup for sleepy tracker (ー。ー) zzz\n");
    meshtastic_NodeInfoLite *node = nodeDB.getMeshNode(nodeDB.getNodeNum());
    node->position.latitude_i = 0;
    node->position.longitude_i = 0;
    node->position.altitude = 0;
    node->position.time = 0;
    nodeDB.setLocalPosition(meshtastic_Position_init_default);
}

bool PositionModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Position *pptr)
{
    auto p = *pptr;

    // If inbound message is a replay (or spoof!) of our own messages, we shouldn't process
    // (why use second-hand sources for our own data?)

    // FIXME this can in fact happen with packets sent from EUD (src=RX_SRC_USER)
    // to set fixed location, EUD-GPS location or just the time (see also issue #900)
    bool isLocal = false;
    if (nodeDB.getNodeNum() == getFrom(&mp)) {
        LOG_DEBUG("Incoming update from MYSELF\n");
        isLocal = true;
        nodeDB.setLocalPosition(p);
    }

    // Log packet size and data fields
    LOG_INFO("POSITION node=%08x l=%d latI=%d lonI=%d msl=%d hae=%d geo=%d pdop=%d hdop=%d vdop=%d siv=%d fxq=%d fxt=%d pts=%d "
             "time=%d\n",
             getFrom(&mp), mp.decoded.payload.size, p.latitude_i, p.longitude_i, p.altitude, p.altitude_hae,
             p.altitude_geoidal_separation, p.PDOP, p.HDOP, p.VDOP, p.sats_in_view, p.fix_quality, p.fix_type, p.timestamp,
             p.time);

    if (p.time && channels.getByIndex(mp.channel).role == meshtastic_Channel_Role_PRIMARY) {
        struct timeval tv;
        uint32_t secs = p.time;

        tv.tv_sec = secs;
        tv.tv_usec = 0;

        // Set from phone RTC Quality to RTCQualityNTP since it should be approximately so
        perhapsSetRTC(isLocal ? RTCQualityNTP : RTCQualityFromNet, &tv);
    }

    nodeDB.updatePosition(getFrom(&mp), p);

    // Only respond to location requests on the channel where we broadcast location.
    if (channels.getByIndex(mp.channel).role == meshtastic_Channel_Role_PRIMARY) {
        ignoreRequest = false;
    } else {
        ignoreRequest = true;
    }

    return false; // Let others look at this message also if they want
}

meshtastic_MeshPacket *PositionModule::allocReply()
{
    if (ignoreRequest) {
        ignoreRequest = false; // Reset for next request
        return nullptr;
    }

    meshtastic_NodeInfoLite *node = service.refreshLocalMeshNode(); // should guarantee there is now a position
    assert(node->has_position);

    // configuration of POSITION packet
    //   consider making this a function argument?
    uint32_t pos_flags = config.position.position_flags;

    // Populate a Position struct with ONLY the requested fields
    meshtastic_Position p = meshtastic_Position_init_default; //   Start with an empty structure
    // if localPosition is totally empty, put our last saved position (lite) in there
    if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0) {
        nodeDB.setLocalPosition(TypeConversions::ConvertToPosition(node->position));
    }
    localPosition.seq_number++;

    // lat/lon are unconditionally included - IF AVAILABLE!
    p.latitude_i = localPosition.latitude_i;
    p.longitude_i = localPosition.longitude_i;
    p.time = localPosition.time;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE) {
        if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE_MSL)
            p.altitude = localPosition.altitude;
        else
            p.altitude_hae = localPosition.altitude_hae;

        if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_GEOIDAL_SEPARATION)
            p.altitude_geoidal_separation = localPosition.altitude_geoidal_separation;
    }

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_DOP) {
        if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_HVDOP) {
            p.HDOP = localPosition.HDOP;
            p.VDOP = localPosition.VDOP;
        } else
            p.PDOP = localPosition.PDOP;
    }

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_SATINVIEW)
        p.sats_in_view = localPosition.sats_in_view;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_TIMESTAMP)
        p.timestamp = localPosition.timestamp;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_SEQ_NO)
        p.seq_number = localPosition.seq_number;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_HEADING)
        p.ground_track = localPosition.ground_track;

    if (pos_flags & meshtastic_Config_PositionConfig_PositionFlags_SPEED)
        p.ground_speed = localPosition.ground_speed;

    // Strip out any time information before sending packets to other nodes - to keep the wire size small (and because other
    // nodes shouldn't trust it anyways) Note: we allow a device with a local GPS to include the time, so that gpsless
    // devices can get time.
    if (getRTCQuality() < RTCQualityDevice) {
        LOG_INFO("Stripping time %u from position send\n", p.time);
        p.time = 0;
    } else {
        p.time = getValidTime(RTCQualityDevice);
        LOG_INFO("Providing time to mesh %u\n", p.time);
    }

    LOG_INFO("Position reply: time=%i, latI=%i, lonI=-%i\n", p.time, p.latitude_i, p.longitude_i);

    // TAK Tracker devices should send their position in a TAK packet over the ATAK port
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER)
        return allocAtakPli();

    return allocDataProtobuf(p);
}

meshtastic_MeshPacket *PositionModule::allocAtakPli()
{
    LOG_INFO("Sending TAK PLI packet\n");
    meshtastic_MeshPacket *mp = allocDataPacket();
    mp->decoded.portnum = meshtastic_PortNum_ATAK_PLUGIN;

    meshtastic_TAKPacket takPacket = {.is_compressed = true,
                                      .has_contact = true,
                                      .contact = {0},
                                      .has_group = true,
                                      .group = {meshtastic_MemberRole_TeamMember, meshtastic_Team_Cyan},
                                      .has_status = true,
                                      .status =
                                          {
                                              .battery = powerStatus->getBatteryChargePercent(),
                                          },
                                      .which_payload_variant = meshtastic_TAKPacket_pli_tag,
                                      {.pli = {
                                           .latitude_i = localPosition.latitude_i,
                                           .longitude_i = localPosition.longitude_i,
                                           .altitude = localPosition.altitude_hae > 0 ? localPosition.altitude_hae : 0,
                                           .speed = localPosition.ground_speed,
                                           .course = static_cast<uint16_t>(localPosition.ground_track),
                                       }}};

    auto length = unishox2_compress_simple(owner.long_name, strlen(owner.long_name), takPacket.contact.device_callsign);
    LOG_DEBUG("Uncompressed device_callsign '%s' - %d bytes\n", owner.long_name, strlen(owner.long_name));
    LOG_DEBUG("Compressed device_callsign '%s' - %d bytes\n", takPacket.contact.device_callsign, length);
    length = unishox2_compress_simple(owner.long_name, strlen(owner.long_name), takPacket.contact.callsign);
    mp->decoded.payload.size =
        pb_encode_to_bytes(mp->decoded.payload.bytes, sizeof(mp->decoded.payload.bytes), &meshtastic_TAKPacket_msg, &takPacket);
    return mp;
}

void PositionModule::sendOurPosition(NodeNum dest, bool wantReplies, uint8_t channel)
{
    // cancel any not yet sent (now stale) position packets
    if (prevPacketId) // if we wrap around to zero, we'll simply fail to cancel in that rare case (no big deal)
        service.cancelSending(prevPacketId);

    meshtastic_MeshPacket *p = allocReply();
    if (p == nullptr) {
        LOG_WARN("allocReply returned a nullptr\n");
        return;
    }

    p->to = dest;
    p->decoded.want_response = config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER ? false : wantReplies;
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER ||
        config.device.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER)
        p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    else
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    prevPacketId = p->id;

    if (channel > 0)
        p->channel = channel;

    service.sendToMesh(p, RX_SRC_LOCAL, true);

    if ((config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER ||
         config.device.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER) &&
        config.power.is_power_saving) {
        LOG_DEBUG("Starting next execution in 5 seconds and then going to sleep.\n");
        sleepOnNextExecution = true;
        setIntervalFromNow(5000);
    }
}

#define RUNONCE_INTERVAL 5000;

int32_t PositionModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        sleepOnNextExecution = false;
        uint32_t nightyNightMs = getConfiguredOrDefaultMs(config.position.position_broadcast_secs);
        LOG_DEBUG("Sleeping for %ims, then awaking to send position again.\n", nightyNightMs);
        doDeepSleep(nightyNightMs, false);
    }

    meshtastic_NodeInfoLite *node = nodeDB.getMeshNode(nodeDB.getNodeNum());
    if (node == nullptr)
        return RUNONCE_INTERVAL;

    // We limit our GPS broadcasts to a max rate
    uint32_t now = millis();
    uint32_t intervalMs = getConfiguredOrDefaultMs(config.position.position_broadcast_secs, default_broadcast_interval_secs);
    uint32_t msSinceLastSend = now - lastGpsSend;
    // Only send packets if the channel util. is less than 25% utilized or we're a tracker with less than 40% utilized.
    if (!airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_TRACKER &&
                                         config.device.role != meshtastic_Config_DeviceConfig_Role_TAK_TRACKER)) {
        return RUNONCE_INTERVAL;
    }

    if (lastGpsSend == 0 || msSinceLastSend >= intervalMs) {
        if (hasValidPosition(node)) {
            lastGpsSend = now;

            lastGpsLatitude = node->position.latitude_i;
            lastGpsLongitude = node->position.longitude_i;

            // If we changed channels, ask everyone else for their latest info
            bool requestReplies = currentGeneration != radioGeneration;
            currentGeneration = radioGeneration;

            LOG_INFO("Sending pos@%x:6 to mesh (wantReplies=%d)\n", localPosition.timestamp, requestReplies);
            sendOurPosition(NODENUM_BROADCAST, requestReplies);
            if (config.device.role == meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND) {
                sendLostAndFoundText();
            }
        }
    } else if (config.position.position_broadcast_smart_enabled) {
        const meshtastic_NodeInfoLite *node2 = service.refreshLocalMeshNode(); // should guarantee there is now a position

        if (hasValidPosition(node2)) {
            // The minimum time (in seconds) that would pass before we are able to send a new position packet.
            const uint32_t minimumTimeThreshold =
                getConfiguredOrDefaultMs(config.position.broadcast_smart_minimum_interval_secs, 30);

            auto smartPosition = getDistanceTraveledSinceLastSend(node->position);

            if (smartPosition.hasTraveledOverThreshold && msSinceLastSend >= minimumTimeThreshold) {
                bool requestReplies = currentGeneration != radioGeneration;
                currentGeneration = radioGeneration;

                LOG_INFO("Sending smart pos@%x:6 to mesh (distanceTraveled=%fm, minDistanceThreshold=%im, timeElapsed=%ims, "
                         "minTimeInterval=%ims)\n",
                         localPosition.timestamp, smartPosition.distanceTraveled, smartPosition.distanceThreshold,
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

    return RUNONCE_INTERVAL; // to save power only wake for our callback occasionally
}

void PositionModule::sendLostAndFoundText()
{
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = NODENUM_BROADCAST;
    char *message = new char[60];
    sprintf(message, "🚨I'm lost! Lat / Lon: %f, %f\a", (lastGpsLatitude * 1e-7), (lastGpsLongitude * 1e-7));
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->want_ack = false;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);

    service.sendToMesh(p, RX_SRC_LOCAL, true);
    delete[] message;
}

struct SmartPosition PositionModule::getDistanceTraveledSinceLastSend(meshtastic_PositionLite currentPosition)
{
    // The minimum distance to travel before we are able to send a new position packet.
    const uint32_t distanceTravelThreshold = getConfiguredOrDefault(config.position.broadcast_smart_minimum_distance, 100);

    // Determine the distance in meters between two points on the globe
    float distanceTraveledSinceLastSend = GeoCoord::latLongToMeter(
        lastGpsLatitude * 1e-7, lastGpsLongitude * 1e-7, currentPosition.latitude_i * 1e-7, currentPosition.longitude_i * 1e-7);

#ifdef GPS_EXTRAVERBOSE
    LOG_DEBUG("--------LAST POSITION------------------------------------\n");
    LOG_DEBUG("lastGpsLatitude=%i, lastGpsLatitude=%i\n", lastGpsLatitude, lastGpsLongitude);

    LOG_DEBUG("--------CURRENT POSITION---------------------------------\n");
    LOG_DEBUG("currentPosition.latitude_i=%i, currentPosition.longitude_i=%i\n", lastGpsLatitude, lastGpsLongitude);

    LOG_DEBUG("--------SMART POSITION-----------------------------------\n");
    LOG_DEBUG("hasTraveledOverThreshold=%i, distanceTraveled=%d, distanceThreshold=% u\n",
              abs(distanceTraveledSinceLastSend) >= distanceTravelThreshold, abs(distanceTraveledSinceLastSend),
              distanceTravelThreshold);

    if (abs(distanceTraveledSinceLastSend) >= distanceTravelThreshold) {
        LOG_DEBUG("\n\n\nSMART SEEEEEEEEENDING\n\n\n");
    }
#endif

    return SmartPosition{.distanceTraveled = abs(distanceTraveledSinceLastSend),
                         .distanceThreshold = distanceTravelThreshold,
                         .hasTraveledOverThreshold = abs(distanceTraveledSinceLastSend) >= distanceTravelThreshold};
}

void PositionModule::handleNewPosition()
{
    meshtastic_NodeInfoLite *node = nodeDB.getMeshNode(nodeDB.getNodeNum());
    const meshtastic_NodeInfoLite *node2 = service.refreshLocalMeshNode(); // should guarantee there is now a position
    // We limit our GPS broadcasts to a max rate
    uint32_t now = millis();
    uint32_t msSinceLastSend = now - lastGpsSend;

    if (hasValidPosition(node2)) {
        auto smartPosition = getDistanceTraveledSinceLastSend(node->position);
        if (smartPosition.hasTraveledOverThreshold) {
            bool requestReplies = currentGeneration != radioGeneration;
            currentGeneration = radioGeneration;

            LOG_INFO("Sending smart pos@%x:6 to mesh (distanceTraveled=%fm, minDistanceThreshold=%im, timeElapsed=%ims)\n",
                     localPosition.timestamp, smartPosition.distanceTraveled, smartPosition.distanceThreshold, msSinceLastSend);
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